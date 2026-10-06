#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include <vsort/camera/discovery.hpp>

using namespace vsort;
using namespace vsort::camera;

namespace {

class FakeDiscovery final : public ICameraDiscovery {
public:
    explicit FakeDiscovery(std::vector<DiscoveredCamera> cameras)
        : cameras_{std::move(cameras)} {}

    [[nodiscard]] Result<std::vector<DiscoveredCamera>> discover() override { return cameras_; }

private:
    std::vector<DiscoveredCamera> cameras_;
};

} // namespace

TEST(DiscoveredCamera, DefaultsToUsb3WithoutNetworkFields) {
    const DiscoveredCamera cam;
    EXPECT_EQ(cam.transport, Transport::Usb3);
    EXPECT_TRUE(cam.ip.empty());
    EXPECT_TRUE(cam.mac.empty());
}

TEST(ICameraDiscovery, ReturnsBothTransports) {
    FakeDiscovery discovery{{
        {.serial = "U1", .model = "MER-U3", .transport = Transport::Usb3},
        {.serial = "G1", .model = "MER-G", .transport = Transport::GigE, .ip = "192.168.1.10"},
    }};
    const auto result = discovery.discover();
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->size(), 2U);
    EXPECT_EQ((*result)[1].transport, Transport::GigE);
    EXPECT_EQ((*result)[1].ip, "192.168.1.10");
}

TEST(ICameraDiscovery, EmptyListIsNotAnError) {
    FakeDiscovery discovery{{}};
    const auto result = discovery.discover();
    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(result->empty());
}
