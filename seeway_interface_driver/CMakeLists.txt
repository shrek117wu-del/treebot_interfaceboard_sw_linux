cmake_minimum_required(VERSION 3.8)
project(seeway_interface_driver)

if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(rclcpp_components REQUIRED)
find_package(rclcpp_lifecycle REQUIRED)
find_package(seeway_interface_msgs REQUIRED)

include_directories(include)

add_library(seeway_driver_node SHARED
  src/comm_handler.cpp
  src/tcp_server_transport.cpp
  src/tcp_client_transport.cpp
  src/serial_transport.cpp
  src/driver_node.cpp
)
target_include_directories(seeway_driver_node PRIVATE include)
target_link_libraries(seeway_driver_node
  pthread
)
ament_target_dependencies(seeway_driver_node
  rclcpp
  rclcpp_components
  rclcpp_lifecycle
  seeway_interface_msgs
)

rclcpp_components_register_node(seeway_driver_node
  PLUGIN "seeway_interface_driver::DriverNode"
  EXECUTABLE seeway_driver_node_exe
)

install(TARGETS
  seeway_driver_node
  ARCHIVE DESTINATION lib
  LIBRARY DESTINATION lib
  RUNTIME DESTINATION lib/${PROJECT_NAME}
)

install(DIRECTORY
  launch
  config
  DESTINATION share/${PROJECT_NAME}
)

ament_package()
