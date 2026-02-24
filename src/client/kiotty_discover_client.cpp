#include "kiotty_discovery_client.hpp"
#include "../protocol/kiotty_discovery_protocol.hpp"

#include <cstring>

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
#  pragma warning(disable: 4996) // strncpy
   typedef int socklen_t;
#  define KIOTTY_CLOSE_SOCKET(s) ::closesocket(s)
#else
#  include <arpa/inet.h>
#  include <netinet/in.h>
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
#  include <mutex>
static std::mutex g_wsa_mutex;
static int        g_wsa_refs = 0;
static void wsa_acquire() {
    std::lock_guard<std::mutex> lk(g_wsa_mutex);
    if (g_wsa_refs++ == 0) { WSADATA wsa; WSAStartup(MAKEWORD(2, 2), &wsa); }
}
static void wsa_release() {
    std::lock_guard<std::mutex> lk(g_wsa_mutex);
    if (--g_wsa_refs == 0) WSACleanup();
}
#else
static void wsa_acquire() {}
static void wsa_release() {}
#endif

struct KiottyDiscoveryResult
{
	char ip_address[INET_ADDRSTRLEN]{ 0 };
	uint32_t num_of_endpoints{ 0 };
	KiottyDiscoveryEndpoint endpoints[MAX_ENDPOINTS]{};
};


// ---------------------------------------------------------------------------
// API
// ---------------------------------------------------------------------------
KiottyDiscoveryResult* KiottyDiscoveryClient_discoverServer(uint16_t discovery_port) {
    wsa_acquire();

    SOCKET sock = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (sock == INVALID_SOCKET) { wsa_release(); return nullptr; }

    // Enable broadcast
    int opt = 1;
    ::setsockopt(sock, SOL_SOCKET, SO_BROADCAST,
                 reinterpret_cast<const char*>(&opt), sizeof(opt));

    // 3-second receive timeout
#if defined(_WIN32) || defined(_WIN64)
    DWORD rcv_timeout = 3000;
    ::setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO,
                 reinterpret_cast<const char*>(&rcv_timeout), sizeof(rcv_timeout));
#else
    struct timeval rcv_timeout {3, 0};
    ::setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &rcv_timeout, sizeof(rcv_timeout));
#endif

    // Bind to an ephemeral source port
    struct sockaddr_in local {};
    local.sin_family      = AF_INET;
    local.sin_addr.s_addr = INADDR_ANY;
    local.sin_port        = 0;
    ::bind(sock, reinterpret_cast<struct sockaddr*>(&local), sizeof(local));

    // Send broadcast discovery request
    KiottyDiscoveryRequest request;
    struct sockaddr_in bcast {};
    bcast.sin_family      = AF_INET;
    bcast.sin_addr.s_addr = INADDR_BROADCAST;
    bcast.sin_port        = htons(discovery_port);
    ::sendto(sock, reinterpret_cast<const char*>(&request), sizeof(request), 0,
             reinterpret_cast<struct sockaddr*>(&bcast), sizeof(bcast));

    // Wait for the first valid response
    char buf[1024] {};
    struct sockaddr_in server_addr {};
    socklen_t server_len = sizeof(server_addr);
    int received = static_cast<int>(::recvfrom(
        sock, buf, static_cast<int>(sizeof(buf)), 0,
        reinterpret_cast<struct sockaddr*>(&server_addr), &server_len));

    KIOTTY_CLOSE_SOCKET(sock);
    wsa_release();

    if (received < static_cast<int>(sizeof(KiottyDiscoveryResponseHeader))) return nullptr;

    auto* hdr = reinterpret_cast<KiottyDiscoveryResponseHeader*>(buf);
    if (!hdr->valid()) return nullptr;

    uint32_t n = hdr->num_of_endpoints;
    if (n > static_cast<uint32_t>(MAX_ENDPOINTS)) n = static_cast<uint32_t>(MAX_ENDPOINTS);

    // Validate that we received enough bytes for the claimed endpoints
    size_t expected = sizeof(KiottyDiscoveryResponseHeader) + n * sizeof(KiottyDiscoveryEndpoint);
    if (static_cast<size_t>(received) < expected) return nullptr;

    // Fill into thread-local result buffer (valid until next call on this thread)
    thread_local static KiottyDiscoveryResult result;
    result = KiottyDiscoveryResult{};
    ::inet_ntop(AF_INET, &server_addr.sin_addr,
                result.ip_address, sizeof(result.ip_address));
    result.num_of_endpoints = n;
    auto* resp = reinterpret_cast<KiottyDiscoveryResponse*>(buf);
    for (uint32_t i = 0; i < n; ++i) result.endpoints[i] = resp->endpoints[i];

    return &result;
}

const char* KiottyDiscoveryClient_getIpAddress(const KiottyDiscoveryResult* result) {
    if (!result) return nullptr;
    return result->ip_address;
}

uint32_t KiottyDiscoveryClient_getNumOfEndpoints(const KiottyDiscoveryResult* result) {
    if (!result) return 0;
    return result->num_of_endpoints;
}

uint32_t KiottyDiscoveryClient_getPort(const KiottyDiscoveryResult* result, size_t index) {
    if (!result || index >= result->num_of_endpoints) return 0;
    return result->endpoints[index].port;
}

const char* KiottyDiscoveryClient_getPortDescription(const KiottyDiscoveryResult* result, size_t index) {
    if (!result || index >= result->num_of_endpoints) return nullptr;
    return result->endpoints[index].description;
}
