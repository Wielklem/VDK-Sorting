#pragma once

#include <compare>
#include <cstddef>
#include <functional>

namespace vsort {

// Type-safe ID: FrameId and ObjectId can't be mixed or built from a raw int implicitly.
template <typename Tag, typename Rep>
class StrongId {
public:
    using rep_type = Rep;

    constexpr StrongId() noexcept = default;
    constexpr explicit StrongId(Rep value) noexcept : value_{value} {}

    [[nodiscard]] constexpr Rep value() const noexcept { return value_; }

    constexpr auto operator<=>(const StrongId&) const noexcept = default;

private:
    Rep value_{};
};

} // namespace vsort

template <typename Tag, typename Rep>
struct std::hash<vsort::StrongId<Tag, Rep>> {
    std::size_t operator()(const vsort::StrongId<Tag, Rep>& id) const noexcept {
        return std::hash<Rep>{}(id.value());
    }
};
