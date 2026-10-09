#pragma once

#include <QImage>
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
    // Camera frame ID of the newest frame (-1 without a frame); matches the analysis results.
    Q_PROPERTY(qint64 frameId READ frameId NOTIFY frameIdChanged)

public:
    explicit VideoItem(QQuickItem* parent = nullptr);

    // Thread-safe; the newest frame wins. nullptr clears the picture.
    void submitFrame(FramePtr frame) override;
    Q_INVOKABLE void clear() override;

    [[nodiscard]] bool hasFrame() const noexcept { return hasFrame_; }
    [[nodiscard]] QSize sourceSize() const noexcept { return sourceSize_; }
    [[nodiscard]] qint64 frameId() const noexcept { return frameId_; }

signals:
    void hasFrameChanged();
    void sourceSizeChanged();
    void frameIdChanged();

protected:
    // Render thread, GUI thread blocked: the picture shown for a frame. Default: the frame itself.
    // Subclasses (HsvViewItem) derive their picture here and may read their GUI-thread members.
    [[nodiscard]] virtual QImage present(const QImage& frame);
    // GUI thread: present() the last frame again (inputs of present() changed).
    void redraw();

    QSGNode* updatePaintNode(QSGNode* oldNode, UpdatePaintNodeData* data) override;
    void geometryChange(const QRectF& newGeometry, const QRectF& oldGeometry) override;

private:
    void onFramesChanged(); // GUI thread

    FrameMailbox mailbox_;
    std::atomic<bool> notifyPending_{false};
    bool hasFrame_{false};      // GUI thread
    QSize sourceSize_;          // GUI thread
    qint64 frameId_{-1};        // GUI thread
    bool redraw_{false};        // GUI thread; read in updatePaintNode (GUI blocked)
    std::uint64_t lastSeen_{0}; // render thread
    bool showing_{false};       // render thread
    QImage base_;               // render thread: the last frame, before present()
};

} // namespace vsort::hmi
