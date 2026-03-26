add_executable(kiotty_discovery_client_app test_client.cpp)
target_link_libraries(kiotty_discovery_client_app
    PRIVATE kiotty_discovery_client
)
