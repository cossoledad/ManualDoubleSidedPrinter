#include "duplex.hpp"
#include "pdf_document.hpp"
#include "printer.hpp"
#include "windows_error.hpp"

#include <commctrl.h>
#include <shobjidl.h>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <functional>
#include <future>
#include <memory>
#include <optional>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace mdsp {
namespace {

constexpr auto title = L"手动双面打印";
constexpr auto window_class = L"ManualDoubleSidedPrinter.MainWindow";
constexpr DWORD window_style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
constexpr int client_width = 564;
constexpr int client_height = 380;
constexpr UINT_PTR task_timer = 1;

enum ControlId { browse_id = 100, printer_id, refresh_id, range_id, stop_id, print_id };
enum class Stage { setup, opening, first_pass, flip, second_pass };

struct FontCloser {
    void operator()(HFONT font) const noexcept { DeleteObject(font); }
};

struct Apartment {
    explicit Apartment(winrt::apartment_type type) { winrt::init_apartment(type); }
    ~Apartment() { winrt::uninit_apartment(); }
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

class MainWindow {
public:
    explicit MainWindow(HINSTANCE instance);
    int run() const;

private:
    static LRESULT CALLBACK dispatch(HWND window, UINT message, WPARAM wparam, LPARAM lparam);
    LRESULT handle(UINT message, WPARAM wparam, LPARAM lparam);

    HWND control(const wchar_t* type, const wchar_t* text, DWORD style, int id = 0, DWORD ex_style = 0) const;
    void create_controls();
    void layout();
    void refresh_printers();
    void choose_pdf();
    void update_plan();
    void update_controls();
    void print();
    void abandon();
    void start_task(Stage stage, std::function<TaskResult()> operation);
    void finish_task();
    void show_error(std::wstring message);
    int px(int value) const { return MulDiv(value, dpi_, USER_DEFAULT_SCREEN_DPI); }

    HINSTANCE instance_;
    HWND window_ = nullptr;
    HWND heading_ = nullptr;
    HWND model_ = nullptr;
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
    UINT dpi_ = USER_DEFAULT_SCREEN_DPI;
    std::unique_ptr<std::remove_pointer_t<HFONT>, FontCloser> normal_font_;
    std::unique_ptr<std::remove_pointer_t<HFONT>, FontCloser> heading_font_;

