# ROS2 自定义消息 .msg / .srv / .action 文件详细定义草案

> **版本**: v1.0  
> **文档编号**: 03  
> **适用读者**: ROS2 软件工程师  
> **所属包**: `robot_interfaces`  
> **前置阅读**: [01_ros2_node_architecture.md](01_ros2_node_architecture.md)

---

## 1. 消息文件（.msg）

### 1.1 `SceneObject.msg`

```
# 场景物体描述（单个对象）
# robot_interfaces/SceneObject

std_msgs/Header header

uint8 object_type         # 见 ObjectType 枚举（05_interface_naming_conventions.md）
string object_id          # 唯一标识符，如 "toilet_01"
string label              # 可读标签，如 "toilet"
float32 confidence        # 检测置信度 [0.0, 1.0]

geometry_msgs/PoseStamped pose          # 物体中心位姿（相机坐标系）
geometry_msgs/Vector3 bounding_box_size # 包围盒尺寸 (x=长, y=宽, z=高), 单位 m

uint8 cleanliness_level   # 清洁度等级：0=脏, 1=轻度污渍, 2=中度, 3=重度
bool requires_cleaning    # 是否需要清洁

# ObjectType 常量
uint8 OBJECT_UNKNOWN     = 0
uint8 OBJECT_TOILET      = 1
uint8 OBJECT_URINAL      = 2
uint8 OBJECT_SINK        = 3
uint8 OBJECT_FLOOR       = 4
uint8 OBJECT_WALL        = 5
uint8 OBJECT_MIRROR      = 6
uint8 OBJECT_FAUCET      = 7
uint8 OBJECT_OBSTACLE    = 8
```

---

### 1.2 `SceneObjectArray.msg`

```
# 场景物体列表
# robot_interfaces/SceneObjectArray

std_msgs/Header header
robot_interfaces/SceneObject[] objects
uint32 total_objects_requiring_cleaning
```

---

### 1.3 `StainMap.msg`

```
# 污渍分布热力图
# robot_interfaces/StainMap

std_msgs/Header header
string target_object_id       # 对应的物体 ID
string camera_frame           # 对应坐标系

# 以深度图分辨率对齐的污渍掩码（单通道 uint8，0=干净 255=严重污渍）
sensor_msgs/Image stain_mask

# 污渍区域列表
StainRegion[] stain_regions

# 总污渍面积估计 (cm^2)
float32 total_stain_area_cm2
float32 stain_coverage_ratio  # 污渍占目标表面的比例 [0.0, 1.0]
```

---

### 1.4 `StainRegion.msg`

```
# 单个污渍区域描述
# robot_interfaces/StainRegion

geometry_msgs/Point center_3d   # 污渍中心 3D 位置（相机坐标系）
float32 area_cm2                # 估计面积
uint8 stain_type                # 污渍类型枚举
float32 severity                # 严重程度 [0.0, 1.0]
geometry_msgs/Vector3 normal    # 表面法向量（用于接触方向规划）

# StainType 常量
uint8 STAIN_UNKNOWN    = 0
uint8 STAIN_WATER      = 1
uint8 STAIN_SOAP       = 2
uint8 STAIN_URINE      = 3
uint8 STAIN_FECES      = 4
uint8 STAIN_RUST       = 5
uint8 STAIN_MOLD       = 6
```

---

### 1.5 `TaskStatus.msg`

```
# 当前任务状态广播
# robot_interfaces/TaskStatus

std_msgs/Header header
string task_id
uint8 task_type          # 见 TaskType 枚举
uint8 status             # 见 TaskStatusCode 枚举
string current_step      # 当前执行步骤描述
float32 progress_percent # 总体进度 [0.0, 100.0]
string active_target_id  # 当前清洁目标物体 ID
string message           # 可读状态描述

# TaskType 常量
uint8 TASK_IDLE           = 0
uint8 TASK_CLEAN_TOILET   = 1
uint8 TASK_CLEAN_URINAL   = 2
uint8 TASK_CLEAN_SINK     = 3
uint8 TASK_CLEAN_FLOOR    = 4
uint8 TASK_CLEAN_WALL     = 5
uint8 TASK_FULL_CLEAN     = 6

# TaskStatusCode 常量
uint8 STATUS_IDLE         = 0
uint8 STATUS_PLANNING     = 1
uint8 STATUS_NAVIGATING   = 2
uint8 STATUS_EXECUTING    = 3
uint8 STATUS_VERIFYING    = 4
uint8 STATUS_PAUSED       = 5
uint8 STATUS_COMPLETED    = 6
uint8 STATUS_FAILED       = 7
uint8 STATUS_ABORTED      = 8
```

