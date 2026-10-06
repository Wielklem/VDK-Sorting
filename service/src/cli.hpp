#pragma once

#include <filesystem>
#include <span>
#include <string_view>

#include <vsort/common/error.hpp>
#include <vsort/common/logging.hpp>
#include <vsort/platform/paths.hpp>

namespace vsort::service {

constexpr int kExitOk = 0;
constexpr int kExitFailure = 1;
constexpr int kExitUsage = 2;

struct Options {
    bool help{false};
    bool version{false};
    bool check{false};      // initialise everything, then exit (startup smoke test)
    bool noConsole{false};  // log to file only
    std::filesystem::path root;  // empty = OS-standard paths (IPaths)
    platform::PathScope scope{platform::PathScope::System};
    log::Level logLevel{log::Level::Info};
};

// args: command line without argv[0]. Accepts "--opt value" and "--opt=value".
[[nodiscard]] Result<Options> parseArgs(std::span<const std::string_view> args);

[[nodiscard]] std::string_view usage() noexcept;

} // namespace vsort::service
