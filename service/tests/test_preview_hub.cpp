#include <algorithm>
#include <atomic>
#include <chrono>
#include <mutex>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include <vsort/ipc/preview_ring.hpp>

#include "ipc/preview_hub.hpp"
#include "test_ipc_util.hpp"

using namespace vsort;
using namespace vsort::service;
using namespace std::chrono_literals;
using vsort::testutil::makeFrame;

namespace {

template <typename Pred>
bool waitFor(Pred pred, std::chrono::milliseconds timeout = 3s) {
    const auto end = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < end) {
        if (pred()) {
            return true;
        }
        std::this_thread::sleep_for(5ms);
    }
    return pred();
}

struct Events {
    std::mutex mutex;
    std::vector<PreviewStream> list;
    void add(const PreviewStream& s) {
        const std::scoped_lock lock{mutex};
        list.push_back(s);
    }
    std::size_t size() {
        const std::scoped_lock lock{mutex};
        return list.size();
    }
    PreviewStream at(std::size_t i) {
        const std::scoped_lock lock{mutex};
        return list.at(i);
    }
};

} // namespace

TEST(PreviewHub, DisabledByDefaultIgnoresFrames) {
    PreviewHub hub{PreviewDefaults{}};
    ASSERT_TRUE(hub.start().has_value());
    const auto f = makeFrame(1, 1, 64, 64, 5);
    hub.submit(f->frame);
    EXPECT_EQ(hub.counters().offered, 0U);
    EXPECT_FALSE(hub.settings(1).enabled);
    EXPECT_TRUE(hub.stream(1).shmName.empty());
}

TEST(PreviewHub, ValidatesSettings) {
    PreviewHub hub{PreviewDefaults{}};
    EXPECT_EQ(hub.setPreview(1, {.enabled = true, .fps = 0, .maxWidth = 640}).error().code,
              Errc::InvalidArgument);
    EXPECT_EQ(hub.setPreview(1, {.enabled = true, .fps = 61, .maxWidth = 640}).error().code,
              Errc::InvalidArgument);
    EXPECT_EQ(hub.setPreview(1, {.enabled = true, .fps = 10, .maxWidth = 8}).error().code,
              Errc::InvalidArgument);
    EXPECT_EQ(hub.setPreview(1, {.enabled = true, .fps = 10, .maxWidth = 9000}).error().code,
              Errc::InvalidArgument);
    EXPECT_TRUE(hub.setPreview(1, {.enabled = true, .fps = 10, .maxWidth = 640}).has_value());
    EXPECT_TRUE(hub.settings(1).enabled);
}

TEST(PreviewHub, PublishesDownscaledFrameToRing) {
    VSORT_SKIP_IF_NO_SHM();
    Events events;
    PreviewHub hub{PreviewDefaults{}, [&](const PreviewStream& s) { events.add(s); }};
    ASSERT_TRUE(hub.start().has_value());
    ASSERT_TRUE(hub.setPreview(1, {.enabled = true, .fps = 60, .maxWidth = 64}).has_value());

    const auto f = makeFrame(1, 42, 128, 64, 90);
    hub.submit(f->frame);
    ASSERT_TRUE(waitFor([&] { return hub.counters().published >= 1; }));

    ASSERT_EQ(events.size(), 1U);
    const auto info = events.at(0);
    EXPECT_EQ(info.cameraId, 1U);
    EXPECT_EQ(info.generation, 1U);
    EXPECT_EQ(info.width, 64U);
    EXPECT_EQ(info.height, 32U);
    EXPECT_EQ(info.shmName, ipc::previewRingName(1, 1));
    EXPECT_EQ(hub.stream(1).shmName, info.shmName);

    auto reader = ipc::PreviewRingReader::open(info.shmName);
    ASSERT_TRUE(reader.has_value()) << reader.error().what();
    std::uint64_t seen = 0;
    const auto frame = (*reader)->readNewest(seen);
    ASSERT_TRUE(frame.has_value());
    EXPECT_EQ(frame->info.frameId, 42U);
    EXPECT_EQ(frame->info.width, 64U);
    EXPECT_EQ(frame->info.height, 32U);
    ASSERT_EQ(frame->pixels.size(), 64U * 32U);
    EXPECT_EQ(frame->pixels.front(), std::byte{90});
}

TEST(PreviewHub, FpsLimitSkipsFrames) {
    VSORT_SKIP_IF_NO_SHM();
    PreviewHub hub{PreviewDefaults{}};
    ASSERT_TRUE(hub.start().has_value());
    ASSERT_TRUE(hub.setPreview(1, {.enabled = true, .fps = 1, .maxWidth = 64}).has_value());

    for (std::uint64_t i = 1; i <= 10; ++i) {
        const auto f = makeFrame(1, i, 64, 64, 1);
        hub.submit(f->frame);
    }
    ASSERT_TRUE(waitFor([&] { return hub.counters().published >= 1; }));
    const auto c = hub.counters();
    EXPECT_EQ(c.offered, 10U);
    EXPECT_GE(c.skipped,
              8U); // 1 fps: at most the first (and maybe one more after 1 s) gets through
    EXPECT_LE(c.published, 2U);
}

