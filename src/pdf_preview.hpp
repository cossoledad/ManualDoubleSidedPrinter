#pragma once

#include "pdf_document.hpp"

#include <windows.h>

#include <future>
#include <optional>

namespace mdsp {

class PdfPreview {
public:
    HWND create(HWND parent, HINSTANCE instance);
    void invalidate() const;
    void reset_view();
    void show(std::shared_ptr<const PdfDocument> document, std::optional<int> page);
    bool poll();
    bool pending() const;
    void draw(HDC dc, RECT bounds, int padding) const;

private:
    static LRESULT CALLBACK dispatch(HWND window, UINT message, WPARAM wparam, LPARAM lparam);
    LRESULT handle(UINT message, WPARAM wparam, LPARAM lparam);
    struct Request {
        std::shared_ptr<const PdfDocument> document;
        std::optional<int> page;

        bool operator==(const Request&) const = default;
    };

    void start();
    void zoom_at(POINT point, double change);
    void pan(POINT point);
    SIZE paper_size(RECT canvas) const;
    int padding() const;
    bool can_pan() const;

    HWND window_ = nullptr;
    HFONT font_ = nullptr;
    double zoom_ = 1.0;
    double offset_x_ = 0.0;
    double offset_y_ = 0.0;
    std::optional<POINT> drag_;
    Request requested_;
    Request rendering_;
    std::optional<PageBitmap> bitmap_;
    bool failed_ = false;
    std::future<PageBitmap> render_;
};

} // namespace mdsp
