#pragma once

#include <QImage>
#include <QList>

#include "video/frame_convert.hpp"
#include "video/video_item.hpp"

namespace vsort::hmi {

// HSV editor picture (G60.20, P60.45): one camera's live frames as the mask of one channel
// (view 0 = H, 1 = S, 2 = V; see channelMask) or as the result (view 3: colour with the pixels
// inside all three ranges green; see rangeOverlay). Per frame it also reports the histogram of
// its channel and the share of pixels in range.
class HsvViewItem : public VideoItem {
    Q_OBJECT
    Q_PROPERTY(int view READ view WRITE setView NOTIFY viewChanged)
    Q_PROPERTY(QList<int> hsvLower READ hsvLower WRITE setHsvLower NOTIFY rangeChanged)
    Q_PROPERTY(QList<int> hsvUpper READ hsvUpper WRITE setHsvUpper NOTIFY rangeChanged)
    // Channel views: pixel count per channel value (180 for H, 256 for S and V); result: empty.
    Q_PROPERTY(QList<int> histogram READ histogram NOTIFY statsChanged)
    // Channel views: % of the pixels inside that channel's range; result: inside all three.
    Q_PROPERTY(double inRangePercent READ inRangePercent NOTIFY statsChanged)

public:
    static constexpr int kResultView = 3;

    explicit HsvViewItem(QQuickItem* parent = nullptr);

    [[nodiscard]] int view() const noexcept { return view_; }
    void setView(int view);
    [[nodiscard]] QList<int> hsvLower() const;
    void setHsvLower(const QList<int>& hsv);
    [[nodiscard]] QList<int> hsvUpper() const;
    void setHsvUpper(const QList<int>& hsv);
    [[nodiscard]] QList<int> histogram() const { return histogram_; }
    [[nodiscard]] double inRangePercent() const noexcept { return inRangePercent_; }

signals:
    void viewChanged();
    void rangeChanged();
    void statsChanged();

protected:
    [[nodiscard]] QImage present(const QImage& frame) override;

private:
    void publish(const QList<int>& histogram, double percent); // GUI thread

    int view_{kResultView}; // GUI thread; read in present() (GUI blocked)
    HsvRange range_;        // same
    QList<int> histogram_;  // GUI thread
    double inRangePercent_{0.0};
};

} // namespace vsort::hmi
