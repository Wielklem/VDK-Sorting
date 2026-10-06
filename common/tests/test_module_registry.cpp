#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include <vsort/common/message_bus.hpp>
#include <vsort/common/module.hpp>
#include <vsort/common/module_registry.hpp>

namespace {

using vsort::Errc;
using vsort::Health;
using vsort::HealthState;
using vsort::IModule;
using vsort::ModuleContext;
using vsort::ModuleRegistry;
using vsort::ModuleState;
using Events = std::vector<std::string>;

struct FakeOptions {
    std::vector<std::string> deps;
    bool failInit{false};
    bool failStart{false};
    HealthState health{HealthState::Ok};
};

class FakeModule final : public IModule {
public:
    FakeModule(std::string name, Events& events, FakeOptions options = {})
        : name_{std::move(name)}
        , events_{events}
        , options_{std::move(options)} {}

    [[nodiscard]] std::string_view name() const noexcept override { return name_; }
    [[nodiscard]] std::vector<std::string> dependencies() const override { return options_.deps; }

    [[nodiscard]] vsort::Result<> init(ModuleContext& /*context*/) override {
        events_.push_back("init:" + name_);
        if (options_.failInit) {
            return vsort::makeError(Errc::DeviceError, "init boom");
        }
        return {};
    }

    [[nodiscard]] vsort::Result<> start() override {
        events_.push_back("start:" + name_);
        if (options_.failStart) {
            return vsort::makeError(Errc::DeviceError, "start boom");
        }
        return {};
    }

    void stop() noexcept override { events_.push_back("stop:" + name_); }

    [[nodiscard]] Health health() const override { return Health{options_.health, "fake"}; }

private:
    std::string name_;
    Events& events_;
    FakeOptions options_;
};

void addFake(ModuleRegistry& registry, const std::string& name, Events& events,
             FakeOptions options = {}) {
    const auto result =
        registry.add(std::make_unique<FakeModule>(name, events, std::move(options)));
    ASSERT_TRUE(result.has_value()) << result.error().what();
}

} // namespace

TEST(ModuleRegistry, StartsInDependencyOrderAndStopsInReverse) {
    Events events;
    vsort::MessageBus bus;
    ModuleContext context{bus};
    ModuleRegistry registry;
    addFake(registry, "c", events, {.deps = {"b"}});
    addFake(registry, "b", events, {.deps = {"a"}});
    addFake(registry, "a", events);

    const auto result = registry.startAll(context);
    ASSERT_TRUE(result.has_value()) << result.error().what();
    EXPECT_EQ(registry.startOrder(), (Events{"a", "b", "c"}));

    registry.stopAll();
    EXPECT_EQ(events, (Events{"init:a", "init:b", "init:c", "start:a", "start:b", "start:c",
                              "stop:c", "stop:b", "stop:a"}));
}

TEST(ModuleRegistry, EarliestRegisteredReadyModuleGoesFirst) {
    Events events;
    vsort::MessageBus bus;
    ModuleContext context{bus};
    ModuleRegistry registry;
    addFake(registry, "b", events, {.deps = {"a"}});
    addFake(registry, "c", events);
    addFake(registry, "a", events);

    ASSERT_TRUE(registry.startAll(context).has_value());
    EXPECT_EQ(registry.startOrder(), (Events{"c", "a", "b"}));
}

TEST(ModuleRegistry, RejectsDuplicateAndEmptyNames) {
    Events events;
    ModuleRegistry registry;
    addFake(registry, "a", events);

    const auto duplicate = registry.add(std::make_unique<FakeModule>("a", events));
    ASSERT_FALSE(duplicate.has_value());
    EXPECT_EQ(duplicate.error().code, Errc::AlreadyExists);

    const auto empty = registry.add(std::make_unique<FakeModule>("", events));
    ASSERT_FALSE(empty.has_value());
    EXPECT_EQ(empty.error().code, Errc::InvalidArgument);

    const auto null = registry.add(nullptr);
    ASSERT_FALSE(null.has_value());
    EXPECT_EQ(null.error().code, Errc::InvalidArgument);
}

