#include <gtest/gtest.h>

#include <vsort/camera/ip_config.hpp>

using namespace vsort;
using namespace vsort::camera;

namespace {

DiscoveredCamera gigE(const char* ip) {
    return {.serial = "G1",
            .model = "MER-G",
            .transport = Transport::GigE,
            .mac = "aa:bb:cc:dd:ee:01",
            .ip = ip,
            .subnetMask = "255.255.255.0",
            .gateway = "",
            .nicIp = "192.168.10.1",
            .nicMask = "255.255.255.0"};
}

GigEIpRequest goodRequest() {
    return {.mac = "AA-BB-CC-DD-EE-01",
            .ip = "192.168.10.21",
            .subnetMask = "255.255.255.0",
            .gateway = "192.168.10.1"};
}

} // namespace

TEST(ParseIpv4, AcceptsValidAndRejectsInvalid) {
    EXPECT_EQ(parseIpv4("192.168.1.10"), 0xC0A8010AU);
    EXPECT_EQ(parseIpv4("0.0.0.0"), 0U);
    for (const char* bad : {"", "1.2.3", "1.2.3.4.5", "256.1.1.1", "1.2.3.", "a.b.c.d", "1..3.4",
                            "1.2.3.4 ", "0001.2.3.4", "-1.2.3.4"}) {
        EXPECT_FALSE(parseIpv4(bad).has_value()) << bad;
    }
}

TEST(Netmask, OnlyContiguousMasksAreValid) {
    EXPECT_TRUE(isValidNetmask(*parseIpv4("255.255.255.0")));
    EXPECT_TRUE(isValidNetmask(*parseIpv4("255.255.0.0")));
    EXPECT_TRUE(isValidNetmask(*parseIpv4("255.255.255.252")));
    EXPECT_FALSE(isValidNetmask(*parseIpv4("255.0.255.0")));
    EXPECT_FALSE(isValidNetmask(*parseIpv4("0.0.0.0")));
    EXPECT_FALSE(isValidNetmask(*parseIpv4("255.255.255.255")));
}

TEST(NormalizeMac, AcceptsCommonFormats) {
    EXPECT_EQ(normalizeMac("AA:bb:CC:dd:EE:01"), "aabbccddee01");
    EXPECT_EQ(normalizeMac("aa-bb-cc-dd-ee-01"), "aabbccddee01");
    EXPECT_EQ(normalizeMac("aabbccddee01"), "aabbccddee01");
    for (const char* bad : {"", "aa:bb:cc:dd:ee", "aa:bb:cc:dd:ee:0g", "aa::bb:cc:dd:ee:01",
                            "aa.bb.cc.dd.ee.01", "aabbccddee0"}) {
        EXPECT_FALSE(normalizeMac(bad).has_value()) << bad;
    }
}

TEST(SubnetStatus, FlagsCamerasOutsideTheNicSubnet) {
    EXPECT_EQ(subnetStatus(gigE("192.168.10.50")), SubnetStatus::Inside);
    EXPECT_EQ(subnetStatus(gigE("192.168.11.50")), SubnetStatus::Outside);
    EXPECT_EQ(subnetStatus(gigE("169.254.20.5")), SubnetStatus::Outside);
}

TEST(SubnetStatus, UnknownWhenDataIsMissing) {
    DiscoveredCamera usb;
    EXPECT_EQ(subnetStatus(usb), SubnetStatus::Unknown);
    auto noNic = gigE("192.168.10.50");
    noNic.nicIp.clear();
    EXPECT_EQ(subnetStatus(noNic), SubnetStatus::Unknown);
}

TEST(ValidateIpRequest, AcceptsGoodRequestAndEmptyGateway) {
    EXPECT_TRUE(validateIpRequest(goodRequest()).has_value());
    auto noGateway = goodRequest();
    noGateway.gateway.clear();
    EXPECT_TRUE(validateIpRequest(noGateway).has_value());
}

TEST(ValidateIpRequest, RejectsBadFields) {
    const auto expectInvalid = [](GigEIpRequest r) {
        const auto result = validateIpRequest(r);
        ASSERT_FALSE(result.has_value());
        EXPECT_EQ(result.error().code, Errc::InvalidArgument);
    };
    auto r = goodRequest();
    r.mac = "nope";
    expectInvalid(r);
    r = goodRequest();
    r.ip = "192.168.10.0"; // network address
    expectInvalid(r);
    r.ip = "192.168.10.255"; // broadcast address
    expectInvalid(r);
    r.ip = "224.0.0.5"; // multicast
    expectInvalid(r);
    r = goodRequest();
    r.subnetMask = "255.0.255.0";
    expectInvalid(r);
    r = goodRequest();
    r.gateway = "10.0.0.1"; // outside the subnet
    expectInvalid(r);
}
