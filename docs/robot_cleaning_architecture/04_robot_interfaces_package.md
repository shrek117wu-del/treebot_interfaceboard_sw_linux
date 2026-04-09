# robot_interfaces 包初始化模板

> **版本**: v1.0  
> **文档编号**: 04  
> **适用读者**: ROS2 软件工程师  
> **说明**: 本文档提供 `robot_interfaces` 包的完整目录结构与初始化文件模板，可直接复制使用。

---

## 1. 目录结构

```
robot_interfaces/
├── CMakeLists.txt
├── package.xml
├── msg/
│   ├── SceneObject.msg
│   ├── SceneObjectArray.msg
│   ├── StainMap.msg
│   ├── StainRegion.msg
│   ├── TaskStatus.msg
│   ├── SkillState.msg
│   ├── ActionProposal.msg
│   ├── GripperState.msg
│   ├── ToolStatus.msg
│   ├── SystemAlert.msg
│   ├── TriggerEvent.msg
│   └── CleaningAreaResult.msg
├── srv/
│   ├── DetectScene.srv
│   ├── LocateTarget.srv
│   ├── MoveToPose.srv
│   ├── SetGripper.srv
│   ├── SprayControl.srv
│   ├── SwitchToolHead.srv
│   └── SetSystemMode.srv
└── action/
    ├── ExecuteTrajectory.action
    ├── CleanSurface.action
    ├── MoveToPosePlanned.action
    ├── ExecuteCleaningPlan.action
    ├── VLAPlanTask.action
    └── NavigateAndClean.action
```

> 所有 `.msg`、`.srv`、`.action` 文件的具体内容见 [03_custom_messages.md](03_custom_messages.md)。

---

## 2. `package.xml`

```xml
<?xml version="1.0"?>
<?xml-model href="http://download.ros.org/schema/package_format3.xsd" schematypens="http://www.w3.org/2001/XMLSchema"?>
<package format="3">
  <name>robot_interfaces</name>
  <version>0.1.0</version>
  <description>
    Custom ROS2 message, service, and action definitions for the
    wheel-arm toilet-cleaning robot system.
  </description>

  <maintainer email="dev@your-org.com">Robot Software Team</maintainer>
  <license>Apache-2.0</license>

  <!-- Build dependencies -->
  <buildtool_depend>ament_cmake</buildtool_depend>
  <buildtool_depend>rosidl_default_generators</buildtool_depend>

  <!-- Message dependencies -->
  <depend>std_msgs</depend>
  <depend>geometry_msgs</depend>
  <depend>sensor_msgs</depend>
  <depend>nav_msgs</depend>
  <depend>trajectory_msgs</depend>
  <depend>action_msgs</depend>

  <!-- Runtime dependencies -->
  <exec_depend>rosidl_default_runtime</exec_depend>

  <!-- Export -->
  <member_of_group>rosidl_interface_packages</member_of_group>

  <export>
    <build_type>ament_cmake</build_type>
  </export>
</package>
```

---

## 3. `CMakeLists.txt`

