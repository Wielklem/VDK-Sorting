// CLI record tool (P20.90): records raw frames from Daheng cameras to a session folder.
// Needs the Galaxy SDK (VSORT_WITH_GALAXY). Example:
//   vsort_record --camera 0=<serial> --frames 2000 --label "good eggs, 5 per s"
#include <cstddef>
#include <exception>
#include <filesystem>
#include <format>
#include <iostream>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

#include <vsort/camera/daheng.hpp>
#include <vsort/common/logging.hpp>
#include <vsort/common/version.hpp>
#include <vsort/platform/paths.hpp>
#include <vsort/platform/service_host.hpp>

#include "record_cli.hpp"
#include "record_session.hpp"

namespace {

using namespace vsort;
using namespace vsort::record;

Result<std::filesystem::path> defaultOutDir() {
    auto paths = platform::makeStandardPaths(platform::PathScope::User);
    if (!paths) {
        return std::unexpected{std::move(paths.error())};
    }
    return (*paths)->dataDir() / "recordings";
}

void printReport(const RecordReport& report) {
    std::cout << std::format("session: {}\n", report.sessionDir.string());
    std::cout << std::format(
        "time: {:.1f} s, frames written {}, dropped by recorder {}, failed {}\n",
        static_cast<double>(report.elapsed.count()) / 1000.0, report.stats.written,
        report.stats.dropped, report.stats.failed);
    for (const auto& cam : report.cameras) {
        const auto& h = cam.health;
        std::cout << std::format("cam{} ({}): received {}, dropped in adapter {}, ID gaps {} ({} "
                                 "missing), reconnects {}\n",
                                 cam.index, cam.serial, h.framesReceived, h.framesDropped, h.idGaps,
                                 h.framesMissed, h.reconnects);
    }
    if (report.clean()) {
        std::cout << "result: OK\n";
    } else {
        std::cout
            << "result: WARNING, frames were dropped or missing, or a camera sent no frames.\n"
               "        Do not use this session as a reference dataset.\n";
    }
}

} // namespace

int main(int argc, char** argv) {
    std::vector<std::string_view> args;
    const std::span<char*> raw{argv, static_cast<std::size_t>(argc)};
    if (!raw.empty()) {
        for (const char* arg : raw.subspan(1)) {
            args.emplace_back(arg);
        }
    }

    auto options = parseArgs(args);
    if (!options) {
        std::cerr << "vsort_record: " << options.error().message << "\n\n" << usage();
        return kExitUsage;
    }
    if (options->help) {
        std::cout << usage();
        return kExitOk;
    }
    if (options->version) {
        std::cout << "vsort_record " << version() << '\n';
        return kExitOk;
    }

    try {
        auto host = platform::makeServiceHost();
        if (!host) {
            std::cerr << "vsort_record: " << host.error().what() << '\n';
            return kExitFailure;
        }
        // Ctrl-C ends the recording cleanly. Must happen before any other thread exists.
        if (const auto handlers = (*host)->installStopHandlers(); !handlers) {
            std::cerr << "vsort_record: " << handlers.error().what() << '\n';
            return kExitFailure;
        }

        if (options->outDir.empty()) {
            auto dir = defaultOutDir();
            if (!dir) {
                std::cerr << "vsort_record: cannot determine the output folder: "
                          << dir.error().what() << "\nhint: pass --out <dir>\n";
                return kExitFailure;
            }
            options->outDir = std::move(*dir);
        }

        log::Config logConfig; // console only
        logConfig.level = options->logLevel;
        if (const auto logging = log::init(logConfig); !logging) {
            std::cerr << "vsort_record: cannot start logging: " << logging.error().what() << '\n';
            return kExitFailure;
        }

        const RecordPlan plan = makePlan(*options);
        const camera::DahengOptions daheng{.poolFrames = framePoolSize(plan.queueDepth)};
        const camera::CameraFactory factory = [daheng] { return camera::makeDahengCamera(daheng); };

        std::cout << "Ctrl-C stops the recording.\n";
        const auto report =
            runRecording(plan, factory, [&host] { return (*host)->stopRequested(); }, std::cout);
        int exitCode = kExitOk;
        if (!report) {
            std::cerr << "vsort_record: " << report.error().what() << '\n';
            exitCode = kExitFailure;
        } else {
            printReport(*report);
            exitCode = report->clean() ? kExitOk : kExitLossy;
        }
        log::shutdown();
        return exitCode;
    } catch (const std::exception& ex) {
        std::cerr << "vsort_record: fatal: " << ex.what() << '\n';
        return kExitFailure;
    }
}
