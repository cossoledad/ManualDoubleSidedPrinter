#include "pdf_preview.hpp"

#include "duplex.hpp"
#include "windows_apartment.hpp"
#include "windows_error.hpp"

#include <uxtheme.h>
#include <windowsx.h>

#include <algorithm>
#include <chrono>
#include <cmath>

namespace mdsp {
namespace {

constexpr auto preview_class = L"MDSPrinter.Preview";

void draw_bitmap(HDC dc, const RECT& target, const PageBitmap& bitmap)
{
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = static_cast<LONG>(bitmap.width);
    info.bmiHeader.biHeight = -static_cast<LONG>(bitmap.height);
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    SetStretchBltMode(dc, HALFTONE);
    SetBrushOrgEx(dc, 0, 0, nullptr);
    StretchDIBits(dc, target.left, target.top, target.right - target.left, target.bottom - target.top,
        0, 0, static_cast<int>(bitmap.width), static_cast<int>(bitmap.height), bitmap.pixels.data(),
        &info, DIB_RGB_COLORS, SRCCOPY);
}

void draw_message(HDC dc, RECT bounds, const wchar_t* message, COLORREF color)
{
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, color);
    DrawTextW(dc, message, -1, &bounds, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
}

} // namespace

HWND PdfPreview::create(HWND parent, HINSTANCE instance)
{
    WNDCLASSW info{};
    info.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
    info.lpfnWndProc = dispatch;
    info.hInstance = instance;
    info.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    info.lpszClassName = preview_class;
    if (!RegisterClassW(&info) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        windows_error(L"无法注册预览窗口");
    }
    if (!CreateWindowExW(0, preview_class, L"PDF 页面预览", WS_CHILD | WS_VISIBLE | WS_GROUP,
        0, 0, 0, 0, parent, nullptr, instance, this)) {
        windows_error(L"无法创建预览窗口");
    }
    return window_;
}

LRESULT CALLBACK PdfPreview::dispatch(HWND window, UINT message, WPARAM wparam, LPARAM lparam)
{
    auto* self = reinterpret_cast<PdfPreview*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        self = static_cast<PdfPreview*>(reinterpret_cast<const CREATESTRUCTW*>(lparam)->lpCreateParams);
        self->window_ = window;
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    if (!self) {
        return DefWindowProcW(window, message, wparam, lparam);
    }
    const LRESULT result = self->handle(message, wparam, lparam);
    if (message == WM_NCDESTROY) {
        self->window_ = nullptr;
    }
    return result;
}

LRESULT PdfPreview::handle(UINT message, WPARAM wparam, LPARAM lparam)
{
    const POINT pointer{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
    switch (message) {
    case WM_SETFONT:
        font_ = reinterpret_cast<HFONT>(wparam);
        if (LOWORD(lparam)) { invalidate(); }
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_SIZE:
        invalidate();
        return 0;
    case WM_PAINT: {
        PAINTSTRUCT paint{};
        const auto target = BeginPaint(window_, &paint);
        RECT client{};
        GetClientRect(window_, &client);
        HDC dc = nullptr;
        const auto buffer = BeginBufferedPaint(target, &client, BPBF_COMPATIBLEBITMAP, nullptr, &dc);
        if (!buffer) { dc = target; }
        if (font_) { SelectObject(dc, font_); }
        draw(dc, client, padding());
        if (buffer) { EndBufferedPaint(buffer, TRUE); }
        EndPaint(window_, &paint);
        return 0;
    }
    case WM_PRINTCLIENT: {
        const auto dc = reinterpret_cast<HDC>(wparam);
        RECT client{};
        GetClientRect(window_, &client);
        const int saved = SaveDC(dc);
        if (saved != 0) {
            if (font_) { SelectObject(dc, font_); }
            draw(dc, client, padding());
            RestoreDC(dc, saved);
        }
        return 0;
    }
    case WM_MOUSEWHEEL: {
        auto anchor = pointer;
        ScreenToClient(window_, &anchor);
        zoom_at(anchor, std::pow(1.2, static_cast<double>(GET_WHEEL_DELTA_WPARAM(wparam)) / WHEEL_DELTA));
        return 0;
    }
    case WM_LBUTTONDOWN:
        if (can_pan()) {
            SetCapture(window_);
            if (GetCapture() == window_) { drag_ = pointer; }
        }
        return 0;
    case WM_LBUTTONDBLCLK:
        reset_view();
        return 0;
    case WM_MOUSEMOVE:
        if (drag_) { pan(pointer); }
        return 0;
    case WM_LBUTTONUP:
    case WM_CANCELMODE:
        if (GetCapture() == window_) { ReleaseCapture(); }
        return 0;
    case WM_CAPTURECHANGED:
        drag_.reset();
        return 0;
    case WM_SETCURSOR:
        if (LOWORD(lparam) == HTCLIENT && can_pan()) {
            SetCursor(LoadCursorW(nullptr, IDC_HAND));
            return TRUE;
        }
        break;
    }
    return DefWindowProcW(window_, message, wparam, lparam);
}

void PdfPreview::invalidate() const
{
    if (window_) { InvalidateRect(window_, nullptr, FALSE); }
}

void PdfPreview::reset_view()
{
    if (window_ && GetCapture() == window_) { ReleaseCapture(); }
    zoom_ = 1;
    offset_x_ = offset_y_ = 0;
    invalidate();
}

int PdfPreview::padding() const
{
    return MulDiv(16, GetDpiForWindow(window_), USER_DEFAULT_SCREEN_DPI);
}

SIZE PdfPreview::paper_size(RECT canvas) const
{
    const double width = bitmap_ ? bitmap_->width : 210.0;
    const double height = bitmap_ ? bitmap_->height : 297.0;
    const double scale = std::min((canvas.right - canvas.left) / width, (canvas.bottom - canvas.top) / height) * zoom_;
    return {std::max(1L, std::lround(width * scale)), std::max(1L, std::lround(height * scale))};
}

bool PdfPreview::can_pan() const
{
    return bitmap_.has_value() || requested_.page == blank_page;
}

void PdfPreview::zoom_at(POINT anchor, double change)
{
    if (!bitmap_ && requested_.page != blank_page) { return; }

    RECT client{};
    GetClientRect(window_, &client);
    if (!PtInRect(&client, anchor)) { return; }
    const int inset = padding();
    InflateRect(&client, -inset, -inset);
    if (IsRectEmpty(&client)) { return; }

    const auto old = paper_size(client);
    const double x = anchor.x - (client.left + client.right) / 2.0;
    const double y = anchor.y - (client.top + client.bottom) / 2.0;
    zoom_ = std::clamp(zoom_ * change, 0.5, 4.0);
    const auto next = paper_size(client);
    offset_x_ += x / next.cx - x / old.cx;
    offset_y_ += y / next.cy - y / old.cy;
    invalidate();
}

void PdfPreview::pan(POINT point)
{
    RECT client{};
    GetClientRect(window_, &client);
    const int inset = padding();
    InflateRect(&client, -inset, -inset);
    if (!IsRectEmpty(&client)) {
        const auto paper = paper_size(client);
        const double dx = static_cast<double>(point.x - drag_->x) / paper.cx;
        const double dy = static_cast<double>(point.y - drag_->y) / paper.cy;
        offset_x_ += dx;
        offset_y_ += dy;
        invalidate();
    }
    drag_ = point;
}

void PdfPreview::show(std::shared_ptr<const PdfDocument> document, std::optional<int> page)
{
    const Request next{std::move(document), page};
    if (next == requested_) {
        return;
    }
    requested_ = next;
    bitmap_.reset();
    failed_ = false;
    reset_view();
    start();
}

void PdfPreview::start()
{
    if (render_.valid() || !requested_.document || requested_.page.value_or(blank_page) == blank_page) {
        return;
    }
    try {
        render_ = std::async(std::launch::async, [document = requested_.document, number = *requested_.page] {
            const Apartment apartment(winrt::apartment_type::multi_threaded);
            constexpr double max_edge = 1400;
            const auto size = document->page_size(number);
            const double scale = max_edge / std::max(size.width, size.height);
            const auto width = static_cast<std::uint32_t>(std::max(1L, std::lround(size.width * scale)));
            const auto height = static_cast<std::uint32_t>(std::max(1L, std::lround(size.height * scale)));
            return document->render_page(number, width, height);
        });
        rendering_ = requested_;
    } catch (...) {
        failed_ = true;
    }
}

bool PdfPreview::poll()
{
    if (!render_.valid() || render_.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
        return false;
    }

    const bool current = rendering_ == requested_;
    try {
        auto image = render_.get();
        if (current) {
            bitmap_ = std::move(image);
        }
    } catch (...) {
        failed_ = current;
    }
    rendering_ = {};
    if (!current) {
        start();
    }
    return true;
}

bool PdfPreview::pending() const
{
    return render_.valid();
}

void PdfPreview::draw(HDC dc, RECT bounds, int padding) const
{
    FillRect(dc, &bounds, GetSysColorBrush(COLOR_3DFACE));
    FrameRect(dc, &bounds, GetSysColorBrush(COLOR_3DLIGHT));
    const RECT frame = bounds;
    InflateRect(&bounds, -padding, -padding);
    if (IsRectEmpty(&bounds)) {
        return;
    }

    if (bitmap_ || requested_.page == blank_page) {
        const int state = SaveDC(dc);
        if (!state) { return; }
        IntersectClipRect(dc, frame.left + 1, frame.top + 1, frame.right - 1, frame.bottom - 1);
        const auto size = paper_size(bounds);
        const auto width = bounds.right - bounds.left;
        const auto height = bounds.bottom - bounds.top;
        const double x = bounds.left + (width - size.cx) / 2.0 + offset_x_ * size.cx;
        const double y = bounds.top + (height - size.cy) / 2.0 + offset_y_ * size.cy;
        if (x >= frame.right || y >= frame.bottom || x + size.cx + 2 <= frame.left || y + size.cy + 3 <= frame.top) {
            RestoreDC(dc, state);
            return;
        }
        const long left = std::lround(x);
        const long top = std::lround(y);
        RECT paper{left, top, left + size.cx, top + size.cy};
        RECT shadow = paper;
        OffsetRect(&shadow, 2, 3);
        FillRect(dc, &shadow, GetSysColorBrush(COLOR_3DSHADOW));
        FillRect(dc, &paper, static_cast<HBRUSH>(GetStockObject(WHITE_BRUSH)));
        if (bitmap_) {
            draw_bitmap(dc, paper, *bitmap_);
        } else {
            draw_message(dc, paper, L"空白占位", RGB(76, 91, 111));
        }
        RestoreDC(dc, state);
        return;
    }

    const wchar_t* message = L"正在渲染页面…";
    if (!requested_.document) {
        message = L"尚未打开 PDF";
    } else if (!requested_.page) {
        message = L"当前没有可预览的打印页";
    } else if (failed_) {
        message = L"无法渲染此页";
    }
    draw_message(dc, bounds, message, GetSysColor(COLOR_BTNTEXT));
}

} // namespace mdsp
