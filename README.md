# kiotty-discovery

A lightweight C++ library for UDP-based service discovery. A server advertises its service endpoints (port + description); any client on the same network can broadcast a discovery request and receive the server's IP address and registered endpoint list in a single round-trip.

---

## Purpose

Many embedded and desktop applications need to locate a companion service at runtime without hardcoding IP addresses. `kiotty-discovery` solves this by:

1. Having the **server** bind to a well-known UDP port and listen for broadcast discovery requests.
2. Having the **client** broadcast a `KiottyDiscoveryRequest` packet to the local network and wait up to 3 seconds for a `KiottyDiscoveryResponse`.
3. Returning the server's IP address and all registered service endpoints to the caller.

The library is split into two independent static libraries so that server-side and client-side code can be linked separately.

---

## API

### Server library (`kiotty_discovery_server.hpp`)

#### Lifecycle

```cpp
// Create a server that will listen on discovery_port for UDP broadcast requests.
KiottyDiscoveryServer* KiottyDiscoveryServer_createServer(uint16_t discovery_port);

// Start the discovery listener in a background thread.
// Returns false if the socket cannot be bound.
bool KiottyDiscoveryServer_openServer(KiottyDiscoveryServer* server);

// Block until the background thread has stopped (use after closeServer()).
void KiottyDiscoveryServer_awaitServer(KiottyDiscoveryServer* server);

// Signal the background thread to stop (non-blocking, safe to call from any thread).
void KiottyDiscoveryServer_closeServer(KiottyDiscoveryServer* server);

// Close the server, join the background thread, and free all resources.
// Equivalent to closeServer() + awaitServer() + cleanup.
void KiottyDiscoveryServer_releaseServer(KiottyDiscoveryServer* server);
```

#### Endpoint management

Must be called **before** `openServer()` for the initial set; may also be called while the server is running.

```cpp
// Add a service endpoint. description is truncated to 11 chars.
// Returns false if the endpoint table is full (max 63).
bool        KiottyDiscoveryServer_addPort(KiottyDiscoveryServer* server,
                                          uint32_t port, const char* description);

void        KiottyDiscoveryServer_removePort(KiottyDiscoveryServer* server, size_t index);
const char* KiottyDiscoveryServer_getIpAddress(KiottyDiscoveryServer* server);
uint32_t    KiottyDiscoveryServer_getNumOfEndpoints(KiottyDiscoveryServer* server);
uint32_t    KiottyDiscoveryServer_getPort(KiottyDiscoveryServer* server, size_t index);
const char* KiottyDiscoveryServer_getPortDescription(KiottyDiscoveryServer* server, size_t index);
```

#### Message queue

Each successful discovery request from a client pushes a message onto an internal queue. Consume messages from a dedicated thread.

```cpp
// Block until a message arrives or timeout_ms elapses.
// Returns nullptr on timeout or after cancelMessage().
// The returned pointer is valid until the next call to awaitMessage on this thread.
KiottyDiscoveryServerMessage* KiottyDiscoveryServer_awaitMessage(KiottyDiscoveryServer* server,
                                                                  uint64_t timeout_ms);

// Retrieve the text content of a message (e.g. "Discovered by 192.168.0.10").
const char* KiottyDiscoveryServer_getMessage(KiottyDiscoveryServerMessage* message);

// Unblock any thread currently waiting in awaitMessage().
void KiottyDiscoveryServer_cancelMessage(KiottyDiscoveryServer* server);
```

---

### Client library (`kiotty_discovery_client.hpp`)

```cpp
// Broadcast a discovery request on discovery_port and return the first response.
// Blocks for up to 3 seconds. Returns nullptr on timeout or error.
// The returned pointer points to thread-local storage — do NOT delete it.
// It remains valid until the next call to discoverServer() on the same thread.
KiottyDiscoveryResult* KiottyDiscoveryClient_discoverServer(uint16_t discovery_port);

const char* KiottyDiscoveryClient_getIpAddress(const KiottyDiscoveryResult* result);
uint32_t    KiottyDiscoveryClient_getNumOfEndpoints(const KiottyDiscoveryResult* result);
uint32_t    KiottyDiscoveryClient_getPort(const KiottyDiscoveryResult* result, size_t index);
const char* KiottyDiscoveryClient_getPortDescription(const KiottyDiscoveryResult* result, size_t index);
```

