# tests/CMakeLists.txt
# GoogleTest integration for T113i daemon – unit, performance, and integration tests.

cmake_minimum_required(VERSION 3.10)

# ---------------------------------------------------------------------------
# GoogleTest dependency (fetched via FetchContent if not found in system)
# ---------------------------------------------------------------------------
include(FetchContent)

FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG        v1.14.0
)

# Prevent overriding the parent project's compiler/linker settings (Windows)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

enable_testing()
include(GoogleTest)

# ---------------------------------------------------------------------------
# Common compile options
# ---------------------------------------------------------------------------
set(TEST_COMPILE_OPTIONS -Wall -Wextra -O0 -g)

# ---------------------------------------------------------------------------
# Shared library target built from daemon sources (excludes main.cpp)
# ---------------------------------------------------------------------------
set(DAEMON_SOURCES
    ${CMAKE_SOURCE_DIR}/src/config_loader.cpp
    ${CMAKE_SOURCE_DIR}/src/serial_comm.cpp
    ${CMAKE_SOURCE_DIR}/src/gpio_controller.cpp
    ${CMAKE_SOURCE_DIR}/src/sensor_reader.cpp
    ${CMAKE_SOURCE_DIR}/src/power_manager.cpp
    ${CMAKE_SOURCE_DIR}/src/input_handler.cpp
    ${CMAKE_SOURCE_DIR}/src/task_executor.cpp
    ${CMAKE_SOURCE_DIR}/src/logger.cpp
    ${CMAKE_SOURCE_DIR}/src/connection_monitor.cpp
    ${CMAKE_SOURCE_DIR}/src/shutdown_manager.cpp
)

add_library(daemon_testlib STATIC ${DAEMON_SOURCES})
target_include_directories(daemon_testlib PUBLIC ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(daemon_testlib
    PUBLIC Threads::Threads rt m)
target_compile_options(daemon_testlib PRIVATE ${TEST_COMPILE_OPTIONS})

# ---------------------------------------------------------------------------
# Helper macro to register a test executable
# ---------------------------------------------------------------------------
macro(add_daemon_test TEST_NAME TEST_SRC)
    add_executable(${TEST_NAME} ${TEST_SRC})
    target_include_directories(${TEST_NAME} PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/mocks)
    target_link_libraries(${TEST_NAME}
        PRIVATE daemon_testlib GTest::gtest_main GTest::gmock_main)
    target_compile_options(${TEST_NAME} PRIVATE ${TEST_COMPILE_OPTIONS})
    gtest_discover_tests(${TEST_NAME})
endmacro()

# ---------------------------------------------------------------------------
# Unit tests
# ---------------------------------------------------------------------------
add_daemon_test(test_logger           unit/test_logger.cpp)
add_daemon_test(test_connection_monitor unit/test_connection_monitor.cpp)
add_daemon_test(test_task_manager     unit/test_task_manager.cpp)
add_daemon_test(test_module_initializer unit/test_module_initializer.cpp)
add_daemon_test(test_config_loader    unit/test_config_loader.cpp)
add_daemon_test(test_protocol         unit/test_protocol.cpp)
add_daemon_test(test_utils            unit/test_utils.cpp)

# ---------------------------------------------------------------------------
# Performance tests (standalone executables, not gtest)
# ---------------------------------------------------------------------------
macro(add_perf_test PERF_NAME PERF_SRC)
    add_executable(${PERF_NAME} ${PERF_SRC})
    target_include_directories(${PERF_NAME} PRIVATE
        ${CMAKE_SOURCE_DIR}/include)
    target_link_libraries(${PERF_NAME}
        PRIVATE daemon_testlib)
    target_compile_options(${PERF_NAME} PRIVATE -O2 -Wall)
endmacro()

add_perf_test(perf_main_loop      perf/perf_main_loop.cpp)
add_perf_test(perf_task_execution perf/perf_task_execution.cpp)
add_perf_test(perf_memory         perf/perf_memory.cpp)
add_perf_test(perf_logging        perf/perf_logging.cpp)

# ---------------------------------------------------------------------------
# Integration tests (require network; run with ctest -L integration)
# ---------------------------------------------------------------------------
macro(add_integration_test INT_NAME INT_SRC)
    add_executable(${INT_NAME} ${INT_SRC})
    target_include_directories(${INT_NAME} PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/mocks)
    target_link_libraries(${INT_NAME}
        PRIVATE daemon_testlib GTest::gtest_main GTest::gmock_main)
    target_compile_options(${INT_NAME} PRIVATE ${TEST_COMPILE_OPTIONS})
    gtest_discover_tests(${INT_NAME} PROPERTIES LABELS "integration")
endmacro()

add_integration_test(integration_test_tcp_loopback
    integration/integration_test_tcp_loopback.cpp)
add_integration_test(integration_test_error_recovery
    integration/integration_test_error_recovery.cpp)
add_integration_test(integration_test_long_running
    integration/integration_test_long_running.cpp)
add_integration_test(integration_test_protocol_compliance
    integration/integration_test_protocol_compliance.cpp)

# ---------------------------------------------------------------------------
# Coverage target (lcov)
# ---------------------------------------------------------------------------
find_program(LCOV_PATH lcov)
find_program(GENHTML_PATH genhtml)
if(LCOV_PATH AND GENHTML_PATH)
    add_custom_target(coverage
        COMMAND ${LCOV_PATH} --capture --directory ${CMAKE_BINARY_DIR}
                --output-file coverage.info
                --ignore-errors mismatch
        COMMAND ${LCOV_PATH} --remove coverage.info
                '/usr/*' '*/googletest/*' '*/tests/*'
                --output-file coverage.info
        COMMAND ${GENHTML_PATH} coverage.info
                --output-directory ${CMAKE_BINARY_DIR}/coverage_html
        COMMENT "Generating HTML coverage report in coverage_html/"
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    )
endif()
