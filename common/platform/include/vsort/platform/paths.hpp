#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>

#include <vsort/common/error.hpp>
#include <vsort/platform/interface.hpp>

namespace vsort::platform {

// Where the service keeps its files. The only source of these paths; never hardcode them elsewhere.
class IPaths : public Interface {
public:
    [[nodiscard]] virtual std::filesystem::path configDir() const = 0;
    [[nodiscard]] virtual std::filesystem::path dataDir() const = 0;
    [[nodiscard]] virtual std::filesystem::path logDir() const = 0;
};

enum class PathScope : std::uint8_t {
    System,  // installed service. Linux: /etc, /var/lib, /var/log. Windows: %ProgramData%
    User,    // developer run.     Linux: XDG dirs.                Windows: %LOCALAPPDATA%
};

// OS-standard locations.
[[nodiscard]] Result<std::unique_ptr<IPaths>> makeStandardPaths(PathScope scope);

// Everything under one root: <root>/config, <root>/data, <root>/log. Portable; for tests and a --root option.
[[nodiscard]] std::unique_ptr<IPaths> makeRootedPaths(const std::filesystem::path& root);

// Creates configDir, dataDir and logDir if missing. Portable.
[[nodiscard]] Result<> ensureDirectories(const IPaths& paths);

} // namespace vsort::platform