    Stage stage_ = Stage::setup;
    Document document_;
    std::filesystem::path file_;
    std::filesystem::path pending_file_;
    std::optional<DuplexPlan> plan_;
    std::optional<PrinterSettings> settings_;
    std::wstring status_override_;
    std::wstring plan_error_;
    std::future<TaskResult> task_;
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
    const int width = bounds.right - bounds.left;
    const int height = bounds.bottom - bounds.top;
    RECT work_area{};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work_area, 0);
    const int x = work_area.left + std::max(0L, (work_area.right - work_area.left - width) / 2);
    const int y = work_area.top + std::max(0L, (work_area.bottom - work_area.top - height) / 2);
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
        if (!IsDialogMessageW(window_, &message)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    }
    return result == -1 ? 1 : static_cast<int>(message.wParam);
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
            }
        }
        if ((LOWORD(wparam) == range_id && HIWORD(wparam) == EN_CHANGE) ||
            (LOWORD(wparam) == printer_id && HIWORD(wparam) == CBN_SELCHANGE)) {
            if (stage_ == Stage::setup) {
                status_override_.clear();
                update_plan();
                update_controls();
            }
            return 0;
        }
        break;
    case DM_GETDEFID:
        return MAKELONG(print_id, DC_HASDEFID);
    case WM_DPICHANGED: {
        dpi_ = HIWORD(wparam);
        const auto& suggested = *reinterpret_cast<const RECT*>(lparam);
        RECT bounds{0, 0, px(client_width), px(client_height)};
        AdjustWindowRectExForDpi(&bounds, window_style, FALSE, 0, dpi_);
        SetWindowPos(window_, nullptr, suggested.left, suggested.top, bounds.right - bounds.left,
                     bounds.bottom - bounds.top, SWP_NOACTIVATE | SWP_NOZORDER);
        layout();
        return 0;
    }
    case WM_CTLCOLORSTATIC: {
        const auto dc = reinterpret_cast<HDC>(wparam);
        SetTextColor(dc, GetSysColor(COLOR_WINDOWTEXT));
        SetBkColor(dc, GetSysColor(COLOR_WINDOW));
        return reinterpret_cast<LRESULT>(GetSysColorBrush(COLOR_WINDOW));
    }
    case WM_PAINT: {
        PAINTSTRUCT paint{};
        const auto dc = BeginPaint(window_, &paint);
        const RECT rule{px(24), px(216), px(client_width - 24), px(216) + 1};
        FillRect(dc, &rule, GetSysColorBrush(COLOR_3DLIGHT));
        EndPaint(window_, &paint);
        return 0;
    }
    case WM_TIMER:
        if (wparam == task_timer) {
            finish_task();
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
    heading_ = control(WC_STATICW, L"打印 PDF", SS_LEFT);
    model_ = control(WC_STATICW, L"HP LaserJet Pro MFP M126a", SS_RIGHT);
    file_label_ = control(WC_STATICW, L"PDF 文件", SS_LEFT);
    file_name_ = control(WC_STATICW, L"尚未选择", SS_LEFT | SS_PATHELLIPSIS | SS_CENTERIMAGE);
    browse_ = control(WC_BUTTONW, L"选择…", BS_PUSHBUTTON | WS_TABSTOP, browse_id);
    printer_label_ = control(WC_STATICW, L"打印机", SS_LEFT);
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
}

