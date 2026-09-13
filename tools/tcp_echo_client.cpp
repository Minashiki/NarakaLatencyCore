#include <winsock2.h>
#include <ws2tcpip.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
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
        if (WSAStartup(MAKEWORD(2, 2), &data) != 0) throw std::runtime_error("WSAStartup failed");
    }
    ~Winsock() { WSACleanup(); }
};

bool TransferAll(SOCKET socketHandle, char* data, int length, bool sending) {
    int transferred = 0;
    while (transferred < length) {
        const auto result = sending
            ? send(socketHandle, data + transferred, length - transferred, 0)
            : recv(socketHandle, data + transferred, length - transferred, 0);
        if (result <= 0) return false;
        transferred += result;
    }
    return true;
}
}  // namespace

int main(int argc, char** argv) {
    try {
        std::string host = "127.0.0.1";
        unsigned short port = 47992;
        int count = 20;
        int intervalMs = 20;
        for (int index = 1; index < argc; ++index) {
            const std::string option = argv[index];
            if (option == "--host" && index + 1 < argc) host = argv[++index];
            else if (option == "--port" && index + 1 < argc)
                port = static_cast<unsigned short>(std::stoul(argv[++index]));
            else if (option == "--count" && index + 1 < argc) count = std::stoi(argv[++index]);
            else if (option == "--interval-ms" && index + 1 < argc) intervalMs = std::stoi(argv[++index]);
            else throw std::invalid_argument(
                "usage: nlc_tcp_echo_client [--host IP] [--port N] [--count N] [--interval-ms N]");
        }
        Winsock winsock;
        const SOCKET socketHandle = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        sockaddr_in server{};
        server.sin_family = AF_INET;
        server.sin_port = htons(port);
        if (inet_pton(AF_INET, host.c_str(), &server.sin_addr) != 1 ||
            connect(socketHandle, reinterpret_cast<const sockaddr*>(&server), sizeof(server)) == SOCKET_ERROR)
            throw std::runtime_error("connect failed");
        DWORD timeout = 2'000;
        setsockopt(socketHandle, SOL_SOCKET, SO_RCVTIMEO,
                   reinterpret_cast<const char*>(&timeout), sizeof(timeout));
        std::vector<double> rtts;
        rtts.reserve(count);
        for (std::uint64_t sequence = 0; sequence < static_cast<std::uint64_t>(count); ++sequence) {
            std::array<char, sizeof(sequence)> payload{};
            std::memcpy(payload.data(), &sequence, sizeof(sequence));
            const auto started = std::chrono::steady_clock::now();
            if (!TransferAll(socketHandle, payload.data(), payload.size(), true) ||
                !TransferAll(socketHandle, payload.data(), payload.size(), false)) break;
            rtts.push_back(std::chrono::duration<double, std::micro>(
                std::chrono::steady_clock::now() - started).count());
            std::this_thread::sleep_for(std::chrono::milliseconds(intervalMs));
        }
        closesocket(socketHandle);
        if (rtts.empty()) return 2;
        std::sort(rtts.begin(), rtts.end());
        const auto average = std::accumulate(rtts.begin(), rtts.end(), 0.0) / rtts.size();
        const auto p95 = rtts[static_cast<std::size_t>(0.95 * (rtts.size() - 1))];
        const auto p99 = rtts[static_cast<std::size_t>(0.99 * (rtts.size() - 1))];
        std::cout << "received=" << rtts.size() << " average_rtt_us=" << std::fixed
                  << std::setprecision(1) << average << " p95_rtt_us=" << p95
                  << " p99_rtt_us=" << p99 << " max_rtt_us=" << rtts.back() << '\n';
        return rtts.size() == static_cast<std::size_t>(count) ? 0 : 2;
    } catch (const std::exception& error) {
        std::cerr << "error: " << error.what() << '\n';
        return 1;
    }
}
