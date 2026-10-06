#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include <vsort/common/config_store.hpp>

using nlohmann::json;
using vsort::ConfigChanged;
using vsort::Errc;
using vsort::FileConfigStore;

namespace {

const json kSchema = json::parse(R"({
  "type": "object",
  "required": ["exposure_us", "mode"],
  "additionalProperties": false,
  "properties": {
    "exposure_us": {"type": "integer", "minimum": 10, "maximum": 100000},
    "mode": {"type": "string", "enum": ["trigger", "free"]}
  }
})");
const json kDefaults = json::parse(R"({"exposure_us": 500, "mode": "trigger"})");
const json kChanged = json::parse(R"({"exposure_us": 900, "mode": "free"})");

class ConfigStoreTest : public ::testing::Test {
protected:
    void SetUp() override {
        dir_ = std::filesystem::temp_directory_path() / "vsort_config_test";
        std::filesystem::remove_all(dir_);
    }
    void TearDown() override { std::filesystem::remove_all(dir_); }

    std::filesystem::path dir_;
    vsort::MessageBus bus_;
};

} // namespace

TEST(ValidateJson, ReportsAllViolations) {
    const auto r = vsort::validateJson(kSchema, json::parse(R"({"exposure_us": 5, "extra": 1})"));
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, Errc::ValidationFailed);
    EXPECT_NE(r.error().message.find("$.exposure_us: below minimum"), std::string::npos);
    EXPECT_NE(r.error().message.find("missing required 'mode'"), std::string::npos);
    EXPECT_NE(r.error().message.find("unknown key 'extra'"), std::string::npos);
}

TEST(ValidateJson, ChecksTypesEnumAndArrayItems) {
    const auto schema = json::parse(R"({"type":"array","items":{"type":"string","enum":["a"]}})");
    EXPECT_TRUE(vsort::validateJson(schema, json::parse(R"(["a","a"])")).has_value());
    EXPECT_FALSE(vsort::validateJson(schema, json::parse(R"(["a","b"])")).has_value());
    EXPECT_FALSE(vsort::validateJson(schema, json::parse("5")).has_value());
}

TEST_F(ConfigStoreTest, RegisterSavesDefaultsAsVersionOne) {
    FileConfigStore store{dir_, bus_};
    ASSERT_TRUE(store.registerModule("camera", kSchema, kDefaults).has_value());
    EXPECT_EQ(*store.get("camera"), kDefaults);
    EXPECT_EQ(store.version("camera")->number, 1U);
    EXPECT_TRUE(std::filesystem::exists(dir_ / "camera.json"));
    EXPECT_TRUE(std::filesystem::exists(dir_ / "history" / "camera" / "1.json"));
}

TEST_F(ConfigStoreTest, RejectsBadNamesBadDefaultsAndDuplicates) {
    FileConfigStore store{dir_, bus_};
    EXPECT_EQ(store.registerModule("../evil", kSchema, kDefaults).error().code,
              Errc::InvalidArgument);
    EXPECT_EQ(store.registerModule("camera", kSchema, json::object()).error().code,
              Errc::ValidationFailed);
    ASSERT_TRUE(store.registerModule("camera", kSchema, kDefaults).has_value());
    EXPECT_EQ(store.registerModule("camera", kSchema, kDefaults).error().code, Errc::AlreadyExists);
    EXPECT_EQ(store.get("nope").error().code, Errc::NotFound);
}

TEST_F(ConfigStoreTest, SetValidatesBumpsVersionAndNotifies) {
    FileConfigStore store{dir_, bus_};
    ASSERT_TRUE(store.registerModule("camera", kSchema, kDefaults).has_value());
    const auto sub = bus_.subscribe<ConfigChanged>(8);

    const auto bad = json::parse(R"({"exposure_us": 1, "mode": "free"})");
    EXPECT_EQ(store.set("camera", bad, "t").error().code, Errc::ValidationFailed);
    EXPECT_EQ(sub->pending(), 0U);

    const auto v = store.set("camera", kChanged, "wilhelm");
    ASSERT_TRUE(v.has_value());
    EXPECT_EQ(v->number, 2U);
    EXPECT_EQ(v->author, "wilhelm");

    const auto msg = sub->tryPop();
    ASSERT_TRUE(msg.has_value());
    EXPECT_EQ(msg->module, "camera");
    EXPECT_EQ(msg->version.number, 2U);
    EXPECT_EQ((*store.get("camera"))["exposure_us"], 900);
}

TEST_F(ConfigStoreTest, UnchangedValueIsNoOp) {
    FileConfigStore store{dir_, bus_};
    ASSERT_TRUE(store.registerModule("camera", kSchema, kDefaults).has_value());
    const auto sub = bus_.subscribe<ConfigChanged>(8);
    const auto v = store.set("camera", kDefaults, "t");
    ASSERT_TRUE(v.has_value());
    EXPECT_EQ(v->number, 1U);
    EXPECT_EQ(sub->pending(), 0U);
}

