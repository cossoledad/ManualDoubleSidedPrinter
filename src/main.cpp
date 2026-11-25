#include "duplex.hpp"
#include "paper_guide.hpp"
#include "pdf_document.hpp"
#include "pdf_preview.hpp"
#include "printer.hpp"
#include "windows_apartment.hpp"
#include "windows_error.hpp"

#include <commctrl.h>
#include <shobjidl.h>
#include <uxtheme.h>
#include <windowsx.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <functional>
#include <future>
#include <memory>
#include <optional>
#include <stdexcept>
#include <thread>
#include <type_traits>
#include <utility>

namespace mdsp {
namespace {

constexpr auto title = L"MDSPrinter";
constexpr auto window_class = L"ManualDoubleSidedPrinter.MainWindow";
constexpr DWORD window_style = WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN;
constexpr int client_width = 1024;
constexpr int client_height = 650;
constexpr int min_client_width = 790;
constexpr int min_client_height = 530;
constexpr UINT_PTR task_timer = 1;
constexpr UINT_PTR preview_timer = 2;
constexpr UINT_PTR guide_timer = 3;
constexpr LPARAM simulation_destination = 1;

enum ControlId {
    browse_id = 100, printer_id, refresh_id, range_id, stop_id, print_id,
    original_id, first_id, second_id, previous_id, next_id, fit_id, guide_canvas_id
};
enum class Stage { setup, opening, first_pass, flip, second_pass };
enum class PreviewMode { original, first_pass, second_pass };

struct FontCloser {
    void operator()(HFONT font) const noexcept { DeleteObject(font); }
};

struct BufferedPainting {
    HRESULT initialized = BufferedPaintInit();
    ~BufferedPainting() { if (SUCCEEDED(initialized)) BufferedPaintUnInit(); }
};

using Document = std::shared_ptr<const PdfDocument>;

struct TaskResult {
    Document document;
    std::optional<PrinterSettings> settings;
    std::wstring error;
};

std::wstring control_text(HWND control)
{
    const int length = GetWindowTextLengthW(control);
    std::wstring text(static_cast<std::size_t>(length) + 1, L'\0');
    text.resize(GetWindowTextW(control, text.data(), length + 1));
    return text;
}

std::wstring describe_pages(std::span<const int> pages)
{
    if (pages.empty()) {
        return L"无需打印";
    }
    std::wstring text;
    constexpr std::size_t visible_pages = 8;
    for (int page : pages.first(std::min(pages.size(), visible_pages))) {
        if (!text.empty()) {
            text += L", ";
        }
        text += page == blank_page ? L"空白" : std::to_wstring(page);
    }
    if (pages.size() > visible_pages) {
        text += L" …（" + std::to_wstring(pages.size()) + L" 面）";
    }
    return text;
}

bool animations_enabled()
{
    BOOL enabled = FALSE;
    return SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &enabled, 0) && enabled;
}

class MainWindow {
public:
    explicit MainWindow(HINSTANCE instance);
    int run() const;

private:
    static LRESULT CALLBACK dispatch(HWND window, UINT message, WPARAM wparam, LPARAM lparam);
    LRESULT handle(UINT message, WPARAM wparam, LPARAM lparam);

    HWND control(const wchar_t* type, const wchar_t* text, DWORD style, int id = 0, DWORD ex_style = 0) const;
    void create_controls();
    void update_fonts();
    void layout();
    void refresh_printers();
    bool simulation_selected() const;
    void simulate_pass(Stage stage);
    void choose_pdf();
    void update_plan();
    void update_controls();
    void update_preview();
    void select_preview(PreviewMode mode);
    void step_preview(int step);
    void update_guide(bool force = false);
    std::optional<double> guide_elapsed() const;
    LRESULT draw_panel(const DRAWITEMSTRUCT& item) const;
    void print();
    void abandon();
    void start_task(Stage stage, std::function<TaskResult()> operation);
    void finish_task();
    void show_error(std::wstring message);
    int px(int value) const { return MulDiv(value, dpi_, USER_DEFAULT_SCREEN_DPI); }
    int rail_width(LONG width) const { return std::clamp(static_cast<int>(width / 2 - px(42)), px(350), px(410)); }
    bool pointer_over_preview(LPARAM screen_point) const;

