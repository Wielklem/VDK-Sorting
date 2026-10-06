// M15.20 Windows implementation is not done yet (P220.10).
// Everything compiles and returns NotSupported, so the Windows CI stays green.
#include <cstddef>
#include <expected>
#include <memory>
#include <string>
#include <string_view>

#include <vsort/platform/paths.hpp>
#include <vsort/platform/service_host.hpp>
#include <vsort/platform/shared_memory.hpp>
#include <vsort/platform/thread_tuning.hpp>

namespace vsort::platform {
namespace {

std::unexpected<Error> notImplemented(std::string_view what) {
    std::string msg{what};
    msg += ": Windows platform layer not implemented (M15.20)";
    return makeError(Errc::NotSupported, std::move(msg));
}

} // namespace

Result<std::unique_ptr<IPaths>> makeStandardPaths(PathScope /*scope*/) {
    return notImplemented("makeStandardPaths");
}

Result<std::unique_ptr<ISharedMemory>> createSharedMemory(std::string_view /*name*/, std::size_t /*size*/) {
    return notImplemented("createSharedMemory");
}

Result<std::unique_ptr<ISharedMemory>> openSharedMemory(std::string_view /*name*/) {
    return notImplemented("openSharedMemory");
}

Result<std::unique_ptr<IThreadTuning>> makeThreadTuning() {
    return notImplemented("makeThreadTuning");
}

Result<std::unique_ptr<IServiceHost>> makeServiceHost() {
    return notImplemented("makeServiceHost");
}

} // namespace vsort::platform
