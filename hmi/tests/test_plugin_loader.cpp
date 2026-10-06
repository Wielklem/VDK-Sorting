#include <QTemporaryDir>

#include <gtest/gtest.h>

#include <vsort/hmi/plugin_loader.hpp>

using namespace vsort::hmi;

TEST(PluginLoader, MissingDirReportsError) {
    PageRegistry registry;
    const auto result = loadPagePlugins(QStringLiteral("does-not-exist-vsort"), registry);
    EXPECT_EQ(result.loaded, 0);
    EXPECT_EQ(result.errors.size(), 1);
    EXPECT_EQ(registry.count(), 0);
}

TEST(PluginLoader, EmptyDirLoadsNothing) {
    QTemporaryDir tmp;
    ASSERT_TRUE(tmp.isValid());
    PageRegistry registry;
    const auto result = loadPagePlugins(tmp.path(), registry);
    EXPECT_EQ(result.loaded, 0);
    EXPECT_TRUE(result.errors.isEmpty());
}
