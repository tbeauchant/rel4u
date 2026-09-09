cmake_minimum_required(VERSION 3.16)
project(rel4u VERSION 1.0.0 LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

# Compiler Warnings & Flags
if(MSVC)
    add_compile_options(/W4 /WX /permissive- /experimental:c11atomics)
else()
    add_compile_options(-Wall -Wextra -Wpedantic -Werror -Wno-unused-parameter)
endif()

# Include directories
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# Platform Libraries
set(REL4U_PLATFORM_LIBS "")
if(WIN32)
    list(APPEND REL4U_PLATFORM_LIBS ws2_32)
else()
    find_package(Threads REQUIRED)
    list(APPEND REL4U_PLATFORM_LIBS Threads::Threads)
    find_library(LIBRT rt)
    if(LIBRT)
        list(APPEND REL4U_PLATFORM_LIBS ${LIBRT})
    endif()
endif()

# Core library source files
set(REL4U_SOURCES
    src/platform/rel4u_time.c
    src/platform/rel4u_threads.c
    src/platform/rel4u_net.c
    src/core/rel4u_mpmc.c
    src/core/rel4u_packet.c
    src/core/rel4u_rtt.c
    src/core/rel4u_window.c
    src/core/rel4u_session.c
    src/core/rel4u_client.c
    src/core/rel4u_server.c
)

# Default build type
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Choose the type of build (Debug or Release)" FORCE)
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS "Debug" "Release" "MinSizeRel" "RelWithDebInfo")
endif()

# Core static library target
add_library(rel4u_static STATIC ${REL4U_SOURCES})
set_target_properties(rel4u_static PROPERTIES
    OUTPUT_NAME "rel4u"
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/static"
    POSITION_INDEPENDENT_CODE ON
)
target_compile_definitions(rel4u_static PUBLIC REL4U_STATIC)
target_link_libraries(rel4u_static PUBLIC ${REL4U_PLATFORM_LIBS})
target_include_directories(rel4u_static PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

# Core shared library target
add_library(rel4u_shared SHARED ${REL4U_SOURCES})
set_target_properties(rel4u_shared PROPERTIES
    OUTPUT_NAME "rel4u"
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/shared"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/shared"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/shared"
    POSITION_INDEPENDENT_CODE ON
)
target_compile_definitions(rel4u_shared PRIVATE REL4U_BUILDING_DLL)
target_link_libraries(rel4u_shared PUBLIC ${REL4U_PLATFORM_LIBS})
target_include_directories(rel4u_shared PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

# Target aliases
add_library(rel4u::static ALIAS rel4u_static)
add_library(rel4u::shared ALIAS rel4u_shared)
add_library(rel4u::rel4u ALIAS rel4u_static)

# Enable testing
enable_testing()

# Unit Tests
add_executable(test_mpmc tests/unit/test_mpmc.c)
target_link_libraries(test_mpmc PRIVATE rel4u_static)
add_test(NAME test_mpmc COMMAND test_mpmc)

add_executable(test_packet tests/unit/test_packet.c)
target_link_libraries(test_packet PRIVATE rel4u_static)
add_test(NAME test_packet COMMAND test_packet)

add_executable(test_rtt tests/unit/test_rtt.c)
target_link_libraries(test_rtt PRIVATE rel4u_static)
add_test(NAME test_rtt COMMAND test_rtt)

add_executable(test_window tests/unit/test_window.c)
target_link_libraries(test_window PRIVATE rel4u_static)
add_test(NAME test_window COMMAND test_window)

# Integration Tests
add_executable(test_integration
    tests/integration/test_integration.c
    tests/framework/network_sim.c
)
target_link_libraries(test_integration PRIVATE rel4u_static)
add_test(NAME test_integration COMMAND test_integration)

# Examples
add_executable(server_echo examples/server_echo.c)
target_link_libraries(server_echo PRIVATE rel4u_static)

add_executable(client_bench examples/client_bench.c)
target_link_libraries(client_bench PRIVATE rel4u_static)

