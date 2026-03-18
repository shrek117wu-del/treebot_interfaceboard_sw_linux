cmake_minimum_required(VERSION 3.8)
project(seeway_interface_msgs)

if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# find dependencies
find_package(ament_cmake REQUIRED)
find_package(rosidl_default_generators REQUIRED)
find_package(std_msgs REQUIRED)

rosidl_generate_interfaces(${PROJECT_NAME}
  "msg/SensorData.msg"
  "msg/GpioStatus.msg"
  "msg/BatteryStatus.msg"
  "msg/SystemStatus.msg"
  "msg/InputEvent.msg"
  "srv/SetGpio.srv"
  "srv/SetPwm.srv"
  "srv/PowerControl.srv"
  "srv/SendTask.srv"
  DEPENDENCIES std_msgs
)

# Install
ament_package()