    HINSTANCE instance_;
    HWND window_ = nullptr;
    HWND heading_ = nullptr;
    HWND file_label_ = nullptr;
    HWND file_name_ = nullptr;
    HWND browse_ = nullptr;
    HWND printer_label_ = nullptr;
    HWND printer_ = nullptr;
    HWND refresh_ = nullptr;
    HWND range_label_ = nullptr;
    HWND range_ = nullptr;
    HWND first_pages_ = nullptr;
    HWND second_pages_ = nullptr;
    HWND status_ = nullptr;
    HWND stop_ = nullptr;
    HWND print_ = nullptr;
    HWND preview_heading_ = nullptr;
    HWND preview_hint_ = nullptr;
    HWND original_ = nullptr;
    HWND first_ = nullptr;
    HWND second_ = nullptr;
    HWND preview_canvas_ = nullptr;
    HWND previous_ = nullptr;
    HWND next_ = nullptr;
    HWND preview_position_ = nullptr;
    HWND guide_caption_ = nullptr;
    HWND guide_canvas_ = nullptr;
    HWND fit_ = nullptr;
    UINT dpi_ = USER_DEFAULT_SCREEN_DPI;
    std::unique_ptr<std::remove_pointer_t<HFONT>, FontCloser> normal_font_;
    std::unique_ptr<std::remove_pointer_t<HFONT>, FontCloser> heading_font_;
    std::unique_ptr<std::remove_pointer_t<HFONT>, FontCloser> section_font_;

    Stage stage_ = Stage::setup;
    bool simulating_ = false;
    Document document_;
    std::filesystem::path file_;
    std::filesystem::path pending_file_;
    std::optional<DuplexPlan> plan_;
    std::optional<PrinterSettings> settings_;
    std::wstring status_override_;
    std::wstring plan_error_;
    std::future<TaskResult> task_;
    PreviewMode preview_mode_ = PreviewMode::original;
    std::size_t preview_index_ = 0;
    PdfPreview preview_;
    std::optional<std::chrono::steady_clock::time_point> guide_start_;
    const wchar_t* active_guide_caption_ = nullptr;
};

MainWindow::MainWindow(HINSTANCE instance) : instance_(instance), dpi_(GetDpiForSystem())
{
    WNDCLASSW window_class_info{};
    window_class_info.lpfnWndProc = dispatch;
    window_class_info.hInstance = instance;
    window_class_info.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(101));
    window_class_info.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    window_class_info.hbrBackground = GetSysColorBrush(COLOR_WINDOW);
    window_class_info.lpszClassName = window_class;
    if (!RegisterClassW(&window_class_info)) {
        windows_error(L"无法注册主窗口");
    }

    RECT bounds{0, 0, px(client_width), px(client_height)};
    AdjustWindowRectExForDpi(&bounds, window_style, FALSE, 0, dpi_);
    RECT work_area{};
    int width = bounds.right - bounds.left;
    int height = bounds.bottom - bounds.top;
    int x = CW_USEDEFAULT;
    int y = CW_USEDEFAULT;
    if (SystemParametersInfoW(SPI_GETWORKAREA, 0, &work_area, 0)) {
        width = std::min<LONG>(width, work_area.right - work_area.left);
        height = std::min<LONG>(height, work_area.bottom - work_area.top);
        x = work_area.left + (work_area.right - work_area.left - width) / 2;
        y = work_area.top + (work_area.bottom - work_area.top - height) / 2;
    }
    if (!CreateWindowExW(0, window_class, title, window_style, x, y, width, height, nullptr, nullptr, instance, this)) {
        windows_error(L"无法创建主窗口");
    }
}

int MainWindow::run() const
{
    ShowWindow(window_, SW_SHOWNORMAL);
    SetFocus(browse_);
    MSG message{};
    BOOL result;
    while ((result = GetMessageW(&message, nullptr, 0, 0)) > 0) {
        if (message.message == WM_MOUSEWHEEL && message.hwnd != preview_canvas_ && pointer_over_preview(message.lParam)) {
            SendMessageW(preview_canvas_, message.message, message.wParam, message.lParam);
            continue;
        }
        if (!IsDialogMessageW(window_, &message)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    }
    return result == -1 ? 1 : static_cast<int>(message.wParam);
}

bool MainWindow::pointer_over_preview(LPARAM screen_point) const
{
    return preview_canvas_ && WindowFromPoint({GET_X_LPARAM(screen_point), GET_Y_LPARAM(screen_point)}) == preview_canvas_;
}

LRESULT CALLBACK MainWindow::dispatch(HWND window, UINT message, WPARAM wparam, LPARAM lparam)
{
    auto* self = reinterpret_cast<MainWindow*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lparam);
        self = static_cast<MainWindow*>(create->lpCreateParams);
        self->window_ = window;
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    if (!self) {
        return DefWindowProcW(window, message, wparam, lparam);
    }

    try {
        return self->handle(message, wparam, lparam);
    } catch (const UserError& error) {
        self->show_error(error.message);
    } catch (const winrt::hresult_error& error) {
        self->show_error(std::wstring(error.message()));
    } catch (const std::exception&) {
        self->show_error(L"操作失败。请重试。");
    }
    return message == WM_CREATE ? -1 : 0;
}

