#include <vsort/platform/thread_tuning.hpp>

#include <algorithm>
#include <cerrno>
#include <cstddef>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <thread>

#include <pthread.h>
#include <sched.h>
#include <sys/resource.h>
#include <unistd.h>

#include "linux_util.hpp"

namespace vsort::platform {
namespace {

constexpr std::size_t kMaxThreadNameLength = 15;  // Linux limit, excluding '\0'
constexpr int kRealTimePriority = 50;             // SCHED_FIFO 1..99; leaves room for IRQ threads
constexpr int kHighNiceOffset = -10;

class LinuxThreadTuning final : public IThreadTuning {
public:
    LinuxThreadTuning() {
        errno = 0;
        const int niceValue = ::getpriority(PRIO_PROCESS, static_cast<id_t>(::getpid()));
        baseNice_ = (errno == 0) ? niceValue : 0;
    }

    [[nodiscard]] Result<> setCurrentThreadName(std::string_view name) override {
        const std::string shortName{name.substr(0, kMaxThreadNameLength)};
        if (const int rc = ::pthread_setname_np(::pthread_self(), shortName.c_str()); rc != 0) {
            return detail::sysError("pthread_setname_np", rc);
        }
        return {};
    }

    [[nodiscard]] Result<> pinCurrentThread(std::span<const unsigned> cpus) override {
        if (cpus.empty()) {
            return makeError(Errc::InvalidArgument, "pinCurrentThread: no CPUs given");
        }
        cpu_set_t set{};
        CPU_ZERO(&set);
        for (const unsigned cpu : cpus) {
            if (cpu >= static_cast<unsigned>(CPU_SETSIZE)) {
                return makeError(Errc::InvalidArgument, "pinCurrentThread: CPU index out of range");
            }
            CPU_SET(cpu, &set);
        }
        if (const int rc = ::pthread_setaffinity_np(::pthread_self(), sizeof(set), &set); rc != 0) {
            return detail::sysError("pthread_setaffinity_np", rc);
        }
        return {};
    }

    [[nodiscard]] Result<> setCurrentThreadPriority(ThreadPriority priority) override {
        sched_param param{};
        int policy = SCHED_OTHER;
        if (priority == ThreadPriority::RealTime) {
            policy = SCHED_FIFO;
            param.sched_priority = kRealTimePriority;
        }
        if (const int rc = ::pthread_setschedparam(::pthread_self(), policy, &param); rc != 0) {
            return detail::sysError("pthread_setschedparam", rc);
        }
        if (priority == ThreadPriority::RealTime) {
            return {};
        }
        // Linux applies a nice value per thread when given the thread ID.
        const int niceValue = (priority == ThreadPriority::High) ? baseNice_ + kHighNiceOffset : baseNice_;
        if (::setpriority(PRIO_PROCESS, static_cast<id_t>(::gettid()), niceValue) != 0) {
            const int err = errno;
            return detail::sysError("setpriority", err);
        }
        return {};
    }

    [[nodiscard]] unsigned availableCpuCount() const override {
        cpu_set_t set{};
        CPU_ZERO(&set);
        if (::sched_getaffinity(0, sizeof(set), &set) == 0) {
            return static_cast<unsigned>(CPU_COUNT(&set));
        }
        return std::max(1U, std::thread::hardware_concurrency());
    }

private:
    int baseNice_{0};
};

} // namespace

Result<std::unique_ptr<IThreadTuning>> makeThreadTuning() {
    return std::make_unique<LinuxThreadTuning>();
}

} // namespace vsort::platform
