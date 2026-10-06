#include <string>
#include <unordered_map>
#include <utility>

#include <vsort/common/logging.hpp>
#include <vsort/common/module_registry.hpp>

namespace vsort {

ModuleRegistry::~ModuleRegistry() {
    stopAll();
}

IModule* ModuleRegistry::find(std::string_view name) const noexcept {
    for (const auto& entry : entries_) {
        if (entry.module->name() == name) {
            return entry.module.get();
        }
    }
    return nullptr;
}

Result<> ModuleRegistry::add(std::unique_ptr<IModule> module) {
    if (!module) {
        return makeError(Errc::InvalidArgument, "null module");
    }
    const std::string name{module->name()};
    if (name.empty()) {
        return makeError(Errc::InvalidArgument, "module name is empty");
    }
    if (!order_.empty()) {
        return makeError(Errc::InvalidArgument, "cannot add '" + name + "' after startAll()");
    }
    if (find(name) != nullptr) {
        return makeError(Errc::AlreadyExists, "module '" + name + "' already registered");
    }
    entries_.push_back(Entry{std::move(module), ModuleState::Created});
    return {};
}

Result<std::vector<std::size_t>> ModuleRegistry::resolveOrder() const {
    const std::size_t count = entries_.size();

    std::unordered_map<std::string, std::size_t> indexByName;
    for (std::size_t i = 0; i < count; ++i) {
        indexByName.emplace(std::string{entries_[i].module->name()}, i);
    }

    std::vector<std::vector<std::size_t>> deps(count);
    for (std::size_t i = 0; i < count; ++i) {
        for (const auto& depName : entries_[i].module->dependencies()) {
            const auto it = indexByName.find(depName);
            if (it == indexByName.end()) {
                return makeError(Errc::NotFound,
                                 "module '" + std::string{entries_[i].module->name()} +
                                     "' depends on unknown module '" + depName + "'");
            }
            deps[i].push_back(it->second);
        }
    }

    // Repeatedly pick the earliest registered module whose dependencies are all placed.
    std::vector<std::size_t> order;
    order.reserve(count);
    std::vector<bool> placed(count, false);
    while (order.size() < count) {
        bool progress = false;
        for (std::size_t i = 0; i < count && !progress; ++i) {
            if (placed[i]) {
                continue;
            }
            bool ready = true;
            for (const auto dep : deps[i]) {
                if (!placed[dep]) {
                    ready = false;
                    break;
                }
            }
            if (ready) {
                placed[i] = true;
                order.push_back(i);
                progress = true;
            }
        }
        if (!progress) {
            std::string names;
            for (std::size_t i = 0; i < count; ++i) {
                if (!placed[i]) {
                    names += (names.empty() ? "'" : ", '") +
                             std::string{entries_[i].module->name()} + "'";
                }
            }
            return makeError(Errc::ValidationFailed, "module dependency cycle involving " + names);
        }
    }
    return order;
}

Result<> ModuleRegistry::startAll(ModuleContext& context) {
    if (!order_.empty()) {
        return makeError(Errc::InvalidArgument, "startAll() already called");
    }
    auto resolved = resolveOrder();
    if (!resolved) {
        return std::unexpected{resolved.error()};
    }
    order_ = std::move(*resolved);

    const auto logger = log::get("core");

    for (const auto index : order_) {
        auto& entry = entries_[index];
        const std::string name{entry.module->name()};
        if (auto result = entry.module->init(context); !result) {
            entry.state = ModuleState::Failed;
            logger->error("module '{}' init failed: {}", name, result.error().what());
            stopAll();
            return makeError(result.error().code,
                             "module '" + name + "' init failed: " + result.error().message);
        }
        entry.state = ModuleState::Initialized;
        logger->info("module '{}' initialized", name);
    }

    for (const auto index : order_) {
        auto& entry = entries_[index];
        const std::string name{entry.module->name()};
        if (auto result = entry.module->start(); !result) {
            entry.module->stop();
            entry.state = ModuleState::Failed;
            logger->error("module '{}' start failed: {}", name, result.error().what());
            stopAll();
            return makeError(result.error().code,
                             "module '" + name + "' start failed: " + result.error().message);
        }
        entry.state = ModuleState::Running;
        logger->info("module '{}' started", name);
    }
    return {};
}

void ModuleRegistry::stopAll() noexcept {
    for (auto it = order_.rbegin(); it != order_.rend(); ++it) {
        auto& entry = entries_[*it];
        if (entry.state != ModuleState::Initialized && entry.state != ModuleState::Running) {
            continue;
        }
        entry.module->stop();
        entry.state = ModuleState::Stopped;
        log::get("core")->info("module '{}' stopped", entry.module->name());
    }
}

std::vector<std::string> ModuleRegistry::startOrder() const {
    std::vector<std::string> names;
    names.reserve(order_.size());
    for (const auto index : order_) {
        names.emplace_back(entries_[index].module->name());
    }
    return names;
}

std::vector<ModuleRegistry::Status> ModuleRegistry::status() const {
    std::vector<std::size_t> indices = order_;
    if (indices.empty()) {
        for (std::size_t i = 0; i < entries_.size(); ++i) {
            indices.push_back(i);
        }
    }
    std::vector<Status> out;
    out.reserve(indices.size());
    for (const auto index : indices) {
        const auto& entry = entries_[index];
        Status s;
        s.name = std::string{entry.module->name()};
        s.state = entry.state;
        if (entry.state == ModuleState::Running) {
            s.health = entry.module->health();
        } else if (entry.state == ModuleState::Failed) {
            s.health = Health{HealthState::Failed, "module failed during startup"};
        }
        out.push_back(std::move(s));
    }
    return out;
}

} // namespace vsort
