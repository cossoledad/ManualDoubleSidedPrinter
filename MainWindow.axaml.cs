using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text.Json;
using System.Threading.Tasks;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Interactivity;
using Avalonia.Media;
using Avalonia.Platform.Storage;
using Avalonia.Threading;
using ManualDoubleSidedPrinter.Core;

namespace ManualDoubleSidedPrinter;

public partial class MainWindow : Window
{
    private const string ConfigDirectoryName = "ManualDoubleSidedPrinter";
    private const string ConfigFileName = "settings.json";

    private string? _pdfPath;
    private int _pageCount;
    private DuplexPlan? _currentPlan;
    private IReadOnlyList<int> _selectedPages = Array.Empty<int>();
    private readonly DispatcherTimer _animationTimer;
    private int _animationFrame;
    private WorkflowState _state = WorkflowState.NeedPdf;

    public MainWindow()
    {
        InitializeComponent();
        var rotate = GetPaperRotate();
        rotate.CenterX = 39;
        rotate.CenterY = 51;

        _animationTimer = new DispatcherTimer
        {
            Interval = TimeSpan.FromMilliseconds(320)
        };
        _animationTimer.Tick += OnAnimationTick;
        _animationTimer.Start();

        LoadPrinters();
        SetWorkflowState(WorkflowState.NeedPdf);
    }

    private async void OnBrowsePdfClick(object? sender, RoutedEventArgs e)
    {
        if (StorageProvider is null)
        {
            SetStatus("无法打开文件选择器", isError: true);
            return;
        }

        var files = await StorageProvider.OpenFilePickerAsync(new FilePickerOpenOptions
        {
            AllowMultiple = false,
            FileTypeFilter = new[]
            {
                new FilePickerFileType("PDF 文件")
                {
                    Patterns = new[] { "*.pdf" },
                    MimeTypes = new[] { "application/pdf" }
                }
            },
            Title = "请选择需要进行手动双面打印的 PDF"
        });

        if (files.Count == 0)
        {
            return;
        }

        var localPath = files[0].TryGetLocalPath();
        if (string.IsNullOrWhiteSpace(localPath) || !File.Exists(localPath))
        {
            SetStatus("文件不可用", isError: true);
            return;
        }

        if (!string.Equals(Path.GetExtension(localPath), ".pdf", StringComparison.OrdinalIgnoreCase))
        {
            SetStatus("仅支持 PDF", isError: true);
            return;
        }

        try
        {
            _pageCount = PdfPageReader.ReadPageCount(localPath);
            _pdfPath = localPath;
            _selectedPages = Enumerable.Range(1, _pageCount).ToList();
            PageModeCombo.SelectedIndex = 0;
            PageRangeBox.Text = string.Empty;
            PageRangeBox.IsEnabled = false;
            ApplyPageSelectionButton.IsEnabled = false;

            PdfPathBox.Text = _pdfPath;
            PdfInfoText.Text = $"文档页数：{_pageCount} 页";
            GeneratePlan();
            SetWorkflowState(WorkflowState.ReadyFirstPass);
        }
        catch (Exception ex)
        {
            SetStatus($"读取失败: {ex.Message}", isError: true);
            SetWorkflowState(WorkflowState.Error);
        }
    }

    private void GeneratePlan()
    {
        if (string.IsNullOrWhiteSpace(_pdfPath) || _pageCount <= 0)
        {
            SetStatus("请先选择 PDF", isError: true);
            return;
        }

        if (_selectedPages.Count == 0)
        {
            SetStatus("页码为空", isError: true);
            return;
        }

        var plan = DuplexPlanner.BuildForM126a(_selectedPages);
        _currentPlan = plan;

        FirstPassText.Text = $"第一次打印: {FormatPages(plan.FirstPassPages)}";
        SecondPassText.Text = $"第二次打印: {FormatPages(plan.SecondPassPages)}";
        PdfInfoText.Text = $"文档页数：{_pageCount} 页  已选：{_selectedPages.Count} 页";

        GuideStep1Text.Text = $"1. 第一遍: {FormatPages(plan.FirstPassPages)}";
        GuideStep2Text.Text = "2. 整叠顺时针旋转180°回纸";
        GuideStep3Text.Text = $"3. 第二遍: {FormatPages(plan.SecondPassPages)}";

        SetStatus("计划已自动生成", isError: false);
    }

