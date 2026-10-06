#include <QObject>
#include <chrono>
#include <memory>
#include <utility>

#include <gtest/gtest.h>

#include "fake_service.hpp"
#include "live/live_view_model.hpp"
#include "live/service_client.hpp"
#include "test_util.hpp"
#include "video/frame_sink.hpp"

namespace {

using namespace std::chrono_literals;
using namespace vsort::hmi;
using namespace vsort::hmi::test;

// Counts the frames it gets, like a VideoItem would show them.
class TestSink final : public QObject, public IFrameSink {
public:
    void submitFrame(FramePtr frame) override {
        last = std::move(frame);
        ++submitted;
    }
    void clear() override { last = nullptr; }

    FramePtr last;
    int submitted{0};
};

constexpr std::uint16_t kCam1 = 61;
constexpr std::uint16_t kCam2 = 62;

ServiceEndpoints testEndpoints() {
    return {.host = QStringLiteral("127.0.0.1"),
            .commandPort = FakeService::kCommandPort,
            .eventPort = FakeService::kEventPort};
}

class LiveViewModelTest : public ::testing::Test {
protected:
    LiveViewModelTest()
        : service{{kCam1, kCam2}}
        , client{testEndpoints()}
        , model{client} {}

    void SetUp() override {
        VSORT_HMI_SKIP_IF_NO_RINGS(service);
        client.start();
    }

    void step() {
        service.pump();
        service.writeFrames();
    }

    bool waitForFrames() {
        return spinUntil(
            [&] {
                return model.rowCount() == 2 &&
                       model.data(model.index(0), LiveViewModel::HasFrameRole).toBool() &&
                       model.data(model.index(1), LiveViewModel::HasFrameRole).toBool();
            },
            [this] { step(); });
    }

    [[nodiscard]] quint64 counter(int row) const {
        return model.data(model.index(row), LiveViewModel::FrameCounterRole).toULongLong();
    }

