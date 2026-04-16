# ros2_benchmark/gemini330_gpu_bench/CMakeLists.txt
#
# CMake 构建文件：支持 CUDA + C++17
# 编译 gpu_upload_node（CUDA 异步上传节点）
# 安装 Python 脚本 camera_benchmark_node.py 及 launch/ 目录

cmake_minimum_required(VERSION 3.18)
project(gemini330_gpu_bench CUDA CXX)

# ── C++ 与 CUDA 标准 ──────────────────────────────────────────────────────────
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CUDA_STANDARD 17)
set(CMAKE_CUDA_STANDARD_REQUIRED ON)

# 默认构建类型
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

# ── 依赖查找 ──────────────────────────────────────────────────────────────────
find_package(ament_cmake REQUIRED)
find_package(rclcpp      REQUIRED)
find_package(sensor_msgs REQUIRED)
find_package(CUDAToolkit REQUIRED)

# ── gpu_upload_node 可执行文件 ────────────────────────────────────────────────
add_executable(gpu_upload_node
  src/gpu_upload_node.cpp
)

# 让 nvcc 以 C++17 编译 .cu / .cpp（分离编译）
set_target_properties(gpu_upload_node PROPERTIES
  CUDA_SEPARABLE_COMPILATION ON
)

target_include_directories(gpu_upload_node PRIVATE
  ${CUDAToolkit_INCLUDE_DIRS}
)

ament_target_dependencies(gpu_upload_node
  rclcpp
  sensor_msgs
)

target_link_libraries(gpu_upload_node
  CUDA::cudart
  CUDA::cuda_driver
)

# ── 安装规则 ──────────────────────────────────────────────────────────────────

# 安装 C++ 可执行文件
install(TARGETS gpu_upload_node
  DESTINATION lib/${PROJECT_NAME}
)

# 安装 Python 脚本（重命名为无 .py 后缀的可执行命令）
install(PROGRAMS camera_benchmark_node.py
  DESTINATION lib/${PROJECT_NAME}
  RENAME camera_benchmark_node
)

# 安装 launch 目录
install(DIRECTORY launch/
  DESTINATION share/${PROJECT_NAME}/launch
)

# 安装 config 目录
install(DIRECTORY config/
  DESTINATION share/${PROJECT_NAME}/config
)

ament_package()
