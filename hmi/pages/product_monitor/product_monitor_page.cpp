
#include <QObject>
#include <QUrl>

#include <vsort/hmi/ipage.hpp>

namespace vsort::hmi {

class ProductMonitorPage final : public QObject, public IPage {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "vdk.vsort.IPage/1")
    Q_INTERFACES(vsort::hmi::IPage)

public:
    [[nodiscard]] QString pageId() const override { return QStringLiteral("G140"); }
    [[nodiscard]] QString title() const override { return QStringLiteral("Product Monitor"); }
    [[nodiscard]] int order() const override { return 140; }
    [[nodiscard]] QUrl qmlSource() const override {
        return QUrl(QStringLiteral("qrc:/VsortPages/ProductMonitor/ProductMonitorPage.qml"));
    }
};

} // namespace vsort::hmi

#include "product_monitor_page.moc"
