#pragma once

#include <cstddef>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <vsort/common/error.hpp>
#include <vsort/common/module.hpp>

namespace vsort {

// Owns the modules and drives their lifecycle. Not thread-safe: use from the main thread.
// One lifecycle only: add() all modules, startAll() once, stopAll() once.
class ModuleRegistry {
public:
    struct Status {
        std::string name;
        ModuleState state{ModuleState::Created};
        Health health;
    };

    ModuleRegistry() = default;
    ~ModuleRegistry();
    ModuleRegistry(const ModuleRegistry&) = delete;
    ModuleRegistry& operator=(const ModuleRegistry&) = delete;
    ModuleRegistry(ModuleRegistry&&) = delete;
    ModuleRegistry& operator=(ModuleRegistry&&) = delete;

    // Errors: InvalidArgument (null/empty name/after startAll), AlreadyExists (duplicate name).
    [[nodiscard]] Result<> add(std::unique_ptr<IModule> module);

    // Resolves dependency order, then init() all, then start() all.
    // On any failure the already initialized modules are stopped in reverse order.
    // Errors: NotFound (missing dependency), ValidationFailed (cycle), or the module's error.
    [[nodiscard]] Result<> startAll(ModuleContext& context);

    // Stops Initialized/Running modules in reverse start order. Idempotent.
    void stopAll() noexcept;

    [[nodiscard]] std::size_t size() const noexcept { return entries_.size(); }
    [[nodiscard]] IModule* find(std::string_view name) const noexcept;
    [[nodiscard]] std::vector<std::string> startOrder() const;
    [[nodiscard]] std::vector<Status> status() const;

private:
    struct Entry {
        std::unique_ptr<IModule> module;
        ModuleState state{ModuleState::Created};
    };

    [[nodiscard]] Result<std::vector<std::size_t>> resolveOrder() const;

    std::vector<Entry> entries_;
    std::vector<std::size_t> order_; // indices into entries_, start order; set by startAll()
};

} // namespace vsort