TEST(PreviewHub, SizeChangeCreatesNewGenerationAndKeepsOldRingBriefly) {
    VSORT_SKIP_IF_NO_SHM();
    Events events;
    PreviewHub hub{PreviewDefaults{}, [&](const PreviewStream& s) { events.add(s); }};
    ASSERT_TRUE(hub.start().has_value());
    ASSERT_TRUE(hub.setPreview(1, {.enabled = true, .fps = 60, .maxWidth = 64}).has_value());
    hub.submit(makeFrame(1, 1, 128, 64, 10)->frame);
    ASSERT_TRUE(waitFor([&] { return hub.counters().published >= 1; }));

    ASSERT_TRUE(hub.setPreview(1, {.enabled = true, .fps = 60, .maxWidth = 32}).has_value());
    std::this_thread::sleep_for(30ms); // fps limit: 16 ms
    hub.submit(makeFrame(1, 2, 128, 64, 20)->frame);
    ASSERT_TRUE(waitFor([&] { return events.size() >= 2; }));

    const auto second = events.at(1);
    EXPECT_EQ(second.generation, 2U);
    EXPECT_EQ(second.width, 32U);
    EXPECT_NE(second.shmName, events.at(0).shmName);
    EXPECT_TRUE(
        ipc::PreviewRingReader::open(events.at(0).shmName).has_value()); // old ring lives on
    EXPECT_TRUE(ipc::PreviewRingReader::open(second.shmName).has_value());
    EXPECT_TRUE(waitFor(
        [&] { return !ipc::PreviewRingReader::open(events.at(0).shmName).has_value(); }, 4s));
}

TEST(PreviewHub, DisablingClosesTheRing) {
    VSORT_SKIP_IF_NO_SHM();
    Events events;
    PreviewHub hub{PreviewDefaults{}, [&](const PreviewStream& s) { events.add(s); }};
    ASSERT_TRUE(hub.start().has_value());
    ASSERT_TRUE(hub.setPreview(1, {.enabled = true, .fps = 60, .maxWidth = 64}).has_value());
    hub.submit(makeFrame(1, 1, 64, 64, 1)->frame);
    ASSERT_TRUE(waitFor([&] { return events.size() >= 1; }));

    ASSERT_TRUE(hub.setPreview(1, {.enabled = false, .fps = 60, .maxWidth = 64}).has_value());
    ASSERT_TRUE(waitFor([&] { return events.size() >= 2; }));
    EXPECT_TRUE(events.at(1).shmName.empty());
    EXPECT_TRUE(hub.stream(1).shmName.empty());

    hub.submit(makeFrame(1, 2, 64, 64, 1)->frame); // ignored while disabled
    EXPECT_EQ(hub.counters().offered, 1U);
}

TEST(PreviewHub, CopiesFramesThatHaveNoOwner) {
    VSORT_SKIP_IF_NO_SHM();
    Events events;
    PreviewHub hub{PreviewDefaults{}, [&](const PreviewStream& s) { events.add(s); }};
    ASSERT_TRUE(hub.start().has_value());
    ASSERT_TRUE(hub.setPreview(1, {.enabled = true, .fps = 60, .maxWidth = 64}).has_value());

    auto f = makeFrame(1, 9, 64, 64, 77, camera::PixelFormat::Mono8, 0, /*withOwner=*/false);
    hub.submit(f->frame);
    std::fill(f->bytes.begin(), f->bytes.end(), std::byte{0}); // camera reuses its buffer
    ASSERT_TRUE(waitFor([&] { return hub.counters().published >= 1; }));

    auto reader = ipc::PreviewRingReader::open(events.at(0).shmName);
    ASSERT_TRUE(reader.has_value());
    std::uint64_t seen = 0;
    const auto frame = (*reader)->readNewest(seen);
    ASSERT_TRUE(frame.has_value());
    EXPECT_EQ(frame->pixels.front(), std::byte{77});
}

TEST(PreviewHub, BadFrameCountsAnErrorAndKeepsRunning) {
    VSORT_SKIP_IF_NO_SHM();
    PreviewHub hub{PreviewDefaults{}};
    ASSERT_TRUE(hub.start().has_value());
    ASSERT_TRUE(hub.setPreview(1, {.enabled = true, .fps = 60, .maxWidth = 64}).has_value());

    auto bad = makeFrame(1, 1, 64, 64, 1);
    bad->frame.meta.strideBytes = 3;
    hub.submit(bad->frame);
    ASSERT_TRUE(waitFor([&] { return hub.counters().errors >= 1; }));

    std::this_thread::sleep_for(30ms);
    hub.submit(makeFrame(1, 2, 64, 64, 1)->frame);
    EXPECT_TRUE(waitFor([&] { return hub.counters().published >= 1; }));
}

TEST(PreviewHub, StopClosesRingsAndIsIdempotent) {
    VSORT_SKIP_IF_NO_SHM();
    Events events;
    {
        PreviewHub hub{PreviewDefaults{}, [&](const PreviewStream& s) { events.add(s); }};
        ASSERT_TRUE(hub.start().has_value());
        EXPECT_EQ(hub.start().error().code, Errc::AlreadyExists);
        ASSERT_TRUE(hub.setPreview(1, {.enabled = true, .fps = 60, .maxWidth = 64}).has_value());
        hub.submit(makeFrame(1, 1, 64, 64, 1)->frame);
        ASSERT_TRUE(waitFor([&] { return events.size() >= 1; }));
        hub.stop();
        hub.stop();
        EXPECT_FALSE(ipc::PreviewRingReader::open(events.at(0).shmName).has_value());
    }
}
