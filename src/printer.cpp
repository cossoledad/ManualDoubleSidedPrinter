#include "printer.hpp"

#include "duplex.hpp"
#include "pdf_document.hpp"
#include "windows_error.hpp"

#include <winspool.h>

#include <algorithm>
#include <cmath>
#include <memory>
#include <type_traits>

namespace mdsp {
namespace {

struct PrinterCloser {
    void operator()(HANDLE printer) const noexcept { ClosePrinter(printer); }
};

struct DcCloser {
    void operator()(HDC dc) const noexcept { DeleteDC(dc); }
};

class PrintJob {
public:
    PrintJob(HDC dc, const std::wstring& name) : dc_(dc)
    {
        DOCINFOW info{};
        info.cbSize = sizeof(info);
        info.lpszDocName = name.c_str();
        if (StartDocW(dc_, &info) <= 0) {
            windows_error(L"无法创建打印任务");
        }
    }

    ~PrintJob()
    {
        if (active_) {
            AbortDoc(dc_);
        }
    }

    void finish()
    {
        if (EndDoc(dc_) <= 0) {
            windows_error(L"无法提交打印任务");
        }
        active_ = false;
    }

    PrintJob(const PrintJob&) = delete;
    PrintJob& operator=(const PrintJob&) = delete;

private:
    HDC dc_;
    bool active_ = true;
};

struct Paper {
    int width;
    int height;
    int dpi_x;
    int dpi_y;
};

struct Placement {
    int x;
    int y;
    int width;
    int height;
    std::uint32_t raster_width;
    std::uint32_t raster_height;
    bool rotate;
};

Placement fit_page(const Paper& paper, PageSize page)
{
    // Compare in physical units so printers with unequal X/Y resolutions preserve aspect ratio.
    const double printable_width = static_cast<double>(paper.width) / paper.dpi_x;
    const double printable_height = static_cast<double>(paper.height) / paper.dpi_y;
    const double normal_scale = std::min(printable_width / page.width, printable_height / page.height);
    const double rotated_scale = std::min(printable_width / page.height, printable_height / page.width);
    constexpr double dips_per_inch = 96;
    const bool rotate = normal_scale < 1 / dips_per_inch && rotated_scale > normal_scale;
    const double scale = std::min(rotate ? rotated_scale : normal_scale, 1 / dips_per_inch);
    const double width_inches = page.width * scale;
    const double height_inches = page.height * scale;
    const int width = std::clamp(static_cast<int>(std::lround((rotate ? height_inches : width_inches) * paper.dpi_x)),
                                 1, paper.width);
    const int height = std::clamp(static_cast<int>(std::lround((rotate ? width_inches : height_inches) * paper.dpi_y)),
                                  1, paper.height);

    constexpr double max_raster_edge = 4096;
    const double render_dpi = std::min({300.0, static_cast<double>(std::max(paper.dpi_x, paper.dpi_y)),
                                       max_raster_edge / std::max(width_inches, height_inches)});
    const auto raster_width = static_cast<std::uint32_t>(std::max(1L, std::lround(width_inches * render_dpi)));
    const auto raster_height = static_cast<std::uint32_t>(std::max(1L, std::lround(height_inches * render_dpi)));
    return {(paper.width - width) / 2, (paper.height - height) / 2, width, height,
            raster_width, raster_height, rotate};
}

void draw_page(HDC dc, const Placement& placement, const PageBitmap& bitmap)
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
    const int lines = StretchDIBits(dc, placement.x, placement.y, placement.width, placement.height,
        0, 0, static_cast<int>(bitmap.width), static_cast<int>(bitmap.height), bitmap.pixels.data(),
        &info, DIB_RGB_COLORS, SRCCOPY);
    if (lines == 0 || lines == GDI_ERROR) {
        windows_error(L"无法将 PDF 页面送入打印任务");
    }
}

} // namespace