LRESULT MainWindow::handle(UINT message, WPARAM wparam, LPARAM lparam)
{
    switch (message) {
    case WM_CREATE:
        create_controls();
        update_fonts();
        layout();
        refresh_printers();
        update_controls();
        return 0;
    case WM_COMMAND:
        if (HIWORD(wparam) == BN_CLICKED) {
            switch (LOWORD(wparam)) {
            case browse_id: choose_pdf(); return 0;
            case refresh_id: refresh_printers(); return 0;
            case stop_id: abandon(); return 0;
            case print_id: print(); return 0;
            case original_id: select_preview(PreviewMode::original); return 0;
            case first_id: select_preview(PreviewMode::first_pass); return 0;
            case second_id: select_preview(PreviewMode::second_pass); return 0;
            case previous_id: step_preview(-1); return 0;
            case next_id: step_preview(1); return 0;
            case fit_id: preview_.reset_view(); return 0;
            }
        }
        if ((LOWORD(wparam) == range_id && HIWORD(wparam) == EN_CHANGE) ||
            (LOWORD(wparam) == printer_id && HIWORD(wparam) == CBN_SELCHANGE)) {
            if (stage_ == Stage::setup) {
                status_override_.clear();
                if (LOWORD(wparam) == range_id) {
                    update_plan();
                }
                update_controls();
            }
            return 0;
        }
        break;
    case DM_GETDEFID:
        return MAKELONG(print_id, DC_HASDEFID);
    case WM_GETMINMAXINFO: {
        auto& info = *reinterpret_cast<MINMAXINFO*>(lparam);
        RECT bounds{0, 0, px(min_client_width), px(min_client_height)};
        AdjustWindowRectExForDpi(&bounds, window_style, FALSE, 0, dpi_);
        info.ptMinTrackSize = {bounds.right - bounds.left, bounds.bottom - bounds.top};
        return 0;
    }
    case WM_DPICHANGED: {
        dpi_ = HIWORD(wparam);
        const auto& suggested = *reinterpret_cast<const RECT*>(lparam);
        update_fonts();
        SetWindowPos(window_, nullptr, suggested.left, suggested.top, suggested.right - suggested.left,
                     suggested.bottom - suggested.top, SWP_NOACTIVATE | SWP_NOZORDER);
        layout();
        return 0;
    }
    case WM_SIZE:
        if (wparam != SIZE_MINIMIZED && next_) {
            layout();
        }
        if (guide_canvas_) { update_guide(true); }
        return 0;
    case WM_SETTINGCHANGE:
        if (guide_canvas_) { update_guide(true); }
        break;
    case WM_CTLCOLORSTATIC: {
        const auto dc = reinterpret_cast<HDC>(wparam);
        SetTextColor(dc, GetSysColor(COLOR_WINDOWTEXT));
        SetBkColor(dc, GetSysColor(COLOR_WINDOW));
        return reinterpret_cast<LRESULT>(GetSysColorBrush(COLOR_WINDOW));
    }
    case WM_PAINT: {
        PAINTSTRUCT paint{};
        const auto dc = BeginPaint(window_, &paint);
        RECT client{};
        GetClientRect(window_, &client);
        const int rail_end = px(24) + rail_width(client.right);
        const RECT rule{px(24), px(191), rail_end, px(191) + 1};
        FillRect(dc, &rule, GetSysColorBrush(COLOR_3DLIGHT));
        const RECT split{rail_end + px(24), px(24), rail_end + px(24) + 1, client.bottom - px(24)};
        FillRect(dc, &split, GetSysColorBrush(COLOR_3DLIGHT));
        EndPaint(window_, &paint);
        return 0;
    }
    case WM_DRAWITEM:
        return draw_panel(*reinterpret_cast<const DRAWITEMSTRUCT*>(lparam));
    case WM_TIMER:
        if (wparam == task_timer) {
            finish_task();
            return 0;
        }
        if (wparam == preview_timer) {
            if (preview_.poll()) {
                preview_.invalidate();
            }
            if (!preview_.pending()) {
                KillTimer(window_, preview_timer);
            }
            return 0;
        }
        if (wparam == guide_timer) {
            update_guide();
            return 0;
        }
        break;
    case WM_CLOSE:
        if (task_.valid()) {
            return 0;
        }
        if (stage_ != Stage::flip || MessageBoxW(window_, L"第二遍尚未提交。确定关闭？", title,
            MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) == IDYES) {
            DestroyWindow(window_);
        }
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(window_, message, wparam, lparam);
}

HWND MainWindow::control(const wchar_t* type, const wchar_t* text, DWORD style, int id, DWORD ex_style) const
{
    const auto child = CreateWindowExW(ex_style, type, text, WS_CHILD | WS_VISIBLE | style, 0, 0, 0, 0, window_,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), instance_, nullptr);
    if (!child) {
        windows_error(L"无法创建界面控件");
    }
    return child;
}

