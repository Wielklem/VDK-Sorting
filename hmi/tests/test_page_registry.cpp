#include <gtest/gtest.h>

#include <vsort/hmi/page_registry.hpp>

namespace {

class FakePage final : public vsort::hmi::IPage {
public:
    FakePage(QString id, int order)
        : id_(std::move(id))
        , order_(order) {}
    [[nodiscard]] QString pageId() const override { return id_; }
    [[nodiscard]] QString title() const override { return id_; }
    [[nodiscard]] int order() const override { return order_; }
    [[nodiscard]] QUrl qmlSource() const override { return QUrl(QStringLiteral("qrc:/x.qml")); }

private:
    QString id_;
    int order_;
};

} // namespace

using vsort::hmi::PageRegistry;

TEST(PageRegistry, SortsByOrder) {
    PageRegistry registry;
    FakePage b{QStringLiteral("B"), 20};
    FakePage a{QStringLiteral("A"), 10};
    FakePage c{QStringLiteral("C"), 30};
    EXPECT_TRUE(registry.addPage(&b));
    EXPECT_TRUE(registry.addPage(&c));
    EXPECT_TRUE(registry.addPage(&a));
    ASSERT_EQ(registry.count(), 3);
    EXPECT_EQ(registry.data(registry.index(0), PageRegistry::PageIdRole).toString().toStdString(),
              "A");
    EXPECT_EQ(registry.data(registry.index(2), PageRegistry::PageIdRole).toString().toStdString(),
              "C");
}

TEST(PageRegistry, RejectsNullAndDuplicate) {
    PageRegistry registry;
    FakePage a{QStringLiteral("A"), 10};
    FakePage dup{QStringLiteral("A"), 20};
    EXPECT_FALSE(registry.addPage(nullptr));
    EXPECT_TRUE(registry.addPage(&a));
    EXPECT_FALSE(registry.addPage(&dup));
    EXPECT_EQ(registry.count(), 1);
}

TEST(PageRegistry, SourceAtOutOfRangeIsEmpty) {
    PageRegistry registry;
    FakePage a{QStringLiteral("A"), 10};
    registry.addPage(&a);
    EXPECT_FALSE(registry.sourceAt(0).isEmpty());
    EXPECT_TRUE(registry.sourceAt(1).isEmpty());
    EXPECT_TRUE(registry.sourceAt(-1).isEmpty());
}