void MainWindow::layout()
{
    auto place = [this](HWND child, int x, int y, int w, int h) {
        MoveWindow(child, px(x), px(y), px(w), px(h), TRUE);
    };

    decltype(normal_font_) normal(CreateFontW(-px(15), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI"));
    decltype(heading_font_) heading(CreateFontW(-px(28), 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI"));
    for (HWND child = GetWindow(window_, GW_CHILD); child; child = GetWindow(child, GW_HWNDNEXT)) {
        const HFONT font = child == heading_ ? heading.get() : normal.get();
        SendMessageW(child, WM_SETFONT, reinterpret_cast<WPARAM>(font ? font : GetStockObject(DEFAULT_GUI_FONT)), TRUE);
    }
    normal_font_ = std::move(normal);
    heading_font_ = std::move(heading);

    place(heading_, 24, 22, 250, 44);
    place(model_, 278, 36, 262, 21);
    place(file_label_, 24, 86, 90, 22);
    place(file_name_, 118, 78, 326, 33);
    place(browse_, 460, 78, 80, 32);
    place(printer_label_, 24, 133, 90, 22);
    place(printer_, 118, 125, 326, 184);
    place(refresh_, 460, 125, 80, 32);
    place(range_label_, 24, 178, 90, 22);
    place(range_, 118, 172, 422, 28);
    place(first_pages_, 24, 232, 516, 21);
    place(second_pages_, 24, 257, 516, 21);
    place(status_, 24, 288, 516, 44);
    place(stop_, 24, 334, 100, 32);
    place(print_, 350, 334, 190, 32);
    InvalidateRect(window_, nullptr, TRUE);
}

void MainWindow::refresh_printers()
{
    if (stage_ != Stage::setup) {
        return;
    }
    try {
        const auto previous = control_text(printer_);
        const auto available = list_printers();
        SendMessageW(printer_, CB_RESETCONTENT, 0, 0);
        for (const auto& name : available.names) {
            SendMessageW(printer_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(name.c_str()));
        }
        auto select = [this](const std::wstring& name) {
            return SendMessageW(printer_, CB_FINDSTRINGEXACT, static_cast<WPARAM>(-1), reinterpret_cast<LPARAM>(name.c_str()));
        };
        auto selection = select(previous);
        if (selection == CB_ERR) {
            selection = select(available.default_name);
        }
        SendMessageW(printer_, CB_SETCURSEL, selection == CB_ERR ? 0 : selection, 0);
        status_override_.clear();
    } catch (const UserError& error) {
        status_override_ = error.message;
    }
    update_controls();
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
    EnableWindow(browse_, setup);
    EnableWindow(printer_, setup && SendMessageW(printer_, CB_GETCOUNT, 0, 0) > 0);
    EnableWindow(refresh_, setup);
    EnableWindow(range_, setup && document_ != nullptr);
    EnableWindow(print_, flip || (setup && plan_ && SendMessageW(printer_, CB_GETCURSEL, 0, 0) != CB_ERR));
    ShowWindow(stop_, flip ? SW_SHOW : SW_HIDE);

    auto hint = status_override_;
    const wchar_t* action = L"打印第一遍";
    switch (stage_) {
    case Stage::setup:
        if (hint.empty()) {
            if (!document_) {
                hint = L"选择要打印的 PDF。";
            } else if (!plan_error_.empty()) {
                hint = plan_error_;
            } else if (SendMessageW(printer_, CB_GETCURSEL, 0, 0) == CB_ERR) {
                hint = L"连接或安装打印机后，点击刷新。";
            }
        }
        if (plan_ && plan_->second_pass.empty()) {
            action = L"打印";
        }
        break;
    case Stage::opening:
        hint = L"正在读取 PDF…";
        break;
    case Stage::first_pass:
        hint = L"正在提交打印任务…";
        break;
    case Stage::flip:
        action = L"已放回，打印第二遍";
        if (hint.empty()) {
            hint = L"等第一遍全部出纸。将整叠纸顺时针旋转 180°（不翻面），放回进纸盘。";
        }
        break;
    case Stage::second_pass:
        action = L"打印第二遍";
        hint = L"正在提交第二遍…";
        break;
    }
    SetWindowTextW(print_, action);
    SetWindowTextW(status_, hint.c_str());
}

void MainWindow::print()
{
    if (stage_ == Stage::setup && plan_) {
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
    } else if (stage_ == Stage::flip && plan_ && settings_) {
        const auto label = file_.filename().wstring() + L" · 第二遍";
        start_task(Stage::second_pass, [document = document_, pages = plan_->second_pass, settings = *settings_, label] {
            print_pass(*document, settings, pages, label);
            return TaskResult{};
        });
    }
}

void MainWindow::abandon()
{
    if (stage_ == Stage::flip && MessageBoxW(window_, L"结束本次双面打印，并放弃第二遍？", title,
        MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) == IDYES) {
        settings_.reset();
        stage_ = Stage::setup;
        status_override_ = L"本次双面流程已结束。";
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
        if (finished != Stage::opening) {
            result.error += L"\n\n请核对打印队列和已出纸张后再重试。";
        }
        show_error(std::move(result.error));
        return;
    }
    if (finished == Stage::opening) {
        document_ = std::move(result.document);
        file_ = std::move(pending_file_);
        const auto file_label = file_.filename().wstring() + L"  ·  " + std::to_wstring(document_->page_count()) + L" 页";
        SetWindowTextW(file_name_, file_label.c_str());
        stage_ = Stage::opening; // Resetting the edit sends EN_CHANGE synchronously.
        SetWindowTextW(range_, L"");
        stage_ = Stage::setup;
        update_plan();
    } else if (finished == Stage::first_pass && plan_ && !plan_->second_pass.empty()) {
        settings_ = std::move(result.settings);
        stage_ = Stage::flip;
    } else {
        settings_.reset();
        stage_ = Stage::setup;
        status_override_ = L"打印任务已提交。";
    }
    update_controls();
    SetFocus(IsWindowEnabled(print_) ? print_ : browse_);
}

void MainWindow::show_error(std::wstring message)
{
    if (status_ && print_) {
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