void MainWindow::create_controls()
{
    heading_ = control(WC_STATICW, title, SS_LEFT);
    file_label_ = control(WC_STATICW, L"PDF 文件", SS_LEFT);
    file_name_ = control(WC_STATICW, L"尚未选择", SS_LEFT | SS_PATHELLIPSIS | SS_CENTERIMAGE);
    browse_ = control(WC_BUTTONW, L"选择", BS_PUSHBUTTON | WS_TABSTOP, browse_id);
    printer_label_ = control(WC_STATICW, L"打印到", SS_LEFT);
    printer_ = control(WC_COMBOBOXW, L"", CBS_DROPDOWNLIST | WS_TABSTOP | WS_VSCROLL, printer_id);
    refresh_ = control(WC_BUTTONW, L"刷新", BS_PUSHBUTTON | WS_TABSTOP, refresh_id);
    range_label_ = control(WC_STATICW, L"页码范围", SS_LEFT);
    range_ = control(WC_EDITW, L"", ES_AUTOHSCROLL | WS_TABSTOP, range_id, WS_EX_CLIENTEDGE);
    SendMessageW(range_, EM_SETCUEBANNER, TRUE, reinterpret_cast<LPARAM>(L"留空打印全部，例如 1-3,5,8-10"));
    first_pages_ = control(WC_STATICW, L"", SS_LEFT | SS_ENDELLIPSIS);
    second_pages_ = control(WC_STATICW, L"", SS_LEFT | SS_ENDELLIPSIS);
    status_ = control(WC_STATICW, L"", SS_LEFT);
    stop_ = control(WC_BUTTONW, L"结束本次", BS_PUSHBUTTON | WS_TABSTOP, stop_id);
    print_ = control(WC_BUTTONW, L"打印第一遍", BS_DEFPUSHBUTTON | WS_TABSTOP, print_id);
    guide_caption_ = control(WC_STATICW, L"", SS_LEFT | SS_CENTERIMAGE);
    guide_canvas_ = control(WC_STATICW, L"纸叠回放示意", SS_OWNERDRAW, guide_canvas_id);
    preview_heading_ = control(WC_STATICW, L"PDF 预览", SS_LEFT);
    preview_hint_ = control(WC_STATICW, L"滚轮缩放 · 拖动平移", SS_RIGHT | SS_CENTERIMAGE);
    constexpr DWORD mode_style = BS_AUTORADIOBUTTON | BS_PUSHLIKE;
    original_ = control(WC_BUTTONW, L"原文", mode_style | WS_GROUP | WS_TABSTOP, original_id);
    first_ = control(WC_BUTTONW, L"第一遍", mode_style, first_id);
    second_ = control(WC_BUTTONW, L"第二遍", mode_style, second_id);
    fit_ = control(WC_BUTTONW, L"适合窗口", BS_PUSHBUTTON | WS_GROUP | WS_TABSTOP, fit_id);
    preview_canvas_ = preview_.create(window_, instance_);
    previous_ = control(WC_BUTTONW, L"上一页", BS_PUSHBUTTON | WS_TABSTOP, previous_id);
    preview_position_ = control(WC_STATICW, L"", SS_CENTER | SS_CENTERIMAGE | SS_ENDELLIPSIS);
    next_ = control(WC_BUTTONW, L"下一页", BS_PUSHBUTTON | WS_TABSTOP, next_id);
}

void MainWindow::update_fonts()
{
    const auto serif = [this](int size, int weight, DWORD charset, const wchar_t* family) {
        return CreateFontW(-px(size), 0, 0, 0, weight, FALSE, FALSE, FALSE, charset,
            OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_ROMAN, family);
    };
    decltype(normal_font_) normal(serif(14, FW_NORMAL, GB2312_CHARSET, L"SimSun"));
    decltype(heading_font_) heading(serif(25, FW_BOLD, DEFAULT_CHARSET, L"Georgia"));
    decltype(section_font_) section(serif(17, FW_BOLD, GB2312_CHARSET, L"SimSun"));
    for (HWND child = GetWindow(window_, GW_CHILD); child; child = GetWindow(child, GW_HWNDNEXT)) {
        const HFONT font = child == heading_ ? heading.get() : child == preview_heading_ ? section.get() : normal.get();
        SendMessageW(child, WM_SETFONT, reinterpret_cast<WPARAM>(font ? font : GetStockObject(DEFAULT_GUI_FONT)), TRUE);
    }
    normal_font_ = std::move(normal);
    heading_font_ = std::move(heading);
    section_font_ = std::move(section);
}

