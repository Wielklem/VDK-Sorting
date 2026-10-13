#include "analysis/stages.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include <opencv2/imgproc.hpp>

namespace vsort::service {

namespace {

constexpr int kFillPadPx = 3; // background margin around a component, so a flood fill can start

// Fills only holes enclosed by the object (specks, shadows). A notch open to the background
// stays background. Skipped when the corner pixel is not background (no safe seed).
void fillHoles(cv::Mat& mask) {
    if (mask.empty() || mask.at<std::uint8_t>(0, 0) != 0) {
        return;
    }
    cv::Mat flooded = mask.clone();
    cv::floodFill(flooded, cv::Point{0, 0}, cv::Scalar{255});
    cv::Mat holes;
    cv::bitwise_not(flooded, holes);
    cv::bitwise_or(mask, holes, mask);
}

double diameterOf(const std::vector<cv::Point>& contour) {
    const cv::Rect box = cv::boundingRect(contour);
    double diameter = std::hypot(static_cast<double>(box.width), static_cast<double>(box.height));
    if (contour.size() >= 5) {
        const cv::RotatedRect ellipse = cv::fitEllipse(contour);
        diameter = std::max(diameter,
                            static_cast<double>(std::max(ellipse.size.width, ellipse.size.height)));
    }
    return diameter;
}

bool inside(const cv::Rect& rect, const cv::Point2d& p) {
    return p.x >= rect.x && p.x < rect.x + rect.width && p.y >= rect.y &&
           p.y < rect.y + rect.height;
}

// Splitting of touching objects (port of eqraftvision split_touching_blobs_concave, one cut per
// pass). Two objects pinch inward where they meet; a single object has at most one deep notch.
constexpr double kSplitMinSolidity = 0.80;     // both pieces must be at least this convex ...
constexpr double kSplitMinChordInside = 0.75;  // share of the cut line that must lie in the mask
constexpr double kSplitMinPieceFrac = 0.10;    // each piece: at least this share of the blob area
constexpr int kSplitSeamPx = 3;                // width of the cut
constexpr int kSplitMaxDepth = 4;              // cuts in a row on one blob (max 16 pieces)
constexpr std::size_t kSplitMaxCandidates = 8; // deepest notches considered

double solidityOf(const cv::Mat& mask) {
    std::vector<std::vector<cv::Point>> contours;
    cv::findContours(mask, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);
    if (contours.empty()) {
        return 0.0;
    }
    const auto& largest =
        *std::ranges::max_element(contours, {}, [](const auto& c) { return cv::contourArea(c); });
    std::vector<cv::Point> hull;
    cv::convexHull(largest, hull);
    const double hullArea = cv::contourArea(hull);
    return hullArea > 0.0 ? static_cast<double>(cv::countNonZero(mask)) / hullArea : 0.0;
}

double chordInsideFraction(const cv::Mat& mask, cv::Point a, cv::Point b) {
    cv::LineIterator it{mask, a, b, 8};
    int on = 0;
    for (int i = 0; i < it.count; ++i, ++it) {
        if (**it != 0) {
            ++on;
        }
    }
    return it.count > 0 ? static_cast<double>(on) / static_cast<double>(it.count) : 0.0;
}

// Two pieces if the shape has a real pinch, otherwise empty.
std::vector<cv::Mat> cutAtDeepestPinch(const cv::Mat& mask, const AnalysisParams& p) {
    std::vector<std::vector<cv::Point>> contours;
    cv::findContours(mask, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_NONE);
    if (contours.empty()) {
        return {};
    }
    const auto& contour =
        *std::ranges::max_element(contours, {}, [](const auto& c) { return cv::contourArea(c); });
    std::vector<int> hullIdx;
    cv::convexHull(contour, hullIdx, false, false);
    if (hullIdx.size() < 3) {
        return {};
    }
    std::ranges::sort(hullIdx);
    std::vector<cv::Vec4i> defects;
    cv::convexityDefects(contour, hullIdx, defects);

    const double area = static_cast<double>(cv::countNonZero(mask));
    const double minDepth = p.splitMinDepthFrac * std::sqrt(area);
    std::vector<std::pair<double, cv::Point>> deep; // depth, deepest point of the notch
    for (const auto& d : defects) {
        const double depth = d[3] / 256.0;
        if (depth >= minDepth) {
            deep.emplace_back(depth, contour[static_cast<std::size_t>(d[2])]);
        }
    }
    if (deep.size() < 2) {
        return {};
    }
    std::ranges::sort(deep, [](const auto& a, const auto& b) { return a.first > b.first; });
    deep.resize(std::min(deep.size(), kSplitMaxCandidates));

    struct CutPair {
        double dist;
        cv::Point a;
        cv::Point b;
    };
    std::vector<CutPair> pairs;
    for (std::size_t i = 0; i < deep.size(); ++i) {
        for (std::size_t j = i + 1; j < deep.size(); ++j) {
            pairs.push_back({cv::norm(deep[i].second - deep[j].second), deep[i].second,
                             deep[j].second});
        }
    }
    std::ranges::sort(pairs, {}, &CutPair::dist); // shortest cut first

    const double parentSolidity = solidityOf(mask);
    const int minPiecePx = static_cast<int>(kSplitMinPieceFrac * area);
    for (const auto& pair : pairs) {
        if (chordInsideFraction(mask, pair.a, pair.b) < kSplitMinChordInside) {
            continue;
        }
        cv::Mat trial = mask.clone();
        cv::line(trial, pair.a, pair.b, cv::Scalar{0}, kSplitSeamPx);
        cv::Mat labels;
        cv::Mat stats;
        cv::Mat centroids;
        const int n = cv::connectedComponentsWithStats(trial, labels, stats, centroids, 8, CV_32S);
        std::vector<int> big;
        for (int label = 1; label < n; ++label) {
            if (stats.at<int>(label, cv::CC_STAT_AREA) >= minPiecePx) {
                big.push_back(label);
            }
        }
        if (big.size() != 2) {
            continue;
        }
        std::vector<cv::Mat> pieces;
        for (const int label : big) {
            cv::Mat piece;
            cv::compare(labels, cv::Scalar{static_cast<double>(label)}, piece, cv::CMP_EQ);
            pieces.push_back(std::move(piece));
        }
        const double worst = std::min(solidityOf(pieces[0]), solidityOf(pieces[1]));
        if (worst >= p.splitMinSolidity || worst > parentSolidity) {
            return pieces;
        }
    }
    return {};
}

// Recursive: a piece may hold a further pinch. Always returns at least the input itself.
void splitAtPinches(const cv::Mat& mask, int depth, const AnalysisParams& p,
                    std::vector<cv::Mat>& out) {
    if (depth < kSplitMaxDepth) {
        const auto pieces = cutAtDeepestPinch(mask, p);
        if (pieces.size() == 2) {
            for (const auto& piece : pieces) {
                splitAtPinches(piece, depth + 1, p, out);
            }
            return;
        }
    }
    out.push_back(mask);
}

// Port of eqraftvision pca_oriented_rect().
void orientedExtent(Blob& blob) {
    const cv::Moments m = cv::moments(blob.contour);
    if (m.m00 <= 0.0) {
        const cv::RotatedRect rect = cv::minAreaRect(blob.hull);
        blob.lengthPx = std::max(rect.size.width, rect.size.height);
        blob.widthPx = std::min(rect.size.width, rect.size.height);
        rect.points(blob.box.data());
        return;
    }
    const double cx = m.m10 / m.m00;
    const double cy = m.m01 / m.m00;
    const double angle = 0.5 * std::atan2(2.0 * m.mu11 / m.m00, (m.mu20 - m.mu02) / m.m00);
    const double c = std::cos(angle);
    const double s = std::sin(angle);
    double u0 = 0.0;
    double u1 = 0.0;
    double v0 = 0.0;
    double v1 = 0.0;
    bool first = true;
    for (const auto& p : blob.hull) {
        const double dx = p.x - cx;
        const double dy = p.y - cy;
        const double u = dx * c + dy * s;  // along the principal axis
        const double v = -dx * s + dy * c; // across it
        if (first) {
            u0 = u1 = u;
            v0 = v1 = v;
            first = false;
        } else {
            u0 = std::min(u0, u);
            u1 = std::max(u1, u);
            v0 = std::min(v0, v);
            v1 = std::max(v1, v);
        }
    }
    blob.lengthPx = u1 - u0;
    blob.widthPx = v1 - v0;
    const std::array<std::pair<double, double>, 4> corners{
        {{u0, v0}, {u1, v0}, {u1, v1}, {u0, v1}}};
    for (std::size_t i = 0; i < corners.size(); ++i) {
        const auto [u, v] = corners.at(i);
        blob.box.at(i) = cv::Point2f{static_cast<float>(u * c - v * s + cx),
                                     static_cast<float>(u * s + v * c + cy)};
    }
    if (blob.widthPx > blob.lengthPx) { // nearly round: the moment axis can be the short one
        std::swap(blob.lengthPx, blob.widthPx);
    }
}

} // namespace

Result<> PreprocessStage::process(AnalysisContext& ctx) const {
    if (ctx.raw.empty()) {
        return makeError(Errc::InvalidArgument, "preprocess: no image");
    }
    const bool color = ctx.pixelFormat == camera::PixelFormat::Rgb8 ||
                       ctx.pixelFormat == camera::PixelFormat::Bgr8;
    if (ctx.raw.type() != (color ? CV_8UC3 : CV_8UC1)) {
        return makeError(Errc::InvalidArgument, "preprocess: image type does not match format");
    }
    switch (ctx.pixelFormat) {
    case camera::PixelFormat::Mono8:
        cv::cvtColor(ctx.raw, ctx.bgr, cv::COLOR_GRAY2BGR);
        break;
    case camera::PixelFormat::BayerRG8:
        // OpenCV names Bayer codes by the other corner: a camera "RG" pattern is BG here.
        cv::cvtColor(ctx.raw, ctx.bgr, cv::COLOR_BayerBG2BGR);
        break;
    case camera::PixelFormat::Rgb8:
        cv::cvtColor(ctx.raw, ctx.bgr, cv::COLOR_RGB2BGR);
        break;
    case camera::PixelFormat::Bgr8:
        ctx.bgr = ctx.raw; // view; later stages only read it
        break;
    }
    return {};
}

Result<> ColorSegmentationStage::process(AnalysisContext& ctx) const {
    if (ctx.bgr.empty() || ctx.bgr.type() != CV_8UC3) {
        return makeError(Errc::InvalidArgument, "segmentation: needs a BGR image (preprocess)");
    }
    const auto& p = params_;
    cv::Mat hsv;
    cv::Mat smoothed;
    cv::GaussianBlur(ctx.bgr, smoothed, cv::Size{5, 5}, 0.0); // less pixel noise at the edge
    cv::cvtColor(smoothed, hsv, cv::COLOR_BGR2HSV);
    cv::inRange(hsv, cv::Scalar{p.hsvLower[0] * 1.0, p.hsvLower[1] * 1.0, p.hsvLower[2] * 1.0},
                cv::Scalar{p.hsvUpper[0] * 1.0, p.hsvUpper[1] * 1.0, p.hsvUpper[2] * 1.0},
                ctx.mask);
    if (p.morphKernelPx > 0) {
        const int k = static_cast<int>(p.morphKernelPx);
        const cv::Mat kernel = cv::getStructuringElement(cv::MORPH_ELLIPSE, cv::Size{k, k});
        if (p.erodeIterations > 0) {
            cv::erode(ctx.mask, ctx.mask, kernel, cv::Point{-1, -1},
                      static_cast<int>(p.erodeIterations));
        }
        if (p.dilateIterations > 0) {
            cv::dilate(ctx.mask, ctx.mask, kernel, cv::Point{-1, -1},
                       static_cast<int>(p.dilateIterations));
        }
    }

    // Round off the per-pixel boundary: blur, then re-threshold (port of eqraftvision
    // _smooth_mask). Trims spurs and fills notches symmetrically; opening alone only trims spurs.
    if (p.edgeSmoothSigmaPx > 0.0) {
        cv::GaussianBlur(ctx.mask, ctx.mask, cv::Size{}, p.edgeSmoothSigmaPx);
        cv::threshold(ctx.mask, ctx.mask, 127.0, 255.0, cv::THRESH_BINARY);
    }

    const cv::Rect roi = ctx.roi & cv::Rect{0, 0, ctx.mask.cols, ctx.mask.rows};
    const double roiArea = static_cast<double>(roi.area());
    const double maskPct = roiArea > 0.0 ? 100.0 * cv::countNonZero(ctx.mask(roi)) / roiArea : 0.0;

    cv::Mat labels;
    cv::Mat stats;
    cv::Mat centroids;
    const int count =
        cv::connectedComponentsWithStats(ctx.mask, labels, stats, centroids, 8, CV_32S);
    ctx.blobs.clear();
    ctx.neighbours.clear();
    for (int label = 1; label < count; ++label) {
        if (stats.at<int>(label, cv::CC_STAT_AREA) < static_cast<int>(p.minBlobAreaPx)) {
            continue;
        }
        const cv::Rect box{
            stats.at<int>(label, cv::CC_STAT_LEFT), stats.at<int>(label, cv::CC_STAT_TOP),
            stats.at<int>(label, cv::CC_STAT_WIDTH), stats.at<int>(label, cv::CC_STAT_HEIGHT)};
        const cv::Rect padded = cv::Rect{box.x - kFillPadPx, box.y - kFillPadPx,
                                         box.width + 2 * kFillPadPx, box.height + 2 * kFillPadPx} &
                                cv::Rect{0, 0, labels.cols, labels.rows};
        cv::Mat component;
        cv::compare(labels(padded), cv::Scalar{static_cast<double>(label)}, component, cv::CMP_EQ);
        fillHoles(component);

        std::vector<cv::Mat> pieces; // touching objects -> one piece each, filtered separately
        splitAtPinches(component, 0, p, pieces);
        for (const cv::Mat& piece : pieces) {
            std::vector<std::vector<cv::Point>> contours;
            cv::findContours(piece, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE,
                             padded.tl());
            if (contours.empty()) {
                continue;
            }
            Blob blob;
            blob.contour = *std::ranges::max_element(
                contours, {}, [](const auto& c) { return cv::contourArea(c); });
            blob.areaPx = cv::countNonZero(piece);
            blob.diameterPx = diameterOf(blob.contour);
            cv::convexHull(blob.contour, blob.hull);
            blob.hullAreaPx = cv::contourArea(blob.hull);
            blob.solidity = blob.hullAreaPx > 0.0 ? blob.areaPx / blob.hullAreaPx : 0.0;

            const cv::Rect hullBox = cv::boundingRect(blob.hull);
            const double aspect =
                hullBox.height > 0 ? static_cast<double>(hullBox.width) / hullBox.height : 0.0;
            const bool shapeOk = aspect >= 1.0 / p.maxAspectRatio &&
                                 aspect <= p.maxAspectRatio && blob.solidity >= p.minSolidity;
            if (!shapeOk || blob.diameterPx < p.minDiameterPx ||
                blob.hullAreaPx < p.minHullAreaPx) {
                continue;
            }
            const cv::Moments m = cv::moments(blob.hull);
            if (m.m00 <= 0.0) {
                continue;
            }
            blob.centroid = cv::Point2d{m.m10 / m.m00, m.m01 / m.m00};
            (inside(roi, blob.centroid) ? ctx.blobs : ctx.neighbours).push_back(std::move(blob));
        }
    }
    std::ranges::sort(ctx.blobs, std::ranges::greater{}, &Blob::areaPx);

    ctx.values.push_back({std::string{kKeyCount}, static_cast<double>(ctx.blobs.size())});
    ctx.values.push_back({std::string{kKeyPresent}, ctx.blobs.empty() ? 0.0 : 1.0});
    ctx.values.push_back({std::string{kKeyMaskPct}, maskPct});
    return {};
}

Result<> SizeStage::process(AnalysisContext& ctx) const {
    ctx.mmPerPx = (params_.roiWidthMm > 0.0 && ctx.roi.width > 0)
                      ? params_.roiWidthMm / ctx.roi.width
                      : params_.mmPerPx;
    for (auto& blob : ctx.blobs) {
        orientedExtent(blob);
    }
    for (auto& blob : ctx.neighbours) {
        orientedExtent(blob);
    }
    if (!ctx.blobs.empty()) {
        const auto& largest = ctx.blobs.front();
        ctx.values.push_back({std::string{kKeyLength}, largest.lengthPx * ctx.mmPerPx});
        ctx.values.push_back({std::string{kKeyWidth}, largest.widthPx * ctx.mmPerPx});
        ctx.values.push_back({std::string{kKeyArea}, largest.areaPx * ctx.mmPerPx * ctx.mmPerPx});
    }
    return {};
}

} // namespace vsort::service
