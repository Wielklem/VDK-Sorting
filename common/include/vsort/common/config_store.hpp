#pragma once

#include <cstdint>
#include <filesystem>
#include <map>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

#include <vsort/common/error.hpp>
#include <vsort/common/message_bus.hpp>
#include <vsort/common/timestamp.hpp>

namespace vsort {

// Snapshot id of one module's config. number 0 = nothing saved yet.
struct ConfigVersion {
    std::uint32_t number{0};
    WallTime time;
    std::string author;
};

// MSG-20-01: published on the bus after a config was saved or rolled back.
struct ConfigChanged {
    std::string module;
    ConfigVersion version;
};

// Validates value against a JSON-schema subset: type, enum, minimum, maximum,
// properties, required, additionalProperties (bool), items. Other keywords are ignored.
// On failure: Errc::ValidationFailed, message lists every violation as "$.path: problem".
[[nodiscard]] Result<> validateJson(const nlohmann::json& schema, const nlohmann::json& value);

// M20 config store: one JSON document per module, validated, versioned.
class IConfigStore {
public:
    IConfigStore() = default;
    virtual ~IConfigStore() = default;
    IConfigStore(const IConfigStore&) = delete;
    IConfigStore& operator=(const IConfigStore&) = delete;
    IConfigStore(IConfigStore&&) = delete;
    IConfigStore& operator=(IConfigStore&&) = delete;

    // Declares a module (name: a-z, 0-9, '_'). Loads the stored config if present
    // (must validate), otherwise saves `defaults` as version 1. Call once per module.
    [[nodiscard]] virtual Result<> registerModule(std::string name, nlohmann::json schema,
                                                  nlohmann::json defaults) = 0;

    [[nodiscard]] virtual Result<nlohmann::json> get(std::string_view module) const = 0;
    [[nodiscard]] virtual Result<ConfigVersion> version(std::string_view module) const = 0;

    // Validates, saves as next version, publishes ConfigChanged.
    // Identical to the current config: no new version, no message.
    [[nodiscard]] virtual Result<ConfigVersion> set(std::string_view module,
                                                    nlohmann::json value,
                                                    std::string_view author) = 0;

    // All saved versions, oldest first.
    [[nodiscard]] virtual Result<std::vector<ConfigVersion>> history(
        std::string_view module) const = 0;

    // Re-saves an old version as a NEW version (history is never rewritten).
    [[nodiscard]] virtual Result<ConfigVersion> rollback(std::string_view module,
                                                         std::uint32_t number,
                                                         std::string_view author) = 0;
};

// Files: <dir>/<module>.json (current) and <dir>/history/<module>/<number>.json.
// `dir` comes from IPaths::configDir(). Thread-safe.
class FileConfigStore final : public IConfigStore {
public:
    FileConfigStore(std::filesystem::path dir, MessageBus& bus);

    [[nodiscard]] Result<> registerModule(std::string name, nlohmann::json schema,
                                          nlohmann::json defaults) override;
    [[nodiscard]] Result<nlohmann::json> get(std::string_view module) const override;
    [[nodiscard]] Result<ConfigVersion> version(std::string_view module) const override;
    [[nodiscard]] Result<ConfigVersion> set(std::string_view module, nlohmann::json value,
                                            std::string_view author) override;
    [[nodiscard]] Result<std::vector<ConfigVersion>> history(
        std::string_view module) const override;
    [[nodiscard]] Result<ConfigVersion> rollback(std::string_view module, std::uint32_t number,
                                                 std::string_view author) override;

private:
    struct Entry {
        nlohmann::json schema;
        nlohmann::json data;
        ConfigVersion version;
    };

    [[nodiscard]] Result<ConfigVersion> commitLocked(const std::string& module, Entry& entry,
                                                     nlohmann::json data, std::string_view author);

    std::filesystem::path dir_;
    MessageBus& bus_;
    mutable std::mutex mutex_;
    std::map<std::string, Entry, std::less<>> modules_;
};

} // namespace vsort
