#include <QObject>
#include <QUrl>

#include <vsort/hmi/ipage.hpp>

namespace vsort::hmi {

class AnalyticsPage final : public QObject, public IPage {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "vdk.vsort.IPage/1")
    Q_INTERFACES(vsort::hmi::IPage)

public:
    [[nodiscard]] QString pageId() const override { return QStringLiteral("G60"); }
    [[nodiscard]] QString title() const override { return QStringLiteral("Analytics"); }
    [[nodiscard]] int order() const override { return 60; }
    [[nodiscard]] QUrl qmlSource() const override {
        return QUrl(QStringLiteral("qrc:/VsortPages/Analytics/AnalyticsPage.qml"));
    }
};

} // namespace vsort::hmi

#include "analytics_page.moc"
