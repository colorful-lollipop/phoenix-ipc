cmake_minimum_required(VERSION 3.16)
project(PhoenixIPC VERSION 1.0.0 LANGUAGES CXX)

# C++17 standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Export compile commands for IDE support
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Build type
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Build type" FORCE)
endif()

# Find GoogleTest
find_package(GTest REQUIRED)

# Core library - Header-only template library
add_library(phoenix_core INTERFACE)
target_include_directories(phoenix_core INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# Main PhoenixIPC library - Header-only
add_library(phoenix_ipc INTERFACE)
target_include_directories(phoenix_ipc INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_link_libraries(phoenix_ipc INTERFACE
    phoenix_core
)

# PhoenixIPC implementation library (static library for SPSC producer/consumer)
add_library(phoenix_ipc_impl STATIC
    src/ipc/phoenix_producer.cpp
    src/ipc/phoenix_consumer.cpp
    src/coordination/coordination_file.cpp
)

target_include_directories(phoenix_ipc_impl PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_link_libraries(phoenix_ipc_impl PRIVATE
    phoenix_ipc
    pthread
)

# Producer example
add_executable(simple_producer
    examples/simple_producer.cpp
)

target_include_directories(simple_producer PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_link_libraries(simple_producer PRIVATE
    phoenix_ipc
    phoenix_ipc_impl
)

# Consumer example
add_executable(simple_consumer
    examples/simple_consumer.cpp
)

target_include_directories(simple_consumer PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_link_libraries(simple_consumer PRIVATE
    phoenix_ipc
    phoenix_ipc_impl
)

# Executables for testing
add_executable(unit_tests
    test/unit_tests.cpp
)

target_include_directories(unit_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_link_libraries(unit_tests PRIVATE
    phoenix_ipc
    GTest::GTest
    GTest::Main
)

add_executable(integration_hello_world
    examples/hello_world.cpp
)

target_include_directories(integration_hello_world PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_link_libraries(integration_hello_world PRIVATE
    phoenix_ipc
)

# Stress and concurrency tests
add_executable(stress_test
    test/stress_test.cpp
)

target_include_directories(stress_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_link_libraries(stress_test PRIVATE
    phoenix_ipc
    GTest::GTest
    GTest::Main
    pthread
)

# CTest configuration
enable_testing()
add_test(NAME unit_tests COMMAND unit_tests)
add_test(NAME hello_world COMMAND integration_hello_world)
add_test(NAME stress_test COMMAND stress_test)

# Installation (optional)
install(TARGETS phoenix_ipc
    ARCHIVE DESTINATION lib
    LIBRARY DESTINATION lib
)

install(DIRECTORY include/phoenix
    DESTINATION include
)
