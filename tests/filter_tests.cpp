#include <winsock2.h>
#include <ws2tcpip.h>

#include "nlc/windivert_engine.h"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstring>
#include <vector>

namespace {

std::vector<std::byte> Ipv4Packet(std::uint8_t protocol, bool fragment = false) {
    const std::size_t transportLength = protocol == IPPROTO_TCP ? sizeof(WINDIVERT_TCPHDR) :
        protocol == IPPROTO_UDP ? sizeof(WINDIVERT_UDPHDR) : sizeof(WINDIVERT_ICMPHDR);
    std::vector<std::byte> packet(sizeof(WINDIVERT_IPHDR) + transportLength);
    auto* ip = reinterpret_cast<WINDIVERT_IPHDR*>(packet.data());
    ip->Version = 4;
    ip->HdrLength = 5;
    ip->Length = WinDivertHelperHtons(static_cast<UINT16>(packet.size()));
    ip->TTL = 64;
    ip->Protocol = protocol;
    if (fragment) WINDIVERT_IPHDR_SET_FRAGOFF(ip, 1);
    if (protocol == IPPROTO_TCP) {
        auto* tcp = reinterpret_cast<WINDIVERT_TCPHDR*>(packet.data() + sizeof(WINDIVERT_IPHDR));
        tcp->HdrLength = 5;
    }
    return packet;
}

std::vector<std::byte> Ipv6Packet(std::uint8_t protocol) {
    const std::size_t transportLength = protocol == IPPROTO_TCP
        ? sizeof(WINDIVERT_TCPHDR) : sizeof(WINDIVERT_UDPHDR);
    std::vector<std::byte> packet(sizeof(WINDIVERT_IPV6HDR) + transportLength);
    auto* ip = reinterpret_cast<WINDIVERT_IPV6HDR*>(packet.data());
    ip->Version = 6;
    ip->Length = WinDivertHelperHtons(static_cast<UINT16>(transportLength));
    ip->NextHdr = protocol;
    ip->HopLimit = 64;
    if (protocol == IPPROTO_TCP) {
        auto* tcp = reinterpret_cast<WINDIVERT_TCPHDR*>(packet.data() + sizeof(WINDIVERT_IPV6HDR));
        tcp->HdrLength = 5;
    }
    return packet;
}

WINDIVERT_ADDRESS Address() {
    WINDIVERT_ADDRESS address{};
    address.Layer = WINDIVERT_LAYER_NETWORK;
    address.Event = WINDIVERT_EVENT_NETWORK_PACKET;
    return address;
}

bool Matches(const std::vector<std::byte>& packet, WINDIVERT_ADDRESS address = Address()) {
    const auto version = static_cast<unsigned>(packet.front()) >> 4U;
    address.IPv6 = version == 6 ? 1 : 0;
    return WinDivertHelperEvalFilter(
        nlc::GlobalWinDivertFilter, packet.data(), static_cast<UINT>(packet.size()), &address) != FALSE;
}

TEST(GlobalFilterTest, CompilesForNetworkLayer) {
    const char* error = nullptr;
    UINT position = 0;
    EXPECT_TRUE(WinDivertHelperCompileFilter(
        nlc::GlobalWinDivertFilter, WINDIVERT_LAYER_NETWORK,
        nullptr, 0, &error, &position));
}

TEST(GlobalFilterTest, ExcludesLoopback) {
    auto address = Address();
    address.Loopback = 1;
    EXPECT_FALSE(Matches(Ipv4Packet(IPPROTO_TCP), address));
}

TEST(GlobalFilterTest, ExcludesImpostor) {
    auto address = Address();
    address.Impostor = 1;
    EXPECT_FALSE(Matches(Ipv4Packet(IPPROTO_UDP), address));
}

TEST(GlobalFilterTest, ExcludesFragments) {
    EXPECT_FALSE(Matches(Ipv4Packet(IPPROTO_UDP, true)));
}

TEST(GlobalFilterTest, ExcludesIcmpAndIcmpV6) {
    EXPECT_FALSE(Matches(Ipv4Packet(IPPROTO_ICMP)));
    EXPECT_FALSE(Matches(Ipv6Packet(IPPROTO_ICMPV6)));
}

TEST(GlobalFilterTest, MatchesTcpIpv4) { EXPECT_TRUE(Matches(Ipv4Packet(IPPROTO_TCP))); }
TEST(GlobalFilterTest, MatchesUdpIpv4) { EXPECT_TRUE(Matches(Ipv4Packet(IPPROTO_UDP))); }
TEST(GlobalFilterTest, MatchesTcpIpv6) { EXPECT_TRUE(Matches(Ipv6Packet(IPPROTO_TCP))); }
TEST(GlobalFilterTest, MatchesUdpIpv6) { EXPECT_TRUE(Matches(Ipv6Packet(IPPROTO_UDP))); }

}  // namespace
