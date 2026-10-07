#pragma once

#include <QByteArray>
#include <QObject>
#include <QString>
#include <QTimer>
#include <QVector>
#include <chrono>
#include <cstdint>
#include <span>
#include <vector>

#include <zmq.hpp>

#include <vsort/ipc/envelope.hpp>

#include "live/camera_info.hpp"
#include "live/cup_data.hpp"

namespace vsort::hmi {

struct ServiceEndpoints {
    QString host{QStringLiteral("127.0.0.1")};
    quint16 commandPort{ipc::kDefaultCommandPort};
    quint16 eventPort{ipc::kDefaultEventPort};
};

// HMI side of the service IPC: ZeroMQ DEALER (commands) and SUB (events), both polled without
// blocking from a GUI-thread timer. ZeroMQ reconnects by itself; "connected" is derived from the
// 1 Hz heartbeat (offline after 3 s without one). Full reconnect handling is P30.90.
class ServiceClient : public QObject {
    Q_OBJECT

public:
    explicit ServiceClient(ServiceEndpoints endpoints = {}, QObject* parent = nullptr);
    ~ServiceClient() override;
    ServiceClient(const ServiceClient&) = delete;
    ServiceClient& operator=(const ServiceClient&) = delete;
    ServiceClient(ServiceClient&&) = delete;
    ServiceClient& operator=(ServiceClient&&) = delete;

    void start(); // connects the sockets and starts polling
    [[nodiscard]] bool connected() const noexcept { return connected_; }

    void requestCameraList();
    void setPreview(quint16 cameraId, bool enabled); // fps / width: keep the service values
    void requestCameraSettings(quint16 cameraId);
    // Replies with cameraSettingsApplied(); errors arrive as commandFailed().
    void setCameraSettings(quint16 cameraId, const CameraSettingsData& settings);
    // Whole config module as JSON. setConfig() is answered with configReceived() (stored copy).
    void requestConfig(const QString& module);
    void setConfig(const QString& module, const QByteArray& json);
    // Product Monitor (P80.100): last cups per lane; laneId 0 = all lanes.
    void requestCupSnapshot(quint16 laneId = 0);

signals:
    void connectedChanged(bool connected);
    void cameraListReceived(const QVector<vsort::hmi::CameraInfo>& cameras);
    void cameraUpdated(const vsort::hmi::CameraInfo& camera);
    void streamChanged(quint16 cameraId, const QString& shmName, quint32 generation);
    void commandFailed(const QString& what);
    void cameraSettingsReceived(quint16 cameraId, const vsort::hmi::CameraSettingsData& settings);
    void cameraSettingsApplied();
    void configReceived(const QString& module, const QByteArray& json, quint32 version);
    void cupSnapshotReceived(const QVector<vsort::hmi::LaneCupsData>& lanes);
    void cupUpdateReceived(const vsort::hmi::LaneCupsData& update);

private:
    void poll();
    void drain(zmq::socket_t& socket, bool hasTopic);
    void handleMessage(std::span<const std::uint8_t> bytes);
    void checkHeartbeat();
    void setConnected(bool connected);
    void sendRequest(ipc::fb::MsgType type, ipc::fb::Payload payloadType,
                     flatbuffers::FlatBufferBuilder& fbb, flatbuffers::Offset<void> payload);

    ServiceEndpoints endpoints_;
    zmq::context_t context_{1};
    zmq::socket_t dealer_{context_, zmq::socket_type::dealer};
    zmq::socket_t sub_{context_, zmq::socket_type::sub};
    QTimer timer_;
    bool started_{false};
    bool connected_{false};
    std::chrono::steady_clock::time_point lastHeartbeat_;
    std::uint64_t nextRequestId_{1};
};

} // namespace vsort::hmi
