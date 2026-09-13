#include <winsock2.h>
#include <ws2tcpip.h>

#include <array>
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <string>

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
        int durationSeconds = 0;
        std::string bindAddress = "127.0.0.1";
        for (int index = 1; index < argc; ++index) {
            const std::string option = argv[index];
            if (option == "--port" && index + 1 < argc) {
                port = static_cast<unsigned short>(std::stoul(argv[++index]));
            } else if (option == "--duration" && index + 1 < argc) {
                durationSeconds = std::stoi(argv[++index]);
            } else if (option == "--bind" && index + 1 < argc) {
                bindAddress = argv[++index];
            } else {
                throw std::invalid_argument(
                    "usage: nlc_udp_echo_server [--bind IP] [--port N] [--duration S]");
            }
        }

        Winsock winsock;
        const SOCKET socketHandle = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (socketHandle == INVALID_SOCKET) {
            throw std::runtime_error("socket failed");
        }
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_port = htons(port);
        if (inet_pton(AF_INET, bindAddress.c_str(), &address.sin_addr) != 1) {
            closesocket(socketHandle);
            throw std::invalid_argument("invalid IPv4 bind address");
        }
        if (bind(socketHandle, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) == SOCKET_ERROR) {
            closesocket(socketHandle);
            throw std::runtime_error("bind failed; is UDP port already in use?");
        }

        DWORD timeoutMs = 250;
        setsockopt(socketHandle, SOL_SOCKET, SO_RCVTIMEO,
                   reinterpret_cast<const char*>(&timeoutMs), sizeof(timeoutMs));
        std::cout << "UDP echo server listening on " << bindAddress << ':' << port << '\n';
        const auto started = std::chrono::steady_clock::now();
        std::array<char, 65'535> buffer{};
        std::uint64_t echoed = 0;
        for (;;) {
            sockaddr_in peer{};
            int peerLength = sizeof(peer);
            const auto received = recvfrom(
                socketHandle, buffer.data(), static_cast<int>(buffer.size()), 0,
                reinterpret_cast<sockaddr*>(&peer), &peerLength);
            if (received != SOCKET_ERROR) {
                if (sendto(socketHandle, buffer.data(), received, 0,
                           reinterpret_cast<const sockaddr*>(&peer), peerLength) != received) {
                    std::cerr << "sendto failed: " << WSAGetLastError() << '\n';
                } else {
                    ++echoed;
                }
            }
            if (durationSeconds > 0 && std::chrono::steady_clock::now() - started >=
                    std::chrono::seconds(durationSeconds)) {
                break;
            }
        }
        closesocket(socketHandle);
        std::cout << "Echoed " << echoed << " datagrams\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "error: " << error.what() << '\n';
        return 1;
    }
}
