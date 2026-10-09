#include "video/video_item.hpp"

#include <QCoreApplication>
#include <QQuickWindow>
#include <QSGSimpleTextureNode>
#include <QSGTexture>
#include <utility>

#include <QtQml/qqml.h>

#include "video/frame_convert.hpp"

namespace vsort::hmi {

namespace {
// Registered by hand: the qml type registrar does not find headers outside hmi/.
void registerVideoItem() {
    qmlRegisterType<VideoItem>("VsortHmi", 1, 0, "VideoItem");
}
} // namespace

// NOLINTNEXTLINE(cert-err58-cpp,cppcoreguidelines-avoid-non-const-global-variables)
Q_COREAPP_STARTUP_FUNCTION(registerVideoItem)

VideoItem::VideoItem(QQuickItem* parent)
    : QQuickItem{parent} {
    setFlag(ItemHasContents, true);
}

void VideoItem::submitFrame(FramePtr frame) {
    mailbox_.put(std::move(frame));
    if (!notifyPending_.exchange(true)) {
        QMetaObject::invokeMethod(this, &VideoItem::onFramesChanged, Qt::QueuedConnection);
    }
}

void VideoItem::clear() {
    mailbox_.clear();
    onFramesChanged();
}

void VideoItem::onFramesChanged() {
    notifyPending_ = false;
    const auto frame = mailbox_.latest();
    const bool has = frame != nullptr;
    const QSize size =
        has ? QSize{static_cast<int>(frame->info.width), static_cast<int>(frame->info.height)}
            : QSize{};
    if (has != hasFrame_) {
        hasFrame_ = has;
        emit hasFrameChanged();
    }
    if (size != sourceSize_) {
        sourceSize_ = size;
        emit sourceSizeChanged();
    }
    const qint64 id = has ? static_cast<qint64>(frame->info.frameId) : -1;
    if (id != frameId_) {
        frameId_ = id;
        emit frameIdChanged();
    }
    update();
}

QImage VideoItem::present(const QImage& frame) {
    return frame;
}

void VideoItem::redraw() {
    redraw_ = true;
    update();
}

void VideoItem::geometryChange(const QRectF& newGeometry, const QRectF& oldGeometry) {
    QQuickItem::geometryChange(newGeometry, oldGeometry);
    update();
}

// Render thread, GUI thread blocked.
QSGNode* VideoItem::updatePaintNode(QSGNode* oldNode, UpdatePaintNodeData* /*data*/) {
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-static-cast-downcast)
    auto* node = static_cast<QSGSimpleTextureNode*>(oldNode);

    bool draw = std::exchange(redraw_, false) && !base_.isNull();
    if (const auto fresh = mailbox_.takeIfNew(lastSeen_)) {
        if (*fresh == nullptr) {
            showing_ = false;
            base_ = QImage{};
            draw = false;
        } else {
            base_ = toImage(**fresh);
            draw = !base_.isNull();
        }
    }
    if (draw && window() != nullptr) {
        const QImage image = present(base_);
        QSGTexture* texture = image.isNull() ? nullptr : window()->createTextureFromImage(image);
        if (texture != nullptr) {
            if (node == nullptr) {
                node = new QSGSimpleTextureNode; // NOLINT(cppcoreguidelines-owning-memory)
                node->setOwnsTexture(true);
                node->setFiltering(QSGTexture::Linear);
            }
            node->setTexture(texture); // deletes the previous texture
            showing_ = true;
        }
    }

    if (node == nullptr) {
        return nullptr;
    }
    node->setRect(showing_ ? fitRect(QSizeF{node->texture()->textureSize()}, size()) : QRectF{});
    return node;
}

} // namespace vsort::hmi
