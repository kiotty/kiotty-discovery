#include "kiotty_discovery_server.hpp"
#include "../protocol/kiotty_discovery_protocol.hpp"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

// ---------------------------------------------------------------------------
// Platform-specific socket includes
// ---------------------------------------------------------------------------
#if defined(_WIN32) || defined(_WIN64)
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <winsock2.h>
#  include <ws2tcpip.h>
#  pragma comment(lib, "ws2_32.lib")
   typedef int socklen_t;
#  define KIOTTY_CLOSE_SOCKET(s) ::closesocket(s)
#else
#  include <arpa/inet.h>
#  include <netinet/in.h>
#  include <sys/select.h>
#  include <sys/socket.h>
#  include <unistd.h>
   typedef int SOCKET;
#  define INVALID_SOCKET  (-1)
#  define SOCKET_ERROR    (-1)
#  define KIOTTY_CLOSE_SOCKET(s) ::close(s)
#endif

// ---------------------------------------------------------------------------
// WinSock reference-counted init (no-op on Linux)
// ---------------------------------------------------------------------------
#if defined(_WIN32) || defined(_WIN64)
static std::mutex  g_wsa_mutex;
static int         g_wsa_refs = 0;

static void wsa_acquire() {
    std::lock_guard<std::mutex> lk(g_wsa_mutex);
    if (g_wsa_refs++ == 0) {
        WSADATA wsa;
        WSAStartup(MAKEWORD(2, 2), &wsa);
    }
}
static void wsa_release() {
    std::lock_guard<std::mutex> lk(g_wsa_mutex);
    if (--g_wsa_refs == 0) WSACleanup();
}
#else
static void wsa_acquire() {}
static void wsa_release() {}
#endif

// ---------------------------------------------------------------------------
// Opaque structs
// ---------------------------------------------------------------------------
struct KiottyDiscoveryServerMessage {
    char content[64] {0};
};

struct KiottyDiscoveryServer {
    uint16_t discovery_port {0};
    char     ip_address[64] {0};

    std::mutex                  endpoints_mutex;
    std::vector<KiottyDiscoveryEndpoint> endpoints;

    std::mutex                  message_mutex;
    std::condition_variable     message_cv;
    std::queue<KiottyDiscoveryServerMessage*> message_queue;
    std::atomic<bool>           cancel_flag {false};

    std::thread                 server_thread;
    std::atomic<bool>           running     {false};
    std::atomic<bool>           thread_done {false};
    std::mutex                  thread_done_mutex;
    std::condition_variable     thread_done_cv;

    SOCKET sock {INVALID_SOCKET};
};

// ---------------------------------------------------------------------------
// Local IP detection via dummy UDP connect
// ---------------------------------------------------------------------------
static void detect_local_ip(char* buf, size_t buf_len) {
    SOCKET tmp = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (tmp == INVALID_SOCKET) {
        ::strncpy(buf, "0.0.0.0", buf_len);
        return;
    }
    struct sockaddr_in dst {};
    dst.sin_family = AF_INET;
    dst.sin_port   = htons(53);
    ::inet_pton(AF_INET, "8.8.8.8", &dst.sin_addr);
    ::connect(tmp, reinterpret_cast<struct sockaddr*>(&dst), sizeof(dst));

    struct sockaddr_in local {};
    socklen_t len = sizeof(local);
    ::getsockname(tmp, reinterpret_cast<struct sockaddr*>(&local), &len);
    ::inet_ntop(AF_INET, &local.sin_addr, buf, static_cast<socklen_t>(buf_len));
    KIOTTY_CLOSE_SOCKET(tmp);
}

