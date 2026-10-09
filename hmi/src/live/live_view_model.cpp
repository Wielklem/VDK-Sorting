#include "live/live_view_model.hpp"

#include <algorithm>
#include <memory>
#include <utility>

namespace vsort::hmi {
namespace {

constexpr int kRenderIntervalMs = 33; // ~30 Hz poll of the rings; the service sets the real fps

QString linkStateName(CameraLinkState state) {
    switch (state) {
    case CameraLinkState::Open:
        return QStringLiteral("open");
    case CameraLinkState::Streaming:
        return QStringLiteral("streaming");
    case CameraLinkState::Reconnecting:
        return QStringLiteral("reconnecting");
    case CameraLinkState::Closed:
        break;
    }
    return QStringLiteral("closed");
}

} // namespace

LiveViewModel::LiveViewModel(ServiceClient& client, QObject* parent)
    : QAbstractListModel{parent}
    , client_{client} {
    connect(&client_, &ServiceClient::cameraListReceived, this, &LiveViewModel::onCameraList);
    connect(&client_, &ServiceClient::cameraUpdated, this, &LiveViewModel::onCameraUpdated);
    connect(&client_, &ServiceClient::streamChanged, this,
            [this](quint16 id, const QString& shm, quint32 /*generation*/) {
                onStreamChanged(id, shm);
            });
    connect(&client_, &ServiceClient::connectedChanged, this, &LiveViewModel::onConnectedChanged);
    connect(&client_, &ServiceClient::cameraRatesReceived, this, &LiveViewModel::onCameraRates);

    renderTimer_.setInterval(kRenderIntervalMs);
    connect(&renderTimer_, &QTimer::timeout, this, &LiveViewModel::renderTick);
    renderTimer_.start();

    if (client_.connected()) {
        client_.requestCameraList();
    }
}

LiveViewModel::~LiveViewModel() = default;

int LiveViewModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : static_cast<int>(entries_.size());
}

QVariant LiveViewModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= rowCount()) {
        return {};
    }
    const Entry& entry = entries_[static_cast<std::size_t>(index.row())];
    switch (role) {
    case CameraIdRole:
        return entry.info.id;
    case SerialRole:
        return entry.info.serial;
    case LinkStateRole:
        return client_.connected() ? linkStateName(entry.info.state) : QStringLiteral("offline");
    case FrozenRole:
        return entry.frozen;
    case HasFrameRole:
        return entry.frameCounter > 0;
    case FrameCounterRole:
        return entry.frameCounter;
    case FrameWidthRole:
        return entry.frameWidth;
    case FrameHeightRole:
        return entry.frameHeight;
    case IncomingFpsRole:
        return entry.incomingFps;
    case AnalysedFpsRole:
        return entry.analysedFps;
    default:
        return {};
    }
}

QHash<int, QByteArray> LiveViewModel::roleNames() const {
    return {{CameraIdRole, "cameraId"},       {SerialRole, "serial"},
            {LinkStateRole, "linkState"},     {FrozenRole, "frozen"},
            {HasFrameRole, "hasFrame"},       {FrameCounterRole, "frameCounter"},
            {FrameWidthRole, "frameWidth"},   {FrameHeightRole, "frameHeight"},
            {IncomingFpsRole, "incomingFps"}, {AnalysedFpsRole, "analysedFps"}};
}

bool LiveViewModel::allFrozen() const noexcept {
    return !entries_.empty() &&
           std::ranges::all_of(entries_, [](const Entry& entry) { return entry.frozen; });
}

int LiveViewModel::rowOf(std::uint16_t cameraId) const noexcept {
    const auto it = std::ranges::find(entries_, cameraId, [](const Entry& e) { return e.info.id; });
    return it == entries_.end() ? -1 : static_cast<int>(it - entries_.begin());
}

void LiveViewModel::notifyRow(int row, const QList<int>& roles) {
    const QModelIndex idx = index(row);
    emit dataChanged(idx, idx, roles);
}

void LiveViewModel::setFrozen(int cameraId, bool frozen) {
    if (cameraId < 0 || cameraId > 0xFFFF) {
        return;
    }
    const auto id = static_cast<std::uint16_t>(cameraId);
    const int row = rowOf(id);
    if (row < 0) {
        return;
    }
    Entry& entry = entries_[static_cast<std::size_t>(row)];
    if (entry.frozen == frozen) {
        return;
    }
    entry.frozen = frozen;
    if (frozen) {
        frozenIds_.insert(id);
    } else {
        frozenIds_.erase(id);
    }
    notifyRow(row, {FrozenRole});
    updateAllFrozen();
}

void LiveViewModel::updateAllFrozen() {
    if (const bool now = allFrozen(); now != lastAllFrozen_) {
        lastAllFrozen_ = now;
        emit allFrozenChanged();
    }
}

void LiveViewModel::toggleFrozen(int cameraId) {
    const int row =
        cameraId < 0 || cameraId > 0xFFFF ? -1 : rowOf(static_cast<std::uint16_t>(cameraId));
    if (row >= 0) {
        setFrozen(cameraId, !entries_[static_cast<std::size_t>(row)].frozen);
    }
}

void LiveViewModel::setAllFrozen(bool frozen) {
    std::vector<int> ids;
    ids.reserve(entries_.size());
    for (const Entry& entry : entries_) {
        ids.push_back(entry.info.id);
    }
    for (const int id : ids) {
        setFrozen(id, frozen);
    }
}