    private void OnPageModeChanged(object? sender, SelectionChangedEventArgs e)
    {
        var isCustom = PageModeCombo.SelectedIndex == 1;
        PageRangeBox.IsEnabled = isCustom;
        ApplyPageSelectionButton.IsEnabled = isCustom;

        if (!isCustom && _pageCount > 0)
        {
            _selectedPages = Enumerable.Range(1, _pageCount).ToList();
            GeneratePlan();
            SetWorkflowState(WorkflowState.ReadyFirstPass);
        }
    }

    private void OnApplyPageSelectionClick(object? sender, RoutedEventArgs e)
    {
        if (_pageCount <= 0)
        {
            SetStatus("请先选择 PDF", isError: true);
            return;
        }

        if (!PageSelectionParser.TryParse(PageRangeBox.Text, _pageCount, out var pages, out var error))
        {
            SetStatus(error ?? "页码格式错误", isError: true);
            return;
        }

        _selectedPages = pages;
        GeneratePlan();
        SetWorkflowState(WorkflowState.ReadyFirstPass);
    }

    private void OnRefreshPrintersClick(object? sender, RoutedEventArgs e)
    {
        LoadPrinters();
    }

    private void OnPrinterSelectionChanged(object? sender, SelectionChangedEventArgs e)
    {
        SaveLastPrinterSelection();
    }

    private async void OnPrintFirstPassClick(object? sender, RoutedEventArgs e)
    {
        if (_state != WorkflowState.ReadyFirstPass)
        {
            SetStatus("当前不在第一遍阶段", isError: true);
            return;
        }

        if (!TryGetPrintableContext(out var printerName, out var plan))
        {
            return;
        }

        SetWorkflowState(WorkflowState.PrintingFirstPass);
        var ok = await PrintPassAsync(printerName!, plan!.FirstPassPages, "M126a_第一遍");
        if (_state != WorkflowState.Error)
        {
            SetWorkflowState(ok ? WorkflowState.WaitingFlip : WorkflowState.ReadyFirstPass);
        }
    }

    private void OnConfirmFlipClick(object? sender, RoutedEventArgs e)
    {
        if (_state != WorkflowState.WaitingFlip)
        {
            SetStatus("先完成第一遍打印", isError: true);
            return;
        }

        SetWorkflowState(WorkflowState.ReadySecondPass);
        SetStatus("可开始第二遍", isError: false);
    }

    private async void OnPrintSecondPassClick(object? sender, RoutedEventArgs e)
    {
        if (_state != WorkflowState.ReadySecondPass)
        {
            SetStatus("请先旋转纸张", isError: true);
            return;
        }

        if (!TryGetPrintableContext(out var printerName, out var plan))
        {
            return;
        }

        SetWorkflowState(WorkflowState.PrintingSecondPass);
        var ok = await PrintPassAsync(printerName!, plan!.SecondPassPages, "M126a_第二遍");
        if (_state != WorkflowState.Error)
        {
            SetWorkflowState(ok ? WorkflowState.Completed : WorkflowState.ReadySecondPass);
        }
    }

    private bool TryGetPrintableContext(out string? printerName, out DuplexPlan? plan)
    {
        printerName = null;
        plan = null;

        if (string.IsNullOrWhiteSpace(_pdfPath) || _pageCount <= 0 || _currentPlan is null)
        {
            SetStatus("请先选择 PDF", isError: true);
            return false;
        }

        printerName = PrinterCombo.SelectedItem?.ToString();
        if (string.IsNullOrWhiteSpace(printerName))
        {
            SetStatus("请选择打印机", isError: true);
            return false;
        }

        plan = _currentPlan;
        return true;
    }

    private async Task<bool> PrintPassAsync(string printerName, IReadOnlyList<int> pages, string jobName)
    {
        if (string.IsNullOrWhiteSpace(_pdfPath))
        {
            SetStatus("PDF路径无效", isError: true);
            return false;
        }

        if (pages.Count == 0)
        {
            SetStatus("无可打印页", isError: true);
            return false;
        }

        string? subsetPdf = null;
        try
        {
            SetStatus("发送打印任务...", isError: false);
            subsetPdf = await Task.Run(() => PdfSubsetComposer.CreateSubsetPdf(_pdfPath, pages));
            await Task.Run(() => PdfPrinter.PrintPdfFile(subsetPdf, printerName, jobName));
            SetStatus($"{jobName} 已发送", isError: false);
            return true;
        }
        catch (Exception ex)
        {
            SetStatus($"打印失败: {ex.Message}", isError: true);
            SetWorkflowState(WorkflowState.Error);
            return false;
        }
        finally
        {
            TryDeleteTempFile(subsetPdf);
        }
    }

