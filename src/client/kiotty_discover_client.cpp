#include "kiotty_discovery_client.hpp"
#include "../protocol/kiotty_discovery_protocol.hpp"

#include <cstring>
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
#  include <iphlpapi.h>
#  pragma comment(lib, "ws2_32.lib")
#  pragma comment(lib, "iphlpapi.lib")
#  pragma warning(disable: 4996) // strncpy
   typedef int socklen_t;
#  define KIOTTY_CLOSE_SOCKET(s) ::closesocket(s)
#else
#  include <arpa/inet.h>
#  include <ifaddrs.h>
#  include <net/if.h>
#  include <netinet/in.h>
#  include <sys/socket.h>
#  include <unistd.h>
   typedef int SOCKET;
#  define INVALID_SOCKET  (-1)
#  define SOCKET_ERROR    (-1)
#  define KIOTTY_CLOSE_SOCKET(s) ::close(s)
#endif

// ---------------------------------------------------------------------------
// Enumerate subnet-directed broadcast addresses for all active interfaces.
// Falls back to INADDR_BROADCAST if none are found.
// ---------------------------------------------------------------------------
static std::vector<uint32_t> get_broadcast_addresses() {
    std::vector<uint32_t> addrs;

#if defined(_WIN32) || defined(_WIN64)
    ULONG size = 15000;
    std::vector<char> buf(size);
    auto* adapters = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buf.data());

    DWORD ret = GetAdaptersAddresses(AF_INET,
        GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER,
        nullptr, adapters, &size);
    if (ret == ERROR_BUFFER_OVERFLOW) {
        buf.resize(size);
        adapters = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buf.data());
        ret = GetAdaptersAddresses(AF_INET,
            GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER,
            nullptr, adapters, &size);
    }
    if (ret == NO_ERROR) {
        for (auto* a = adapters; a; a = a->Next) {
            if (a->OperStatus != IfOperStatusUp) continue;
            for (auto* uni = a->FirstUnicastAddress; uni; uni = uni->Next) {
                auto* sa = reinterpret_cast<sockaddr_in*>(uni->Address.lpSockaddr);
                if (sa->sin_family != AF_INET) continue;
                uint32_t ip     = ntohl(sa->sin_addr.s_addr);
                uint8_t  prefix = static_cast<uint8_t>(uni->OnLinkPrefixLength);
                uint32_t mask   = prefix ? (~0u << (32u - prefix)) : 0u;
                uint32_t bcast  = (ip & mask) | (~mask);
                addrs.push_back(htonl(bcast));
            }
        }
    }
#else
    struct ifaddrs* ifap = nullptr;
    if (getifaddrs(&ifap) == 0) {
        for (auto* ifa = ifap; ifa; ifa = ifa->ifa_next) {
            if (!ifa->ifa_addr || ifa->ifa_addr->sa_family != AF_INET) continue;
            if (!(ifa->ifa_flags & IFF_BROADCAST))                      continue;
            if (!ifa->ifa_broadaddr)                                     continue;
            auto* bsa = reinterpret_cast<sockaddr_in*>(ifa->ifa_broadaddr);
            addrs.push_back(bsa->sin_addr.s_addr);
        }
        freeifaddrs(ifap);
    }
#endif

    if (addrs.empty())
        addrs.push_back(INADDR_BROADCAST); // last-resort fallback
    return addrs;
}

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

    // Send discovery request to every subnet's directed broadcast address.
    // Using per-interface broadcast (e.g. 192.168.1.255) instead of
    // INADDR_BROADCAST (255.255.255.255) ensures the packet is reliably
    // delivered on all LAN segments, regardless of VirtualBox or other
    // virtual adapters the host may have.
    KiottyDiscoveryRequest request;
    struct sockaddr_in bcast {};
    bcast.sin_family = AF_INET;
    bcast.sin_port   = htons(discovery_port);
    for (uint32_t bcast_addr : get_broadcast_addresses()) {
        bcast.sin_addr.s_addr = bcast_addr;
        ::sendto(sock, reinterpret_cast<const char*>(&request), sizeof(request), 0,
                 reinterpret_cast<struct sockaddr*>(&bcast), sizeof(bcast));
    }

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
