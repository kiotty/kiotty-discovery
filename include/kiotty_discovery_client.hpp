#if !defined(__KIOTTY_DISCOVERY_CLIENT__)
#define __KIOTTY_DISCOVERY_CLIENT__

#if defined(_WIN32) || defined(_WIN64) // Windows
    #ifdef KIOTTY_DISCOVERY_CLIENT_EXPORTS
        #define KIOTTY_DISCOVERY_CLIENT_API __declspec(dllexport)
    #else
        #define KIOTTY_DISCOVERY_CLIENT_API /*__declspec(dllimport)*/
    #endif
#elif defined(__linux__) || defined(__unix__) || defined(__ANDROID__) // Linux / Android
    #ifdef KIOTTY_DISCOVERY_CLIENT_EXPORTS
        #define KIOTTY_DISCOVERY_CLIENT_API __attribute__((visibility("default")))
    #else
        #define KIOTTY_DISCOVERY_CLIENT_API
    #endif
#else
    #define KIOTTY_DISCOVERY_CLIENT_API
    #pragma warning Unknown dynamic link import/export semantics.
#endif

#include <cstdint>
#include <cstddef>

// Result of a discovery request.
// Returned as a pointer to a thread-local buffer; valid until the next call
// to KiottyDiscoveryClient_discoverServer on the same thread.
struct KiottyDiscoveryResult;


// Sends a UDP broadcast on discovery_port and returns the first server response.
// Returns nullptr on timeout or error.
// The returned pointer is valid until the next call on the same thread — do NOT delete it.
KIOTTY_DISCOVERY_CLIENT_API KiottyDiscoveryResult* KiottyDiscoveryClient_discoverServer(uint16_t discovery_port);
KIOTTY_DISCOVERY_CLIENT_API const char* KiottyDiscoveryClient_getIpAddress(const KiottyDiscoveryResult* result);
KIOTTY_DISCOVERY_CLIENT_API uint32_t KiottyDiscoveryClient_getNumOfEndpoints(const KiottyDiscoveryResult* result);
KIOTTY_DISCOVERY_CLIENT_API uint32_t KiottyDiscoveryClient_getPort(const KiottyDiscoveryResult* result, size_t index);
KIOTTY_DISCOVERY_CLIENT_API const char* KiottyDiscoveryClient_getPortDescription(const KiottyDiscoveryResult* result, size_t index);

#endif // __KIOTTY_DISCOVERY_CLIENT__
