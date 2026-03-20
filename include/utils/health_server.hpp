#pragma once
// =============================================================================
// include/utils/health_server.hpp  --  Minimal HTTP health-check endpoint
// Listens on localhost:8080, responds to GET /health with JSON status.
// Thread-safe. No external dependencies.
// =============================================================================
#include <string>
#include <functional>
#include <atomic>
#include <thread>
#include <sstream>
#ifdef _WIN32
#  include <winsock2.h>
#  pragma comment(lib,"ws2_32.lib")
#else
#  include <sys/socket.h>
#  include <netinet/in.h>
#  include <unistd.h>
#  include <arpa/inet.h>
#endif

namespace agent {

// Callback to build the health payload JSON string
using HealthProvider = std::function<std::string()>;

class HealthServer {
public:
    explicit HealthServer(int port = 8080) : port_(port) {}

    ~HealthServer() { stop(); }

    // Start serving in a background thread.
    // provider() is called on each request to build the JSON payload.
    void start(HealthProvider provider) {
        if (running_.load()) return;
        provider_ = std::move(provider);
        running_.store(true);
        thread_ = std::thread([this]{ serve(); });
    }

    void stop() {
        running_.store(false);
        // Wake up the accept() call
#ifndef _WIN32
        if (sock_ >= 0) ::shutdown(sock_, SHUT_RDWR);
#else
        if (sock_ != INVALID_SOCKET) ::shutdown(sock_, SD_BOTH);
#endif
        if (thread_.joinable()) thread_.join();
    }

    bool is_running() const { return running_.load(); }

private:
    void serve() {
#ifdef _WIN32
        WSADATA wd; WSAStartup(MAKEWORD(2,2), &wd);
        sock_ = socket(AF_INET, SOCK_STREAM, 0);
        if (sock_ == INVALID_SOCKET) { running_.store(false); return; }
        int yes = 1; setsockopt(sock_, SOL_SOCKET, SO_REUSEADDR, (char*)&yes, sizeof(yes));
        sockaddr_in addr{}; addr.sin_family=AF_INET; addr.sin_port=htons((u_short)port_);
        addr.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
        if (bind(sock_,(sockaddr*)&addr,sizeof(addr))||listen(sock_,4)) {
            closesocket(sock_); running_.store(false); return; }
#else
        sock_ = socket(AF_INET, SOCK_STREAM, 0);
        if (sock_ < 0) { running_.store(false); return; }
        int yes = 1; setsockopt(sock_, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
        sockaddr_in addr{}; addr.sin_family=AF_INET; addr.sin_port=htons((uint16_t)port_);
        addr.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
        if (bind(sock_,(sockaddr*)&addr,sizeof(addr))<0 || listen(sock_,4)<0) {
            ::close(sock_); sock_=-1; running_.store(false); return; }
#endif
        while (running_.load()) {
#ifdef _WIN32
            SOCKET client = accept(sock_,nullptr,nullptr);
            if (client == INVALID_SOCKET) break;
#else
            int client = accept(sock_, nullptr, nullptr);
            if (client < 0) break;
#endif
            // Read request (we don't care about its content — always serve /health)
            char buf[512] = {}; recv(client, buf, sizeof(buf)-1, 0);
            std::string body = provider_ ? provider_() :
                "{\"status\":\"ok\",\"note\":\"no provider\"}";
            std::ostringstream resp;
            resp << "HTTP/1.1 200 OK\r\n"
                 << "Content-Type: application/json\r\n"
                 << "Content-Length: " << body.size() << "\r\n"
                 << "Connection: close\r\n\r\n"
                 << body;
            std::string r = resp.str();
            send(client, r.c_str(), (int)r.size(), 0);
#ifdef _WIN32
            closesocket(client);
#else
            ::close(client);
#endif
        }
#ifdef _WIN32
        closesocket(sock_); WSACleanup();
#else
        ::close(sock_); sock_ = -1;
#endif
    }

    int port_;
    HealthProvider provider_;
    std::atomic<bool> running_{false};
    std::thread thread_;
#ifdef _WIN32
    SOCKET sock_ = INVALID_SOCKET;
#else
    int sock_ = -1;
#endif
};

}  // namespace agent
