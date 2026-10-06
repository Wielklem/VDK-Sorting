#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <vsort/camera/camera.hpp>
#include <vsort/common/error.hpp>
#include <vsort/common/logging.hpp>

namespace vsort::record {

constexpr int kExitOk = 0;
constexpr int kExitFailure = 1;
constexpr int kExitUsage = 2;
constexpr int kExitLossy = 3; // recorded, but frames were dropped or missing

inline constexpr std::size_t kMaxQueueDepth = 1024;

struct CameraSpec {
    std::uint16_t index{0}; // logical camera ID; names the file camNN.vrec
    std::string serial;
};

struct Options {
    bool help{false};
    bool version{false};
    std::vector<CameraSpec> cameras;
    std::filesystem::path outDir; // empty = per-user data folder + "recordings"
    std::string label;
    double exposureUs{10000.0};
    double gainDb{0.0};
    camera::TriggerMode trigger{camera::TriggerMode::Hardware};
    std::chrono::seconds duration{0}; // 0 = until stopped (Ctrl-C) or --frames is reached
    std::uint64_t framesPerCamera{0}; // 0 = no limit
    std::size_t queueDepth{32};
    log::Level logLevel{log::Level::Info};
};

// args: command line without argv[0]. Accepts "--opt value" and "--opt=value".
// At least one --camera is required, except for --help and --version.
[[nodiscard]] Result<Options> parseArgs(std::span<const std::string_view> args);

[[nodiscard]] std::string_view usage() noexcept;

} // namespace vsort::record
