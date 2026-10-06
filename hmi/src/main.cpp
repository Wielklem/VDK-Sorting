#include <QDebug>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QVariant>

#include <vsort/common/version.hpp>
#include <vsort/hmi/page_registry.hpp>
#include <vsort/hmi/plugin_loader.hpp>

int main(int argc, char* argv[]) {
    QGuiApplication app(argc, argv);
    const auto ver = vsort::version();
    QGuiApplication::setApplicationVersion(
        QString::fromUtf8(ver.data(), static_cast<qsizetype>(ver.size())));

    // Declared before the engine so it outlives it.
    vsort::hmi::PageRegistry registry;
    const auto loaded = vsort::hmi::loadPagePlugins(vsort::hmi::defaultPluginDir(), registry);
    for (const auto& error : loaded.errors) {
        qWarning().noquote() << error;
    }

    QQmlApplicationEngine engine;
    engine.setInitialProperties({{QStringLiteral("pages"), QVariant::fromValue(&registry)}});
    QObject::connect(
        &engine, &QQmlApplicationEngine::objectCreationFailed, &app,
        [] { QCoreApplication::exit(1); }, Qt::QueuedConnection);
    engine.loadFromModule("VsortHmi", "Main");

    return QGuiApplication::exec();
}
