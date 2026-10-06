#pragma once

#include <vsort/platform/service_host.hpp>

#include "cli.hpp"

namespace vsort::service {

// Paths, logging, run until stop is requested, graceful shutdown. Returns the process exit code.
// Precondition: host.installStopHandlers() was called before any other thread was started.
[[nodiscard]] int runService(const Options& options, platform::IServiceHost& host);

} // namespace vsort::service