    private void LoadPrinters()
    {
        try
        {
            var printers = PdfPrinter.GetInstalledPrinters();
            PrinterCombo.ItemsSource = printers;
            var config = LoadAppConfig();
            var restored = false;

            if (!string.IsNullOrWhiteSpace(config.LastPrinterName))
            {
                var index = printers
                    .Select((name, idx) => new { name, idx })
                    .FirstOrDefault(item => string.Equals(item.name, config.LastPrinterName, StringComparison.OrdinalIgnoreCase))
                    ?.idx ?? -1;

                if (index >= 0)
                {
                    PrinterCombo.SelectedIndex = index;
                    restored = true;
                }
            }

            if (printers.Count > 0)
            {
                if (!restored)
                {
                    PrinterCombo.SelectedIndex = 0;
                    SaveLastPrinterSelection();
                }

                SetStatus(restored
                    ? $"已恢复打印机 ({printers.Count})"
                    : $"已加载打印机 ({printers.Count})", isError: false);
            }
            else
            {
                SetStatus("未检测到打印机", isError: true);
            }
        }
        catch (Exception ex)
        {
            SetStatus($"读取打印机失败: {ex.Message}", isError: true);
        }
    }

    private static string FormatPages(IReadOnlyList<int> pages)
    {
        if (pages.Count == 0)
        {
            return "-";
        }

        return string.Join(", ", pages.Select(page => page == 0 ? "空白" : page.ToString()));
    }

    private static void TryDeleteTempFile(string? filePath)
    {
        if (string.IsNullOrWhiteSpace(filePath))
        {
            return;
        }

        try
        {
            if (File.Exists(filePath))
            {
                File.Delete(filePath);
            }
        }
        catch
        {
            // Ignore cleanup errors for temp files.
        }
    }

    private void SetStatus(string text, bool isError)
    {
        StatusText.Text = text;
        StatusText.Foreground = isError ? Avalonia.Media.Brushes.Firebrick : Avalonia.Media.Brushes.DarkGreen;
    }

    private void SetWorkflowState(WorkflowState state)
    {
        _state = state;
        _animationFrame = 0;

        var done = Brushes.SeaGreen;
        var active = Brushes.SteelBlue;
        var idle = new SolidColorBrush(Color.Parse("#8FA2B7"));

        SetStepIndicator(Step1Indicator, state is not WorkflowState.NeedPdf, false, done, active, idle);
        SetStepIndicator(Step2Indicator, state is WorkflowState.PrintingFirstPass or WorkflowState.WaitingFlip or WorkflowState.ReadySecondPass or WorkflowState.PrintingSecondPass or WorkflowState.Completed, state is WorkflowState.WaitingFlip or WorkflowState.ReadySecondPass or WorkflowState.PrintingSecondPass or WorkflowState.Completed, done, active, idle);
        SetStepIndicator(Step3Indicator, state is WorkflowState.WaitingFlip or WorkflowState.ReadySecondPass or WorkflowState.PrintingSecondPass or WorkflowState.Completed, state is WorkflowState.ReadySecondPass or WorkflowState.PrintingSecondPass or WorkflowState.Completed, done, active, idle);
        SetStepIndicator(Step4Indicator, state is WorkflowState.PrintingSecondPass or WorkflowState.Completed, state is WorkflowState.Completed, done, active, idle);

        switch (state)
        {
            case WorkflowState.NeedPdf:
                StateBadge.Background = idle;
                StateTitleText.Text = "等待PDF";
                StateHintText.Text = "选择文档";
                break;
            case WorkflowState.ReadyFirstPass:
                StateBadge.Background = active;
                StateTitleText.Text = "准备第一遍";
                StateHintText.Text = "点击打印第一遍";
                break;
            case WorkflowState.PrintingFirstPass:
                StateBadge.Background = active;
                StateTitleText.Text = "第一遍打印中";
                StateHintText.Text = "请等待";
                break;
            case WorkflowState.WaitingFlip:
                StateBadge.Background = Brushes.DarkOrange;
                StateTitleText.Text = "请旋转";
                StateHintText.Text = "顺时针旋转180°后点已旋转";
                break;
            case WorkflowState.ReadySecondPass:
                StateBadge.Background = active;
                StateTitleText.Text = "准备第二遍";
                StateHintText.Text = "点击打印第二遍";
                break;
            case WorkflowState.PrintingSecondPass:
                StateBadge.Background = active;
                StateTitleText.Text = "第二遍打印中";
                StateHintText.Text = "请等待";
                break;
            case WorkflowState.Completed:
                StateBadge.Background = done;
                StateTitleText.Text = "完成";
                StateHintText.Text = "可更换PDF继续";
                break;
            default:
                StateBadge.Background = Brushes.Firebrick;
                StateTitleText.Text = "异常";
                StateHintText.Text = "请重试";
                break;
        }

        PrintFirstPassButton.IsEnabled = state == WorkflowState.ReadyFirstPass;
        ConfirmFlipButton.IsEnabled = state == WorkflowState.WaitingFlip;
        PrintSecondPassButton.IsEnabled = state == WorkflowState.ReadySecondPass;
        PrintProgress.IsVisible = state is WorkflowState.PrintingFirstPass or WorkflowState.PrintingSecondPass;
    }

