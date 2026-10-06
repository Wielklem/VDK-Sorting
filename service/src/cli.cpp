#include "cli.hpp"

#include <cstddef>
#include <format>
#include <optional>
#include <string>

namespace vsort::service {
namespace {

std::optional<log::Level> parseLevel(std::string_view text) {
    using L = log::Level;
    if (text == "trace") return L::Trace;
    if (text == "debug") return L::Debug;
    if (text == "info") return L::Info;
    if (text == "warn") return L::Warn;
    if (text == "error") return L::Error;
    if (text == "critical") return L::Critical;
    if (text == "off") return L::Off;
    return std::nullopt;
}

} // namespace

std::string_view usage() noexcept {
    return "Usage: vsort_service [options]\n"
           "  --root <dir>       keep config, data and log under <dir>\n"
           "  --user             use per-user OS locations instead of system locations\n"
           "  --log-level <lvl>  trace|debug|info|warn|error|critical|off (default: info)\n"
           "  --no-console       log to file only\n"
           "  --check            initialise and exit (startup smoke test)\n"
           "  --version          print version and exit\n"
           "  -h, --help         print this help and exit\n";
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
        } else if (arg == "--check") {
            options.check = true;
        } else if (arg == "--no-console") {
            options.noConsole = true;
        } else if (arg == "--user") {
            options.scope = platform::PathScope::User;
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
    return options;
}

} // namespace vsort::service