void LiveViewModel::onCameraList(const QVector<CameraInfo>& cameras) {
    beginResetModel();
    entries_.clear();
    entries_.reserve(static_cast<std::size_t>(cameras.size()));
    for (const CameraInfo& info : cameras) {
        Entry& entry = entries_.emplace_back();
        entry.info = info;
        entry.frozen = frozenIds_.contains(info.id);
    }
    latest_.clear();
    for (const Sink& sink : sinks_) {
        sink.sink->clear(); // stale pictures of the old camera list
    }
    endResetModel();
    emit cameraCountChanged();
    updateAllFrozen();
    for (Entry& entry : entries_) {
        attach(entry);
        if (!entry.info.previewEnabled) {
            client_.setPreview(entry.info.id, true);
        }
    }
}

void LiveViewModel::onCameraUpdated(const CameraInfo& camera) {
    const int row = rowOf(camera.id);
    if (row < 0) {
        return;
    }
    Entry& entry = entries_[static_cast<std::size_t>(row)];
    entry.info = camera;
    attach(entry);
    notifyRow(row, {LinkStateRole});
}

void LiveViewModel::onStreamChanged(quint16 cameraId, const QString& shmName) {
    const int row = rowOf(cameraId);
    if (row < 0) {
        return;
    }
    Entry& entry = entries_[static_cast<std::size_t>(row)];
    entry.info.shmName = shmName;
    attach(entry); // empty name: ring closed, detach (the last image stays on screen)
}

void LiveViewModel::onCameraRates(const QVector<CameraRateData>& rates) {
    for (const CameraRateData& rate : rates) {
        const int row = rowOf(rate.cameraId);
        if (row < 0) {
            continue;
        }
        Entry& entry = entries_[static_cast<std::size_t>(row)];
        entry.incomingFps = rate.incomingFps;
        entry.analysedFps = rate.analysedFps;
        notifyRow(row, {IncomingFpsRole, AnalysedFpsRole});
    }
}

void LiveViewModel::onConnectedChanged(bool isConnected) {
    emit connectedChanged();
    if (!isConnected) {
        for (Entry& entry : entries_) { // old rates would look live
            entry.incomingFps = -1.0;
            entry.analysedFps = -1.0;
        }
    }
    if (!entries_.empty()) {
        emit dataChanged(index(0), index(rowCount() - 1),
                         {LinkStateRole, IncomingFpsRole, AnalysedFpsRole});
    }
    if (isConnected) { // first connect or service restart: state is always re-fetched
        client_.requestCameraList();
    }
}

void LiveViewModel::attach(Entry& entry) {
    const QString& wanted = entry.info.shmName;
    if (wanted.isEmpty()) {
        entry.reader.reset();
        entry.attachedName.clear();
        return;
    }
    if (entry.reader && entry.attachedName == wanted) {
        return;
    }
    auto reader = ipc::PreviewRingReader::open(wanted.toStdString());
    if (!reader) {
        entry.reader.reset(); // not there (yet): renderTick retries
        entry.attachedName.clear();
        return;
    }
    entry.reader = std::move(*reader);
    entry.attachedName = wanted;
    entry.lastSeen = 0;
}

void LiveViewModel::renderTick() {
    for (std::size_t i = 0; i < entries_.size(); ++i) {
        Entry& entry = entries_[i];
        if (entry.frozen) {
            continue;
        }
        if (!entry.reader && !entry.info.shmName.isEmpty()) {
            attach(entry);
        }
        if (!entry.reader) {
            continue;
        }
        const auto frame = entry.reader->readNewest(entry.lastSeen);
        if (!frame) {
            continue;
        }
        entry.frameWidth = static_cast<int>(frame->info.width);
        entry.frameHeight = static_cast<int>(frame->info.height);
        const FramePtr shared = std::make_shared<const ipc::PreviewFrame>(std::move(*frame));
        latest_[entry.info.id] = shared;
        deliver(entry.info.id, shared);
        ++entry.frameCounter;
        notifyRow(static_cast<int>(i),
                  {HasFrameRole, FrameCounterRole, FrameWidthRole, FrameHeightRole});
    }
}

void LiveViewModel::deliver(std::uint16_t cameraId, const FramePtr& frame) {
    for (const Sink& sink : sinks_) {
        if (sink.cameraId == cameraId) {
            sink.sink->submitFrame(frame);
        }
    }
}

void LiveViewModel::attachVideo(int cameraId, QObject* item) {
    auto* sink = dynamic_cast<IFrameSink*>(item);
    if (sink == nullptr || cameraId < 0 || cameraId > 0xFFFF) {
        return;
    }
    detachVideo(item); // attaching twice moves it
    const auto id = static_cast<std::uint16_t>(cameraId);
    sinks_.push_back(Sink{id, item, sink});
    connect(item, &QObject::destroyed, this, [this, item] { detachVideo(item); });
    if (const auto it = latest_.find(id); it != latest_.end()) {
        sink->submitFrame(it->second);
    }
}

void LiveViewModel::detachVideo(QObject* item) {
    std::erase_if(sinks_, [item](const Sink& sink) { return sink.object == item; });
}

} // namespace vsort::hmi