    private static void SetStepIndicator(Border target, bool active, bool done, IBrush doneBrush, IBrush activeBrush, IBrush idleBrush)
    {
        target.Background = done ? doneBrush : active ? activeBrush : idleBrush;
    }

    private RotateTransform GetPaperRotate()
    {
        if (AnimPaper.RenderTransform is RotateTransform rotate)
        {
            return rotate;
        }

        rotate = new RotateTransform();
        AnimPaper.RenderTransform = rotate;
        return rotate;
    }

    private void OnAnimationTick(object? sender, EventArgs e)
    {
        _animationFrame++;

        // Reset animation visuals each frame before state-specific effects.
        AnimCheck.IsVisible = false;
        AnimArrow.IsVisible = true;
        AnimArrow.Text = "▼";
        AnimPrinterLight.Background = new SolidColorBrush(Color.Parse("#7F8C99"));
        var rotate = GetPaperRotate();
        rotate.Angle = 0;
        AnimPaper.Margin = new Thickness(0, 18, 0, 0);

        switch (_state)
        {
            case WorkflowState.NeedPdf:
                AnimArrow.Text = _animationFrame % 2 == 0 ? "▼" : "▽";
                break;

            case WorkflowState.ReadyFirstPass:
                AnimPaper.Margin = new Thickness(0, 14 + (_animationFrame % 2) * 6, 0, 0);
                break;

            case WorkflowState.PrintingFirstPass:
            case WorkflowState.PrintingSecondPass:
                AnimPrinterLight.Background = _animationFrame % 2 == 0 ? Brushes.LimeGreen : Brushes.Gold;
                AnimPaper.Margin = new Thickness(0, 24 + (_animationFrame % 3) * 12, 0, 0);
                break;

            case WorkflowState.WaitingFlip:
                AnimArrow.Text = "↻";
                rotate.Angle = _animationFrame % 2 == 0 ? -22 : 22;
                break;

            case WorkflowState.ReadySecondPass:
                AnimArrow.Text = _animationFrame % 2 == 0 ? "▲" : "△";
                rotate.Angle = 180;
                break;

            case WorkflowState.Completed:
                AnimArrow.IsVisible = false;
                AnimCheck.IsVisible = _animationFrame % 2 == 0;
                AnimPrinterLight.Background = Brushes.SeaGreen;
                break;
        }
    }

    private static string GetConfigPath()
    {
        var directory = Path.Combine(Path.GetTempPath(), ConfigDirectoryName);
        Directory.CreateDirectory(directory);
        return Path.Combine(directory, ConfigFileName);
    }

    private AppConfig LoadAppConfig()
    {
        try
        {
            var configPath = GetConfigPath();
            if (!File.Exists(configPath))
            {
                return new AppConfig();
            }

            var json = File.ReadAllText(configPath);
            return JsonSerializer.Deserialize<AppConfig>(json) ?? new AppConfig();
        }
        catch
        {
            return new AppConfig();
        }
    }

    private void SaveLastPrinterSelection()
    {
        try
        {
            var selectedPrinter = PrinterCombo.SelectedItem?.ToString();
            if (string.IsNullOrWhiteSpace(selectedPrinter))
            {
                return;
            }

            var config = new AppConfig { LastPrinterName = selectedPrinter };
            var json = JsonSerializer.Serialize(config, new JsonSerializerOptions { WriteIndented = true });
            File.WriteAllText(GetConfigPath(), json);
        }
        catch
        {
            // Ignore config persistence failures.
        }
    }

    private sealed class AppConfig
    {
        public string? LastPrinterName { get; init; }
    }

    private enum WorkflowState
    {
        NeedPdf,
        ReadyFirstPass,
        PrintingFirstPass,
        WaitingFlip,
        ReadySecondPass,
        PrintingSecondPass,
        Completed,
        Error
    }
}