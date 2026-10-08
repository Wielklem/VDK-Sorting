#include <chrono>
#include <filesystem>
#include <thread>

#include <gtest/gtest.h>

#include <vsort/common/machine_config.hpp>

#include "cups/cup_monitor.hpp"
#include "ipc/command_handler.hpp"
#include "ipc/cup_messages.hpp"
#include "test_ipc_util.hpp"

using namespace vsort;
using namespace vsort::service;
using namespace std::chrono_literals;
namespace fb = vsort::ipc::fb;

namespace {

class CupMonitorTest : public ::testing::Test {
protected:
    void SetUp() override {
        std::filesystem::remove_all(dir);
        store = std::make_unique<FileConfigStore>(dir, bus);
        ASSERT_TRUE(registerMachineConfig(*store).has_value());
    }
    void TearDown() override {
        store.reset();
        std::filesystem::remove_all(dir);
    }

    static ObjectRecord rec(std::int64_t cupId, std::uint16_t sensorId) {
        ObjectRecord r;
        r.laneId = 1;
        r.cupId = cupId;
        r.sensorId = sensorId;
        r.status = PhotoStatus::Ok;
        return r;
    }

    std::filesystem::path dir = std::filesystem::temp_directory_path() / "vsort_cup_monitor_test";
    MessageBus bus;
    std::unique_ptr<FileConfigStore> store;
};

class FakeCups final : public ICupSource {
public:
    std::vector<LaneSnapshot> lanes;
    [[nodiscard]] std::vector<LaneSnapshot>
    snapshot(std::optional<std::uint16_t> laneId) const override {
        last = laneId;
        return lanes;
    }
    mutable std::optional<std::uint16_t> last;
};

} // namespace

TEST_F(CupMonitorTest, RecordsOnTheBusBecomeCupUpdates) {
    CupMonitor monitor{store.get()};
    EXPECT_EQ(monitor.name(), "cups");
    ModuleContext context{bus};
    ASSERT_TRUE(monitor.init(context).has_value());
    auto updates = bus.subscribe<CupUpdate>();

    bus.publish(rec(0, 1));
    bus.publish(rec(1, 1));
    bus.publish(rec(0, 2));
    monitor.flush();

    std::vector<CupUpdate> got;
    updates->drain([&](const CupUpdate& u) { got.push_back(u); });
    ASSERT_EQ(got.size(), 1U);
    EXPECT_EQ(got[0].laneId, 1);
    EXPECT_EQ(got[0].seq, 1U);
    ASSERT_EQ(got[0].cups.size(), 2U);

    const auto snap = monitor.snapshot(std::nullopt);
    ASSERT_EQ(snap.size(), 1U);
    EXPECT_EQ(snap[0].seq, 1U);
    ASSERT_EQ(snap[0].cups.size(), 2U);
    EXPECT_EQ(snap[0].cups[1].cells[1].status, CellStatus::Ok); // cup 0, sensor 2
    EXPECT_EQ(monitor.health().state, HealthState::Ok);
}

TEST_F(CupMonitorTest, ThreadPublishesWithoutFlush) {
    CupMonitor monitor{store.get(), CupMonitorOptions{.table = {}, .interval = 10ms}};
    ModuleContext context{bus};
    ASSERT_TRUE(monitor.init(context).has_value());
    auto updates = bus.subscribe<CupUpdate>();
    ASSERT_TRUE(monitor.start().has_value());
    bus.publish(rec(7, 1));
    std::optional<CupUpdate> got;
    const auto deadline = std::chrono::steady_clock::now() + 3s;
    while (!got && std::chrono::steady_clock::now() < deadline) {
        got = updates->tryPop();
        std::this_thread::sleep_for(1ms);
    }
    monitor.stop();
    ASSERT_TRUE(got.has_value());
    EXPECT_EQ(got->cups.at(0).cupId, 7);
}

TEST(CupMessages, UpdateEnvelopeRoundTrip) {
    const CupUpdate update{
        .laneId = 2,
        .seq = 9,
        .cups = {CupState{.cupId = -4,
                          .cells = {{.sensorId = 1, .status = CellStatus::Ok},
                                    {.sensorId = 2, .status = CellStatus::NoData},
                                    {.sensorId = 3, .status = CellStatus::Pending}}}}};
    const auto bytes = makeCupUpdateEnvelope(update);
    const auto env = ipc::parseEnvelope(bytes);
    ASSERT_TRUE(env.has_value());
    EXPECT_EQ((*env)->msg_type(), static_cast<std::uint16_t>(fb::MsgType::CupUpdate));
    const auto* event = (*env)->payload_as_CupUpdateEvent();
    ASSERT_NE(event, nullptr);
    EXPECT_EQ(event->lane_id(), 2);
    EXPECT_EQ(event->seq(), 9U);
    ASSERT_EQ(event->cups()->size(), 1U);
    const auto* cup = event->cups()->Get(0);
    EXPECT_EQ(cup->cup_id(), -4);
    ASSERT_EQ(cup->cells()->size(), 3U);
    EXPECT_EQ(cup->cells()->Get(1)->status(), fb::CellStatus::NoData);
    EXPECT_EQ(cup->cells()->Get(2)->status(), fb::CellStatus::Pending);
}

