#pragma once

namespace vsort::platform {

// Base for all platform interfaces: owned through unique_ptr, never copied or moved.
class Interface {
public:
    Interface() = default;
    virtual ~Interface() = default;
    Interface(const Interface&) = delete;
    Interface& operator=(const Interface&) = delete;
    Interface(Interface&&) = delete;
    Interface& operator=(Interface&&) = delete;
};

} // namespace vsort::platform
