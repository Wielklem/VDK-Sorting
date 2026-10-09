#include "record_cli.hpp"

#include <array>
#include <charconv>
#include <format>
#include <limits>
#include <optional>
#include <string>
#include <system_error>
#include <utility>

namespace vsort::record {
namespace {

constexpr std::int64_t kMaxDurationSeconds = std::int64_t{7} * 24 * 3600;

template <typename T>
Result<T> parseNumber(std::string_view text, std::string_view option) {
    T value{};
    const char* const first = text.data();
    const char* const last = first + text.size();
    const auto [end, ec] = std::from_chars(first, last, value);
    if (text.empty() || ec != std::errc{} || end != last) {
        return makeError(Errc::InvalidArgument,
                         std::format("option {} needs a number, got '{}'", option, text));
    }
    return value;
}

Result<CameraSpec> parseCamera(std::string_view text) {
    const auto eq = text.find('=');
    if (eq == std::string_view::npos || eq == 0 || eq + 1 == text.size()) {
        return makeError(Errc::InvalidArgument,
                         std::format("--camera expects <index>=<serial>, got '{}'", text));
    }
    const auto index = parseNumber<unsigned int>(text.substr(0, eq), "--camera");
    if (!index) {
        return std::unexpected{index.error()};
    }
    if (*index > std::numeric_limits<std::uint16_t>::max()) {
        return makeError(Errc::InvalidArgument,
                         std::format("--camera index {} is too large (max 65535)", *index));
    }
    return CameraSpec{.index = static_cast<std::uint16_t>(*index),
                      .serial = std::string{text.substr(eq + 1)}};
}

std::optional<log::Level> parseLevel(std::string_view text) {
    using L = log::Level;
    constexpr std::array<std::pair<std::string_view, L>, 7> kLevels{{{"trace", L::Trace},
                                                                     {"debug", L::Debug},
                                                                     {"info", L::Info},
                                                                     {"warn", L::Warn},
                                                                     {"error", L::Error},
                                                                     {"critical", L::Critical},
                                                                     {"off", L::Off}}};
    for (const auto& [name, level] : kLevels) {
        if (text == name) {
            return level;
        }
    }
    return std::nullopt;
}

Result<> validate(const Options& options) {
    if (options.cameras.empty()) {
        return makeError(Errc::InvalidArgument, "at least one --camera <index>=<serial> is needed");
    }
    for (std::size_t i = 0; i < options.cameras.size(); ++i) {
        for (std::size_t j = i + 1; j < options.cameras.size(); ++j) {
            if (options.cameras[i].index == options.cameras[j].index) {
                return makeError(Errc::InvalidArgument, std::format("camera index {} is used twice",
                                                                    options.cameras[i].index));
            }
            if (options.cameras[i].serial == options.cameras[j].serial) {
                return makeError(
                    Errc::InvalidArgument,
                    std::format("camera serial '{}' is used twice", options.cameras[i].serial));
            }
        }
    }
    if (options.exposureUs && !(*options.exposureUs > 0.0)) {
        return makeError(Errc::InvalidArgument, "--exposure-us must be greater than 0");
    }
    if (options.gainDb && !(*options.gainDb >= 0.0)) {
        return makeError(Errc::InvalidArgument, "--gain-db must not be negative");
    }
    if (options.queueDepth == 0 || options.queueDepth > kMaxQueueDepth) {
        return makeError(Errc::InvalidArgument,
                         std::format("--queue-depth must be 1..{}", kMaxQueueDepth));
    }
    return {};
}

} // namespace

std::string_view usage() noexcept {
    return "Usage: vsort_record --camera <index>=<serial> [--camera ...] [options]\n"
           "  --camera <idx>=<sn>   camera to record; repeat per camera. <idx> is the logical\n"
           "                        camera ID and names the file (cam<idx>.vrec)\n"
           "  --root <dir>          service root (as vsort_service --root): apply its saved\n"
           "                        camera settings per camera ID (exposure, gain, trigger,\n"
           "                        edge, ROI). The options below override them for all cameras\n"
           "  --out <dir>           sessions are created below <dir> (default: data folder of\n"
           "                        --root, else per-user data folder; subfolder recordings)\n"
           "  --label <text>        free text stored in session.json\n"
           "  --duration <s>        stop after <s> seconds (default: until Ctrl-C)\n"
           "  --frames <n>          stop when every camera delivered at least <n> frames\n"
           "  --exposure-us <us>    exposure time in microseconds (default: saved, else 10000)\n"
           "  --gain-db <db>        gain in dB (default: saved, else 0)\n"
           "  --trigger <mode>      hardware (Line0) | freerun (default: saved, else hardware)\n"
           "  --queue-depth <n>     frames waiting for the disk writer, 1..1024 (default 32)\n"
           "  --log-level <lvl>     trace|debug|info|warn|error|critical|off (default: info)\n"
           "  --version             print version and exit\n"
           "  -h, --help            print this help and exit\n"
           "Exit codes: 0 ok, 1 failure, 2 usage error, 3 recorded with dropped or missing "
           "frames.\n";
}

Result<Options> parseArgs(std::span<const std::string_view> args) {
    Options options;
    for (std::size_t i = 0; i < args.size(); ++i) {
        std::string_view arg = args[i];
        std::string_view inlineValue;
        bool hasInline = false;
        if (arg.starts_with("--")) {
            if (const auto eq = arg.find('='); eq != std::string_view::npos) {
                inlineValue = arg.substr(eq + 1);
                arg = arg.substr(0, eq);
                hasInline = true;
            }
        }
        const auto value = [&]() -> Result<std::string_view> {
            if (hasInline) {
                return inlineValue;
            }
            if (i + 1 < args.size()) {
                return args[++i];
            }
            return makeError(Errc::InvalidArgument, std::format("option {} needs a value", arg));
        };

        if (arg == "-h" || arg == "--help") {
            options.help = true;
        } else if (arg == "--version") {
            options.version = true;
        } else if (arg == "--camera") {
            const auto v = value();
            if (!v) {
                return std::unexpected{v.error()};
            }
            auto spec = parseCamera(*v);
            if (!spec) {
                return std::unexpected{std::move(spec.error())};
            }
            options.cameras.push_back(std::move(*spec));
        } else if (arg == "--root") {
            const auto v = value();
            if (!v) {
                return std::unexpected{v.error()};
            }
            if (v->empty()) {
                return makeError(Errc::InvalidArgument, "--root must not be empty");
            }
            // Command line is UTF-8.
            options.root = std::filesystem::path{std::u8string{v->begin(), v->end()}};
        } else if (arg == "--out") {
            const auto v = value();
            if (!v) {
                return std::unexpected{v.error()};
            }
            if (v->empty()) {
                return makeError(Errc::InvalidArgument, "--out must not be empty");
            }
            // Command line is UTF-8.
            options.outDir = std::filesystem::path{std::u8string{v->begin(), v->end()}};
        } else if (arg == "--label") {
            const auto v = value();
            if (!v) {
                return std::unexpected{v.error()};
            }
            options.label = std::string{*v};
        } else if (arg == "--duration") {
            const auto v = value();
            if (!v) {
                return std::unexpected{v.error()};
            }
            const auto seconds = parseNumber<std::int64_t>(*v, arg);
            if (!seconds) {
                return std::unexpected{seconds.error()};
            }
            if (*seconds < 0 || *seconds > kMaxDurationSeconds) {
                return makeError(Errc::InvalidArgument, "--duration must be 0..604800 seconds");
            }
            options.duration = std::chrono::seconds{*seconds};
        } else if (arg == "--frames") {
            const auto v = value();
            if (!v) {
                return std::unexpected{v.error()};
            }
            const auto frames = parseNumber<std::uint64_t>(*v, arg);
            if (!frames) {
                return std::unexpected{frames.error()};
            }
            options.framesPerCamera = *frames;
        } else if (arg == "--exposure-us") {
            const auto v = value();
            if (!v) {
                return std::unexpected{v.error()};
            }
            const auto exposure = parseNumber<double>(*v, arg);
            if (!exposure) {
                return std::unexpected{exposure.error()};
            }
            options.exposureUs = *exposure;
        } else if (arg == "--gain-db") {
            const auto v = value();
            if (!v) {
                return std::unexpected{v.error()};
            }
            const auto gain = parseNumber<double>(*v, arg);
            if (!gain) {
                return std::unexpected{gain.error()};
            }
            options.gainDb = *gain;
        } else if (arg == "--trigger") {
            const auto v = value();
            if (!v) {
                return std::unexpected{v.error()};
            }
            if (*v == "hardware") {
                options.trigger = camera::TriggerMode::Hardware;
            } else if (*v == "freerun") {
                options.trigger = camera::TriggerMode::FreeRun;
            } else {
                return makeError(Errc::InvalidArgument,
                                 std::format("unknown trigger mode '{}' (hardware|freerun)", *v));
            }
        } else if (arg == "--queue-depth") {
            const auto v = value();
            if (!v) {
                return std::unexpected{v.error()};
            }
            const auto depth = parseNumber<std::uint64_t>(*v, arg);
            if (!depth) {
                return std::unexpected{depth.error()};
            }
            // Out-of-range values are rejected by validate(); keep the cast safe.
            options.queueDepth =
                *depth > kMaxQueueDepth ? kMaxQueueDepth + 1 : static_cast<std::size_t>(*depth);
        } else if (arg == "--log-level") {
            const auto v = value();
            if (!v) {
                return std::unexpected{v.error()};
            }
            const auto level = parseLevel(*v);
            if (!level) {
                return makeError(Errc::InvalidArgument, std::format("unknown log level '{}'", *v));
            }
            options.logLevel = *level;
        } else {
            return makeError(Errc::InvalidArgument, std::format("unknown option '{}'", arg));
        }
    }

    if (options.help || options.version) {
        return options;
    }
    if (const auto ok = validate(options); !ok) {
        return std::unexpected{ok.error()};
    }
    return options;
}

} // namespace vsort::record