    FakeService service;
    ServiceClient client;
    LiveViewModel model;
};

TEST_F(LiveViewModelTest, ConnectsListsCamerasAndReceivesFrames) {
    ASSERT_TRUE(waitForFrames());
    EXPECT_TRUE(model.connected());
    EXPECT_EQ(model.cameraCount(), 2);
    EXPECT_EQ(model.data(model.index(0), LiveViewModel::CameraIdRole).toInt(), kCam1);
    EXPECT_EQ(model.data(model.index(1), LiveViewModel::SerialRole).toString(), "SN62");
    EXPECT_EQ(model.data(model.index(0), LiveViewModel::LinkStateRole).toString(), "streaming");
    EXPECT_EQ(model.data(model.index(0), LiveViewModel::FrameWidthRole).toInt(), 64);
    EXPECT_EQ(service.setPreviewRequests(), 2); // the HMI switched both previews on
}

TEST_F(LiveViewModelTest, AttachedSinksGetTheFramesOfTheirCameraOnly) {
    TestSink sink1;
    TestSink sink2;
    model.attachVideo(kCam1, &sink1);
    model.attachVideo(kCam2, &sink2);
    ASSERT_TRUE(waitForFrames());
    ASSERT_TRUE(
        spinUntil([&] { return sink1.submitted > 2 && sink2.submitted > 2; }, [this] { step(); }));
    EXPECT_EQ(sink1.last->info.width, 64U);
    // The test pattern depends on the camera id, so the two pictures differ.
    EXPECT_NE(sink1.last->pixels, sink2.last->pixels);
}

TEST_F(LiveViewModelTest, LateSinkGetsTheNewestFrameImmediately) {
    ASSERT_TRUE(waitForFrames());
    TestSink late;
    model.attachVideo(kCam2, &late);
    ASSERT_NE(late.last, nullptr);
    EXPECT_EQ(late.submitted, 1);
}

TEST_F(LiveViewModelTest, DestroyedAndForeignObjectsAreHandled) {
    ASSERT_TRUE(waitForFrames());
    QObject notASink;
    model.attachVideo(kCam1, &notASink); // ignored, no crash
    {
        TestSink gone;
        model.attachVideo(kCam1, &gone);
    } // destroyed: must be detached
    spinFor(200ms, [this] { step(); });
    SUCCEED();
}

TEST_F(LiveViewModelTest, FreezeOneCameraKeepsItsImageAndLeavesTheOthersRunning) {
    TestSink sink1;
    TestSink sink2;
    model.attachVideo(kCam1, &sink1);
    model.attachVideo(kCam2, &sink2);
    ASSERT_TRUE(waitForFrames());
    model.setFrozen(kCam1, true);
    EXPECT_TRUE(model.data(model.index(0), LiveViewModel::FrozenRole).toBool());
    EXPECT_FALSE(model.allFrozen());

    const quint64 frozenCounter = counter(0);
    const quint64 runningCounter = counter(1);
    const int frozenSubmits = sink1.submitted;
    const int runningSubmits = sink2.submitted;
    spinFor(400ms, [this] { step(); });

    EXPECT_EQ(counter(0), frozenCounter);
    EXPECT_EQ(sink1.submitted, frozenSubmits); // the frozen camera's item gets nothing new
    EXPECT_GT(counter(1), runningCounter);
    EXPECT_GT(sink2.submitted, runningSubmits);

    model.setFrozen(kCam1, false);
    EXPECT_TRUE(spinUntil([&] { return counter(0) > frozenCounter; }, [this] { step(); }));
    EXPECT_GT(sink1.submitted, frozenSubmits);
}

TEST_F(LiveViewModelTest, FreezeAllAndUnfreezeAll) {
    ASSERT_TRUE(waitForFrames());
    model.setAllFrozen(true);
    EXPECT_TRUE(model.allFrozen());
    const quint64 c0 = counter(0);
    const quint64 c1 = counter(1);
    spinFor(300ms, [this] { step(); });
    EXPECT_EQ(counter(0), c0);
    EXPECT_EQ(counter(1), c1);

    model.toggleFrozen(kCam2);
    EXPECT_FALSE(model.allFrozen());

    model.setAllFrozen(false);
    EXPECT_FALSE(model.allFrozen());
    EXPECT_TRUE(spinUntil([&] { return counter(0) > c0 && counter(1) > c1; }, [this] { step(); }));
}

TEST_F(LiveViewModelTest, UnknownCameraIsIgnored) {
    ASSERT_TRUE(waitForFrames());
    model.setFrozen(999, true);
    model.toggleFrozen(-1);
    EXPECT_FALSE(model.allFrozen());
}

TEST_F(LiveViewModelTest, ServiceOfflineAndBackRefetchesTheCameraList) {
    ASSERT_TRUE(waitForFrames());
    const int requestsBefore = service.cameraListRequests();

    service.setHeartbeats(false);
    EXPECT_TRUE(spinUntil([&] { return !model.connected(); }, [this] { service.pump(); }, 6s));
    EXPECT_EQ(model.data(model.index(0), LiveViewModel::LinkStateRole).toString(), "offline");

    service.setHeartbeats(true);
    EXPECT_TRUE(spinUntil([&] { return model.connected(); }, [this] { step(); }));
    EXPECT_TRUE(spinUntil([&] { return service.cameraListRequests() > requestsBefore; },
                          [this] { step(); }));
}

TEST_F(LiveViewModelTest, FrozenStateSurvivesACameraListRefresh) {
    ASSERT_TRUE(waitForFrames());
    model.setFrozen(kCam2, true);
    client.requestCameraList();
    EXPECT_TRUE(
        spinUntil([&] { return model.data(model.index(1), LiveViewModel::FrozenRole).toBool(); },
                  [this] { step(); }));
    EXPECT_FALSE(model.data(model.index(0), LiveViewModel::FrozenRole).toBool());
}

} // namespace
