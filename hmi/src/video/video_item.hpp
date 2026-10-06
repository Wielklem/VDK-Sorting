#pragma once

#include <QQuickItem>
#include <QSize>
#include <atomic>
#include <cstdint>

#include "video/frame_mailbox.hpp"
#include "video/frame_sink.hpp"

namespace vsort::hmi {

// Shows the newest submitted frame as a GPU texture, aspect-fit and centred.
class VideoItem : public QQuickItem, public IFrameSink {
    Q_OBJECT
    Q_PROPERTY(bool hasFrame READ hasFrame NOTIFY hasFrameChanged)
    Q_PROPERTY(QSize sourceSize READ sourceSize NOTIFY sourceSizeChanged)

public:
    explicit VideoItem(QQuickItem* parent = nullptr);

    // Thread-safe; the newest frame wins. nullptr clears the picture.
    void submitFrame(FramePtr frame) override;
    Q_INVOKABLE void clear() override;

    [[nodiscard]] bool hasFrame() const noexcept { return hasFrame_; }
    [[nodiscard]] QSize sourceSize() const noexcept { return sourceSize_; }

signals:
    void hasFrameChanged();
    void sourceSizeChanged();

protected:
    QSGNode* updatePaintNode(QSGNode* oldNode, UpdatePaintNodeData* data) override;
    void geometryChange(const QRectF& newGeometry, const QRectF& oldGeometry) override;

private:
    void onFramesChanged(); // GUI thread

    FrameMailbox mailbox_;
    std::atomic<bool> notifyPending_{false};
    bool hasFrame_{false};      // GUI thread
    QSize sourceSize_;          // GUI thread
    std::uint64_t lastSeen_{0}; // render thread
    bool showing_{false};       // render thread
};

} // namespace vsort::hmi
