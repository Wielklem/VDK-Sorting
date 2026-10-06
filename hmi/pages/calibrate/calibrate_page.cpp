#include <QObject>
#include <QUrl>

#include <vsort/hmi/ipage.hpp>

namespace vsort::hmi {

class CalibratePage final : public QObject, public IPage {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "vdk.vsort.IPage/1")
    Q_INTERFACES(vsort::hmi::IPage)

public:
    [[nodiscard]] QString pageId() const override { return QStringLiteral("G35"); }
    [[nodiscard]] QString title() const override { return QStringLiteral("Calibrate"); }
    [[nodiscard]] int order() const override { return 35; }
    [[nodiscard]] QUrl qmlSource() const override {
        return QUrl(QStringLiteral("qrc:/VsortPages/Calibrate/CalibratePage.qml"));
    }
};

} // namespace vsort::hmi

#include "calibrate_page.moc"
