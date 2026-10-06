#include <vsort/platform/paths.hpp>

#include <filesystem>
#include <memory>
#include <utility>

#include "linux_util.hpp"

namespace vsort::platform {
namespace {

class LinuxPaths final : public IPaths {
public:
    LinuxPaths(std::filesystem::path config, std::filesystem::path data, std::filesystem::path log)
        : config_{std::move(config)}, data_{std::move(data)}, log_{std::move(log)} {}

    [[nodiscard]] std::filesystem::path configDir() const override { return config_; }
    [[nodiscard]] std::filesystem::path dataDir() const override { return data_; }
    [[nodiscard]] std::filesystem::path logDir() const override { return log_; }

private:
    std::filesystem::path config_;
    std::filesystem::path data_;
    std::filesystem::path log_;
};

// XDG base dir: the variable if absolute (the spec ignores relative values), else the fallback.
std::filesystem::path xdgDir(const char* variable, const std::filesystem::path& fallback) {
    const std::filesystem::path dir{detail::envString(variable)};
    return dir.is_absolute() ? dir : fallback;
}

} // namespace

Result<std::unique_ptr<IPaths>> makeStandardPaths(PathScope scope) {
    if (scope == PathScope::System) {
        // Matches the systemd unit (P90.60): ConfigurationDirectory/StateDirectory/LogsDirectory=vsort.
        return std::make_unique<LinuxPaths>("/etc/vsort", "/var/lib/vsort", "/var/log/vsort");
    }
    const std::filesystem::path home{detail::envString("HOME")};
    if (!home.is_absolute()) {
        return makeError(Errc::NotFound, "HOME is not set");
    }
    return std::make_unique<LinuxPaths>(
        xdgDir("XDG_CONFIG_HOME", home / ".config") / "vsort",
        xdgDir("XDG_DATA_HOME", home / ".local" / "share") / "vsort",
        xdgDir("XDG_STATE_HOME", home / ".local" / "state") / "vsort" / "log");
}

} // namespace vsort::platform