TEST_F(ConfigStoreTest, PersistsAcrossInstances) {
    {
        FileConfigStore store{dir_, bus_};
        ASSERT_TRUE(store.registerModule("camera", kSchema, kDefaults).has_value());
        ASSERT_TRUE(store.set("camera", kChanged, "t").has_value());
    }
    FileConfigStore again{dir_, bus_};
    ASSERT_TRUE(again.registerModule("camera", kSchema, kDefaults).has_value());
    EXPECT_EQ((*again.get("camera"))["exposure_us"], 900);
    EXPECT_EQ(again.version("camera")->number, 2U);
    EXPECT_EQ(again.history("camera")->size(), 2U);
}

TEST_F(ConfigStoreTest, RollbackCreatesNewVersionAndNotifies) {
    FileConfigStore store{dir_, bus_};
    ASSERT_TRUE(store.registerModule("camera", kSchema, kDefaults).has_value());
    ASSERT_TRUE(store.set("camera", kChanged, "t").has_value());
    const auto sub = bus_.subscribe<ConfigChanged>(8);

    const auto v = store.rollback("camera", 1U, "wilhelm");
    ASSERT_TRUE(v.has_value());
    EXPECT_EQ(v->number, 3U);
    EXPECT_EQ(*store.get("camera"), kDefaults);
    EXPECT_EQ(store.history("camera")->size(), 3U);
    EXPECT_EQ(sub->tryPop()->version.number, 3U);
    EXPECT_EQ(store.rollback("camera", 99U, "t").error().code, Errc::NotFound);
}

TEST_F(ConfigStoreTest, InvalidFileOnDiskIsRejectedNotOverwritten) {
    {
        FileConfigStore store{dir_, bus_};
        ASSERT_TRUE(store.registerModule("camera", kSchema, kDefaults).has_value());
    }
    {
        std::ofstream out{dir_ / "camera.json", std::ios::binary | std::ios::trunc};
        out << R"({"version":2,"author":"x","time_ns":0,"data":{"exposure_us":1,"mode":"free"}})";
    }
    FileConfigStore again{dir_, bus_};
    EXPECT_EQ(again.registerModule("camera", kSchema, kDefaults).error().code,
              Errc::ValidationFailed);
}

TEST_F(ConfigStoreTest, UnknownModuleReturnsNotFound) {
    FileConfigStore store{dir_, bus_};
    EXPECT_EQ(store.version("nope").error().code, Errc::NotFound);
    EXPECT_EQ(store.set("nope", kChanged, "t").error().code, Errc::NotFound);
    EXPECT_EQ(store.history("nope").error().code, Errc::NotFound);
    EXPECT_EQ(store.rollback("nope", 1U, "t").error().code, Errc::NotFound);
}

TEST_F(ConfigStoreTest, RejectsUppercaseModuleName) {
    FileConfigStore store{dir_, bus_};
    EXPECT_EQ(store.registerModule("Camera", kSchema, kDefaults).error().code,
              Errc::InvalidArgument);
}

TEST_F(ConfigStoreTest, CorruptJsonOnDiskIsRejected) {
    {
        FileConfigStore store{dir_, bus_};
        ASSERT_TRUE(store.registerModule("camera", kSchema, kDefaults).has_value());
    }
    {
        std::ofstream out{dir_ / "camera.json", std::ios::binary | std::ios::trunc};
        out << "{ this is not json";
    }
    FileConfigStore again{dir_, bus_};
    EXPECT_EQ(again.registerModule("camera", kSchema, kDefaults).error().code, Errc::ParseError);
}

TEST_F(ConfigStoreTest, HistoryIsOldestFirstWithAuthors) {
    FileConfigStore store{dir_, bus_};
    ASSERT_TRUE(store.registerModule("camera", kSchema, kDefaults).has_value());
    ASSERT_TRUE(store.set("camera", kChanged, "wilhelm").has_value());
    const auto h = store.history("camera");
    ASSERT_TRUE(h.has_value());
    ASSERT_EQ(h->size(), 2U);
    EXPECT_EQ((*h)[0].number, 1U);
    EXPECT_EQ((*h)[0].author, "default");
    EXPECT_EQ((*h)[1].number, 2U);
    EXPECT_EQ((*h)[1].author, "wilhelm");
}

TEST_F(ConfigStoreTest, ConcurrentSetsAreSerialised) {
    constexpr int kThreads = 4;
    constexpr int kPerThread = 25;
    constexpr int kTotal = kThreads * kPerThread;

    FileConfigStore store{dir_, bus_};
    ASSERT_TRUE(store.registerModule("camera", kSchema, kDefaults).has_value());
    const auto sub = bus_.subscribe<ConfigChanged>(256);
    {
        std::vector<std::jthread> threads;
        for (int t = 0; t < kThreads; ++t) {
            threads.emplace_back([&store, t] {
                for (int i = 0; i < kPerThread; ++i) {
                    json value = kDefaults;
                    value["exposure_us"] = 1000 + (t * 100) + i; // unique per call
                    EXPECT_TRUE(store.set("camera", value, "t").has_value());
                }
            });
        }
    }
    EXPECT_EQ(store.version("camera")->number, static_cast<std::uint32_t>(kTotal) + 1U);
    EXPECT_EQ(store.history("camera")->size(), static_cast<std::size_t>(kTotal) + 1U);
    EXPECT_EQ(sub->pending(), static_cast<std::size_t>(kTotal));
}
