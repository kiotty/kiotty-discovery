add_executable(integration integration.cpp)
target_link_libraries(integration
    PRIVATE kiotty_discovery_server
            kiotty_discovery_client
)