```cmake
cmake_minimum_required(VERSION 3.8)
project(robot_interfaces)

# Default to C++17
if(NOT CMAKE_CXX_STANDARD)
  set(CMAKE_CXX_STANDARD 17)
endif()

if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# ── Find packages ──────────────────────────────────────────────────────────────
find_package(ament_cmake REQUIRED)
find_package(rosidl_default_generators REQUIRED)
find_package(std_msgs REQUIRED)
find_package(geometry_msgs REQUIRED)
find_package(sensor_msgs REQUIRED)
find_package(nav_msgs REQUIRED)
find_package(trajectory_msgs REQUIRED)
find_package(action_msgs REQUIRED)

# ── Collect interface files ────────────────────────────────────────────────────
set(MSG_FILES
  "msg/SceneObject.msg"
  "msg/SceneObjectArray.msg"
  "msg/StainMap.msg"
  "msg/StainRegion.msg"
  "msg/TaskStatus.msg"
  "msg/SkillState.msg"
  "msg/ActionProposal.msg"
  "msg/GripperState.msg"
  "msg/ToolStatus.msg"
  "msg/SystemAlert.msg"
  "msg/TriggerEvent.msg"
  "msg/CleaningAreaResult.msg"
)

set(SRV_FILES
  "srv/DetectScene.srv"
  "srv/LocateTarget.srv"
  "srv/MoveToPose.srv"
  "srv/SetGripper.srv"
  "srv/SprayControl.srv"
  "srv/SwitchToolHead.srv"
  "srv/SetSystemMode.srv"
)

set(ACTION_FILES
  "action/ExecuteTrajectory.action"
  "action/CleanSurface.action"
  "action/MoveToPosePlanned.action"
  "action/ExecuteCleaningPlan.action"
  "action/VLAPlanTask.action"
  "action/NavigateAndClean.action"
)

# ── Generate interfaces ────────────────────────────────────────────────────────
rosidl_generate_interfaces(${PROJECT_NAME}
  ${MSG_FILES}
  ${SRV_FILES}
  ${ACTION_FILES}
  DEPENDENCIES
    std_msgs
    geometry_msgs
    sensor_msgs
    nav_msgs
    trajectory_msgs
    action_msgs
)

# ── Ament export ───────────────────────────────────────────────────────────────
ament_export_dependencies(rosidl_default_runtime)

ament_package()
```

---

## 4. 初始化步骤（快速开始）

### 4.1 在 ROS2 工作空间中创建包

```bash
# 进入工作空间 src 目录
cd ~/robot_ws/src

# 创建接口包（纯接口包不含 C++ 源码）
ros2 pkg create robot_interfaces \
    --build-type ament_cmake \
    --dependencies std_msgs geometry_msgs sensor_msgs nav_msgs trajectory_msgs action_msgs

# 创建子目录
cd robot_interfaces
mkdir -p msg srv action
```

### 4.2 复制文件并替换模板

1. 将上方 `package.xml` 和 `CMakeLists.txt` 内容覆盖到对应文件。
2. 将 [03_custom_messages.md](03_custom_messages.md) 中各 `.msg`/`.srv`/`.action` 定义复制到对应子目录。

### 4.3 构建

```bash
cd ~/robot_ws
colcon build --packages-select robot_interfaces
source install/setup.bash
```

### 4.4 验证

```bash
# 检查消息类型是否正确生成
ros2 interface list | grep robot_interfaces

# 查看某条消息结构
ros2 interface show robot_interfaces/msg/SceneObject
ros2 interface show robot_interfaces/action/CleanSurface
```

---

## 5. 跨包使用说明

其他 ROS2 包引用 `robot_interfaces` 时，在其 `package.xml` 中添加：

```xml
<depend>robot_interfaces</depend>
```

在 `CMakeLists.txt` 中添加：

```cmake
find_package(robot_interfaces REQUIRED)

# 对目标可执行文件或库链接接口
ament_target_dependencies(your_target robot_interfaces)
```

在 C++ 源码中引用：

```cpp
#include "robot_interfaces/msg/scene_object.hpp"
#include "robot_interfaces/srv/detect_scene.hpp"
#include "robot_interfaces/action/clean_surface.hpp"
```

在 Python 源码中引用：

```python
from robot_interfaces.msg import SceneObject, TaskStatus
from robot_interfaces.srv import DetectScene
from robot_interfaces.action import CleanSurface
```

---

## 6. 版本兼容说明

| 字段 | 约定 |
|------|------|
| 向前兼容 | 新增字段只追加到末尾，不得修改已有字段 |
| 破坏性变更 | 需升级包版本号（`package.xml` `<version>`）并在 Changelog 中记录 |
| 枚举扩展 | 新增枚举值只追加，已有常量不得重新编号 |
| 废弃字段 | 用注释标注 `# DEPRECATED since vX.Y`，保留至少一个大版本 |
