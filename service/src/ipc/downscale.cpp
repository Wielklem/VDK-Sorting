#include "ipc/downscale.hpp"

#include <algorithm>
#include <cstring>

#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>

namespace vsort::service {

PreviewSize previewSize(std::uint32_t srcWidth, std::uint32_t srcHeight,
                        std::uint32_t maxWidth) noexcept {
    if (srcWidth == 0 || srcHeight == 0 || maxWidth == 0) {
        return {};
    }
    if (srcWidth <= maxWidth) {
        return {srcWidth, srcHeight};
    }
    const std::uint64_t scaled =
        (static_cast<std::uint64_t>(srcHeight) * maxWidth + srcWidth / 2) / srcWidth;
    return {maxWidth, static_cast<std::uint32_t>(std::max<std::uint64_t>(scaled, 1))};
}

Result<ScaledImage> downscaleForPreview(const camera::Frame& frame, std::uint32_t maxWidth) {
    const auto& meta = frame.meta;
    if (meta.width == 0 || meta.height == 0 || maxWidth == 0) {
        return makeError(Errc::InvalidArgument, "empty frame or maxWidth 0");
    }

    int srcType = CV_8UC1;
    std::uint32_t srcChannels = 1;
    switch (meta.pixelFormat) {
    case camera::PixelFormat::Mono8:
    case camera::PixelFormat::BayerRG8:
        break;
    case camera::PixelFormat::Rgb8:
    case camera::PixelFormat::Bgr8:
        srcType = CV_8UC3;
        srcChannels = 3;
        break;
    default:
        return makeError(Errc::NotSupported, "unsupported pixel format");
    }
    const bool gray = meta.pixelFormat == camera::PixelFormat::Mono8;

    const std::uint64_t rowBytes = static_cast<std::uint64_t>(meta.width) * srcChannels;
    if (meta.strideBytes < rowBytes) {
        return makeError(Errc::InvalidArgument, "stride smaller than one row");
    }
    const std::uint64_t needed =
        static_cast<std::uint64_t>(meta.strideBytes) * (meta.height - 1) + rowBytes;
    if (frame.data.size() < needed) {
        return makeError(Errc::InvalidArgument, "frame buffer shorter than its metadata says");
    }

    const PreviewSize target = previewSize(meta.width, meta.height, maxWidth);
    ScaledImage out;
    out.width = target.width;
    out.height = target.height;
    out.format = gray ? ipc::PreviewPixelFormat::Mono8 : ipc::PreviewPixelFormat::Rgb8;
    out.pixels.resize(static_cast<std::size_t>(target.width) * target.height *
                      ipc::bytesPerPixel(out.format));

    try {
        // cv::Mat has no const view; the data is only read.
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-const-cast)
        void* srcData = const_cast<std::byte*>(frame.data.data());
        const cv::Mat src(static_cast<int>(meta.height), static_cast<int>(meta.width), srcType,
                          srcData, meta.strideBytes);
        cv::Mat dst(static_cast<int>(target.height), static_cast<int>(target.width),
                    gray ? CV_8UC1 : CV_8UC3, out.pixels.data());

        const bool shrink = target.width < meta.width;
        const int interpolation = shrink ? cv::INTER_AREA : cv::INTER_LINEAR;

        if (meta.pixelFormat == camera::PixelFormat::BayerRG8) {
            cv::Mat rgb;
            // OpenCV names Bayer codes by the other corner: a camera "RG" pattern is BG here.
            cv::cvtColor(src, rgb, cv::COLOR_BayerBG2RGB);
            cv::resize(rgb, dst, dst.size(), 0, 0, interpolation);
        } else {
            cv::resize(src, dst, dst.size(), 0, 0, interpolation);
            if (meta.pixelFormat == camera::PixelFormat::Bgr8) {
                cv::cvtColor(dst, dst, cv::COLOR_BGR2RGB);
            }
        }
    } catch (const cv::Exception& ex) {
        return makeError(Errc::Internal, std::string{"OpenCV: "} + ex.what());
    }
    return out;
}

} // namespace vsort::service
