#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include <vsort/platform/thread_tuning.hpp>

#include "test_util.hpp"

using vsort::platform::ThreadPriority;

TEST(ThreadTuning, NameAffinityPriority) {
    auto tuning = vsort::platform::makeThreadTuning();
    VSORT_SKIP_IF_NOT_SUPPORTED(tuning);
    ASSERT_TRUE(tuning.has_value()) << tuning.error().what();
    auto& t = **tuning;
    EXPECT_GE(t.availableCpuCount(), 1U);

    std::thread worker{[&t] {
        EXPECT_TRUE(t.setCurrentThreadName("vsort-test-name-longer-than-15").has_value());

        // All CPUs, so at least one is allowed even under a restricted cpuset (CI containers).
        std::vector<unsigned> cpus;
        for (unsigned i = 0; i < std::thread::hardware_concurrency(); ++i) {
            cpus.push_back(i);
        }
        const auto pinned = t.pinCurrentThread(cpus);
        EXPECT_TRUE(pinned.has_value()) << pinned.error().what();

        const auto normal = t.setCurrentThreadPriority(ThreadPriority::Normal);
        EXPECT_TRUE(normal.has_value()) << normal.error().what();

        // Needs privileges: either outcome is fine, only a non-permission error is a bug.
        const auto rt = t.setCurrentThreadPriority(ThreadPriority::RealTime);
        if (!rt.has_value()) {
            EXPECT_EQ(rt.error().code, vsort::Errc::PermissionDenied) << rt.error().what();
        }
    }};
    worker.join();
}

TEST(ThreadTuning, RejectsEmptyCpuList) {
    auto tuning = vsort::platform::makeThreadTuning();
    VSORT_SKIP_IF_NOT_SUPPORTED(tuning);
    ASSERT_TRUE(tuning.has_value());
    const auto r = (*tuning)->pinCurrentThread({});
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, vsort::Errc::InvalidArgument);
}
