#include <QCoreApplication>
#include <QDir>
#include <QLibrary>
#include <QPluginLoader>
#include <memory>

#include <vsort/hmi/plugin_loader.hpp>

namespace vsort::hmi {

QString defaultPluginDir() {
    const QString env = qEnvironmentVariable("VSORT_HMI_PAGES");
    if (!env.isEmpty()) {
        return env;
    }
    return QCoreApplication::applicationDirPath() + QStringLiteral("/pages");
}

PluginLoadResult loadPagePlugins(const QString& dirPath, PageRegistry& registry) {
    PluginLoadResult result;
    const QDir dir(dirPath);
    if (!dir.exists()) {
        result.errors << QStringLiteral("Page plugin directory not found: ") + dirPath;
        return result;
    }
    for (const QString& name : dir.entryList(QDir::Files, QDir::Name)) {
        const QString path = dir.absoluteFilePath(name);
        if (!QLibrary::isLibrary(path)) {
            continue;
        }
        auto loader = std::make_unique<QPluginLoader>(path);
        QObject* instance = loader->instance();
        if (instance == nullptr) {
            result.errors << name + QStringLiteral(": ") + loader->errorString();
            continue;
        }
        auto* page = qobject_cast<IPage*>(instance);
        if (page == nullptr) {
            result.errors << name + QStringLiteral(": not an IPage plugin");
            loader->unload();
            continue;
        }
        if (!registry.addPage(page)) {
            result.errors << name + QStringLiteral(": duplicate page id ") + page->pageId();
            loader->unload();
            continue;
        }
        // Loader (and so the plugin) lives as long as the registry.
        loader->setParent(&registry);
        static_cast<void>(loader.release());
        ++result.loaded;
    }
    return result;
}

} // namespace vsort::hmi
