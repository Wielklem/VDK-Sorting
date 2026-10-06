#pragma once

#include <QString>
#include <QStringList>

#include <vsort/hmi/page_registry.hpp>

namespace vsort::hmi {

struct PluginLoadResult {
    int loaded = 0;
    QStringList errors;
};

// $VSORT_HMI_PAGES if set, otherwise <application dir>/pages.
[[nodiscard]] QString defaultPluginDir();

// Loads every IPage plugin in dirPath into the registry. Plugins stay loaded until exit.
PluginLoadResult loadPagePlugins(const QString& dirPath, PageRegistry& registry);

} // namespace vsort::hmi