---

### 1.6 `SkillState.msg`

```
# 技能执行状态
# robot_interfaces/SkillState

std_msgs/Header header
string skill_id           # 技能唯一 ID
uint8 skill_type          # 见 SkillType 枚举（06_task_skill_bt_spec.md）
uint8 state               # 见 SkillStateCode 枚举
float32 progress_percent
string message

# SkillStateCode 常量
uint8 SKILL_IDLE       = 0
uint8 SKILL_RUNNING    = 1
uint8 SKILL_SUCCESS    = 2
uint8 SKILL_FAILURE    = 3
uint8 SKILL_PREEMPTED  = 4
```

---

### 1.7 `ActionProposal.msg`

```
# VLA 输出的高层动作建议
# robot_interfaces/ActionProposal

std_msgs/Header header
string proposal_id
string natural_language_instruction   # 原始语言指令
string[] proposed_task_sequence       # 任务步骤序列（可读描述）
float32 confidence
bool requires_confirmation            # 是否需要人工确认后执行
```

---

### 1.8 `GripperState.msg`

```
# 末端工具 / 夹爪状态
# robot_interfaces/GripperState

std_msgs/Header header
bool is_attached          # 工具是否已安装
uint8 tool_type           # 见 ToolType 枚举
float32 opening_percent   # 开合百分比 [0.0, 1.0]（夹爪类）
float32 force_n           # 当前接触力 (N)
bool is_spraying          # 是否正在喷雾

# ToolType 常量
uint8 TOOL_NONE      = 0
uint8 TOOL_BRUSH     = 1
uint8 TOOL_SPONGE    = 2
uint8 TOOL_SPRAY     = 3
uint8 TOOL_SCRUBBER  = 4
uint8 TOOL_SQUEEGEE  = 5
```

---

### 1.9 `ToolStatus.msg`

```
# 清洁工具整体状态
# robot_interfaces/ToolStatus

std_msgs/Header header
uint8 current_tool_type
bool is_operational
float32 detergent_remaining_ml   # 清洁剂剩余量
float32 water_pressure_bar       # 水压
bool pump_active
string error_message
```

---

### 1.10 `SystemAlert.msg`

```
# 系统告警消息
# robot_interfaces/SystemAlert

std_msgs/Header header
uint8 severity             # 严重级别
string alert_code          # 告警代码
string source_node         # 告警来源节点
string description
bool requires_stop         # 是否需要立即停机

# Severity 常量
uint8 SEVERITY_INFO     = 0
uint8 SEVERITY_WARNING  = 1
uint8 SEVERITY_ERROR    = 2
uint8 SEVERITY_CRITICAL = 3
```

---

### 1.11 `TriggerEvent.msg`

```
# Quest / 外部触发事件
# robot_interfaces/TriggerEvent

std_msgs/Header header
string event_type      # "start_record", "stop_record", "label_point", "emergency_stop"
string payload_json    # 附加数据（JSON 格式字符串）
```

---

## 2. 服务文件（.srv）

### 2.1 `DetectScene.srv`

```
# 触发完整场景检测
# robot_interfaces/DetectScene

# Request
bool force_redetect     # true = 忽略缓存，强制重新检测
string camera_hint      # "head" | "wrist" | "both"
---
# Response
bool success
string message
robot_interfaces/SceneObjectArray scene
```

---

### 2.2 `LocateTarget.srv`

```
# 定位特定类别清洁目标
# robot_interfaces/LocateTarget

# Request
uint8 object_type       # 使用 SceneObject.msg 中的 ObjectType 枚举
string object_id        # 若已知 ID 则填入，否则留空
---
# Response
bool success
string message
geometry_msgs/PoseStamped target_pose
robot_interfaces/SceneObject object_info
```

