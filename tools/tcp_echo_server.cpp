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
        if (WSAStartup(MAKEWORD(2, 2), &data) != 0) throw std::runtime_error("WSAStartup failed");
    }
    ~Winsock() { WSACleanup(); }
};

bool SendAll(SOCKET socketHandle, const char* data, int length) {
    int sent = 0;
    while (sent < length) {
        const auto result = send(socketHandle, data + sent, length - sent, 0);
        if (result <= 0) return false;
        sent += result;
    }
    return true;
}
}  // namespace

int main(int argc, char** argv) {
    try {
        std::string bindAddress = "127.0.0.1";
        unsigned short port = 47992;
        int durationSeconds = 0;
        for (int index = 1; index < argc; ++index) {
            const std::string option = argv[index];
            if (option == "--bind" && index + 1 < argc) bindAddress = argv[++index];
            else if (option == "--port" && index + 1 < argc)
                port = static_cast<unsigned short>(std::stoul(argv[++index]));
            else if (option == "--duration" && index + 1 < argc)
                durationSeconds = std::stoi(argv[++index]);
            else throw std::invalid_argument(
                "usage: nlc_tcp_echo_server [--bind IP] [--port N] [--duration S]");
        }
        Winsock winsock;
        const SOCKET listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (listener == INVALID_SOCKET) throw std::runtime_error("socket failed");
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_port = htons(port);
        if (inet_pton(AF_INET, bindAddress.c_str(), &address.sin_addr) != 1)
            throw std::invalid_argument("invalid IPv4 bind address");
        BOOL reuse = TRUE;
        setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&reuse), sizeof(reuse));
        if (bind(listener, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) == SOCKET_ERROR ||
            listen(listener, SOMAXCONN) == SOCKET_ERROR) {
            closesocket(listener);
            throw std::runtime_error("bind/listen failed");
        }
        u_long nonBlocking = 1;
        ioctlsocket(listener, FIONBIO, &nonBlocking);
        std::cout << "TCP echo server listening on " << bindAddress << ':' << port << '\n';
        const auto started = std::chrono::steady_clock::now();
        std::uint64_t echoed = 0;
        while (durationSeconds == 0 || std::chrono::steady_clock::now() - started <
                std::chrono::seconds(durationSeconds)) {
            const SOCKET client = accept(listener, nullptr, nullptr);
            if (client == INVALID_SOCKET) {
                Sleep(10);
                continue;
            }
            DWORD timeout = 500;
            setsockopt(client, SOL_SOCKET, SO_RCVTIMEO,
                       reinterpret_cast<const char*>(&timeout), sizeof(timeout));
            std::array<char, 4096> buffer{};
            for (;;) {
                const auto received = recv(client, buffer.data(), static_cast<int>(buffer.size()), 0);
                if (received <= 0 || !SendAll(client, buffer.data(), received)) break;
                ++echoed;
            }
            closesocket(client);
        }
        closesocket(listener);
        std::cout << "Echoed " << echoed << " TCP reads\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "error: " << error.what() << '\n';
        return 1;
    }
}