void MainWindow::layout()
{
    RECT client{};
    GetClientRect(window_, &client);
    const int left = px(24);
    const int width = rail_width(client.right);
    const int rail_end = left + width;
    const int edit_left = left + px(82);
    const int button_width = px(66);
    const int button_left = rail_end - button_width;
    const int edit_width = button_left - edit_left - px(8);
    const int footer_top = client.bottom - px(54);
    const int guide_top = px(276);
    const int guide_height = std::clamp(static_cast<int>(client.bottom) - px(398), px(70), px(132));
    const int preview_left = rail_end + px(48);
    const int preview_width = std::max(0L, client.right - preview_left - px(24));
    const int nav_width = px(64);
    auto place = [](HWND child, int x, int y, int w, int h) {
        SetWindowPos(child, nullptr, x, y, std::max(0, w), std::max(0, h),
            SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOREDRAW);
    };

    place(heading_, left, px(23), width, px(34));
    place(file_label_, left, px(80), px(78), px(22));
    place(file_name_, edit_left, px(70), edit_width, px(32));
    place(browse_, button_left, px(70), button_width, px(30));
    place(printer_label_, left, px(120), px(78), px(22));
    place(printer_, edit_left, px(111), edit_width, px(174));
    place(refresh_, button_left, px(111), button_width, px(30));
    place(range_label_, left, px(159), px(78), px(22));
    place(range_, edit_left, px(151), rail_end - edit_left, px(28));
    place(first_pages_, left, px(205), width, px(22));
    place(second_pages_, left, px(226), width, px(22));
    place(guide_caption_, left, px(249), width, px(25));
    place(guide_canvas_, left, guide_top, width, guide_height);
    const int status_top = stage_ == Stage::flip ? guide_top + guide_height + px(10) : footer_top - px(52);
    place(status_, left, status_top, width, px(44));
    place(stop_, left, footer_top, px(100), px(32));
    place(print_, rail_end - px(172), footer_top, px(172), px(32));

    place(preview_heading_, preview_left, px(25), px(130), px(30));
    place(preview_hint_, preview_left + px(130), px(25), preview_width - px(130), px(30));
    const int mode_width = px(74);
    place(original_, preview_left, px(63), mode_width, px(31));
    place(first_, preview_left + mode_width + px(4), px(63), mode_width, px(31));
    place(second_, preview_left + 2 * (mode_width + px(4)), px(63), mode_width, px(31));
    place(fit_, preview_left + preview_width - px(83), px(63), px(83), px(31));
    place(preview_canvas_, preview_left, px(107), preview_width, footer_top - px(119));
    place(previous_, preview_left, footer_top, nav_width, px(32));
    place(preview_position_, preview_left + nav_width + px(7), footer_top,
        preview_width - 2 * (nav_width + px(7)), px(32));
    place(next_, preview_left + preview_width - nav_width, footer_top, nav_width, px(32));
    RedrawWindow(window_, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN);
}

void MainWindow::refresh_printers()
{
    if (stage_ != Stage::setup) {
        return;
    }
    Printers available;
    try {
        available = list_printers();
        status_override_.clear();
    } catch (const UserError& error) {
        status_override_ = error.message;
        if (SendMessageW(printer_, CB_GETCOUNT, 0, 0) > 0) {
            update_controls();
            return;
        }
    }

    const auto previous = control_text(printer_);
    const bool was_simulation = simulation_selected();
    SendMessageW(printer_, CB_RESETCONTENT, 0, 0);
    for (const auto& name : available.names) {
        SendMessageW(printer_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(name.c_str()));
    }
    const auto find = [this](const std::wstring& name) {
        return SendMessageW(printer_, CB_FINDSTRINGEXACT, static_cast<WPARAM>(-1), reinterpret_cast<LPARAM>(name.c_str()));
    };
    auto selection = find(previous);
    if (selection == CB_ERR) { selection = find(available.default_name); }
    const auto simulation = SendMessageW(printer_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"模拟打印（不出纸）"));
    if (simulation >= 0) { SendMessageW(printer_, CB_SETITEMDATA, simulation, simulation_destination); }
    if (was_simulation) { selection = simulation; }
    SendMessageW(printer_, CB_SETCURSEL, selection == CB_ERR ? 0 : selection, 0);
    update_controls();
}

bool MainWindow::simulation_selected() const
{
    const auto index = SendMessageW(printer_, CB_GETCURSEL, 0, 0);
    return index != CB_ERR && SendMessageW(printer_, CB_GETITEMDATA, index, 0) == simulation_destination;
}