Printers list_printers()
{
    constexpr DWORD flags = PRINTER_ENUM_LOCAL | PRINTER_ENUM_CONNECTIONS;
    DWORD needed = 0;
    DWORD count = 0;
    if (!EnumPrintersW(flags, nullptr, 4, nullptr, 0, &needed, &count) && GetLastError() != ERROR_INSUFFICIENT_BUFFER) {
        windows_error(L"无法读取打印机列表");
    }

    Printers printers;
    if (needed != 0) {
        std::vector<BYTE> buffer(needed);
        if (!EnumPrintersW(flags, nullptr, 4, buffer.data(), needed, &needed, &count)) {
            windows_error(L"无法读取打印机列表");
        }
        const auto entries = reinterpret_cast<const PRINTER_INFO_4W*>(buffer.data());
        for (DWORD index = 0; index < count; ++index) {
            if (entries[index].pPrinterName) {
                printers.names.emplace_back(entries[index].pPrinterName);
            }
        }
        std::ranges::sort(printers.names, [](const auto& left, const auto& right) {
            return CompareStringOrdinal(left.c_str(), -1, right.c_str(), -1, TRUE) == CSTR_LESS_THAN;
        });
    }

    DWORD length = 0;
    GetDefaultPrinterW(nullptr, &length);
    if (length != 0) {
        std::wstring name(length, L'\0');
        if (GetDefaultPrinterW(name.data(), &length)) {
            name.resize(length - 1);
            printers.default_name = std::move(name);
        }
    }
    return printers;
}

PrinterSettings read_printer_settings(const std::wstring& name)
{
    auto device_name = name;
    HANDLE handle = nullptr;
    if (!OpenPrinterW(device_name.data(), &handle, nullptr)) {
        windows_error(L"无法打开所选打印机");
    }
    const std::unique_ptr<void, PrinterCloser> printer(handle);
    const auto size = DocumentPropertiesW(nullptr, printer.get(), device_name.data(), nullptr, nullptr, 0);
    if (size <= 0) {
        windows_error(L"无法读取打印机设置");
    }

    PrinterSettings settings{name, std::vector<std::byte>(size)};
    auto* mode = reinterpret_cast<DEVMODEW*>(settings.devmode.data());
    if (DocumentPropertiesW(nullptr, printer.get(), device_name.data(), mode, nullptr, DM_OUT_BUFFER) != IDOK) {
        windows_error(L"无法读取打印机设置");
    }
    if (mode->dmFields & DM_DUPLEX) {
        mode->dmDuplex = DMDUP_SIMPLEX;
    }
    if (mode->dmFields & DM_COPIES) {
        mode->dmCopies = 1;
    }
    if (mode->dmFields & DM_NUP) {
        mode->dmNup = DMNUP_ONEUP;
    }
    if (DocumentPropertiesW(nullptr, printer.get(), device_name.data(), mode, mode, DM_IN_BUFFER | DM_OUT_BUFFER) != IDOK) {
        windows_error(L"打印机驱动无法应用单面设置");
    }
    if (((mode->dmFields & DM_DUPLEX) && mode->dmDuplex != DMDUP_SIMPLEX) ||
        ((mode->dmFields & DM_COPIES) && mode->dmCopies != 1)) {
        throw UserError{L"请在打印机首选项中设为单面、单份后重试。"};
    }
    return settings;
}

void print_pass(const PdfDocument& document, const PrinterSettings& settings,
                std::span<const int> pages, const std::wstring& job_name)
{
    const auto* mode = reinterpret_cast<const DEVMODEW*>(settings.devmode.data());
    const std::unique_ptr<std::remove_pointer_t<HDC>, DcCloser> dc(
        CreateDCW(L"WINSPOOL", settings.name.c_str(), nullptr, mode));
    if (!dc) {
        windows_error(L"无法连接到打印机");
    }
    const Paper paper{GetDeviceCaps(dc.get(), HORZRES), GetDeviceCaps(dc.get(), VERTRES),
                      GetDeviceCaps(dc.get(), LOGPIXELSX), GetDeviceCaps(dc.get(), LOGPIXELSY)};
    if (paper.width <= 0 || paper.height <= 0 || paper.dpi_x <= 0 || paper.dpi_y <= 0) {
        throw UserError{L"无法取得打印机的纸张尺寸或分辨率。"};
    }

    PrintJob job(dc.get(), job_name);
    for (int number : pages) {
        if (StartPage(dc.get()) <= 0) {
            windows_error(L"打印机无法开始新的一页");
        }
        if (number == blank_page) {
            if (!PatBlt(dc.get(), 0, 0, paper.width, paper.height, WHITENESS)) {
                windows_error(L"打印机无法写入占位空白页");
            }
        } else {
            const auto placement = fit_page(paper, document.page_size(number));
            const auto bitmap = document.render_page(number, placement.raster_width, placement.raster_height,
                                                     placement.rotate);
            draw_page(dc.get(), placement, bitmap);
        }
        if (EndPage(dc.get()) <= 0) {
            windows_error(L"打印机无法接收本页");
        }
    }
    job.finish();
}

} // namespace mdsp
