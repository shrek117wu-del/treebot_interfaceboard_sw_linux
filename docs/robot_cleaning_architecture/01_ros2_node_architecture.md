# 轮臂式厕所清洁机器人 ROS2 软件节点架构图 + Topic/Service/Action 设计文档

> **版本**: v1.0  
> **文档编号**: 01  
> **适用读者**: 系统架构师、ROS2 软件工程师

---

## 1. 系统硬件拓扑回顾

```
┌────────────────────────────────────────────────────────────┐
│                    x86 主控（4090 / Thor）                  │
│  ┌─────────────┐  ┌──────────────┐  ┌──────────────────┐  │
│  │ 感知节点组   │  │ 任务管理节点  │  │ VLA 推理节点      │  │
│  │ perception  │  │ task_manager │  │ vla_inference    │  │
│  └──────┬──────┘  └──────┬───────┘  └────────┬─────────┘  │
│         │                │                    │            │
│  ┌──────▼──────────────────────────────────────▼─────────┐ │
│  │               ROS2 DDS 中间件（Fast DDS）              │ │
│  └──────┬────────────────────────────────────────────────┘ │
│         │ 以太网 / USB3                                     │
└─────────┼──────────────────────────────────────────────────┘
          │
  ┌───────┼──────────────────────┐
  │       │       RK3588 底盘    │
  │  ┌────▼──────┐  ┌──────────┐ │
  │  │ slam_node │  │ nav_node │ │
  │  └───────────┘  └──────────┘ │
  └──────────────────────────────┘

外设连接（USB3 / GigE to x86）:
  - Orbbec Gemini 330  → /wrist_camera/*
  - Orbbec Gemini 305  → /head_camera/*
  - FR5 机械臂 TCP/IP  → /fr5_driver/*
  - Meta Quest 3s      → /quest_teleop/*（采集模式）
```

---

## 2. ROS2 节点总览

### 2.1 节点列表

| 节点名称 | 包名 | 运行平台 | 职责 |
|---------|------|---------|------|
| `head_camera_node` | `orbbec_camera` | x86 | Gemini 305 彩色+深度流发布 |
| `wrist_camera_node` | `orbbec_camera` | x86 | Gemini 330 彩色+深度流发布 |
| `perception_node` | `robot_perception` | x86 | 场景语义分割、污渍检测、目标位姿估计 |
| `slam_node` | `robot_slam` | RK3588 | 建图与定位（Nav2 兼容） |
| `nav_node` | `robot_navigation` | RK3588 | 全局/局部路径规划、底盘速度指令 |
| `arm_driver_node` | `fr5_ros2_driver` | x86 | FR5 SDK 封装、关节状态发布、轨迹执行 |
| `arm_planner_node` | `robot_arm_planner` | x86 | MoveIt2 规划、IK 求解、避障 |
| `task_manager_node` | `robot_task_manager` | x86 | 任务调度、行为树执行引擎 |
| `skill_executor_node` | `robot_skill_lib` | x86 | 技能库封装、原子清洁动作执行 |
| `vla_inference_node` | `robot_vla` | x86 (GPU) | VLA 模型推理、高层语言指令解析 |
| `tool_controller_node` | `robot_tool_ctrl` | x86 | 清洁工具（喷头/刷头）控制 |
| `quest_teleop_node` | `quest2ros2` | x86 | Meta Quest 3s 数据接收/示教采集 |
| `system_monitor_node` | `robot_monitor` | x86 | 系统健康监控、告警发布 |

---

## 3. Topic 设计

### 3.1 感知相关 Topics

