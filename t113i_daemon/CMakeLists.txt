# CMakeLists.txt for T113i daemon

cmake_minimum_required(VERSION 3.10)
project(seeway_interface_daemon)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -O2")

# ---------------------------------------------------------------------------
# Cross-compilation toolchain support
# Pass -DARM_TOOLCHAIN=path/to/toolchain.cmake on the cmake command line when
# cross-compiling for arm-linux-gnueabihf.
# ---------------------------------------------------------------------------
if(DEFINED ARM_TOOLCHAIN)
    include(${ARM_TOOLCHAIN})
endif()

find_package(Threads REQUIRED)

include_directories(include)

# ---------------------------------------------------------------------------
# Source files
# ---------------------------------------------------------------------------
set(SOURCES
    src/config_loader.cpp
    src/serial_comm.cpp
    src/gpio_controller.cpp
    src/sensor_reader.cpp
    src/power_manager.cpp
    src/input_handler.cpp
    src/task_executor.cpp
    src/logger.cpp
    src/connection_monitor.cpp
    src/shutdown_manager.cpp
    src/main.cpp
)

add_executable(${PROJECT_NAME} ${SOURCES})

target_include_directories(${PROJECT_NAME} PRIVATE include)

# ---------------------------------------------------------------------------
# Linked libraries
# ---------------------------------------------------------------------------
target_link_libraries(${PROJECT_NAME}
    PRIVATE
        Threads::Threads
        rt    # clock_gettime on older glibc
        m     # math library
)

# ---------------------------------------------------------------------------
# Cross-compilation settings
# ---------------------------------------------------------------------------
if(CMAKE_CROSSCOMPILING)
    message(STATUS "Cross-compiling for ${CMAKE_SYSTEM_PROCESSOR}")
    set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
    set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
    set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
else()
    message(STATUS "Native build on ${CMAKE_HOST_SYSTEM_PROCESSOR}")
endif()

# ---------------------------------------------------------------------------
# Compile-time definitions
# ---------------------------------------------------------------------------
target_compile_definitions(${PROJECT_NAME} PRIVATE
    PROTOCOL_VERSION_MAJOR=1
    PROTOCOL_VERSION_MINOR=0
)

# ---------------------------------------------------------------------------
# Install
# ---------------------------------------------------------------------------
install(TARGETS ${PROJECT_NAME} DESTINATION bin)
install(DIRECTORY config/ DESTINATION /etc/seeway_interface)
install(DIRECTORY include/ DESTINATION include/seeway_interface)

# ---------------------------------------------------------------------------
# Testing (opt-in with -DENABLE_TESTING=ON)
# ---------------------------------------------------------------------------
option(ENABLE_TESTING "Build unit and integration tests" OFF)
if(ENABLE_TESTING)
    message(STATUS "Tests enabled – configuring tests/")
    add_subdirectory(tests)
endif()

