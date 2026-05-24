using System;
using System.Diagnostics;
using System.IO;
using System.Threading.Tasks;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Controls.ApplicationLifetimes;
using Avalonia.Platform;

namespace ManualDoubleSidedPrinter;

class Program
{
    private const string AppName = "ManualDoubleSidedPrinter";

    // Initialization code. Don't use any Avalonia, third-party APIs or any
    // SynchronizationContext-reliant code before AppMain is called: things aren't initialized
    // yet and stuff might break.
    [STAThread]
    public static void Main(string[] args)
    {
        InstallCrashLogging();
        WriteLog("Application startup.");

        try
        {
            BuildAvaloniaApp().StartWithClassicDesktopLifetime(args);
            WriteLog("Application exited normally.");
        }
        catch (Exception ex)
        {
            WriteLog($"Fatal startup exception: {ex}");
            throw;
        }
    }

    // Avalonia configuration, don't remove; also used by visual designer.
    public static AppBuilder BuildAvaloniaApp()
        => AppBuilder.Configure<App>()
            .UsePlatformDetect()
            .With(new Win32PlatformOptions
            {
                RenderingMode = new[]
                {
                    Win32RenderingMode.Software,
                    Win32RenderingMode.AngleEgl,
                    Win32RenderingMode.Wgl
                }
            })
#if DEBUG
            .WithDeveloperTools()
#endif
            .WithInterFont()
            .LogToTrace();

    private static void InstallCrashLogging()
    {
        AppDomain.CurrentDomain.UnhandledException += (_, e) =>
            WriteLog($"Unhandled exception: {e.ExceptionObject}");

        TaskScheduler.UnobservedTaskException += (_, e) =>
        {
            WriteLog($"Unobserved task exception: {e.Exception}");
            e.SetObserved();
        };
    }

    private static void WriteLog(string message)
    {
        try
        {
            var dir = Path.Combine(Path.GetTempPath(), AppName);
            Directory.CreateDirectory(dir);
            var path = Path.Combine(dir, "startup.log");
            var line = $"[{DateTime.Now:yyyy-MM-dd HH:mm:ss.fff}] {message}{Environment.NewLine}";
            File.AppendAllText(path, line);
        }
        catch
        {
            // Avoid crashing while writing diagnostics.
        }
    }
}
