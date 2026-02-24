#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <thread>

#include "kiotty_discovery_client.hpp"
#include "kiotty_discovery_server.hpp"

// Discovery UDP port used by this test
static constexpr uint16_t DISCOVERY_PORT = 50001;

// Expected endpoints registered before the server starts
struct ExpectedEndpoint { uint32_t port; const char* description; };
static const ExpectedEndpoint EXPECTED[] = {
    {8080, "HTTP API"},
    {9090, "gRPC"},
    {5432, "Database"},
};
static constexpr uint32_t EXPECTED_COUNT = 3;

int main() {
    bool test_ok = true;

    // -----------------------------------------------------------------------
    // Create server and register ports BEFORE opening (requirement 3)
    // -----------------------------------------------------------------------
    KiottyDiscoveryServer* server = KiottyDiscoveryServer_createServer(DISCOVERY_PORT);

    for (const auto& ep : EXPECTED) {
        if (!KiottyDiscoveryServer_addPort(server, ep.port, ep.description)) {
            printf("[FAIL] Could not add port %u\n", ep.port);
            test_ok = false;
        }
    }

    printf("[INFO] Server IP  : %s\n", KiottyDiscoveryServer_getIpAddress(server));
    printf("[INFO] Endpoints  : %u registered\n", KiottyDiscoveryServer_getNumOfEndpoints(server));

    // -----------------------------------------------------------------------
    // Thread 1 — run server (requirement 1)
    // -----------------------------------------------------------------------
    std::thread server_thread([&] {
        if (!KiottyDiscoveryServer_openServer(server)) {
            printf("[FAIL] KiottyDiscoveryServer_openServer() failed\n");
            test_ok = false;
            return;
        }
        KiottyDiscoveryServer_awaitServer(server);
        printf("[INFO] Server thread: server stopped.\n");
    });

    // Give the server socket a moment to bind
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    // -----------------------------------------------------------------------
    // Thread 2 — print server messages (requirement 2)
    // -----------------------------------------------------------------------
    std::atomic<bool> msg_loop_running {true};
    std::thread message_thread([&] {
        while (msg_loop_running.load()) {
            KiottyDiscoveryServerMessage* msg =
                KiottyDiscoveryServer_awaitMessage(server, 1000 /*ms*/);
            if (msg) {
                printf("[MSG ] %s\n", KiottyDiscoveryServer_getMessage(msg));
            }
        }
    });

    // -----------------------------------------------------------------------
    // Client discovery (requirement 4)
    // -----------------------------------------------------------------------
    printf("[INFO] Discovering server on port %u ...\n", DISCOVERY_PORT);
    KiottyDiscoveryResult* result = KiottyDiscoveryClient_discoverServer(DISCOVERY_PORT);

    if (!result) {
        printf("[FAIL] discoverServer() returned nullptr\n");
        test_ok = false;
    } else {
        // Verify IP address
        const char* ip = KiottyDiscoveryClient_getIpAddress(result);
        printf("[INFO] Discovered IP  : %s\n", ip);
        if (!ip || ip[0] == '\0') {
            printf("[FAIL] IP address is empty\n");
            test_ok = false;
        }

        // Verify endpoint count
        uint32_t n = KiottyDiscoveryClient_getNumOfEndpoints(result);
        printf("[INFO] num_of_endpoints: %u\n", n);
        if (n != EXPECTED_COUNT) {
            printf("[FAIL] Expected %u endpoints, got %u\n", EXPECTED_COUNT, n);
            test_ok = false;
        }

        // Verify each port and description
        uint32_t check = (n < EXPECTED_COUNT) ? n : EXPECTED_COUNT;
        for (uint32_t i = 0; i < check; ++i) {
            uint32_t    port = KiottyDiscoveryClient_getPort(result, i);
            const char* desc = KiottyDiscoveryClient_getPortDescription(result, i);
            printf("[INFO]   [%u] port=%-5u  desc=%s\n", i, port, desc ? desc : "<null>");

            if (port != EXPECTED[i].port) {
                printf("[FAIL]   Port mismatch at index %u: expected %u, got %u\n",
                       i, EXPECTED[i].port, port);
                test_ok = false;
            }
            if (!desc || ::strncmp(desc, EXPECTED[i].description, 11) != 0) {
                printf("[FAIL]   Description mismatch at index %u: expected \"%s\", got \"%s\"\n",
                       i, EXPECTED[i].description, desc ? desc : "<null>");
                test_ok = false;
            }
        }

        // result points to thread-local storage — no delete needed
    }

    // Allow the message thread to print any pending messages
    std::this_thread::sleep_for(std::chrono::milliseconds(300));

    // -----------------------------------------------------------------------
    // Shutdown
    // -----------------------------------------------------------------------
    msg_loop_running = false;
    KiottyDiscoveryServer_cancelMessage(server);  // unblock awaitMessage
    message_thread.join();

    KiottyDiscoveryServer_closeServer(server);    // signal server thread to stop
    server_thread.join();

    KiottyDiscoveryServer_releaseServer(server);  // cleanup (deletes server)

    printf("\n%s\n", test_ok ? "=== TEST PASSED ===" : "=== TEST FAILED ===");
    return test_ok ? 0 : 1;
}
