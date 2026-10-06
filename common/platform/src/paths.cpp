#include <vsort/platform/paths.hpp>

#include <filesystem>
#include <memory>
#include <system_error>
#include <utility>

namespace vsort::platform {
namespace {

class RootedPaths final : public IPaths {
public:
    explicit RootedPaths(std::filesystem::path root) : root_{std::move(root)} {}

    [[nodiscard]] std::filesystem::path configDir() const override { return root_ / "config"; }
    [[nodiscard]] std::filesystem::path dataDir() const override { return root_ / "data"; }
    [[nodiscard]] std::filesystem::path logDir() const override { return root_ / "log"; }

private:
    std::filesystem::path root_;
};

} // namespace

std::unique_ptr<IPaths> makeRootedPaths(const std::filesystem::path& root) {
    return std::make_unique<RootedPaths>(root);
}

Result<> ensureDirectories(const IPaths& paths) {
    for (const auto& dir : {paths.configDir(), paths.dataDir(), paths.logDir()}) {
        std::error_code ec;
        std::filesystem::create_directories(dir, ec);
        if (ec) {
            return makeError(Errc::IoError, dir.string() + ": " + ec.message());
        }
    }
    return {};
}

} // namespace vsort::platform
