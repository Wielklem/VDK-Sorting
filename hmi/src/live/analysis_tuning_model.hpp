#pragma once

#include <QByteArray>
#include <QList>
#include <QObject>
#include <QString>
#include <array>
#include <cstdint>
#include <deque>
#include <map>
#include <vector>

#include <nlohmann/json.hpp>

#include "live/service_client.hpp"

namespace vsort::hmi {

// Model behind the HSV editor (G60.20 Parameters, P60.45): the colour range (OpenCV scale,
// H 0..179, S and V 0..255) of the sensors of one camera, from the "analysis" config. Edits only
// go to the preview (HsvViewItem pictures) until save(), which writes an override for every
// sensor of that camera (its other parameters copied from what it uses now). The service applies
// it without a restart. Switching the camera discards unsaved edits.
class AnalysisTuningModel : public QObject {
    Q_OBJECT
    Q_PROPERTY(int cameraId READ cameraId NOTIFY cameraIdChanged)
    Q_PROPERTY(QList<int> hsvLower READ hsvLower NOTIFY rangeChanged)
    Q_PROPERTY(QList<int> hsvUpper READ hsvUpper NOTIFY rangeChanged)
    Q_PROPERTY(QString sensors READ sensors NOTIFY stateChanged)
    Q_PROPERTY(bool canSave READ canSave NOTIFY stateChanged)
    Q_PROPERTY(bool dirty READ dirty NOTIFY stateChanged)
    Q_PROPERTY(bool loaded READ loaded NOTIFY stateChanged)
    Q_PROPERTY(QString status READ status NOTIFY stateChanged)

public:
    // `client` must outlive the model.
    explicit AnalysisTuningModel(ServiceClient& client, QObject* parent = nullptr);
    ~AnalysisTuningModel() override = default;
    AnalysisTuningModel(const AnalysisTuningModel&) = delete;
    AnalysisTuningModel& operator=(const AnalysisTuningModel&) = delete;
    AnalysisTuningModel(AnalysisTuningModel&&) = delete;
    AnalysisTuningModel& operator=(AnalysisTuningModel&&) = delete;

    [[nodiscard]] int cameraId() const noexcept { return cameraId_; }
    [[nodiscard]] QList<int> hsvLower() const { return {lower_[0], lower_[1], lower_[2]}; }
    [[nodiscard]] QList<int> hsvUpper() const { return {upper_[0], upper_[1], upper_[2]}; }
    // Names of the sensors that use the camera ("Camera 1, Camera 5"); empty: none.
    [[nodiscard]] QString sensors() const;
    [[nodiscard]] bool canSave() const;
    [[nodiscard]] bool dirty() const noexcept { return dirty_; }
    [[nodiscard]] bool loaded() const noexcept { return loaded_; }
    [[nodiscard]] QString status() const { return status_; }

    Q_INVOKABLE void selectCamera(int cameraId);
    // bound 0 = lower, 1 = upper; channel 0 = H, 1 = S, 2 = V. Clamped; the other bound follows
    // so that lower <= upper.
    Q_INVOKABLE void setValue(int bound, int channel, int value);
    Q_INVOKABLE void reload(); // discards unsaved edits
    Q_INVOKABLE void save();

signals:
    void cameraIdChanged();
    void rangeChanged();
    void stateChanged();

private:
    struct Sensor {
        std::uint16_t id{0};
        QString name;
    };
    struct Pending {
        bool save{false};
        bool force{false}; // load only: overwrite unsaved edits
    };

    void requestLoad(bool force);
    void onConfigReceived(const QString& module, const QByteArray& json, quint32 version);
    void onFailed(const QString& what);
    void showCamera(); // range of the selected camera from the config
    [[nodiscard]] const std::vector<Sensor>& cameraSensors() const;
    [[nodiscard]] nlohmann::json effectiveParams(std::uint16_t sensorId) const;
    void setDirty(bool dirty);
    void setStatus(const QString& status);

    ServiceClient& client_;
    nlohmann::json analysis_;                     // the whole module as the service has it
    std::map<int, std::vector<Sensor>> byCamera_; // from the "machine" config
    int cameraId_{-1};
    std::array<int, 3> lower_{0, 0, 0};
    std::array<int, 3> upper_{179, 255, 255};
    bool dirty_{false};
    bool loaded_{false};
    QString status_;
    std::deque<Pending> inFlight_; // "analysis" replies come back in request order
};

} // namespace vsort::hmi
