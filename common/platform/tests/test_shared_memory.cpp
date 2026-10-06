#include <cstddef>

#include <gtest/gtest.h>

#include <vsort/platform/shared_memory.hpp>

#include "test_util.hpp"

using vsort::platform::createSharedMemory;
using vsort::platform::openSharedMemory;

TEST(SharedMemory, OwnerAndClientShareBytes) {
    auto owner = createSharedMemory("vsort_test_shm_a", 4096);
    VSORT_SKIP_IF_NOT_SUPPORTED(owner);
    ASSERT_TRUE(owner.has_value()) << owner.error().what();
    (*owner)->bytes()[0] = std::byte{0xAB};

    auto client = openSharedMemory("vsort_test_shm_a");
    ASSERT_TRUE(client.has_value()) << client.error().what();
    EXPECT_EQ((*client)->size(), 4096U);
    EXPECT_EQ((*client)->bytes()[0], std::byte{0xAB});

    (*client)->bytes()[1] = std::byte{0xCD};
    EXPECT_EQ((*owner)->bytes()[1], std::byte{0xCD});
}

TEST(SharedMemory, RemovedWhenOwnerDestroyed) {
    {
        auto owner = createSharedMemory("vsort_test_shm_b", 64);
        VSORT_SKIP_IF_NOT_SUPPORTED(owner);
        ASSERT_TRUE(owner.has_value()) << owner.error().what();
    }
    const auto client = openSharedMemory("vsort_test_shm_b");
    ASSERT_FALSE(client.has_value());
    EXPECT_EQ(client.error().code, vsort::Errc::NotFound);
}

TEST(SharedMemory, RejectsInvalidName) {
    const auto shm = createSharedMemory("bad/name", 64);
    VSORT_SKIP_IF_NOT_SUPPORTED(shm);
    ASSERT_FALSE(shm.has_value());
    EXPECT_EQ(shm.error().code, vsort::Errc::InvalidArgument);
}

TEST(SharedMemory, NameValidation) {
    EXPECT_TRUE(vsort::platform::isValidSharedMemoryName("preview_cam-01"));
    EXPECT_FALSE(vsort::platform::isValidSharedMemoryName(""));
    EXPECT_FALSE(vsort::platform::isValidSharedMemoryName("a b"));
}