void MainWindow::choose_pdf()
{
    if (stage_ != Stage::setup) {
        return;
    }
    winrt::com_ptr<IFileOpenDialog> dialog;
    winrt::check_hresult(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(dialog.put())));
    constexpr COMDLG_FILTERSPEC filter{L"PDF 文件 (*.pdf)", L"*.pdf"};
    winrt::check_hresult(dialog->SetFileTypes(1, &filter));
    DWORD options = 0;
    winrt::check_hresult(dialog->GetOptions(&options));
    winrt::check_hresult(dialog->SetOptions(options | FOS_FORCEFILESYSTEM | FOS_FILEMUSTEXIST));
    const HRESULT picked = dialog->Show(window_);
    if (picked == HRESULT_FROM_WIN32(ERROR_CANCELLED)) {
        return;
    }
    winrt::check_hresult(picked);
    winrt::com_ptr<IShellItem> item;
    winrt::check_hresult(dialog->GetResult(item.put()));
    PWSTR path = nullptr;
    winrt::check_hresult(item->GetDisplayName(SIGDN_FILESYSPATH, &path));
    const std::unique_ptr<wchar_t, decltype(&CoTaskMemFree)> selected(path, CoTaskMemFree);
    pending_file_ = selected.get();
    start_task(Stage::opening, [path = pending_file_] {
        TaskResult result;
        result.document = std::make_shared<PdfDocument>(path);
        return result;
    });
}

void MainWindow::update_plan()
{
    plan_.reset();
    plan_error_.clear();
    if (document_) {
        try {
            plan_ = build_for_m126a(select_pages(control_text(range_), document_->page_count()));
        } catch (const std::invalid_argument&) {
            plan_error_ = L"请输入有效页码，例如 1-3,5,8-10。";
        } catch (const std::out_of_range&) {
            plan_error_ = L"页码必须在 1 到 " + std::to_wstring(document_->page_count()) + L" 之间。";
        }
    }
    const auto first = plan_ ? L"第一遍    " + describe_pages(plan_->first_pass) : L"";
    const auto second = plan_ ? L"第二遍    " + describe_pages(plan_->second_pass) : L"";
    SetWindowTextW(first_pages_, first.c_str());
    SetWindowTextW(second_pages_, second.c_str());
}

void MainWindow::update_controls()
{
    const bool setup = stage_ == Stage::setup;
    const bool flip = stage_ == Stage::flip;
    const bool simulated = setup ? simulation_selected() : simulating_;
    EnableWindow(browse_, setup);
    EnableWindow(printer_, setup && SendMessageW(printer_, CB_GETCOUNT, 0, 0) > 0);
    EnableWindow(refresh_, setup);
    EnableWindow(range_, setup && document_ != nullptr);
    EnableWindow(print_, flip || (setup && plan_ && SendMessageW(printer_, CB_GETCURSEL, 0, 0) != CB_ERR));
    ShowWindow(stop_, flip ? SW_SHOW : SW_HIDE);
    const bool guide_was_shown = (GetWindowLongPtrW(guide_canvas_, GWL_STYLE) & WS_VISIBLE) != 0;
    ShowWindow(guide_canvas_, flip ? SW_SHOW : SW_HIDE);
    ShowWindow(guide_caption_, flip ? SW_SHOW : SW_HIDE);
    if (guide_was_shown != flip) {
        layout();
    }
    update_guide(true);

    auto hint = status_override_;
    const wchar_t* action = simulated ? L"模拟第一遍" : L"打印第一遍";
    switch (stage_) {
    case Stage::setup:
        if (hint.empty()) {
            if (!document_) {
                hint = L"选择要打印的 PDF。";
            } else if (!plan_error_.empty()) {
                hint = plan_error_;
            } else if (SendMessageW(printer_, CB_GETCURSEL, 0, 0) == CB_ERR) {
                hint = L"连接或安装打印机后，点击刷新。";
            } else if (simulated) {
                hint = L"模拟模式仅演练流程，不提交打印任务。";
            }
        }
        if (plan_ && plan_->second_pass.empty()) {
            action = simulated ? L"模拟打印" : L"打印";
        }
        break;
    case Stage::opening:
        hint = L"正在读取 PDF…";
        break;
    case Stage::first_pass:
        hint = simulated ? L"正在模拟第一遍…" : L"正在提交打印任务…";
        break;
    case Stage::flip:
        action = simulated ? L"继续模拟第二遍" : L"已放回，打印第二遍";
        if (hint.empty()) {
            hint = simulated ? L"模拟模式无需操作打印机。看完放纸示意后继续。" :
                L"等第一遍全部出纸，保持印刷面朝向。\n将整叠纸顺时针旋转 180°，再放回进纸盘。";
        }
        break;
    case Stage::second_pass:
        action = simulated ? L"模拟第二遍" : L"打印第二遍";
        hint = simulated ? L"正在模拟第二遍…" : L"正在提交第二遍…";
        break;
    }
    SetWindowTextW(print_, action);
    SetWindowTextW(status_, hint.c_str());
    update_preview();
}

