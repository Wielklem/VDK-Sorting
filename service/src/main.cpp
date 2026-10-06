#include <cstddef>
#include <exception>
#include <iostream>
#include <span>
#include <string_view>
#include <vector>

#include <vsort/common/version.hpp>
#include <vsort/platform/service_host.hpp>

#include "cli.hpp"
#include "service_app.hpp"

int main(int argc, char** argv) {
    using namespace vsort;
    using namespace vsort::service;

    std::vector<std::string_view> args;
    const std::span<char*> raw{argv, static_cast<std::size_t>(argc)};
    if (!raw.empty()) {
        for (const char* arg : raw.subspan(1)) {
            args.emplace_back(arg);
        }
    }

    const auto options = parseArgs(args);
    if (!options) {
        std::cerr << "vsort_service: " << options.error().message << "\n\n" << usage();
        return kExitUsage;
    }
    if (options->help) {
        std::cout << usage();
        return kExitOk;
    }
    if (options->version) {
        std::cout << "vsort_service " << version() << '\n';
        return kExitOk;
    }

    try {
        auto host = platform::makeServiceHost();
        if (!host) {
            std::cerr << "vsort_service: " << host.error().what() << '\n';
            return kExitFailure;
        }
        // Must happen before any other thread exists (logging starts one).
        if (const auto handlers = (*host)->installStopHandlers(); !handlers) {
            std::cerr << "vsort_service: " << handlers.error().what() << '\n';
            return kExitFailure;
        }
        return runService(*options, **host);
    } catch (const std::exception& ex) {
        std::cerr << "vsort_service: fatal: " << ex.what() << '\n';
        return kExitFailure;
    }
}