`KiottyDiscoveryResult` layout:

```cpp
struct KiottyDiscoveryResult {
    char     ip_address[INET_ADDRSTRLEN];
    uint32_t num_of_endpoints;
    KiottyDiscoveryEndpoint endpoints[KIOTTY_DISCOVERY_MAX_ENDPOINTS];
};
```

---

## Constraints

| Constraint | Value |
|---|---|
| Max endpoints per server | 63 |
| Endpoint description length | 11 chars (+ null terminator) |
| Discovery response max size | 1024 bytes |
| Client receive timeout | 3 seconds (hardcoded) |
| Protocol | UDP (IPv4 only) |
| C++ standard | C++17 or later |
| Platforms | Windows (WinSock2), Linux, Android |
| Thread safety | Server and client functions are safe to call from different threads. `awaitMessage` and `discoverServer` return pointers to **per-thread** storage. |

---

## Build

### Prerequisites

- CMake ≥ 3.14
- C++17-capable compiler (MSVC 2019+, GCC 9+, Clang 9+)
- On Windows: `ws2_32` is linked automatically

### Standalone build (includes integration test)

```sh
cmake -S . -B build
cmake --build build --config Release
./build/test/Release/integration   # Linux / macOS
build\test\Release\integration.exe # Windows
```

### Options

| CMake option | Default | Description |
|---|---|---|
| `KIOTTY_DISCOVERY_BUILD_SHARED` | `OFF` | Build shared libraries instead of static |
| `KIOTTY_DISCOVERY_BUILD_TESTS` | `ON` (top-level) / `OFF` (FetchContent) | Build the integration test |

---

## FetchContent integration

```cmake
include(FetchContent)

FetchContent_Declare(
    kiotty_discovery
    GIT_REPOSITORY https://github.com/your-org/kiotty-discovery.git
    GIT_TAG        main   # or a specific tag/commit
)
FetchContent_MakeAvailable(kiotty_discovery)

# Link against whichever libraries you need
target_link_libraries(my_server_app PRIVATE kiotty_discovery_server)
target_link_libraries(my_client_app PRIVATE kiotty_discovery_client)
```

Headers are exposed automatically via `target_include_directories(PUBLIC ...)` — no manual include path configuration needed.

---

## Typical usage pattern

### Server

```cpp
#include "kiotty_discovery_server.hpp"
#include <thread>

int main() {
    auto* srv = KiottyDiscoveryServer_createServer(50001);
    KiottyDiscoveryServer_addPort(srv, 8080, "HTTP API");
    KiottyDiscoveryServer_addPort(srv, 9090, "gRPC");

    // Thread 1: run the discovery listener
    std::thread srv_thread([&] {
        KiottyDiscoveryServer_openServer(srv);
        KiottyDiscoveryServer_awaitServer(srv);
    });

    // Thread 2: consume discovery events
    std::thread msg_thread([&] {
        while (true) {
            auto* msg = KiottyDiscoveryServer_awaitMessage(srv, 5000);
            if (!msg) break;
            printf("%s\n", KiottyDiscoveryServer_getMessage(msg));
        }
    });

    // ... application logic ...

    KiottyDiscoveryServer_cancelMessage(srv);
    msg_thread.join();
    KiottyDiscoveryServer_closeServer(srv);
    srv_thread.join();
    KiottyDiscoveryServer_releaseServer(srv);
}
```

### Client

```cpp
#include "kiotty_discovery_client.hpp"
#include <cstdio>

int main() {
    auto* result = KiottyDiscoveryClient_discoverServer(50001);
    if (!result) { puts("no server found"); return 1; }

    printf("server: %s  endpoints: %u\n",
           KiottyDiscoveryClient_getIpAddress(result),
           KiottyDiscoveryClient_getNumOfEndpoints(result));

    for (uint32_t i = 0; i < KiottyDiscoveryClient_getNumOfEndpoints(result); ++i) {
        printf("  [%u] %u  %s\n", i,
               KiottyDiscoveryClient_getPort(result, i),
               KiottyDiscoveryClient_getPortDescription(result, i));
    }
    // result points to thread-local storage — do NOT delete
}
```
