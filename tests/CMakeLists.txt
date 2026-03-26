# tests/CMakeLists.txt – GoogleTest-based test suite for the T113i daemon
#
# Unit tests:       tests/unit/test_*.cpp
# Integration tests: tests/integration/integration_test_*.cpp
# Performance tests: tests/perf/perf_*.cpp  (standalone executables)
#
# Usage:
#   cd build && cmake .. -DBUILD_TESTS=ON && make && ctest --output-on-failure

cmake_minimum_required(VERSION 3.10)
project(seeway_interface_tests)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -O1 -g")

# ---------------------------------------------------------------------------
# Paths
# ---------------------------------------------------------------------------
set(DAEMON_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/..")
set(DAEMON_INCLUDE "${DAEMON_ROOT}/t113i_daemon/include")
set(DAEMON_SRC     "${DAEMON_ROOT}/t113i_daemon/src")

# ---------------------------------------------------------------------------
# GoogleTest
# ---------------------------------------------------------------------------
find_package(GTest QUIET)

if(NOT GTest_FOUND)
    # Try to fetch via FetchContent if not installed
    include(FetchContent)
    FetchContent_Declare(
        googletest
        URL https://github.com/google/googletest/archive/v1.14.0.tar.gz
        URL_HASH SHA256=8ad598c73ad796e0d8280b082cebd82a630d73e73cd3c70057938a6501bba5d7
    )
    FetchContent_MakeAvailable(googletest)
    set(GTEST_LIBRARIES gtest gtest_main)
else()
    set(GTEST_LIBRARIES GTest::GTest GTest::Main)
endif()

find_package(Threads REQUIRED)

# ---------------------------------------------------------------------------
# Daemon source files (shared by all test targets – excludes main.cpp)
# ---------------------------------------------------------------------------
set(DAEMON_SOURCES
    ${DAEMON_SRC}/config_loader.cpp
    ${DAEMON_SRC}/serial_comm.cpp
    ${DAEMON_SRC}/gpio_controller.cpp
    ${DAEMON_SRC}/sensor_reader.cpp
    ${DAEMON_SRC}/power_manager.cpp
    ${DAEMON_SRC}/input_handler.cpp
    ${DAEMON_SRC}/task_executor.cpp
    ${DAEMON_SRC}/logger.cpp
    ${DAEMON_SRC}/connection_monitor.cpp
    ${DAEMON_SRC}/shutdown_manager.cpp
)

# ---------------------------------------------------------------------------
# Helper: add a unit/integration test target
# ---------------------------------------------------------------------------
function(add_gtest_target TARGET_NAME)
    cmake_parse_arguments(ARG "" "" "SOURCES;EXTRA_SOURCES" ${ARGN})

    add_executable(${TARGET_NAME}
        ${ARG_SOURCES}
        ${ARG_EXTRA_SOURCES}
        ${DAEMON_SOURCES}
    )

    target_include_directories(${TARGET_NAME} PRIVATE
        ${DAEMON_INCLUDE}
    )

    target_link_libraries(${TARGET_NAME} PRIVATE
        ${GTEST_LIBRARIES}
        Threads::Threads
        rt
        m
    )

    add_test(NAME ${TARGET_NAME} COMMAND ${TARGET_NAME})
endfunction()

# ---------------------------------------------------------------------------
# Unit tests
# ---------------------------------------------------------------------------
enable_testing()

add_gtest_target(test_logger
    SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/unit/test_logger.cpp
)

add_gtest_target(test_connection_monitor
    SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/unit/test_connection_monitor.cpp
)

add_gtest_target(test_task_manager
    SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/unit/test_task_manager.cpp
)

add_gtest_target(test_module_initializer
    SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/unit/test_module_initializer.cpp
)

add_gtest_target(test_config_loader
    SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/unit/test_config_loader.cpp
)

add_gtest_target(test_protocol
    SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/unit/test_protocol.cpp
)

add_gtest_target(test_utils
    SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/unit/test_utils.cpp
)

# ---------------------------------------------------------------------------
# Integration tests
# ---------------------------------------------------------------------------
add_gtest_target(integration_test_tcp_loopback
    SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/integration/integration_test_tcp_loopback.cpp
)

add_gtest_target(integration_test_error_recovery
    SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/integration/integration_test_error_recovery.cpp
)

add_gtest_target(integration_test_long_running
    SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/integration/integration_test_long_running.cpp
)

add_gtest_target(integration_test_protocol_compliance
    SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/integration/integration_test_protocol_compliance.cpp
)

# ---------------------------------------------------------------------------
# Performance tests (standalone executables, not gtest)
# ---------------------------------------------------------------------------
function(add_perf_target TARGET_NAME SRC_FILE)
    add_executable(${TARGET_NAME} ${SRC_FILE} ${DAEMON_SOURCES})
    target_include_directories(${TARGET_NAME} PRIVATE ${DAEMON_INCLUDE})
    target_link_libraries(${TARGET_NAME} PRIVATE Threads::Threads rt m)
endfunction()

add_perf_target(perf_main_loop
    ${CMAKE_CURRENT_SOURCE_DIR}/perf/perf_main_loop.cpp)

add_perf_target(perf_task_execution
    ${CMAKE_CURRENT_SOURCE_DIR}/perf/perf_task_execution.cpp)

add_perf_target(perf_memory
    ${CMAKE_CURRENT_SOURCE_DIR}/perf/perf_memory.cpp)

add_perf_target(perf_logging
    ${CMAKE_CURRENT_SOURCE_DIR}/perf/perf_logging.cpp)

# ---------------------------------------------------------------------------
# Code coverage (optional, requires GCC/lcov)
# ---------------------------------------------------------------------------
option(ENABLE_COVERAGE "Enable code coverage instrumentation" OFF)
if(ENABLE_COVERAGE)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU")
        set(COVERAGE_FLAGS "--coverage -fprofile-arcs -ftest-coverage")
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${COVERAGE_FLAGS}")
        set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} --coverage")
        message(STATUS "Code coverage enabled")
    else()
        message(WARNING "Coverage requires GCC; ignoring ENABLE_COVERAGE")
    endif()
endif()