---

### 2.3 `MoveToPose.srv`

```
# 机械臂单点运动（不经过 MoveIt2 规划）
# robot_interfaces/MoveToPose

# Request
geometry_msgs/PoseStamped target_pose
float32 speed_scale     # 速度比例 [0.1, 1.0]
bool use_joint_space    # true = 关节空间运动，false = 笛卡尔直线
---
# Response
bool success
string message
float32 execution_time_sec
```

---

### 2.4 `SetGripper.srv`

```
# 设置末端工具状态
# robot_interfaces/SetGripper

# Request
uint8 tool_type
float32 target_opening_percent   # [0.0, 1.0]，夹爪适用
float32 target_force_n           # 目标力矩，力控模式适用
bool enable_force_control
---
# Response
bool success
string message
```

---

### 2.5 `SprayControl.srv`

```
# 控制喷雾
# robot_interfaces/SprayControl

# Request
float32 spray_volume_ml      # 喷雾量
float32 spray_duration_sec   # 喷雾持续时间
uint8 spray_pattern          # 0=点喷, 1=扇形, 2=锥形
---
# Response
bool success
string message
float32 actual_volume_ml
```

---

### 2.6 `SwitchToolHead.srv`

```
# 切换清洁工具头
# robot_interfaces/SwitchToolHead

# Request
uint8 target_tool_type    # 目标工具类型
bool auto_stow_current    # 是否自动存放当前工具
---
# Response
bool success
string message
uint8 previous_tool_type
uint8 current_tool_type
```

---

### 2.7 `SetSystemMode.srv`

```
# 切换系统运行模式
# robot_interfaces/SetSystemMode

# Request
uint8 mode    # 见 SystemMode 枚举

# SystemMode 常量
uint8 MODE_STANDBY    = 0
uint8 MODE_CLEANING   = 1
uint8 MODE_TELEOP     = 2
uint8 MODE_DATA_COLLECT = 3
uint8 MODE_EMERGENCY  = 4
---
# Response
bool success
string message
uint8 previous_mode
uint8 current_mode
```

---

## 3. Action 文件（.action）

### 3.1 `ExecuteTrajectory.action`

```
# 执行完整关节轨迹
# robot_interfaces/ExecuteTrajectory

# Goal
trajectory_msgs/JointTrajectory trajectory
float32 speed_scale       # [0.1, 1.0]
bool enable_force_limit   # 是否启用力限保护
float32 max_force_n       # 最大接触力（N）
---
# Result
bool success
string message
float32 actual_execution_time_sec
uint8 stop_reason         # 0=正常, 1=力限触发, 2=碰撞, 3=抢占

# Feedback
float32 progress_percent
uint8 current_point_index
float32 current_speed_mps
geometry_msgs/PoseStamped current_end_effector_pose
```

---

### 3.2 `CleanSurface.action`

```
# 清洁指定表面区域
# robot_interfaces/CleanSurface

# Goal
string target_object_id
uint8 clean_mode          # 0=AUTO, 1=WIPE_HORIZONTAL, 2=WIPE_VERTICAL, 3=SCRUB
geometry_msgs/PoseStamped surface_center
geometry_msgs/Vector3 surface_size   # 区域尺寸 (m)
geometry_msgs/Vector3 surface_normal # 表面法向量
float32 target_force_n               # 期望接触力
uint8 max_passes                     # 最大擦拭遍数
---
# Result
bool success
string message
float32 cleaned_area_percent         # 清洁覆盖率
uint8 passes_completed
bool stain_removed                   # 二次检测结果

# Feedback
float32 progress_percent
uint8 current_pass
float32 current_force_n
float32 cleaned_area_percent
string current_step_description
```

---

### 3.3 `MoveToPosePlanned.action`

```
# MoveIt2 规划后执行
# robot_interfaces/MoveToPosePlanned

# Goal
geometry_msgs/PoseStamped target_pose
string planning_group     # 默认 "fr5_arm"
float32 velocity_scale    # [0.1, 1.0]
float32 acceleration_scale
bool cartesian_path       # 是否强制笛卡尔路径
float32 eef_step          # 笛卡尔路径步长（m）
---
# Result
bool success
string message
float32 planning_time_sec
float32 execution_time_sec
float32 path_length_m

# Feedback
float32 progress_percent
geometry_msgs/PoseStamped current_pose
float32 distance_to_goal_m
```