TEST(CupSnapshotCommand, AnswersFromTheCupSource) {
    PreviewHub hub{PreviewDefaults{}};
    testutil::FakeCameras cameras;
    CommandHandler handler{hub, cameras, nullptr, "9.9.9"};

    auto call = [&](std::uint16_t lane) {
        flatbuffers::FlatBufferBuilder fbb;
        const auto p = fb::CreateGetCupSnapshotRequest(fbb, lane);
        return handler.handle(testutil::request(fbb, fb::MsgType::GetCupSnapshot, 5,
                                                fb::Payload::GetCupSnapshotRequest, p.Union()));
    };

    const auto unsupported = call(0);
    EXPECT_EQ(ipc::parseEnvelope(unsupported).value()->status(),
              static_cast<std::uint16_t>(Errc::NotSupported));

    FakeCups cups;
    cups.lanes = {LaneSnapshot{
        .laneId = 1,
        .seq = 3,
        .depth = 50,
        .cups = {CupState{.cupId = 10, .cells = {{.sensorId = 1, .status = CellStatus::Ok}}}}}};
    handler.setCupSource(&cups);

    const auto bytes = call(1);
    const auto env = ipc::parseEnvelope(bytes).value();
    EXPECT_EQ(env->status(), 0U);
    EXPECT_EQ(env->request_id(), 5U);
    const auto* reply = env->payload_as_CupSnapshotReply();
    ASSERT_NE(reply, nullptr);
    ASSERT_EQ(reply->lanes()->size(), 1U);
    const auto* lane = reply->lanes()->Get(0);
    EXPECT_EQ(lane->lane_id(), 1);
    EXPECT_EQ(lane->seq(), 3U);
    EXPECT_EQ(lane->depth(), 50);
    EXPECT_EQ(lane->cups()->Get(0)->cup_id(), 10);
    EXPECT_EQ(cups.last, std::optional<std::uint16_t>{1});

    (void)call(0);
    EXPECT_FALSE(cups.last.has_value()); // 0 = all lanes

    flatbuffers::FlatBufferBuilder fbb;
    const auto hello = fb::CreateHelloRequest(fbb, ipc::kProtocolVersion);
    const auto helloBytes = handler.handle(
        testutil::request(fbb, fb::MsgType::Hello, 1, fb::Payload::HelloRequest, hello.Union()));
    const auto* caps =
        ipc::parseEnvelope(helloBytes).value()->payload_as_HelloReply()->capabilities();
    bool hasCups = false;
    for (const auto* c : *caps) {
        hasCups = hasCups || c->str() == "cups";
    }
    EXPECT_TRUE(hasCups);
}

TEST_F(CupMonitorTest, MeasurementsReachTheUpdatesAndTheIpcMessage) {
    CupMonitor monitor{store.get()};
    ModuleContext context{bus};
    ASSERT_TRUE(monitor.init(context).has_value());
    auto updates = bus.subscribe<CupUpdate>();

    auto photo = rec(4, 1);
    photo.frameId = FrameId{40};
    bus.publish(photo);
    bus.publish(Measurement{.laneId = 1,
                            .cupId = 4,
                            .sensorId = 1,
                            .cameraId = 0,
                            .frameId = FrameId{40},
                            .values = {{"present", 1.0}, {"length_mm", 61.25}}});
    monitor.flush();

    std::optional<CupUpdate> got;
    updates->drain([&](const CupUpdate& u) { got = u; });
    ASSERT_TRUE(got.has_value());
    const auto& cell = got->cups.at(0).cells.at(0);
    ASSERT_EQ(cell.measurements.size(), 2U);
    EXPECT_EQ(cell.measurements[1].key, "length_mm");

    const auto bytes = makeCupUpdateEnvelope(*got);
    const auto env = ipc::parseEnvelope(bytes);
    ASSERT_TRUE(env.has_value());
    const auto* fbCell = (*env)->payload_as_CupUpdateEvent()->cups()->Get(0)->cells()->Get(0);
    ASSERT_NE(fbCell->measurements(), nullptr);
    ASSERT_EQ(fbCell->measurements()->size(), 2U);
    EXPECT_EQ(fbCell->measurements()->Get(1)->key()->str(), "length_mm");
    EXPECT_DOUBLE_EQ(fbCell->measurements()->Get(1)->value(), 61.25);
    const auto* noMeasurements =
        (*env)->payload_as_CupUpdateEvent()->cups()->Get(0)->cells()->Get(1);
    EXPECT_EQ(noMeasurements->measurements(), nullptr);
    EXPECT_EQ(monitor.health().state, HealthState::Ok);
}
