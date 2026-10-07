#include <cstdint>

#include <gtest/gtest.h>

#include "fake_service.hpp"
#include "live/service_client.hpp"
#include "test_util.hpp"

namespace {

using namespace vsort::hmi;
using namespace vsort::hmi::test;

constexpr std::uint16_t kCam = 61;

class ServiceEventsTest : public ::testing::Test {
protected:
    ServiceEventsTest()
        : service{{kCam}}
        , client{{.host = QStringLiteral("127.0.0.1"),
                  .commandPort = FakeService::kCommandPort,
                  .eventPort = FakeService::kEventPort}} {}

    void SetUp() override {
        VSORT_HMI_SKIP_IF_NO_RINGS(service);
        client.start();
    }

    FakeService service;
    ServiceClient client;
};

} // namespace

TEST_F(ServiceEventsTest, CameraListChangedEventMakesTheClientAskForTheListAgain) {
    ASSERT_TRUE(spinUntil([&] { return client.connected(); }, [this] { service.pump(); }));
    const int before = service.cameraListRequests();
    service.publishCameraListChanged();
    EXPECT_TRUE(spinUntil([&] { return service.cameraListRequests() > before; },
                          [this] { service.pump(); }));
}