void MainWindow::update_preview()
{
    std::span<const int> pages;
    std::size_t count = 0;
    int mode_id = original_id;
    switch (preview_mode_) {
    case PreviewMode::original:
        count = document_ ? static_cast<std::size_t>(document_->page_count()) : 0;
        break;
    case PreviewMode::first_pass:
        mode_id = first_id;
        if (plan_) { pages = plan_->first_pass; }
        count = pages.size();
        break;
    case PreviewMode::second_pass:
        mode_id = second_id;
        if (plan_) { pages = plan_->second_pass; }
        count = pages.size();
        break;
    }

    const bool interactive = stage_ == Stage::setup || stage_ == Stage::flip;
    EnableWindow(original_, interactive && document_ != nullptr);
    EnableWindow(first_, interactive && plan_ && !plan_->first_pass.empty());
    EnableWindow(second_, interactive && plan_ && !plan_->second_pass.empty());
    CheckRadioButton(window_, original_id, second_id, mode_id);

    const bool original = preview_mode_ == PreviewMode::original;
    preview_index_ = count == 0 ? 0 : std::min(preview_index_, count - 1);
    std::optional<int> page;
    std::wstring position;
    if (count > 0) {
        page = original ? static_cast<int>(preview_index_ + 1) : pages[preview_index_];
        position = std::to_wstring(preview_index_ + 1) + L" / " + std::to_wstring(count);
        if (!original) {
            position += *page == blank_page ? L"  ·  空白占位" : L"  ·  PDF 第 " + std::to_wstring(*page) + L" 页";
        }
    }
    EnableWindow(previous_, interactive && preview_index_ > 0);
    EnableWindow(next_, interactive && count > 0 && preview_index_ < count - 1);
    EnableWindow(fit_, count > 0);
    SetWindowTextW(previous_, original ? L"上一页" : L"上一面");
    SetWindowTextW(next_, original ? L"下一页" : L"下一面");
    SetWindowTextW(preview_position_, position.c_str());
    preview_.show(document_, page);
    if (preview_.pending()) {
        SetTimer(window_, preview_timer, 40, nullptr);
    }
    preview_.invalidate();
}

void MainWindow::select_preview(PreviewMode mode)
{
    if (preview_mode_ != mode && (stage_ == Stage::setup || stage_ == Stage::flip)) {
        preview_mode_ = mode;
        preview_index_ = 0;
        update_preview();
    }
}

void MainWindow::step_preview(int step)
{
    if (step < 0 && IsWindowEnabled(previous_)) {
        --preview_index_;
        update_preview();
    } else if (step > 0 && IsWindowEnabled(next_)) {
        ++preview_index_;
        update_preview();
    }
}

std::optional<double> MainWindow::guide_elapsed() const
{
    if (!guide_start_) {
        return std::nullopt;
    }
    const auto elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - *guide_start_).count();
    return std::fmod(elapsed, paper_guide_duration);
}

void MainWindow::update_guide(bool force)
{
    if (force) {
        const bool animate = stage_ == Stage::flip && !IsIconic(window_) && animations_enabled();
        if (animate && !guide_start_ && SetTimer(window_, guide_timer, 33, nullptr)) {
            guide_start_ = std::chrono::steady_clock::now();
        } else if (!animate && guide_start_) {
            guide_start_.reset();
            KillTimer(window_, guide_timer);
        }
    }
    if (stage_ != Stage::flip) { return; }
    const auto* caption = paper_guide_caption(guide_elapsed());
    if (caption != active_guide_caption_) {
        SetWindowTextW(guide_caption_, caption);
        active_guide_caption_ = caption;
    }
    InvalidateRect(guide_canvas_, nullptr, FALSE);
}

LRESULT MainWindow::draw_panel(const DRAWITEMSTRUCT& item) const
{
    if (item.CtlID != guide_canvas_id) {
        return FALSE;
    }
    HDC dc = nullptr;
    const auto buffer = BeginBufferedPaint(item.hDC, &item.rcItem, BPBF_COMPATIBLEBITMAP, nullptr, &dc);
    if (!buffer) {
        dc = item.hDC;
    }
    const int original_state = SaveDC(dc);
    if (normal_font_) {
        SelectObject(dc, normal_font_.get());
    }
    draw_paper_guide(dc, item.rcItem, guide_elapsed());
    if (original_state != 0) {
        RestoreDC(dc, original_state);
    }
    if (buffer) {
        EndBufferedPaint(buffer, TRUE);
    }
    return TRUE;
}

void MainWindow::print()
{
    if (stage_ == Stage::setup && plan_) {
        simulating_ = simulation_selected();
        if (simulating_) {
            simulate_pass(Stage::first_pass);
            return;
        }
        auto name = control_text(printer_);
        if (name.empty()) {
            return;
        }
        const auto label = file_.filename().wstring() + L" · 第一遍";
        start_task(Stage::first_pass,
            [document = document_, pages = plan_->first_pass, name = std::move(name), label] {
                auto settings = read_printer_settings(name);
                print_pass(*document, settings, pages, label);
                TaskResult result;
                result.settings = std::move(settings);
                return result;
            });
    } else if (stage_ == Stage::flip && plan_ && simulating_) {
        simulate_pass(Stage::second_pass);
    } else if (stage_ == Stage::flip && plan_ && settings_) {
        const auto label = file_.filename().wstring() + L" · 第二遍";
        start_task(Stage::second_pass, [document = document_, pages = plan_->second_pass, settings = *settings_, label] {
            print_pass(*document, settings, pages, label);
            return TaskResult{};
        });
    }
}

