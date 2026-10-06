#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QtLogging>
#include <atomic>
#include <iostream>

#include <vsort/common/version.hpp>

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

    QQmlApplicationEngine engine;
    QObject::connect(
        &engine, &QQmlApplicationEngine::objectCreationFailed, &app,
        [] { QCoreApplication::exit(1); }, Qt::QueuedConnection);
    if (selfTest) {
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
