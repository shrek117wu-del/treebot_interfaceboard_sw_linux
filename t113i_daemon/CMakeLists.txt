# CMakeLists.txt for T113i daemon

cmake_minimum_required(VERSION 3.10)
project(seeway_interface_daemon)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

# Optional: Add threading
find_package(Threads REQUIRED)

include_directories(include)

file(GLOB SOURCES "src/*.cpp")

add_executable(${PROJECT_NAME} ${SOURCES})

target_link_libraries(${PROJECT_NAME} PRIVATE Threads::Threads)

# Install
install(TARGETS ${PROJECT_NAME} DESTINATION bin)
install(DIRECTORY config DESTINATION /etc/seeway_interface)