| Topic 名称 | 消息类型 | 发布者 | 订阅者 | 频率 | 说明 |
|-----------|---------|--------|--------|------|------|
| `/head_camera/color/image_raw` | `sensor_msgs/Image` | `head_camera_node` | `perception_node`, `vla_inference_node` | 30 Hz | 头部 RGB 图像 |
| `/head_camera/depth/image_raw` | `sensor_msgs/Image` | `head_camera_node` | `perception_node` | 30 Hz | 头部深度图像 |
| `/head_camera/depth/points` | `sensor_msgs/PointCloud2` | `head_camera_node` | `perception_node`, `slam_node` | 10 Hz | 头部点云 |
| `/wrist_camera/color/image_raw` | `sensor_msgs/Image` | `wrist_camera_node` | `perception_node`, `skill_executor_node` | 30 Hz | 腕部 RGB 图像 |
| `/wrist_camera/depth/image_raw` | `sensor_msgs/Image` | `wrist_camera_node` | `perception_node` | 30 Hz | 腕部深度图像 |
| `/wrist_camera/depth/points` | `sensor_msgs/PointCloud2` | `wrist_camera_node` | `perception_node`, `arm_planner_node` | 10 Hz | 腕部点云 |
| `/perception/scene_objects` | `robot_interfaces/SceneObjectArray` | `perception_node` | `task_manager_node`, `vla_inference_node` | 5 Hz | 场景物体列表（类型+位姿+边界框） |
| `/perception/stain_map` | `robot_interfaces/StainMap` | `perception_node` | `task_manager_node`, `skill_executor_node` | 5 Hz | 污渍位置热力图 |
| `/perception/surface_normal` | `geometry_msgs/PoseArray` | `perception_node` | `arm_planner_node` | 5 Hz | 清洁面法向量阵列 |

### 3.2 导航与定位 Topics

| Topic 名称 | 消息类型 | 发布者 | 订阅者 | 频率 | 说明 |
|-----------|---------|--------|--------|------|------|
| `/slam/map` | `nav_msgs/OccupancyGrid` | `slam_node` | `nav_node`, `task_manager_node` | 1 Hz | 占用栅格地图 |
| `/slam/pose` | `geometry_msgs/PoseWithCovarianceStamped` | `slam_node` | `nav_node`, `task_manager_node` | 50 Hz | 机器人全局位姿 |
| `/cmd_vel` | `geometry_msgs/Twist` | `nav_node` | 底盘驱动 | 20 Hz | 底盘速度指令 |
| `/odom` | `nav_msgs/Odometry` | 底盘驱动 | `slam_node`, `nav_node` | 50 Hz | 里程计数据 |

### 3.3 机械臂相关 Topics

| Topic 名称 | 消息类型 | 发布者 | 订阅者 | 频率 | 说明 |
|-----------|---------|--------|--------|------|------|
| `/fr5/joint_states` | `sensor_msgs/JointState` | `arm_driver_node` | `arm_planner_node`, `robot_state_publisher` | 125 Hz | FR5 关节状态 |
| `/fr5/joint_trajectory` | `trajectory_msgs/JointTrajectory` | `arm_planner_node` | `arm_driver_node` | — | 关节轨迹指令 |
| `/fr5/end_effector_pose` | `geometry_msgs/PoseStamped` | `arm_driver_node` | `skill_executor_node`, `perception_node` | 50 Hz | 末端位姿（工具坐标系） |
| `/fr5/gripper_state` | `robot_interfaces/GripperState` | `arm_driver_node` | `skill_executor_node` | 20 Hz | 末端工具/夹爪状态 |

### 3.4 任务与技能 Topics

| Topic 名称 | 消息类型 | 发布者 | 订阅者 | 频率 | 说明 |
|-----------|---------|--------|--------|------|------|
| `/task/current_task` | `robot_interfaces/TaskStatus` | `task_manager_node` | 全体监控节点 | 1 Hz | 当前任务状态广播 |
| `/skill/execution_state` | `robot_interfaces/SkillState` | `skill_executor_node` | `task_manager_node` | 10 Hz | 技能执行状态 |
| `/vla/action_proposal` | `robot_interfaces/ActionProposal` | `vla_inference_node` | `task_manager_node` | 2 Hz | VLA 高层动作建议 |
| `/tool/status` | `robot_interfaces/ToolStatus` | `tool_controller_node` | `skill_executor_node` | 10 Hz | 清洁工具状态 |
| `/system/alert` | `robot_interfaces/SystemAlert` | `system_monitor_node` | `task_manager_node` | 事件驱动 | 系统告警 |

### 3.5 示教采集 Topics

| Topic 名称 | 消息类型 | 发布者 | 订阅者 | 频率 | 说明 |
|-----------|---------|--------|--------|------|------|
| `/quest/hand_pose_left` | `geometry_msgs/PoseStamped` | `quest_teleop_node` | `arm_planner_node` | 90 Hz | Quest 左手控制器位姿 |
| `/quest/hand_pose_right` | `geometry_msgs/PoseStamped` | `quest_teleop_node` | `arm_planner_node` | 90 Hz | Quest 右手控制器位姿 |
| `/quest/trigger_event` | `robot_interfaces/TriggerEvent` | `quest_teleop_node` | `task_manager_node` | 事件驱动 | 示教触发事件 |

---

