#include <QObject>
#include <QUrl>

#include <vsort/hmi/ipage.hpp>

namespace vsort::hmi {

class CapturePage final : public QObject, public IPage {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "vdk.vsort.IPage/1")
    Q_INTERFACES(vsort::hmi::IPage)

public:
    [[nodiscard]] QString pageId() const override { return QStringLiteral("G150"); }
    [[nodiscard]] QString title() const override { return QStringLiteral("Photo Capture"); }
    [[nodiscard]] int order() const override { return 150; }
    [[nodiscard]] QUrl qmlSource() const override {
        return QUrl(QStringLiteral("qrc:/VsortPages/Capture/CapturePage.qml"));
    }
};

} // namespace vsort::hmi

#include "capture_page.moc"
