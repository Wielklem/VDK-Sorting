#include <chrono>
#include <thread>

#include <gtest/gtest.h>

#include <vsort/platform/service_host.hpp>

#include "test_util.hpp"

#if defined(__linux__)
#include <signal.h>
#include <unistd.h>
#endif

using namespace std::chrono_literals;

TEST(ServiceHost, RequestStopWakesWaiter) {
    auto host = vsort::platform::makeServiceHost();
    VSORT_SKIP_IF_NOT_SUPPORTED(host);
    ASSERT_TRUE(host.has_value()) << host.error().what();
    auto& h = **host;

    EXPECT_FALSE(h.stopRequested());
    EXPECT_FALSE(h.waitForStop(10ms));

    std::thread stopper{[&h] {
        std::this_thread::sleep_for(20ms);
        h.requestStop();
    }};
    EXPECT_TRUE(h.waitForStop(5s));
    stopper.join();
    EXPECT_TRUE(h.stopRequested());

    h.notifyReady();  // no-op outside systemd; must not crash
}

#if defined(__linux__)
TEST(ServiceHost, SigtermRequestsStop) {
    auto host = vsort::platform::makeServiceHost();
    ASSERT_TRUE(host.has_value()) << host.error().what();
    auto& h = **host;

    ASSERT_TRUE(h.installStopHandlers().has_value());
    EXPECT_FALSE(h.installStopHandlers().has_value());  // second call rejected

    ASSERT_EQ(::kill(::getpid(), SIGTERM), 0);
    EXPECT_TRUE(h.waitForStop(5s));
}
#endif
