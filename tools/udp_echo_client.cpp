#include <winsock2.h>
#include <ws2tcpip.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {

class Winsock final {
public:
    Winsock() {
        WSADATA data{};
        if (WSAStartup(MAKEWORD(2, 2), &data) != 0) {
            throw std::runtime_error("WSAStartup failed");
        }
    }
    ~Winsock() { WSACleanup(); }
};

}  // namespace

int main(int argc, char** argv) {
    try {
        unsigned short port = 47991;
        int count = 20;
        int intervalMs = 50;
        std::string host = "127.0.0.1";
        for (int index = 1; index < argc; ++index) {
            const std::string option = argv[index];
            if (option == "--port" && index + 1 < argc) {
                port = static_cast<unsigned short>(std::stoul(argv[++index]));
            } else if (option == "--count" && index + 1 < argc) {
                count = std::stoi(argv[++index]);
            } else if (option == "--interval-ms" && index + 1 < argc) {
                intervalMs = std::stoi(argv[++index]);
            } else if (option == "--host" && index + 1 < argc) {
                host = argv[++index];
            } else {
                throw std::invalid_argument(
                    "usage: nlc_udp_echo_client [--host IP] [--port N] [--count N] [--interval-ms N]");
            }
        }

        Winsock winsock;
        const SOCKET socketHandle = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (socketHandle == INVALID_SOCKET) {
            throw std::runtime_error("socket failed");
        }
        DWORD timeoutMs = 2'000;
        setsockopt(socketHandle, SOL_SOCKET, SO_RCVTIMEO,
                   reinterpret_cast<const char*>(&timeoutMs), sizeof(timeoutMs));
        sockaddr_in server{};
        server.sin_family = AF_INET;
        server.sin_port = htons(port);
        if (inet_pton(AF_INET, host.c_str(), &server.sin_addr) != 1) {
            closesocket(socketHandle);
            throw std::invalid_argument("invalid IPv4 host address");
        }

        std::vector<double> rtts;
        rtts.reserve(static_cast<std::size_t>(count));
        std::array<char, 64> buffer{};
        for (int sequence = 0; sequence < count; ++sequence) {
            const auto payload = std::to_string(sequence);
            const auto started = std::chrono::steady_clock::now();
            if (sendto(socketHandle, payload.data(), static_cast<int>(payload.size()), 0,
                       reinterpret_cast<const sockaddr*>(&server), sizeof(server)) == SOCKET_ERROR) {
                throw std::runtime_error("sendto failed");
            }
            sockaddr_in peer{};
            int peerLength = sizeof(peer);
            const auto received = recvfrom(
                socketHandle, buffer.data(), static_cast<int>(buffer.size()), 0,
                reinterpret_cast<sockaddr*>(&peer), &peerLength);
            if (received == SOCKET_ERROR) {
                std::cout << "timeout sequence=" << sequence << '\n';
            } else {
                const auto rttUs = std::chrono::duration<double, std::micro>(
                    std::chrono::steady_clock::now() - started).count();
                rtts.push_back(rttUs);
                std::cout << "sequence=" << sequence << " rtt_us=" << std::fixed
                          << std::setprecision(1) << rttUs << '\n';
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(intervalMs));
        }
        closesocket(socketHandle);
        if (rtts.empty()) {
            return 2;
        }
        std::sort(rtts.begin(), rtts.end());
        const auto average = std::accumulate(rtts.begin(), rtts.end(), 0.0) / rtts.size();
        const auto p95 = rtts[static_cast<std::size_t>(0.95 * (rtts.size() - 1))];
        std::cout << "received=" << rtts.size() << " average_rtt_us=" << average
                  << " p95_rtt_us=" << p95 << '\n';
        return rtts.size() == static_cast<std::size_t>(count) ? 0 : 2;
    } catch (const std::exception& error) {
        std::cerr << "error: " << error.what() << '\n';
        return 1;
    }
}