// ---------------------------------------------------------------------------
// Server thread
// ---------------------------------------------------------------------------
static void server_thread_func(KiottyDiscoveryServer* server) {
    while (server->running.load()) {
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(server->sock, &rfds);
        struct timeval tv {0, 100000}; // 100 ms
        int ret = ::select(static_cast<int>(server->sock) + 1, &rfds, nullptr, nullptr, &tv);
        if (ret <= 0 || !FD_ISSET(server->sock, &rfds)) continue;

        char req_buf[sizeof(KiottyDiscoveryRequest) + 16];
        struct sockaddr_in client_addr {};
        socklen_t client_len = sizeof(client_addr);
        int received = static_cast<int>(::recvfrom(
            server->sock, req_buf, static_cast<int>(sizeof(req_buf)), 0,
            reinterpret_cast<struct sockaddr*>(&client_addr), &client_len));

        if (received != static_cast<int>(sizeof(KiottyDiscoveryRequest))) continue;
        auto* req = reinterpret_cast<KiottyDiscoveryRequest*>(req_buf);
        if (!req->valid()) continue;

        // Build response into a raw buffer (KiottyDiscoveryResponseHeader has
        // no default constructor, so we use placement new).
        char resp_buf[sizeof(KiottyDiscoveryResponse)] {};
        auto* response = reinterpret_cast<KiottyDiscoveryResponse*>(resp_buf);
        size_t resp_size;
        {
            std::lock_guard<std::mutex> lk(server->endpoints_mutex);
            uint32_t n = static_cast<uint32_t>(server->endpoints.size());
            if (n > static_cast<uint32_t>(MAX_ENDPOINTS)) n = static_cast<uint32_t>(MAX_ENDPOINTS);
            new (&response->header) KiottyDiscoveryResponseHeader(n);
            for (uint32_t i = 0; i < n; ++i) response->endpoints[i] = server->endpoints[i];
            resp_size = sizeof(KiottyDiscoveryResponseHeader) + n * sizeof(KiottyDiscoveryEndpoint);
        }
        ::sendto(server->sock, resp_buf, static_cast<int>(resp_size), 0,
                 reinterpret_cast<struct sockaddr*>(&client_addr), client_len);

        // Create a message recording the discoverer's IP
        char client_ip[32] {};
        ::inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, sizeof(client_ip));
        auto* msg = new KiottyDiscoveryServerMessage;
        ::snprintf(msg->content, sizeof(msg->content), "Discovered by %s", client_ip);
        {
            std::lock_guard<std::mutex> lk(server->message_mutex);
            server->message_queue.push(msg);
        }
        server->message_cv.notify_one();
    }

    // Signal completion
    {
        std::lock_guard<std::mutex> lk(server->thread_done_mutex);
        server->thread_done = true;
    }
    server->thread_done_cv.notify_all();
}

// ---------------------------------------------------------------------------
// API
// ---------------------------------------------------------------------------
KiottyDiscoveryServer* KiottyDiscoveryServer_createServer(uint16_t discovery_port) {
    wsa_acquire();
    auto* server = new KiottyDiscoveryServer;
    server->discovery_port = discovery_port;
    detect_local_ip(server->ip_address, sizeof(server->ip_address));
    return server;
}

bool KiottyDiscoveryServer_addPort(KiottyDiscoveryServer* server, uint32_t port, const char* description) {
    std::lock_guard<std::mutex> lk(server->endpoints_mutex);
    if (server->endpoints.size() >= MAX_ENDPOINTS) return false;
    KiottyDiscoveryEndpoint ep {};
    ep.port = port;
    if (description) ::strncpy(ep.description, description, sizeof(ep.description) - 1);
    server->endpoints.push_back(ep);
    return true;
}

const char* KiottyDiscoveryServer_getIpAddress(KiottyDiscoveryServer* server) {
    return server->ip_address;
}

uint32_t KiottyDiscoveryServer_getNumOfEndpoints(KiottyDiscoveryServer* server) {
    std::lock_guard<std::mutex> lk(server->endpoints_mutex);
    return static_cast<uint32_t>(server->endpoints.size());
}

uint32_t KiottyDiscoveryServer_getPort(KiottyDiscoveryServer* server, size_t index) {
    std::lock_guard<std::mutex> lk(server->endpoints_mutex);
    if (index >= server->endpoints.size()) return 0;
    return server->endpoints[index].port;
}

