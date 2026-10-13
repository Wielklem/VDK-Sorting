#pragma once

#include <QString>
#include <QVector>
#include <QtGlobal>

namespace vsort::hmi {

// P60.100 Photo capture, as the service sends it (CaptureStatusReply). The state values are those
// of the IPC schema.
enum class CaptureStateData : int { Idle = 0, Running = 1, Done = 2, Stopped = 3, Failed = 4 };

struct CaptureSensorData {
    quint16 sensorId{0};
    quint16 cameraId{0};
    bool selected{false}; // part of the current or last session
    quint32 saved{0};
    quint32 target{0};
    QString name;     // sensor name from the config
    QString laneName; // lane name from the config
};

struct CaptureStatusData {
    CaptureStateData state{CaptureStateData::Idle};
    QString sessionFolder; // empty before the first start
    QString defaultFolder; // proposed by the service
    QString errorText;     // state Failed
    QVector<CaptureSensorData> sensors;
};

// StartCaptureRequest. `mode` is the CaptureMode of the IPC schema: 1 no potato, 2 single,
// 3 multiple, 4 size range (length of the largest object in mm, min..max).
struct CaptureStartData {
    QString folder; // on the service PC
    quint32 photosPerSensor{50};
    int mode{1};
    double minMm{0.0};
    double maxMm{0.0};
    QVector<quint16> sensorIds;
};

} // namespace vsort::hmi
