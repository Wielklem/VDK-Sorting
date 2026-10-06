#pragma once

#include <algorithm>
#include <cstddef>
#include <memory>
#include <span>
#include <string>
#include <string_view>

#include <vsort/common/error.hpp>
#include <vsort/platform/interface.hpp>

namespace vsort::platform {

// Named memory region shared between processes (service -> HMI preview frames, P30.10).
class ISharedMemory : public Interface {
public:
    [[nodiscard]] virtual std::span<std::byte> bytes() noexcept = 0;
    [[nodiscard]] virtual std::size_t size() const noexcept = 0;
    [[nodiscard]] virtual const std::string& name() const noexcept = 0;
};

inline constexpr std::size_t kMaxSharedMemoryNameLength = 64;

// 1..64 chars of [A-Za-z0-9_-]; the OS-specific prefix is added by the implementation.
[[nodiscard]] constexpr bool isValidSharedMemoryName(std::string_view name) noexcept {
    if (name.empty() || name.size() > kMaxSharedMemoryNameLength) {
        return false;
    }
    return std::ranges::all_of(name, [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
               c == '_' || c == '-';
    });
}

// Owner side. Replaces a stale region with the same name (e.g. after a crash).
// The name is removed when the returned object is destroyed.
[[nodiscard]] Result<std::unique_ptr<ISharedMemory>> createSharedMemory(std::string_view name,
                                                                        std::size_t size);

// Client side. Maps the whole existing region read-write. NotFound if it doesn't exist.
[[nodiscard]] Result<std::unique_ptr<ISharedMemory>> openSharedMemory(std::string_view name);

} // namespace vsort::platform
