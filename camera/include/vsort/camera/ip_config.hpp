#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include <vsort/camera/discovery.hpp>
#include <vsort/common/error.hpp>

namespace vsort::camera {

// Dotted IPv4 -> integer (host order). Exactly four decimal parts, each 0..255.
[[nodiscard]] inline std::optional<std::uint32_t> parseIpv4(std::string_view text) noexcept {
    std::uint32_t value = 0;
    std::size_t pos = 0;
    for (int part = 0; part < 4; ++part) {
        std::uint32_t octet = 0;
        std::size_t digits = 0;
        while (pos < text.size() && text[pos] >= '0' && text[pos] <= '9') {
            octet = octet * 10U + static_cast<std::uint32_t>(text[pos] - '0');
            ++digits;
            ++pos;
            if (digits > 3 || octet > 255U) {
                return std::nullopt;
            }
        }
        if (digits == 0) {
            return std::nullopt;
        }
        value = (value << 8U) | octet;
        if (part < 3) {
            if (pos >= text.size() || text[pos] != '.') {
                return std::nullopt;
            }
            ++pos;
        }
    }
    if (pos != text.size()) {
        return std::nullopt;
    }
    return value;
}

// Contiguous ones, at least 2 host bits (/30 or wider).
[[nodiscard]] constexpr bool isValidNetmask(std::uint32_t mask) noexcept {
    const std::uint32_t inverse = ~mask;
    return mask != 0 && inverse >= 3U && (inverse & (inverse + 1U)) == 0;
}

// "aa:bb:cc:dd:ee:ff", "AA-BB-CC-DD-EE-FF" or "aabbccddeeff" -> 12 lowercase hex digits.
[[nodiscard]] inline std::optional<std::string> normalizeMac(std::string_view text) {
    const bool separated = text.size() == 17;
    if (text.size() != 12 && !separated) {
        return std::nullopt;
    }
    std::string out;
    out.reserve(12);
    for (std::size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        if (separated && i % 3 == 2) {
            if (c != ':' && c != '-') {
                return std::nullopt;
            }
        } else if ((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f')) {
            out.push_back(c);
        } else if (c >= 'A' && c <= 'F') {
            out.push_back(static_cast<char>(c - 'A' + 'a'));
        } else {
            return std::nullopt;
        }
    }
    return out;
}

enum class SubnetStatus : std::uint8_t {
    Unknown, // USB3, or camera/NIC addresses missing or invalid
    Inside,  // camera IP is in the subnet of its NIC: it can stream
    Outside, // camera IP is outside the NIC subnet: needs a new IP
};

[[nodiscard]] inline SubnetStatus subnetStatus(const DiscoveredCamera& cam) noexcept {
    if (cam.transport != Transport::GigE) {
        return SubnetStatus::Unknown;
    }
    const auto camIp = parseIpv4(cam.ip);
    const auto nicIp = parseIpv4(cam.nicIp);
    const auto nicMask = parseIpv4(cam.nicMask);
    if (!camIp || !nicIp || !nicMask || !isValidNetmask(*nicMask)) {
        return SubnetStatus::Unknown;
    }
    return ((*camIp ^ *nicIp) & *nicMask) == 0 ? SubnetStatus::Inside : SubnetStatus::Outside;
}

struct GigEIpRequest {
    std::string mac; // identifies the camera
    std::string ip;
    std::string subnetMask;
    std::string gateway;   // empty = none
    bool persistent{true}; // false: only until the camera is power-cycled
};

[[nodiscard]] inline Result<> validateIpRequest(const GigEIpRequest& r) {
    if (!normalizeMac(r.mac)) {
        return makeError(Errc::InvalidArgument, "invalid MAC address '" + r.mac + "'");
    }
    const auto ip = parseIpv4(r.ip);
    if (!ip) {
        return makeError(Errc::InvalidArgument, "invalid IP address '" + r.ip + "'");
    }
    const auto mask = parseIpv4(r.subnetMask);
    if (!mask || !isValidNetmask(*mask)) {
        return makeError(Errc::InvalidArgument, "invalid subnet mask '" + r.subnetMask + "'");
    }
    const std::uint32_t first = *ip >> 24U;
    if (first == 0 || first == 127 || first >= 224) {
        return makeError(Errc::InvalidArgument, "IP address is not a usable unicast address");
    }
    const std::uint32_t host = *ip & ~*mask;
    if (host == 0 || host == ~*mask) {
        return makeError(Errc::InvalidArgument, "IP address is the network or broadcast address");
    }
    if (!r.gateway.empty()) {
        const auto gw = parseIpv4(r.gateway);
        if (!gw) {
            return makeError(Errc::InvalidArgument, "invalid gateway '" + r.gateway + "'");
        }
        if (((*gw ^ *ip) & *mask) != 0) {
            return makeError(Errc::InvalidArgument, "gateway is outside the subnet");
        }
    }
    return {};
}

// Sets the IP of a GigE camera by MAC address (replaces AutoIPConfigTool).
class IGigEConfigurator {
public:
    IGigEConfigurator() = default;
    virtual ~IGigEConfigurator() = default;
    IGigEConfigurator(const IGigEConfigurator&) = delete;
    IGigEConfigurator& operator=(const IGigEConfigurator&) = delete;
    IGigEConfigurator(IGigEConfigurator&&) = delete;
    IGigEConfigurator& operator=(IGigEConfigurator&&) = delete;

    // Validates the request, then configures the camera. NotFound if no camera has this MAC.
    [[nodiscard]] virtual Result<> setIp(const GigEIpRequest& request) = 0;
};

} // namespace vsort::camera
