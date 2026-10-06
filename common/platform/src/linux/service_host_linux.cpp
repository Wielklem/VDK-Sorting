#include <vsort/platform/service_host.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdlib>
#include <ctime>
#include <iterator>
#include <memory>
#include <mutex>
#include <stop_token>
#include <string>
#include <string_view>
#include <thread>

#include <pthread.h>
#include <csignal>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include "linux_util.hpp"

namespace vsort::platform {
namespace {

constexpr long kSignalPollNs = 100'000'000;  // 100 ms: how fast the signal thread notices shutdown
constexpr int kForcedExitCode = 130;         // second SIGINT/SIGTERM while stopping

class LinuxServiceHost final : public IServiceHost {
public:
    LinuxServiceHost()
        : notifySocket_{detail::envString("NOTIFY_SOCKET")},
          underManager_{!notifySocket_.empty() || !detail::envString("INVOCATION_ID").empty()} {}

    ~LinuxServiceHost() override {
        if (signalThread_.joinable()) {
            signalThread_.request_stop();
            signalThread_.join();
            ::pthread_sigmask(SIG_SETMASK, &oldMask_, nullptr);
        }
    }

    LinuxServiceHost(const LinuxServiceHost&) = delete;
    LinuxServiceHost& operator=(const LinuxServiceHost&) = delete;
    LinuxServiceHost(LinuxServiceHost&&) = delete;
    LinuxServiceHost& operator=(LinuxServiceHost&&) = delete;

    [[nodiscard]] Result<> installStopHandlers() override {
        if (signalThread_.joinable()) {
            return makeError(Errc::AlreadyExists, "stop handlers already installed");
        }
        sigset_t set{};
        sigemptyset(&set);
        sigaddset(&set, SIGTERM);
        sigaddset(&set, SIGINT);
        // Blocked here; threads started later inherit the mask, so only the signal thread takes them.
        if (const int rc = ::pthread_sigmask(SIG_BLOCK, &set, &oldMask_); rc != 0) {
            return detail::sysError("pthread_sigmask", rc);
        }
        signalThread_ = std::jthread{[this, set](const std::stop_token& stop) {
            const timespec poll{.tv_sec = 0, .tv_nsec = kSignalPollNs};
            while (!stop.stop_requested()) {
                if (::sigtimedwait(&set, nullptr, &poll) < 0) {
                    continue;  // timeout or EINTR
                }
                if (stopRequested()) {
                    std::_Exit(kForcedExitCode);
                }
                requestStop();
            }
        }};
        return {};
    }

    [[nodiscard]] bool stopRequested() const noexcept override { return stop_.load(); }

    void requestStop() noexcept override {
        {
            const std::lock_guard lock{mutex_};
            stop_.store(true);
        }
        cv_.notify_all();
    }

    [[nodiscard]] bool waitForStop(std::chrono::milliseconds timeout) override {
        std::unique_lock lock{mutex_};
        return cv_.wait_for(lock, timeout, [this] { return stop_.load(); });
    }

    void notifyReady() noexcept override { sdNotify("READY=1"); }
    void notifyStopping() noexcept override { sdNotify("STOPPING=1"); }
    void notifyWatchdog() noexcept override { sdNotify("WATCHDOG=1"); }
    [[nodiscard]] bool underServiceManager() const noexcept override { return underManager_; }

private:
    // Minimal sd_notify(3), so no libsystemd dependency.
    void sdNotify(std::string_view message) const noexcept {
        sockaddr_un addr{};
        if (notifySocket_.empty() || notifySocket_.size() >= sizeof(addr.sun_path)) {
            return;
        }
        addr.sun_family = AF_UNIX;
        std::ranges::copy(notifySocket_, std::begin(addr.sun_path));
        if (addr.sun_path[0] == '@') {
            addr.sun_path[0] = '\0';  // abstract socket namespace
        }
        const auto length = static_cast<socklen_t>(offsetof(sockaddr_un, sun_path) + notifySocket_.size());
        const int fd = ::socket(AF_UNIX, SOCK_DGRAM | SOCK_CLOEXEC, 0);
        if (fd < 0) {
            return;
        }
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast): POSIX socket API
        ::sendto(fd, message.data(), message.size(), MSG_NOSIGNAL, reinterpret_cast<const sockaddr*>(&addr), length);
        ::close(fd);
    }

    std::string notifySocket_;
    bool underManager_{false};
    std::atomic<bool> stop_{false};
    std::mutex mutex_;
    std::condition_variable cv_;
    sigset_t oldMask_{};
    std::jthread signalThread_;  // last member: destroyed first
};

} // namespace

Result<std::unique_ptr<IServiceHost>> makeServiceHost() {
    return std::make_unique<LinuxServiceHost>();
}

} // namespace vsort::platform
