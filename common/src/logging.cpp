#include <vsort/common/logging.hpp>

#include <chrono>
#include <format>
#include <mutex>
#include <system_error>
#include <utility>
#include <vector>

#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

namespace vsort::log {
namespace {

constexpr const char* kPattern = "%Y-%m-%dT%H:%M:%S.%f%z [%l] [%n] [t%t] %v";

struct State {
    std::mutex mutex;
    std::vector<spdlog::sink_ptr> sinks;
    spdlog::level::level_enum level{spdlog::level::info};
};

State& state() {
    static State instance;
    return instance;
}

spdlog::level::level_enum toSpdlog(Level level) noexcept {
    switch (level) {
        case Level::Trace:    return spdlog::level::trace;
        case Level::Debug:    return spdlog::level::debug;
        case Level::Info:     return spdlog::level::info;
        case Level::Warn:     return spdlog::level::warn;
        case Level::Error:    return spdlog::level::err;
        case Level::Critical: return spdlog::level::critical;
        case Level::Off:      return spdlog::level::off;
    }
    return spdlog::level::info;
}

spdlog::sink_ptr makeConsoleSink() {
    auto sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
    sink->set_pattern(kPattern);
    return sink;
}

std::shared_ptr<spdlog::logger> makeLogger(const std::string& name, const State& s) {
    auto logger = std::make_shared<spdlog::logger>(name, s.sinks.begin(), s.sinks.end());
    logger->set_level(s.level);
    logger->flush_on(spdlog::level::warn);
    return logger;
}

} // namespace

Result<void> init(const Config& config) {
    auto& s = state();
    const std::lock_guard lock{s.mutex};

    std::vector<spdlog::sink_ptr> sinks;
    try {
        if (config.console) {
            sinks.push_back(makeConsoleSink());
        }
        if (!config.directory.empty()) {
            std::error_code ec;
            std::filesystem::create_directories(config.directory, ec);
            if (ec) {
                return makeError(Errc::IoError,
                                 std::format("cannot create log directory '{}': {}",
                                             config.directory.string(), ec.message()));
            }
            const auto file = config.directory / config.fileName;
            auto sink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
                file.string(), config.maxFileSize, config.maxFiles);
            sink->set_pattern(kPattern);
            sinks.push_back(std::move(sink));
        }
    } catch (const spdlog::spdlog_ex& ex) {
        return makeError(Errc::IoError, ex.what());
    }

    spdlog::drop_all();
    s.sinks = std::move(sinks);
    s.level = toSpdlog(config.level);
    spdlog::set_default_logger(makeLogger("vsort", s));
    spdlog::flush_every(std::chrono::seconds{2});
    return {};
}

std::shared_ptr<spdlog::logger> get(std::string_view module) {
    auto& s = state();
    const std::lock_guard lock{s.mutex};
    const std::string name{module};
    if (auto existing = spdlog::get(name)) {
        return existing;
    }
    if (s.sinks.empty()) {
        s.sinks.push_back(makeConsoleSink());  // fallback if init() was not called
    }
    auto logger = makeLogger(name, s);
    spdlog::register_logger(logger);
    return logger;
}

void setLevel(Level level) {
    auto& s = state();
    const std::lock_guard lock{s.mutex};
    s.level = toSpdlog(level);
    spdlog::set_level(s.level);
}

void shutdown() {
    auto& s = state();
    const std::lock_guard lock{s.mutex};
    spdlog::shutdown();
    s.sinks.clear();
}

} // namespace vsort::log
