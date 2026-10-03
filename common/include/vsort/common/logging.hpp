#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>

#include <spdlog/logger.h>

#include <vsort/common/error.hpp>

namespace vsort::log {

enum class Level : std::uint8_t { Trace, Debug, Info, Warn, Error, Critical, Off };

struct Config {
    std::filesystem::path directory;  // empty = console only; supplied by IPaths (M15)
    std::string fileName{"vsort.log"};
    std::size_t maxFileSize{std::size_t{10} * 1024U * 1024U};
    std::size_t maxFiles{5U};
    Level level{Level::Info};
    bool console{true};
};

// Call once at startup, before creating module loggers.
[[nodiscard]] Result<void> init(const Config& config);

// Named logger per module, e.g. get("camera"). Shares the sinks from init().
[[nodiscard]] std::shared_ptr<spdlog::logger> get(std::string_view module);

void setLevel(Level level);

// Flush and release all loggers. Call at shutdown.
void shutdown();

} // namespace vsort::log
