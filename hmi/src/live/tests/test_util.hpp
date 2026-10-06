#pragma once

#include <QCoreApplication>
#include <QThread>
#include <chrono>
#include <functional>

#include <vsort/common/error.hpp>

// Windows stubs (M15.20 not done) return NotSupported for shared memory: skip instead of fail.
#define VSORT_HMI_SKIP_IF_NO_RINGS(service)                                                        \
    do {                                                                                           \
        if (!(service).ringsAvailable()) {                                                         \
            GTEST_SKIP() << "shared memory not available on this platform";                        \
        }                                                                                          \
    } while (false)

namespace vsort::hmi::test {

// Runs the Qt event loop and `step` (service pump, frame writer) until `done` or the timeout.
inline bool spinUntil(const std::function<bool()>& done, const std::function<void()>& step,
                      std::chrono::milliseconds timeout = std::chrono::milliseconds{5000}) {
    const auto end = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < end) {
        step();
        QCoreApplication::processEvents();
        if (done()) {
            return true;
        }
        QThread::msleep(5);
    }
    return done();
}

inline void spinFor(std::chrono::milliseconds duration, const std::function<void()>& step) {
    (void)spinUntil([] { return false; }, step, duration);
}

} // namespace vsort::hmi::test
