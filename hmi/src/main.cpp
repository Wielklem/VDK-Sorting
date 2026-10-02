#include <QGuiApplication>
#include <QQmlApplicationEngine>

#include <vsort/common/version.hpp>

int main(int argc, char* argv[]) {
    QGuiApplication app(argc, argv);
    const auto ver = vsort::version();
    QGuiApplication::setApplicationVersion(
        QString::fromUtf8(ver.data(), static_cast<qsizetype>(ver.size())));

    QQmlApplicationEngine engine;
    QObject::connect(
        &engine, &QQmlApplicationEngine::objectCreationFailed, &app,
        [] { QCoreApplication::exit(1); }, Qt::QueuedConnection);
    engine.loadFromModule("VsortHmi", "Main");

    return QGuiApplication::exec();
}