TEST(ModuleRegistry, MissingDependencyIsNotFound) {
    Events events;
    vsort::MessageBus bus;
    ModuleContext context{bus};
    ModuleRegistry registry;
    addFake(registry, "a", events, {.deps = {"ghost"}});

    const auto result = registry.startAll(context);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, Errc::NotFound);
    EXPECT_TRUE(events.empty());
}

TEST(ModuleRegistry, DependencyCycleIsRejected) {
    Events events;
    vsort::MessageBus bus;
    ModuleContext context{bus};
    ModuleRegistry registry;
    addFake(registry, "a", events, {.deps = {"b"}});
    addFake(registry, "b", events, {.deps = {"a"}});

    const auto result = registry.startAll(context);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, Errc::ValidationFailed);
    EXPECT_TRUE(events.empty());
}

TEST(ModuleRegistry, InitFailureStopsEarlierModules) {
    Events events;
    vsort::MessageBus bus;
    ModuleContext context{bus};
    ModuleRegistry registry;
    addFake(registry, "a", events);
    addFake(registry, "b", events, {.failInit = true});
    addFake(registry, "c", events);

    const auto result = registry.startAll(context);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, Errc::DeviceError);
    EXPECT_EQ(events, (Events{"init:a", "init:b", "stop:a"}));
}

TEST(ModuleRegistry, StartFailureRollsBackEverything) {
    Events events;
    vsort::MessageBus bus;
    ModuleContext context{bus};
    ModuleRegistry registry;
    addFake(registry, "a", events);
    addFake(registry, "b", events, {.failStart = true});
    addFake(registry, "c", events);

    const auto result = registry.startAll(context);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, Errc::DeviceError);
    EXPECT_EQ(events, (Events{"init:a", "init:b", "init:c", "start:a", "start:b", "stop:b",
                              "stop:c", "stop:a"}));
}

TEST(ModuleRegistry, StopAllIsIdempotentAndRunsOnDestruction) {
    Events events;
    vsort::MessageBus bus;
    ModuleContext context{bus};
    {
        ModuleRegistry registry;
        addFake(registry, "a", events);
        ASSERT_TRUE(registry.startAll(context).has_value());
        registry.stopAll();
        registry.stopAll();
    }
    EXPECT_EQ(events, (Events{"init:a", "start:a", "stop:a"}));

    Events events2;
    {
        ModuleRegistry registry;
        addFake(registry, "x", events2);
        ASSERT_TRUE(registry.startAll(context).has_value());
    }
    EXPECT_EQ(events2, (Events{"init:x", "start:x", "stop:x"}));
}

TEST(ModuleRegistry, CannotAddOrStartTwiceAfterStart) {
    Events events;
    vsort::MessageBus bus;
    ModuleContext context{bus};
    ModuleRegistry registry;
    addFake(registry, "a", events);
    ASSERT_TRUE(registry.startAll(context).has_value());

    EXPECT_EQ(registry.add(std::make_unique<FakeModule>("b", events)).error().code,
              Errc::InvalidArgument);
    EXPECT_EQ(registry.startAll(context).error().code, Errc::InvalidArgument);
}

TEST(ModuleRegistry, StatusReportsStateAndHealth) {
    Events events;
    vsort::MessageBus bus;
    ModuleContext context{bus};
    ModuleRegistry registry;
    addFake(registry, "a", events);
    addFake(registry, "b", events, {.deps = {"a"}, .health = HealthState::Degraded});

    auto before = registry.status();
    ASSERT_EQ(before.size(), 2U);
    EXPECT_EQ(before[0].state, ModuleState::Created);
    EXPECT_EQ(before[0].health.state, HealthState::Unknown);

    ASSERT_TRUE(registry.startAll(context).has_value());
    const auto running = registry.status();
    ASSERT_EQ(running.size(), 2U);
    EXPECT_EQ(running[0].name, "a");
    EXPECT_EQ(running[0].state, ModuleState::Running);
    EXPECT_EQ(running[0].health.state, HealthState::Ok);
    EXPECT_EQ(running[1].health.state, HealthState::Degraded);

    registry.stopAll();
    EXPECT_EQ(registry.status()[0].state, ModuleState::Stopped);
    EXPECT_NE(registry.find("a"), nullptr);
    EXPECT_EQ(registry.find("zzz"), nullptr);
}
