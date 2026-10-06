#include <vsort/common/config_store.hpp>

#include <algorithm>
#include <chrono>
#include <fstream>
#include <system_error>
#include <utility>

namespace vsort {
namespace {

using nlohmann::json;

bool typeMatches(const json& value, const std::string& type) {
    if (type == "object") {
        return value.is_object();
    }
    if (type == "array") {
        return value.is_array();
    }
    if (type == "string") {
        return value.is_string();
    }
    if (type == "boolean") {
        return value.is_boolean();
    }
    if (type == "integer") {
        return value.is_number_integer();
    }
    if (type == "number") {
        return value.is_number();
    }
    if (type == "null") {
        return value.is_null();
    }
    return false;
}

void validateNode(const json& schema, const json& value, const std::string& path,
                  std::vector<std::string>& errors) {
    if (!schema.is_object()) {
        return;
    }
    if (const auto it = schema.find("type"); it != schema.end() && it->is_string()) {
        const auto type = it->get<std::string>();
        if (!typeMatches(value, type)) {
            errors.push_back(path + ": expected " + type);
            return;
        }
    }
    if (const auto it = schema.find("enum"); it != schema.end() && it->is_array()) {
        if (std::find(it->begin(), it->end(), value) == it->end()) {
            errors.push_back(path + ": value not in enum");
        }
    }
    if (value.is_number()) {
        const auto number = value.get<double>();
        if (const auto it = schema.find("minimum"); it != schema.end() && it->is_number()) {
            if (number < it->get<double>()) {
                errors.push_back(path + ": below minimum");
            }
        }
        if (const auto it = schema.find("maximum"); it != schema.end() && it->is_number()) {
            if (number > it->get<double>()) {
                errors.push_back(path + ": above maximum");
            }
        }
    }
    if (value.is_object()) {
        if (const auto it = schema.find("required"); it != schema.end() && it->is_array()) {
            for (const auto& name : *it) {
                if (name.is_string() && !value.contains(name.get<std::string>())) {
                    errors.push_back(path + ": missing required '" + name.get<std::string>() +
                                     "'");
                }
            }
        }
        const auto props = schema.find("properties");
        const bool hasProps = props != schema.end() && props->is_object();
        if (hasProps) {
            for (const auto& [key, sub] : props->items()) {
                if (value.contains(key)) {
                    validateNode(sub, value.at(key), path + "." + key, errors);
                }
            }
        }
        if (const auto it = schema.find("additionalProperties");
            it != schema.end() && it->is_boolean() && !it->get<bool>()) {
            for (const auto& [key, unused] : value.items()) {
                if (!hasProps || !props->contains(key)) {
                    errors.push_back(path + ": unknown key '" + key + "'");
                }
            }
        }
    }
    if (value.is_array()) {
        if (const auto it = schema.find("items"); it != schema.end()) {
            for (std::size_t i = 0; i < value.size(); ++i) {
                validateNode(*it, value[i], path + "[" + std::to_string(i) + "]", errors);
            }
        }
    }
}

bool validModuleName(std::string_view name) {
    return !name.empty() && std::ranges::all_of(name, [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_';
    });
}

json toRecord(const ConfigVersion& v, const json& data) {
    return json{{"version", v.number},
                {"author", v.author},
                {"time_ns", v.time.ns()},
                {"data", data}};
}

struct Record {
    ConfigVersion version;
    json data;
};

Result<Record> parseRecord(const json& rec) {
    try {
        Record out;
        out.version.number = rec.at("version").get<std::uint32_t>();
        out.version.author = rec.at("author").get<std::string>();
        out.version.time = WallTime{WallTime::duration{rec.at("time_ns").get<std::int64_t>()}};
        out.data = rec.at("data");
        return out;
    } catch (const json::exception& e) {
        return makeError(Errc::ParseError, std::string{"bad config record: "} + e.what());
    }
}

Result<json> readJsonFile(const std::filesystem::path& path) {
    std::ifstream in{path, std::ios::binary};
    if (!in.is_open()) {
        return makeError(Errc::NotFound, "cannot open " + path.generic_string());
    }
    auto parsed = json::parse(in, nullptr, false);
    if (parsed.is_discarded()) {
        return makeError(Errc::ParseError, "invalid JSON in " + path.generic_string());
    }
    return parsed;
}

// Write to <path>.tmp, then rename: a crash never leaves a half-written config.
Result<> writeJsonFile(const std::filesystem::path& path, const json& value) {
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    if (ec) {
        return makeError(Errc::IoError, "cannot create directory: " + ec.message());
    }
    auto tmp = path;
    tmp += ".tmp";
    {
        std::ofstream out{tmp, std::ios::binary | std::ios::trunc};
        if (!out.is_open()) {
            return makeError(Errc::IoError, "cannot write " + tmp.generic_string());
        }
        out << value.dump(2) << '\n';
        out.flush();
        if (!out) {
            return makeError(Errc::IoError, "write failed for " + tmp.generic_string());
        }
    }
    std::filesystem::rename(tmp, path, ec);
    if (ec) {
        return makeError(Errc::IoError, "rename failed: " + ec.message());
    }
    return {};
}

} // namespace

Result<> validateJson(const json& schema, const json& value) {
    std::vector<std::string> errors;
    validateNode(schema, value, "$", errors);
    if (errors.empty()) {
        return {};
    }
    std::string message;
    for (const auto& e : errors) {
        if (!message.empty()) {
            message += "; ";
        }
        message += e;
    }
    return makeError(Errc::ValidationFailed, std::move(message));
}

FileConfigStore::FileConfigStore(std::filesystem::path dir, MessageBus& bus)
    : dir_{std::move(dir)}
    , bus_{bus} {}

Result<ConfigVersion> FileConfigStore::commitLocked(const std::string& module, Entry& entry,
                                                    json data, std::string_view author) {
    ConfigVersion next;
    next.number = entry.version.number + 1U;
    next.time = WallTime::now();
    next.author = std::string{author};
    const auto record = toRecord(next, data);
    const auto historyFile = dir_ / "history" / module / (std::to_string(next.number) + ".json");
    if (auto r = writeJsonFile(historyFile, record); !r) {
        return std::unexpected{std::move(r.error())};
    }
    if (auto r = writeJsonFile(dir_ / (module + ".json"), record); !r) {
        return std::unexpected{std::move(r.error())};
    }
    entry.data = std::move(data);
    entry.version = next;
    return next;
}

Result<> FileConfigStore::registerModule(std::string name, json schema, json defaults) {
    if (!validModuleName(name)) {
        return makeError(Errc::InvalidArgument, "invalid module name '" + name + "'");
    }
    if (!schema.is_object()) {
        return makeError(Errc::InvalidArgument, "schema of '" + name + "' must be an object");
    }
    if (auto r = validateJson(schema, defaults); !r) {
        return makeError(Errc::ValidationFailed,
                         "defaults of '" + name + "': " + r.error().message);
    }
    const std::lock_guard lock{mutex_};
    if (modules_.contains(name)) {
        return makeError(Errc::AlreadyExists, "module '" + name + "' already registered");
    }
    Entry entry;
    entry.schema = std::move(schema);

    const auto file = dir_ / (name + ".json");
    std::error_code ec;
    if (std::filesystem::exists(file, ec)) {
        auto raw = readJsonFile(file);
        if (!raw) {
            return std::unexpected{std::move(raw.error())};
        }
        auto rec = parseRecord(*raw);
        if (!rec) {
            return std::unexpected{std::move(rec.error())};
        }
        if (auto r = validateJson(entry.schema, rec->data); !r) {
            return makeError(Errc::ValidationFailed,
                             "stored config of '" + name + "': " + r.error().message);
        }
        entry.data = std::move(rec->data);
        entry.version = std::move(rec->version);
    } else {
        if (auto r = commitLocked(name, entry, std::move(defaults), "default"); !r) {
            return std::unexpected{std::move(r.error())};
        }
    }
    modules_.emplace(std::move(name), std::move(entry));
    return {};
}

Result<json> FileConfigStore::get(std::string_view module) const {
    const std::lock_guard lock{mutex_};
    const auto it = modules_.find(module);
    if (it == modules_.end()) {
        return makeError(Errc::NotFound, "unknown module '" + std::string{module} + "'");
    }
    return it->second.data;
}

Result<ConfigVersion> FileConfigStore::version(std::string_view module) const {
    const std::lock_guard lock{mutex_};
    const auto it = modules_.find(module);
    if (it == modules_.end()) {
        return makeError(Errc::NotFound, "unknown module '" + std::string{module} + "'");
    }
    return it->second.version;
}

Result<ConfigVersion> FileConfigStore::set(std::string_view module, json value,
                                           std::string_view author) {
    ConfigChanged changed;
    {
        const std::lock_guard lock{mutex_};
        const auto it = modules_.find(module);
        if (it == modules_.end()) {
            return makeError(Errc::NotFound, "unknown module '" + std::string{module} + "'");
        }
        if (auto r = validateJson(it->second.schema, value); !r) {
            return std::unexpected{std::move(r.error())};
        }
        if (value == it->second.data) {
            return it->second.version;
        }
        auto committed = commitLocked(it->first, it->second, std::move(value), author);
        if (!committed) {
            return std::unexpected{std::move(committed.error())};
        }
        changed = ConfigChanged{.module = it->first, .version = *committed};
    }
    bus_.publish(changed);  // outside the lock: subscribers may call back into the store
    return changed.version;
}

Result<std::vector<ConfigVersion>> FileConfigStore::history(std::string_view module) const {
    const std::lock_guard lock{mutex_};
    const auto it = modules_.find(module);
    if (it == modules_.end()) {
        return makeError(Errc::NotFound, "unknown module '" + std::string{module} + "'");
    }
    std::vector<ConfigVersion> out;
    std::error_code ec;
    for (std::filesystem::directory_iterator dirIt{dir_ / "history" / it->first, ec};
         !ec && dirIt != std::filesystem::directory_iterator{}; dirIt.increment(ec)) {
        auto raw = readJsonFile(dirIt->path());
        if (!raw) {
            return std::unexpected{std::move(raw.error())};
        }
        auto rec = parseRecord(*raw);
        if (!rec) {
            return std::unexpected{std::move(rec.error())};
        }
        out.push_back(std::move(rec->version));
    }
    if (ec) {
        return makeError(Errc::IoError, "cannot read history: " + ec.message());
    }
    std::ranges::sort(out, {}, &ConfigVersion::number);
    return out;
}

Result<ConfigVersion> FileConfigStore::rollback(std::string_view module, std::uint32_t number,
                                                std::string_view author) {
    ConfigChanged changed;
    {
        const std::lock_guard lock{mutex_};
        const auto it = modules_.find(module);
        if (it == modules_.end()) {
            return makeError(Errc::NotFound, "unknown module '" + std::string{module} + "'");
        }
        const auto file = dir_ / "history" / it->first / (std::to_string(number) + ".json");
        auto raw = readJsonFile(file);
        if (!raw) {
            return std::unexpected{std::move(raw.error())};
        }
        auto rec = parseRecord(*raw);
        if (!rec) {
            return std::unexpected{std::move(rec.error())};
        }
        // The schema may have changed since this version was saved.
        if (auto r = validateJson(it->second.schema, rec->data); !r) {
            return std::unexpected{std::move(r.error())};
        }
        auto committed = commitLocked(it->first, it->second, std::move(rec->data), author);
        if (!committed) {
            return std::unexpected{std::move(committed.error())};
        }
        changed = ConfigChanged{.module = it->first, .version = *committed};
    }
    bus_.publish(changed);
    return changed.version;
}

} // namespace vsort