void MainWindow::simulate_pass(Stage stage)
{
    preview_mode_ = stage == Stage::first_pass ? PreviewMode::first_pass : PreviewMode::second_pass;
    preview_index_ = 0;
    start_task(stage, [] {
        std::this_thread::sleep_for(std::chrono::milliseconds(550));
        return TaskResult{};
    });
}

void MainWindow::abandon()
{
    if (stage_ == Stage::flip && MessageBoxW(window_, L"结束本次双面打印，并放弃第二遍？", title,
        MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) == IDYES) {
        settings_.reset();
        stage_ = Stage::setup;
        status_override_ = simulating_ ? L"本次模拟已结束。" : L"本次双面流程已结束。";
        update_controls();
        SetFocus(print_);
    }
}

void MainWindow::start_task(Stage stage, std::function<TaskResult()> operation)
{
    if (!SetTimer(window_, task_timer, 60, nullptr)) {
        windows_error(L"无法启动后台任务");
    }
    try {
        task_ = std::async(std::launch::async, [operation = std::move(operation)] {
            try {
                const Apartment apartment(winrt::apartment_type::multi_threaded);
                return operation();
            } catch (const UserError& error) {
                return TaskResult{.error = error.message};
            } catch (const winrt::hresult_error& error) {
                return TaskResult{.error = std::wstring(error.message())};
            } catch (const std::exception&) {
                return TaskResult{.error = L"后台处理失败。"};
            }
        });
    } catch (...) {
        KillTimer(window_, task_timer);
        throw;
    }
    status_override_.clear();
    stage_ = stage;
    update_controls();
}

void MainWindow::finish_task()
{
    if (!task_.valid() || task_.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
        return;
    }
    KillTimer(window_, task_timer);
    const Stage finished = std::exchange(stage_, stage_ == Stage::second_pass ? Stage::flip : Stage::setup);
    auto result = task_.get();
    if (!result.error.empty()) {
        if (finished != Stage::opening && !simulating_) {
            result.error += L"\n\n请核对打印队列和已出纸张后再重试。";
        }
        show_error(std::move(result.error));
        return;
    }
    if (finished == Stage::opening) {
        document_ = std::move(result.document);
        file_ = std::move(pending_file_);
        preview_mode_ = PreviewMode::original;
        preview_index_ = 0;
        const auto file_label = file_.filename().wstring() + L"  ·  " + std::to_wstring(document_->page_count()) + L" 页";
        SetWindowTextW(file_name_, file_label.c_str());
        stage_ = Stage::opening; // Resetting the edit sends EN_CHANGE synchronously.
        SetWindowTextW(range_, L"");
        stage_ = Stage::setup;
        update_plan();
    } else if (finished == Stage::first_pass && plan_ && !plan_->second_pass.empty()) {
        settings_ = std::move(result.settings);
        stage_ = Stage::flip;
        preview_mode_ = PreviewMode::second_pass;
        preview_index_ = 0;
    } else {
        settings_.reset();
        stage_ = Stage::setup;
        status_override_ = simulating_ ? L"模拟完成，未提交打印任务。" : L"打印任务已提交。";
    }
    update_controls();
    if (stage_ == Stage::flip) {
        SetFocus(second_);
    } else {
        SetFocus(IsWindowEnabled(print_) ? print_ : browse_);
    }
}

void MainWindow::show_error(std::wstring message)
{
    if (next_) {
        update_controls();
    }
    MessageBoxW(window_, message.c_str(), title, MB_OK | MB_ICONERROR);
}

} // namespace
} // namespace mdsp

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int)
{
    try {
        const mdsp::Apartment apartment(winrt::apartment_type::single_threaded);
        const mdsp::BufferedPainting painting;
        INITCOMMONCONTROLSEX controls{sizeof(INITCOMMONCONTROLSEX), ICC_STANDARD_CLASSES};
        InitCommonControlsEx(&controls);
        return mdsp::MainWindow(instance).run();
    } catch (const mdsp::UserError& error) {
        MessageBoxW(nullptr, error.message.c_str(), mdsp::title, MB_OK | MB_ICONERROR);
    } catch (const winrt::hresult_error& error) {
        MessageBoxW(nullptr, error.message().c_str(), mdsp::title, MB_OK | MB_ICONERROR);
    } catch (const std::exception&) {
        MessageBoxW(nullptr, L"无法启动应用。", mdsp::title, MB_OK | MB_ICONERROR);
    }
    return 1;
}
