#include "live/analysis_tuning_model.hpp"

#include <QStringList>
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>

#include <vsort/common/machine_config.hpp>

namespace vsort::hmi {
namespace {

const QString kAnalysis = QStringLiteral("analysis");
const QString kMachine = QStringLiteral("machine");

constexpr std::array<int, 3> kMax{179, 255, 255};

// Number at a flattened key ("a" or "a.b"); nullptr when missing or not a number.
const nlohmann::json* numberAt(const nlohmann::json& params, const std::string& key) {
    const auto dot = key.find('.');
    const nlohmann::json* node = &params;
    for (const auto& part :
         dot == std::string::npos
             ? std::vector<std::string>{key}
             : std::vector<std::string>{key.substr(0, dot), key.substr(dot + 1)}) {
        if (!node->is_object() || !node->contains(part)) {
            return nullptr;
        }
        node = &node->at(part);
    }
    return node->is_number() ? node : nullptr;
}

void setAt(nlohmann::json& params, const std::string& key, double value) {
    const auto dot = key.find('.');
    nlohmann::json& node =
        dot == std::string::npos ? params[key] : params[key.substr(0, dot)][key.substr(dot + 1)];
    if (node.is_number_integer()) {
        node = static_cast<std::int64_t>(std::llround(value));
    } else {
        node = value;
    }
}

nlohmann::json parse(const QByteArray& json) {
    return nlohmann::json::parse(json.constData(), json.constData() + json.size(), nullptr, false);
}

bool validAnalysis(const nlohmann::json& j) {
    return j.is_object() && j.contains("defaults") && j.at("defaults").is_object() &&
           j.contains("sensors") && j.at("sensors").is_array();
}

// [h, s, v] from a params object; `fallback` where missing or not a number.
std::array<int, 3> hsvOf(const nlohmann::json& params, const char* key,
                         const std::array<int, 3>& fallback) {
    std::array<int, 3> out = fallback;
    if (!params.contains(key) || !params.at(key).is_array() || params.at(key).size() != 3) {
        return out;
    }
    for (std::size_t i = 0; i < 3; ++i) {
        const auto& v = params.at(key).at(i);
        if (v.is_number_integer()) {
            out.at(i) = std::clamp(v.get<int>(), 0, kMax.at(i));
        }
    }
    return out;
}

} // namespace

AnalysisTuningModel::AnalysisTuningModel(ServiceClient& client, QObject* parent)
    : QObject{parent}
    , client_{client} {
    connect(&client_, &ServiceClient::configReceived, this, &AnalysisTuningModel::onConfigReceived);
    connect(&client_, &ServiceClient::commandFailed, this, &AnalysisTuningModel::onFailed);
    connect(&client_, &ServiceClient::connectedChanged, this, [this](bool isConnected) {
        if (isConnected) {
            client_.requestConfig(kMachine);
            requestLoad(false);
        }
    });
    if (client_.connected()) {
        client_.requestConfig(kMachine);
        requestLoad(false);
    }
}

QVariantMap AnalysisTuningModel::params() const {
    QVariantMap out;
    const auto add = [&out, this](const std::string& key, const nlohmann::json& v) {
        if (!v.is_number()) {
            return;
        }
        const auto edit = edits_.find(key);
        out.insert(QString::fromStdString(key),
                   edit != edits_.end() ? edit->second : v.get<double>());
    };
    for (const auto& [key, value] : shown_.items()) {
        if (value.is_object()) {
            for (const auto& [sub, v] : value.items()) {
                add(key + "." + sub, v);
            }
        } else {
            add(key, value);
        }
    }
    return out;
}

void AnalysisTuningModel::setParam(const QString& key, double value) {
    const std::string k = key.toStdString();
    const auto* current = numberAt(shown_, k);
    if (current == nullptr) {
        return;
    }
    const double v = current->is_number_integer() ? std::round(value) : value;
    const auto edit = edits_.find(k);
    const double before = edit != edits_.end() ? edit->second : current->get<double>();
    if (v == before) {
        return;
    }
    edits_[k] = v;
    emit paramsChanged();
    setDirty(true);
}

QString AnalysisTuningModel::sensors() const {
    QStringList names;
    for (const auto& s : cameraSensors()) {
        names.push_back(s.name);
    }
    return names.join(QStringLiteral(", "));
}

bool AnalysisTuningModel::canSave() const {
    return loaded_ && !cameraSensors().empty();
}

const std::vector<AnalysisTuningModel::Sensor>& AnalysisTuningModel::cameraSensors() const {
    static const std::vector<Sensor> kNone;
    const auto it = byCamera_.find(cameraId_);
    return it != byCamera_.end() ? it->second : kNone;
}

void AnalysisTuningModel::selectCamera(int cameraId) {
    if (cameraId == cameraId_) {
        return;
    }
    cameraId_ = cameraId;
    emit cameraIdChanged();
    setDirty(false);
    showCamera();
    emit stateChanged();
}

void AnalysisTuningModel::setValue(int bound, int channel, int value) {
    if (bound < 0 || bound > 1 || channel < 0 || channel > 2) {
        return;
    }
    const auto c = static_cast<std::size_t>(channel);
    const int v = std::clamp(value, 0, kMax.at(c));
    auto lower = lower_;
    auto upper = upper_;
    if (bound == 0) {
        lower.at(c) = v;
        upper.at(c) = std::max(upper.at(c), v);
    } else {
        upper.at(c) = v;
        lower.at(c) = std::min(lower.at(c), v);
    }
    if (lower == lower_ && upper == upper_) {
        return;
    }
    lower_ = lower;
    upper_ = upper;
    emit rangeChanged();
    setDirty(true);
}

void AnalysisTuningModel::reload() {
    requestLoad(true);
}

void AnalysisTuningModel::requestLoad(bool force) {
    inFlight_.push_back({.save = false, .force = force});
    setStatus(QStringLiteral("Loading..."));
    client_.requestConfig(kAnalysis);
}

void AnalysisTuningModel::save() {
    if (!client_.connected()) {
        setStatus(QStringLiteral("Service offline"));
        return;
    }
    if (!canSave()) {
        setStatus(QStringLiteral("No sensor uses camera %1").arg(cameraId_));
        return;
    }
    nlohmann::json out = analysis_;
    auto& list = out["sensors"];
    for (const auto& s : cameraSensors()) {
        auto it = std::find_if(list.begin(), list.end(), [&](const nlohmann::json& e) {
            return e.value("sensor_id", -1) == static_cast<int>(s.id);
        });
        if (it == list.end()) {
            list.push_back({{"sensor_id", s.id}, {"params", effectiveParams(s.id)}});
            it = list.end() - 1;
        }
        auto& params = (*it)["params"];
        params["hsv_lower"] = {lower_[0], lower_[1], lower_[2]};
        params["hsv_upper"] = {upper_[0], upper_[1], upper_[2]};
        for (const auto& [key, value] : edits_) {
            setAt(params, key, value);
        }
    }
    inFlight_.push_back({.save = true, .force = false});
    setStatus(QStringLiteral("Saving..."));
    const std::string text = out.dump();
    client_.setConfig(kAnalysis, QByteArray{text.data(), static_cast<qsizetype>(text.size())});
}

nlohmann::json AnalysisTuningModel::effectiveParams(std::uint16_t sensorId) const {
    for (const auto& e : analysis_.at("sensors")) {
        if (e.value("sensor_id", -1) == static_cast<int>(sensorId) && e.contains("params")) {
            return e.at("params");
        }
    }
    return analysis_.at("defaults");
}

void AnalysisTuningModel::showCamera() {
    if (!loaded_) {
        return;
    }
    const auto& sensors = cameraSensors();
    const nlohmann::json params =
        sensors.empty() ? analysis_.at("defaults") : effectiveParams(sensors.front().id);
    const auto lower = hsvOf(params, "hsv_lower", {0, 0, 0});
    const auto upper = hsvOf(params, "hsv_upper", kMax);
    if (lower != lower_ || upper != upper_) {
        lower_ = lower;
        upper_ = upper;
        emit rangeChanged();
    }
    shown_ = params.is_object() ? params : nlohmann::json::object();
    edits_.clear();
    emit paramsChanged();
}

void AnalysisTuningModel::onConfigReceived(const QString& module, const QByteArray& json,
                                           quint32 version) {
    if (module == kMachine) {
        const auto machine = MachineConfig::fromJson(parse(json));
        byCamera_.clear();
        if (machine) {
            for (const auto* lane : machine->lanes()) {
                for (const auto& s : lane->sensors) {
                    byCamera_[s.cameraId].push_back(
                        {.id = s.id,
                         .name = QString::fromUtf8(s.name.data(),
                                                   static_cast<qsizetype>(s.name.size()))});
                }
            }
        }
        if (!dirty_) {
            showCamera();
        }
        emit stateChanged();
        return;
    }
    if (module != kAnalysis) {
        return;
    }
    Pending kind;
    if (!inFlight_.empty()) {
        kind = inFlight_.front();
        inFlight_.pop_front();
    }
    if (!kind.save && dirty_ && !kind.force) {
        return; // a background load must not overwrite unsaved edits
    }
    auto parsed = parse(json);
    if (!validAnalysis(parsed)) {
        setStatus(QStringLiteral("Invalid analysis config from service"));
        return;
    }
    analysis_ = std::move(parsed);
    loaded_ = true;
    dirty_ = false;
    showCamera();
    setStatus(kind.save ? QStringLiteral("Saved (version %1)").arg(version) : QString{});
    emit stateChanged();
}

void AnalysisTuningModel::onFailed(const QString& what) {
    if (inFlight_.empty()) {
        return;
    }
    inFlight_.clear();
    setStatus(what);
}

void AnalysisTuningModel::setDirty(bool dirty) {
    if (dirty_ == dirty) {
        return;
    }
    dirty_ = dirty;
    emit stateChanged();
}

void AnalysisTuningModel::setStatus(const QString& status) {
    if (status_ == status) {
        return;
    }
    status_ = status;
    emit stateChanged();
}

} // namespace vsort::hmi
