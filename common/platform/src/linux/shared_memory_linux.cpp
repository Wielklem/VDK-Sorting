#include <vsort/platform/shared_memory.hpp>

#include <cerrno>
#include <cstddef>
#include <expected>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include "linux_util.hpp"

namespace vsort::platform {
namespace {

constexpr mode_t kShmPermissions = 0660;  // owner + group (service and HMI users share a group)

class LinuxSharedMemory final : public ISharedMemory {
public:
    LinuxSharedMemory(std::string name, std::string osName, std::byte* data, std::size_t size,
                      bool owner) noexcept
        : name_{std::move(name)}, osName_{std::move(osName)}, data_{data}, size_{size}, owner_{owner} {}

    ~LinuxSharedMemory() override {
        ::munmap(data_, size_);
        if (owner_) {
            ::shm_unlink(osName_.c_str());
        }
    }

    LinuxSharedMemory(const LinuxSharedMemory&) = delete;
    LinuxSharedMemory& operator=(const LinuxSharedMemory&) = delete;
    LinuxSharedMemory(LinuxSharedMemory&&) = delete;
    LinuxSharedMemory& operator=(LinuxSharedMemory&&) = delete;

    [[nodiscard]] std::span<std::byte> bytes() noexcept override { return {data_, size_}; }
    [[nodiscard]] std::size_t size() const noexcept override { return size_; }
    [[nodiscard]] const std::string& name() const noexcept override { return name_; }

private:
    std::string name_;
    std::string osName_;
    std::byte* data_;
    std::size_t size_;
    bool owner_;
};

std::string toOsName(std::string_view name) {
    std::string out{"/vsort_"};
    out += name;
    return out;
}

// Maps fd and closes it; the mapping stays valid without the descriptor.
Result<std::byte*> mapAndClose(int fd, std::size_t size) {
    void* addr = ::mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    const int err = errno;
    ::close(fd);
    if (addr == MAP_FAILED) {
        return detail::sysError("mmap", err);
    }
    return static_cast<std::byte*>(addr);
}

} // namespace

Result<std::unique_ptr<ISharedMemory>> createSharedMemory(std::string_view name, std::size_t size) {
    if (!isValidSharedMemoryName(name) || size == 0) {
        return makeError(Errc::InvalidArgument, "createSharedMemory: invalid name or zero size");
    }
    const std::string osName = toOsName(name);
    ::shm_unlink(osName.c_str());  // remove a stale region left by a crashed run

    const int fd = ::shm_open(osName.c_str(), O_CREAT | O_EXCL | O_RDWR, kShmPermissions);
    if (fd < 0) {
        const int err = errno;
        return detail::sysError("shm_open " + osName, err);
    }
    ::fchmod(fd, kShmPermissions);  // undo umask
    if (::ftruncate(fd, static_cast<off_t>(size)) != 0) {
        const int err = errno;
        ::close(fd);
        ::shm_unlink(osName.c_str());
        return detail::sysError("ftruncate " + osName, err);
    }
    auto data = mapAndClose(fd, size);
    if (!data) {
        ::shm_unlink(osName.c_str());
        return std::unexpected{data.error()};
    }
    return std::make_unique<LinuxSharedMemory>(std::string{name}, osName, *data, size, true);
}

Result<std::unique_ptr<ISharedMemory>> openSharedMemory(std::string_view name) {
    if (!isValidSharedMemoryName(name)) {
        return makeError(Errc::InvalidArgument, "openSharedMemory: invalid name");
    }
    const std::string osName = toOsName(name);
    const int fd = ::shm_open(osName.c_str(), O_RDWR, 0);
    if (fd < 0) {
        const int err = errno;
        return detail::sysError("shm_open " + osName, err);
    }
    struct stat info{};
    if (::fstat(fd, &info) != 0 || info.st_size <= 0) {
        ::close(fd);
        return makeError(Errc::IoError, "openSharedMemory: cannot determine size of " + osName);
    }
    const auto size = static_cast<std::size_t>(info.st_size);
    auto data = mapAndClose(fd, size);
    if (!data) {
        return std::unexpected{data.error()};
    }
    return std::make_unique<LinuxSharedMemory>(std::string{name}, osName, *data, size, false);
}

} // namespace vsort::platform
