#if !defined(__KIOTTY_DISCOVERY_SERVER__)
#define __KIOTTY_DISCOVERY_SERVER__

#if defined(_WIN32) || defined(_WIN64) // Windows
    #ifdef KIOTTY_DISCOVERY_SERVER_EXPORTS
        #define KIOTTY_DISCOVERY_SERVER_API __declspec(dllexport)
    #else
        #define KIOTTY_DISCOVERY_SERVER_API /*__declspec(dllimport)*/
    #endif
#elif defined(__linux__) || defined(__unix__) || defined(__ANDROID__) // Linux / Android
    #ifdef KIOTTY_DISCOVERY_SERVER_EXPORTS
        #define KIOTTY_DISCOVERY_SERVER_API __attribute__((visibility("default")))
    #else
        #define KIOTTY_DISCOVERY_SERVER_API
    #endif
#else
    #define KIOTTY_DISCOVERY_SERVER_API
    #pragma warning Unknown dynamic link import/export semantics.
#endif

#include <cstdint>
#include <cstddef>

struct KiottyDiscoveryServer;
struct KiottyDiscoveryServerMessage;


KIOTTY_DISCOVERY_SERVER_API KiottyDiscoveryServer* KiottyDiscoveryServer_createServer(uint16_t discovery_port);
// description cannot exceed 14 byte. so it automatically truncated to 13 bytes or less.
// cannot add port if the num of endpoints exceeds max endpoints
KIOTTY_DISCOVERY_SERVER_API bool KiottyDiscoveryServer_addPort(KiottyDiscoveryServer* server, uint32_t port, const char* description);
KIOTTY_DISCOVERY_SERVER_API const char* KiottyDiscoveryServer_getIpAddress(KiottyDiscoveryServer* server);
KIOTTY_DISCOVERY_SERVER_API uint32_t KiottyDiscoveryServer_getNumOfEndpoints(KiottyDiscoveryServer* server);
KIOTTY_DISCOVERY_SERVER_API uint32_t KiottyDiscoveryServer_getPort(KiottyDiscoveryServer* server, size_t index);
KIOTTY_DISCOVERY_SERVER_API const char* KiottyDiscoveryServer_getPortDescription(KiottyDiscoveryServer* server, size_t index);
KIOTTY_DISCOVERY_SERVER_API void KiottyDiscoveryServer_removePort(KiottyDiscoveryServer* server, size_t index);


// await message from server thread. if timeout, return nullptr.
KIOTTY_DISCOVERY_SERVER_API KiottyDiscoveryServerMessage* KiottyDiscoveryServer_awaitMessage(KiottyDiscoveryServer* server, uint64_t timeout_ms);
// The returned string pointer is valid until the next call to awaitMessage on the same thread.
KIOTTY_DISCOVERY_SERVER_API const char* KiottyDiscoveryServer_getMessage(KiottyDiscoveryServerMessage* message);
KIOTTY_DISCOVERY_SERVER_API void KiottyDiscoveryServer_cancelMessage(KiottyDiscoveryServer* server);


// Open Server in another thread.
KIOTTY_DISCOVERY_SERVER_API bool KiottyDiscoveryServer_openServer(KiottyDiscoveryServer* server);
// await until the server is closed by thread called close server
KIOTTY_DISCOVERY_SERVER_API void KiottyDiscoveryServer_awaitServer(KiottyDiscoveryServer* server);
// close server. it can be called in another thread that gives signal to openServer calling thread.
KIOTTY_DISCOVERY_SERVER_API void KiottyDiscoveryServer_closeServer(KiottyDiscoveryServer* server);

// if the server thread is opened, give close signal to server thread and join it.
// and next, if the thread server thread is closed, release server resource.
KIOTTY_DISCOVERY_SERVER_API void KiottyDiscoveryServer_releaseServer(KiottyDiscoveryServer* server);

#endif // __KIOTTY_DISCOVERY_SERVER__