const char* KiottyDiscoveryServer_getPortDescription(KiottyDiscoveryServer* server, size_t index) {
    std::lock_guard<std::mutex> lk(server->endpoints_mutex);
    if (index >= server->endpoints.size()) return nullptr;
    return server->endpoints[index].description;
}

void KiottyDiscoveryServer_removePort(KiottyDiscoveryServer* server, size_t index) {
    std::lock_guard<std::mutex> lk(server->endpoints_mutex);
    if (index < server->endpoints.size())
        server->endpoints.erase(server->endpoints.begin() + static_cast<ptrdiff_t>(index));
}

KiottyDiscoveryServerMessage* KiottyDiscoveryServer_awaitMessage(KiottyDiscoveryServer* server, uint64_t timeout_ms) {
    server->cancel_flag = false;
    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    std::unique_lock<std::mutex> lk(server->message_mutex);
    server->message_cv.wait_until(lk, deadline, [&] {
        return !server->message_queue.empty() || server->cancel_flag.load();
    });
    if (server->cancel_flag.load() || server->message_queue.empty()) return nullptr;

    // Copy into thread-local buffer, free the heap allocation, return pointer to local.
    // The pointer is valid until the next call to awaitMessage on this thread.
    thread_local static KiottyDiscoveryServerMessage local_msg;
    KiottyDiscoveryServerMessage* queued = server->message_queue.front();
    server->message_queue.pop();
    local_msg = *queued;
    delete queued;
    return &local_msg;
}

const char* KiottyDiscoveryServer_getMessage(KiottyDiscoveryServerMessage* message) {
    if (!message) return nullptr;
    return message->content;
}

void KiottyDiscoveryServer_cancelMessage(KiottyDiscoveryServer* server) {
    server->cancel_flag = true;
    server->message_cv.notify_all();
}

bool KiottyDiscoveryServer_openServer(KiottyDiscoveryServer* server) {
    server->sock = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (server->sock == INVALID_SOCKET) return false;

    int opt = 1;
    ::setsockopt(server->sock, SOL_SOCKET, SO_REUSEADDR,
                 reinterpret_cast<const char*>(&opt), sizeof(opt));
    ::setsockopt(server->sock, SOL_SOCKET, SO_BROADCAST,
                 reinterpret_cast<const char*>(&opt), sizeof(opt));

    struct sockaddr_in addr {};
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port        = htons(server->discovery_port);

    if (::bind(server->sock, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR) {
        KIOTTY_CLOSE_SOCKET(server->sock);
        server->sock = INVALID_SOCKET;
        return false;
    }

    server->thread_done = false;
    server->running     = true;
    server->server_thread = std::thread(server_thread_func, server);
    return true;
}

void KiottyDiscoveryServer_awaitServer(KiottyDiscoveryServer* server) {
    std::unique_lock<std::mutex> lk(server->thread_done_mutex);
    server->thread_done_cv.wait(lk, [&] { return server->thread_done.load(); });
}

void KiottyDiscoveryServer_closeServer(KiottyDiscoveryServer* server) {
    server->running = false;
}

void KiottyDiscoveryServer_releaseServer(KiottyDiscoveryServer* server) {
    // Signal thread to stop
    server->running = false;

    // Wait for thread to finish
    {
        std::unique_lock<std::mutex> lk(server->thread_done_mutex);
        server->thread_done_cv.wait(lk, [&] { return server->thread_done.load(); });
    }
    if (server->server_thread.joinable()) server->server_thread.join();

    // Close socket
    if (server->sock != INVALID_SOCKET) {
        KIOTTY_CLOSE_SOCKET(server->sock);
        server->sock = INVALID_SOCKET;
    }

    // Cancel any blocked awaitMessage and drain the queue
    server->cancel_flag = true;
    server->message_cv.notify_all();
    {
        std::lock_guard<std::mutex> lk(server->message_mutex);
        while (!server->message_queue.empty()) {
            delete server->message_queue.front();
            server->message_queue.pop();
        }
    }

    wsa_release();
    delete server;
}
