#pragma once

#include <winrt/base.h>

#include <cstdint>
#include <filesystem>
#include <memory>

namespace mdsp {

struct PageSize {
    double width;
    double height;
};

struct PageBitmap {
    std::uint32_t width;
    std::uint32_t height;
    winrt::com_array<std::uint8_t> pixels;
};

class PdfDocument {
public:
    // PDF operations run on an MTA worker. The Windows PDF document is agile.
    explicit PdfDocument(const std::filesystem::path& path);
    ~PdfDocument();

    int page_count() const noexcept;
    PageSize page_size(int page_number) const;
    PageBitmap render_page(int page_number, std::uint32_t width, std::uint32_t height,
                           bool rotate_clockwise = false) const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace mdsp
