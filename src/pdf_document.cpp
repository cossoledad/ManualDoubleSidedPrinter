#include "pdf_document.hpp"

#include "windows_error.hpp"

#include <shcore.h>
#include <shlwapi.h>
#include <winrt/Windows.Data.Pdf.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Graphics.Imaging.h>
#include <winrt/Windows.Storage.Streams.h>
#include <winrt/Windows.UI.h>

#include <cmath>
#include <limits>
#include <mutex>

namespace mdsp {
namespace {

namespace pdf = winrt::Windows::Data::Pdf;
namespace imaging = winrt::Windows::Graphics::Imaging;
using winrt::Windows::Storage::Streams::InMemoryRandomAccessStream;
using winrt::Windows::Storage::Streams::IRandomAccessStream;

} // namespace

struct PdfDocument::Impl {
    mutable std::mutex mutex;
    IRandomAccessStream source{nullptr};
    pdf::PdfDocument document{nullptr};
    int page_count = 0;

    pdf::PdfPage page(int number) const
    {
        if (number < 1 || number > page_count) {
            throw UserError{L"文档中没有第 " + std::to_wstring(number) + L" 页。"};
        }
        return document.GetPage(static_cast<std::uint32_t>(number - 1));
    }
};

PdfDocument::PdfDocument(const std::filesystem::path& path) try : impl_(std::make_unique<Impl>())
{
    // Hold a read-only stream so both passes refer to the same file contents.
    winrt::com_ptr<IStream> file;
    winrt::check_hresult(SHCreateStreamOnFileEx(path.c_str(), STGM_READ | STGM_SHARE_DENY_WRITE,
        FILE_ATTRIBUTE_NORMAL, FALSE, nullptr, file.put()));
    winrt::check_hresult(CreateRandomAccessStreamOverStream(file.get(), BSOS_DEFAULT,
        winrt::guid_of<IRandomAccessStream>(), winrt::put_abi(impl_->source)));
    impl_->document = pdf::PdfDocument::LoadFromStreamAsync(impl_->source).get();
    const auto count = impl_->document.PageCount();
    if (count == 0 || count > static_cast<std::uint32_t>(std::numeric_limits<int>::max())) {
        throw UserError{L"PDF 没有可打印的页面，或页数超出支持范围。"};
    }
    impl_->page_count = static_cast<int>(count);
}
catch (const winrt::hresult_error& error) {
    throw UserError{L"无法打开 PDF。" + std::wstring(error.message())};
}

PdfDocument::~PdfDocument() = default;

int PdfDocument::page_count() const noexcept
{
    return impl_->page_count;
}

PageSize PdfDocument::page_size(int page_number) const
{
    const std::scoped_lock lock(impl_->mutex);
    const auto size = impl_->page(page_number).Size();
    if (!std::isfinite(size.Width) || !std::isfinite(size.Height) || size.Width <= 0 || size.Height <= 0) {
        throw UserError{L"第 " + std::to_wstring(page_number) + L" 页的尺寸无效。"};
    }
    return {size.Width, size.Height};
}

PageBitmap PdfDocument::render_page(int page_number, std::uint32_t width, std::uint32_t height,
                                    bool rotate_clockwise) const
{
    const std::scoped_lock lock(impl_->mutex);
    const auto page = impl_->page(page_number);
    InMemoryRandomAccessStream stream;
    pdf::PdfPageRenderOptions options;
    options.DestinationWidth(width);
    options.DestinationHeight(height);
    options.BackgroundColor({255, 255, 255, 255});
    options.BitmapEncoderId(imaging::BitmapEncoder::BmpEncoderId());
    page.RenderToStreamAsync(stream, options).get();

    stream.Seek(0);
    const auto decoder = imaging::BitmapDecoder::CreateAsync(stream).get();
    imaging::BitmapTransform transform;
    if (rotate_clockwise) {
        transform.Rotation(imaging::BitmapRotation::Clockwise90Degrees);
    }
    auto pixels = decoder.GetPixelDataAsync(imaging::BitmapPixelFormat::Bgra8, imaging::BitmapAlphaMode::Ignore,
        transform, imaging::ExifOrientationMode::IgnoreExifOrientation, imaging::ColorManagementMode::DoNotColorManage)
        .get().DetachPixelData();
    const auto pixel_width = decoder.PixelWidth();
    const auto pixel_height = decoder.PixelHeight();
    return rotate_clockwise ? PageBitmap{pixel_height, pixel_width, std::move(pixels)} :
        PageBitmap{pixel_width, pixel_height, std::move(pixels)};
}

} // namespace mdsp