---

### 3.4 `ExecuteCleaningPlan.action`

```
# 执行完整多区域清洁方案
# robot_interfaces/ExecuteCleaningPlan

# Goal
string[] target_object_ids      # 按顺序清洁的目标列表
bool auto_replan                 # 是否允许动态重规划
bool verify_after_each           # 每区域完成后是否二次核验
uint8 quality_level              # 0=快速, 1=标准, 2=深度清洁
---
# Result
bool success
string message
uint8 areas_completed
uint8 areas_failed
float32 total_time_sec
CleaningAreaResult[] area_results

# Feedback
string current_area_id
uint8 areas_completed
uint8 total_areas
float32 overall_progress_percent
string current_step
```

---

### 3.5 `VLAPlanTask.action`

```
# VLA 模型规划任务序列
# robot_interfaces/VLAPlanTask

# Goal
string instruction               # 自然语言清洁指令
sensor_msgs/Image scene_image    # 当前场景快照
robot_interfaces/SceneObjectArray scene_context  # 可选：感知结果辅助
bool execute_after_plan          # 规划完成后是否直接执行
---
# Result
bool success
string message
string[] task_steps
robot_interfaces/ActionProposal proposal
float32 inference_time_sec

# Feedback
string status_message
float32 inference_progress_percent
```

---

### 3.6 `NavigateAndClean.action`

```
# 导航到指定位置并执行清洁
# robot_interfaces/NavigateAndClean

# Goal
geometry_msgs/PoseStamped clean_station_pose  # 清洁站位目标点
string target_object_id
uint8 clean_mode
---
# Result
bool success
string message
bool navigation_success
bool cleaning_success
float32 total_time_sec

# Feedback
uint8 phase             # 0=导航中, 1=清洁中, 2=验证中
float32 phase_progress_percent
string phase_description
```

---

## 4. 消息文件汇总

| 类型 | 文件名 | 说明 |
|------|--------|------|
| msg | `SceneObject.msg` | 单个场景物体 |
| msg | `SceneObjectArray.msg` | 场景物体列表 |
| msg | `StainMap.msg` | 污渍分布图 |
| msg | `StainRegion.msg` | 单个污渍区域 |
| msg | `TaskStatus.msg` | 任务状态 |
| msg | `SkillState.msg` | 技能执行状态 |
| msg | `ActionProposal.msg` | VLA 动作建议 |
| msg | `GripperState.msg` | 末端工具状态 |
| msg | `ToolStatus.msg` | 清洁工具状态 |
| msg | `SystemAlert.msg` | 系统告警 |
| msg | `TriggerEvent.msg` | 外部触发事件 |
| msg | `CleaningAreaResult.msg` | 区域清洁结果（用于 Action） |
| srv | `DetectScene.srv` | 触发场景检测 |
| srv | `LocateTarget.srv` | 定位清洁目标 |
| srv | `MoveToPose.srv` | 单点运动 |
| srv | `SetGripper.srv` | 设置末端工具 |
| srv | `SprayControl.srv` | 控制喷雾 |
| srv | `SwitchToolHead.srv` | 切换工具头 |
| srv | `SetSystemMode.srv` | 切换系统模式 |
| action | `ExecuteTrajectory.action` | 执行关节轨迹 |
| action | `CleanSurface.action` | 清洁表面 |
| action | `MoveToPosePlanned.action` | MoveIt2 规划移动 |
| action | `ExecuteCleaningPlan.action` | 完整清洁方案 |
| action | `VLAPlanTask.action` | VLA 规划任务 |
| action | `NavigateAndClean.action` | 导航并清洁 |

> **注**: `CleaningAreaResult.msg` 内嵌于 `ExecuteCleaningPlan.action` 反馈/结果中，需单独定义：

```
# CleaningAreaResult.msg
string object_id
bool success
float32 cleaned_area_percent
uint8 passes_completed
float32 time_sec
string failure_reason
```
