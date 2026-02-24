cmake_minimum_required(VERSION 3.14)
project(kiotty_discovery VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ---------------------------------------------------------------------------
# Options
# ---------------------------------------------------------------------------
option(KIOTTY_DISCOVERY_BUILD_SHARED "Build as shared library instead of static" OFF)

# Build tests only when this is the top-level project (not via FetchContent)
if(CMAKE_PROJECT_NAME STREQUAL PROJECT_NAME)
    option(KIOTTY_DISCOVERY_BUILD_TESTS "Build test programs" ON)
else()
    option(KIOTTY_DISCOVERY_BUILD_TESTS "Build test programs" OFF)
endif()

# ---------------------------------------------------------------------------
# Library type
# ---------------------------------------------------------------------------
if(KIOTTY_DISCOVERY_BUILD_SHARED)
    set(KIOTTY_LIB_TYPE SHARED)
else()
    set(KIOTTY_LIB_TYPE STATIC)
endif()

# ---------------------------------------------------------------------------
# Server library
# ---------------------------------------------------------------------------
add_library(kiotty_discovery_server ${KIOTTY_LIB_TYPE}
    src/server/kiotty_discover_server.cpp
)
target_include_directories(kiotty_discovery_server
    PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
)
if(KIOTTY_DISCOVERY_BUILD_SHARED)
    target_compile_definitions(kiotty_discovery_server
        PRIVATE KIOTTY_DISCOVERY_SERVER_EXPORTS
    )
endif()
if(WIN32)
    target_link_libraries(kiotty_discovery_server PRIVATE ws2_32)
    target_compile_definitions(kiotty_discovery_server PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()

# ---------------------------------------------------------------------------
# Client library
# ---------------------------------------------------------------------------
add_library(kiotty_discovery_client ${KIOTTY_LIB_TYPE}
    src/client/kiotty_discover_client.cpp
)
target_include_directories(kiotty_discovery_client
    PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
)
if(KIOTTY_DISCOVERY_BUILD_SHARED)
    target_compile_definitions(kiotty_discovery_client
        PRIVATE KIOTTY_DISCOVERY_CLIENT_EXPORTS
    )
endif()
if(WIN32)
    target_link_libraries(kiotty_discovery_client PRIVATE ws2_32)
endif()

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
if(KIOTTY_DISCOVERY_BUILD_TESTS)
    add_subdirectory(test)
endif()
