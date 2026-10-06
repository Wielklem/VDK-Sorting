#pragma once

#include <QAbstractListModel>
#include <QByteArray>
#include <QRectF>
#include <QString>
#include <QVariantList>
#include <deque>
#include <map>
#include <vector>

#include "live/service_client.hpp"

namespace vsort::hmi {

// One ROI. Coordinates are fractions (0..1) of the camera image.
struct RoiEntry {
    int id{0};
    QString name;
    double x{0.0};
    double y{0.0};
    double w{0.0};
    double h{0.0};
};

// Model behind the G30.20 ROI editor: the ROIs of the selected camera. All edits are clamped to
// the image and to a minimum size. Nothing is sent to the service before save(); the whole
// "rois" config module (all cameras) is read with reload() and written with save().
class RoiEditorModel : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int cameraId READ cameraId NOTIFY cameraIdChanged)
    Q_PROPERTY(int selected READ selected NOTIFY selectedChanged)
    Q_PROPERTY(QRectF selectedRect READ selectedRect NOTIFY selectedRectChanged)
    Q_PROPERTY(QVariantList rois READ rois NOTIFY roisChanged)
    Q_PROPERTY(bool dirty READ dirty NOTIFY dirtyChanged)
    Q_PROPERTY(bool loaded READ loaded NOTIFY loadedChanged)
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)

public:
    static constexpr double kMinSize = 0.01; // smallest width / height (fraction of the image)

    enum Role { RoiIdRole = Qt::UserRole + 1, RoiNameRole, RoiXRole, RoiYRole, RoiWRole, RoiHRole };

    // `client` must outlive the model.
    explicit RoiEditorModel(ServiceClient& client, QObject* parent = nullptr);
    ~RoiEditorModel() override = default;
    RoiEditorModel(const RoiEditorModel&) = delete;
    RoiEditorModel& operator=(const RoiEditorModel&) = delete;
    RoiEditorModel(RoiEditorModel&&) = delete;
    RoiEditorModel& operator=(RoiEditorModel&&) = delete;

    [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

    [[nodiscard]] int cameraId() const noexcept { return cameraId_; }
    [[nodiscard]] int selected() const noexcept { return selected_; }
    [[nodiscard]] QRectF selectedRect() const;
    // [{x, y, width, height, label}], fractions; the page scales them to frame pixels.
    [[nodiscard]] QVariantList rois() const;
    [[nodiscard]] bool dirty() const noexcept { return dirty_; }
    [[nodiscard]] bool loaded() const noexcept { return loaded_; }
    [[nodiscard]] QString status() const { return status_; }

    Q_INVOKABLE void selectCamera(int cameraId);
    Q_INVOKABLE void select(int row); // -1 = nothing
    // Corners in any order, fractions. Returns the new row, or -1 when too small.
    Q_INVOKABLE int addRoi(double x1, double y1, double x2, double y2);
    Q_INVOKABLE void removeRoi(int row);
    // Moves, keeping the size; the ROI stays inside the image.
    Q_INVOKABLE void moveTo(int row, double x, double y);
    // Resizes / places freely; clamped to the image and kMinSize.
    Q_INVOKABLE void setRect(int row, double x, double y, double w, double h);
    Q_INVOKABLE void reload(); // discards unsaved edits
    Q_INVOKABLE void save();

signals:
    void cameraIdChanged();
    void selectedChanged();
    void selectedRectChanged();
    void roisChanged();
    void dirtyChanged();
    void loadedChanged();
    void statusChanged();

private:
    struct Pending {
        bool save{false};
        bool force{false}; // load only: overwrite unsaved edits
    };
    using Config = std::map<int, std::vector<RoiEntry>>;

    [[nodiscard]] const std::vector<RoiEntry>& rows() const;
    [[nodiscard]] bool validRow(int row) const;
    [[nodiscard]] RoiEntry& at(int row);
    [[nodiscard]] static bool parseConfig(const QByteArray& json, Config& out);
    [[nodiscard]] QByteArray toJson() const;

    void requestLoad(bool force);
    void onConfigReceived(const QString& module, const QByteArray& json, quint32 version);
    void onFailed(const QString& what);
    void changed(int row);
    void setSelected(int row);
    void setDirty(bool dirty);
    void setStatus(const QString& status);

    ServiceClient& client_;
    Config byCamera_;
    int cameraId_{-1};
    int selected_{-1};
    bool dirty_{false};
    bool loaded_{false};
    QString status_;
    std::deque<Pending> inFlight_; // replies come back in request order
};

} // namespace vsort::hmi
