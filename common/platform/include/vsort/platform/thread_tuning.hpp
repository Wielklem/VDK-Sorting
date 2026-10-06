#pragma once

#include <cstdint>
#include <memory>
#include <span>
#include <string_view>

#include <vsort/common/error.hpp>
#include <vsort/platform/interface.hpp>

namespace vsort::platform {

enum class ThreadPriority : std::uint8_t {
    Normal,    // default time-sharing
    High,      // raised, still time-shared (may need privileges)
    RealTime,  // fixed real-time priority (Linux: needs CAP_SYS_NICE or an rtprio limit)
};

// All calls act on the calling thread, so no native thread handles leak out of the platform layer.
class IThreadTuning : public Interface {
public:
    [[nodiscard]] virtual Result<> setCurrentThreadName(std::string_view name) = 0;  // Linux: max 15 chars
    [[nodiscard]] virtual Result<> pinCurrentThread(std::span<const unsigned> cpus) = 0;
    [[nodiscard]] virtual Result<> setCurrentThreadPriority(ThreadPriority priority) = 0;
    [[nodiscard]] virtual unsigned availableCpuCount() const = 0;
};

[[nodiscard]] Result<std::unique_ptr<IThreadTuning>> makeThreadTuning();

} // namespace vsort::platform
