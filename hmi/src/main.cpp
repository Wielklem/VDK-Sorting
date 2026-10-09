#include <QDebug>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QVariant>
#include <QtLogging>
#include <atomic>
#include <iostream>

#include <vsort/common/version.hpp>
#include <vsort/hmi/page_registry.hpp>
#include <vsort/hmi/plugin_loader.hpp>

#include "live/analysis_tuning_model.hpp"
#include "live/camera_settings_model.hpp"
#include "live/live_view_model.hpp"
#include "live/product_monitor_model.hpp"
#include "live/roi_editor_model.hpp"
#include "live/service_client.hpp"

namespace {

constexpr int kSelfTestMs = 1500;

std::atomic<int>& qmlProblemCount() {
    static std::atomic<int> count{0};
    return count;
}

// Self-test only: count QML warnings/errors and forward every message to stderr.
void selfTestHandler(QtMsgType type, const QMessageLogContext& ctx, const QString& msg) {
    const bool problem = type == QtWarningMsg || type == QtCriticalMsg || type == QtFatalMsg;
    if (problem && msg.contains(QStringLiteral(".qml"))) {
        ++qmlProblemCount();
    }
    std::cerr << qFormatLogMessage(type, ctx, msg).toLocal8Bit().constData() << '\n';
}

} // namespace

int main(int argc, char* argv[]) {
    QGuiApplication app(argc, argv);
    const auto ver = vsort::version();
    QGuiApplication::setApplicationVersion(
        QString::fromUtf8(ver.data(), static_cast<qsizetype>(ver.size())));

    const QStringList args = QGuiApplication::arguments();
    const bool selfTest = args.contains(QStringLiteral("--selftest"));
    const bool gallery = selfTest || args.contains(QStringLiteral("--gallery"));
    if (selfTest) {
        qInstallMessageHandler(selfTestHandler);
    }

    // Declared before the engine so it outlives it.
    vsort::hmi::PageRegistry registry;
    // Live view backend (P30.60); also declared before the engine. Only used by the real pages.
    vsort::hmi::ServiceClient serviceClient;
    // NOLINTNEXTLINE(misc-const-correctness): QML calls non-const invokables
    vsort::hmi::LiveViewModel liveView{serviceClient};
    // NOLINTNEXTLINE(misc-const-correctness): QML calls non-const invokables
    vsort::hmi::RoiEditorModel roiEditor{serviceClient};
    // NOLINTNEXTLINE(misc-const-correctness): QML calls non-const invokables
    vsort::hmi::CameraSettingsModel cameraSettings{serviceClient};
    // NOLINTNEXTLINE(misc-const-correctness): QML calls non-const invokables
    vsort::hmi::ProductMonitorModel productMonitor{serviceClient};
    // NOLINTNEXTLINE(misc-const-correctness): QML calls non-const invokables
    vsort::hmi::AnalysisTuningModel analysisTuning{serviceClient};
    // Read-only second ROI model: the ROI overlay of the Analytics page follows its own camera.
    // NOLINTNEXTLINE(misc-const-correctness): QML calls non-const invokables
    vsort::hmi::RoiEditorModel roiOverlay{serviceClient};
    if (!gallery) {
        const auto loaded = vsort::hmi::loadPagePlugins(vsort::hmi::defaultPluginDir(), registry);
        for (const auto& error : loaded.errors) {
            qWarning().noquote() << error;
        }
    }

    QQmlApplicationEngine engine;
    QObject::connect(
        &engine, &QQmlApplicationEngine::objectCreationFailed, &app,
        [] { QCoreApplication::exit(1); }, Qt::QueuedConnection);
    if (!gallery) {
        engine.setInitialProperties({{QStringLiteral("pages"), QVariant::fromValue(&registry)}});
        engine.rootContext()->setContextProperty(QStringLiteral("liveView"), &liveView);
        engine.rootContext()->setContextProperty(QStringLiteral("roiEditor"), &roiEditor);
        engine.rootContext()->setContextProperty(QStringLiteral("cameraSettings"), &cameraSettings);
        engine.rootContext()->setContextProperty(QStringLiteral("productMonitor"), &productMonitor);
        engine.rootContext()->setContextProperty(QStringLiteral("analysisTuning"), &analysisTuning);
        engine.rootContext()->setContextProperty(QStringLiteral("roiOverlay"), &roiOverlay);
        serviceClient.start();
    } else if (selfTest) {
        engine.setInitialProperties({{QStringLiteral("selfTest"), true}});
    }
    engine.loadFromModule("VsortHmi", gallery ? "Gallery" : "Main");

    if (selfTest) {
        QTimer::singleShot(kSelfTestMs, &app, &QCoreApplication::quit);
    }
    const int rc = QGuiApplication::exec();
    if (selfTest && qmlProblemCount() > 0) {
        std::cerr << "selftest: " << qmlProblemCount() << " QML problem(s)\n";
        return 1;
    }
    return rc;
}