## 4. Service 设计

| Service 名称 | 服务类型 | 服务端 | 调用端 | 说明 |
|-------------|---------|--------|--------|------|
| `/perception/detect_scene` | `robot_interfaces/DetectScene` | `perception_node` | `task_manager_node` | 触发一次完整场景检测 |
| `/perception/locate_target` | `robot_interfaces/LocateTarget` | `perception_node` | `task_manager_node` | 定位指定类型清洁目标 |
| `/fr5/set_gripper` | `robot_interfaces/SetGripper` | `arm_driver_node` | `skill_executor_node` | 设置末端工具状态（开/关/力矩） |
| `/fr5/move_to_pose` | `robot_interfaces/MoveToPose` | `arm_planner_node` | `skill_executor_node` | 单点运动（不带轨迹规划） |
| `/nav/navigate_to` | `nav2_msgs/NavigateToPose` | `nav_node` | `task_manager_node` | 导航到指定位置 |
| `/task/pause` | `std_srvs/Trigger` | `task_manager_node` | 外部 UI / 远程 | 暂停当前任务 |
| `/task/resume` | `std_srvs/Trigger` | `task_manager_node` | 外部 UI / 远程 | 恢复任务 |
| `/task/abort` | `std_srvs/Trigger` | `task_manager_node` | 外部 UI / 远程 | 终止任务并回归安全位 |
| `/tool/spray` | `robot_interfaces/SprayControl` | `tool_controller_node` | `skill_executor_node` | 控制喷雾量和时间 |
| `/tool/switch_head` | `robot_interfaces/SwitchToolHead` | `tool_controller_node` | `skill_executor_node` | 切换清洁工具头 |
| `/system/set_mode` | `robot_interfaces/SetSystemMode` | `system_monitor_node` | 外部 UI | 切换系统模式（清洁/示教/待机） |

---

## 5. Action 设计

| Action 名称 | Action 类型 | 服务端 | 调用端 | 说明 |
|------------|------------|--------|--------|------|
| `/arm/execute_trajectory` | `robot_interfaces/ExecuteTrajectory` | `arm_driver_node` | `skill_executor_node` | 执行完整关节轨迹，带中间反馈 |
| `/arm/clean_surface` | `robot_interfaces/CleanSurface` | `skill_executor_node` | `task_manager_node` | 清洁指定平面区域（含力反馈闭环） |
| `/arm/move_to_pose_planned` | `robot_interfaces/MoveToPosePlanned` | `arm_planner_node` | `skill_executor_node` | MoveIt2 规划后执行，带进度反馈 |
| `/nav/navigate_and_clean` | `robot_interfaces/NavigateAndClean` | `task_manager_node` | 外部任务接口 | 导航到清洁位置并执行清洁任务 |
| `/task/execute_cleaning_plan` | `robot_interfaces/ExecuteCleaningPlan` | `task_manager_node` | 外部 UI / VLA | 执行完整清洁方案（多区域） |
| `/vla/plan_task` | `robot_interfaces/VLAPlanTask` | `vla_inference_node` | `task_manager_node` | VLA 模型根据视觉输入规划任务序列 |

---

## 6. 节点依赖关系图（简化）

```
quest_teleop_node ──────────────────────────────────────────┐
                                                            │
head_camera_node ──┐                                        │
                   ├──▶ perception_node ──▶ task_manager_node ◀── vla_inference_node
wrist_camera_node ─┘         │                  │
                             │                  ├──▶ skill_executor_node ──▶ arm_planner_node
                             ▼                  │              │               │
                      stain_map,                │              ▼               ▼
                      scene_objects             │       tool_controller   arm_driver_node
                                               │              │              (FR5)
                         slam_node ──▶ nav_node ◀─────────────┘
                              │
                         底盘 RK3588
```

---

## 7. 参考

- [FR5 ROS2 驱动文档](https://fairino-doc-zhs.readthedocs.io/3.9.3/ROSGuide/ros2guide.html)
- [Orbbec OrbbecSDK_ROS2](https://github.com/orbbec/OrbbecSDK_ROS2)
- [Quest2ROS2](https://github.com/Taokt/Quest2ROS2)
- [Nav2 导航框架](https://navigation.ros.org/)
- [MoveIt2](https://moveit.picknik.ai/)
- 详细接口消息定义 → [03_custom_messages.md](03_custom_messages.md)
- 命名规范 → [05_interface_naming_conventions.md](05_interface_naming_conventions.md)
