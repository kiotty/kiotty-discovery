#if !defined(__KIOTTY_DISCOVERY_PROTOCOL__)
#define __KIOTTY_DISCOVERY_PROTOCOL__

#include <cstring>
#include <cstdint>

constexpr static const char KIOTTY_DISCOVERY_REQUEST_MAGIC[] = {'K','I','O','T','D','R','E', 'Q'};
struct KiottyDiscoveryRequest
{
    char magic[sizeof(KIOTTY_DISCOVERY_REQUEST_MAGIC)] {0};

    explicit KiottyDiscoveryRequest() {
        memcpy(magic, KIOTTY_DISCOVERY_REQUEST_MAGIC, sizeof(magic));
    }
    inline bool valid() {
        return strncmp(magic, KIOTTY_DISCOVERY_REQUEST_MAGIC, sizeof(magic)) == 0;
    }
};

constexpr static const char KIOTTY_DISCOVERY_RESPONSE_MAGIC[] = {'K','I','O','T','D','R','E','S'};
struct KiottyDiscoveryResponseHeader
{
    char magic[sizeof(KIOTTY_DISCOVERY_RESPONSE_MAGIC)] {0};
    uint32_t num_of_endpoints;

    explicit KiottyDiscoveryResponseHeader(uint32_t num_of_endpoints) :
        num_of_endpoints(num_of_endpoints)
    {
        memcpy(magic, KIOTTY_DISCOVERY_RESPONSE_MAGIC, sizeof(magic));
    }
    inline bool valid() {
        return strncmp(magic, KIOTTY_DISCOVERY_RESPONSE_MAGIC, sizeof(magic)) == 0;
    }
};

struct KiottyDiscoveryEndpoint
{
    uint32_t port {0};
    char description[12] {0};
};

// MAX_ENDPOINTS is derived from actual struct sizes and must equal KIOTTY_DISCOVERY_MAX_ENDPOINTS.
constexpr static size_t MAX_ENDPOINTS =
    (1024 - sizeof(KiottyDiscoveryResponseHeader)) / sizeof(KiottyDiscoveryEndpoint);
struct KiottyDiscoveryResponse
{
    KiottyDiscoveryResponseHeader header;
    KiottyDiscoveryEndpoint endpoints[MAX_ENDPOINTS];
};
static_assert(sizeof(KiottyDiscoveryResponse) <= 1024);


#endif // __KIOTTY_DISCOVERY_PROTOCOL__
