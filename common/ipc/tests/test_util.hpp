#pragma once

#include <gtest/gtest.h>

#include <vsort/common/error.hpp>

// Windows stubs (M15.20 not done) return NotSupported: skip instead of fail.
#define VSORT_SKIP_IF_NOT_SUPPORTED(result)                                                        \
    do {                                                                                           \
        if (!(result).has_value() && (result).error().code == vsort::Errc::NotSupported) {         \
            GTEST_SKIP() << (result).error().what();                                               \
        }                                                                                          \
    } while (false)
