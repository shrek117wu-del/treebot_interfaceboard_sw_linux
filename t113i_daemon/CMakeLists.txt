# CMakeLists.txt for T113i daemon

cmake_minimum_required(VERSION 3.10)
project(seeway_interface_daemon)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

find_package(Threads REQUIRED)

include_directories(include)

# Explicitly list sources so that new files are picked up cleanly
set(SOURCES
    src/config_loader.cpp
    src/main.cpp
)

add_executable(${PROJECT_NAME} ${SOURCES})

target_include_directories(${PROJECT_NAME} PRIVATE include)
target_link_libraries(${PROJECT_NAME} PRIVATE Threads::Threads)

# Install
install(TARGETS ${PROJECT_NAME} DESTINATION bin)
install(DIRECTORY config/ DESTINATION /etc/seeway_interface)
