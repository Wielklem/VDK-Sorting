#include <vsort/common/version.hpp>

namespace vsort {

std::string_view version() noexcept {
    return VSORT_VERSION;
}

} // namespace vsort
