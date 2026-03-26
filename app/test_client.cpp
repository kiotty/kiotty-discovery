
#include "kiotty_discovery_client.hpp"
#include <cstdint>
#include <cstdio>
#include <thread>

int main()
{
    uint16_t port;
    printf("Set the discovery port\n");
    scanf("%u", &port);

    do {
        auto* result = KiottyDiscoveryClient_discoverServer(port);
        if (result) {
            const char* ip = KiottyDiscoveryClient_getIpAddress(result);
            printf("[INFO] Discovered IP  : %s\n", ip);
            uint32_t n = KiottyDiscoveryClient_getNumOfEndpoints(result);
            printf("[INFO] num_of_endpoints: %u\n", n);
            for (uint32_t i = 0; i < n; ++i) {
                uint32_t    port = KiottyDiscoveryClient_getPort(result, i);
                const char* desc = KiottyDiscoveryClient_getPortDescription(result, i);
                printf("[INFO]   [%u] port=%-5u  desc=%s\n", i, port, desc ? desc : "<null>");
            }

            // Allow the message thread to print any pending messages
            std::this_thread::sleep_for(std::chrono::milliseconds(300));
        }
    } while(true);

    return 0;
}