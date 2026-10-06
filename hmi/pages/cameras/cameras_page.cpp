#include <QObject>
#include <QUrl>

#include <vsort/hmi/ipage.hpp>

namespace vsort::hmi {

class CamerasPage final : public QObject, public IPage {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "vdk.vsort.IPage/1")
    Q_INTERFACES(vsort::hmi::IPage)

public:
    [[nodiscard]] QString pageId() const override { return QStringLiteral("G30"); }
    [[nodiscard]] QString title() const override { return QStringLiteral("Cameras"); }
    [[nodiscard]] int order() const override { return 30; }
    [[nodiscard]] QUrl qmlSource() const override {
        return QUrl(QStringLiteral("qrc:/VsortPages/Cameras/CamerasPage.qml"));
    }
};

} // namespace vsort::hmi

#include "cameras_page.moc"
