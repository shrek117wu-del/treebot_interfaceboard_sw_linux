# ROS2 接口枚举规范与命名规范

> **版本**: v1.0  
> **文档编号**: 05  
> **适用读者**: 全体开发人员  
> **前置阅读**: [03_custom_messages.md](03_custom_messages.md), [04_robot_interfaces_package.md](04_robot_interfaces_package.md)

---

## 1. 总体原则

本文档为轮臂式厕所清洁机器人项目的 **ROS2 接口命名规范**，所有包（`robot_interfaces`、`robot_perception`、`robot_task_manager`、`robot_skill_lib`、`robot_vla` 等）均须遵守。

| 原则 | 说明 |
|------|------|
| 一致性 | 同类接口必须使用统一前缀、分隔符和大小写风格 |
| 可读性 | 名称须能从字面推断功能，不使用无意义缩写 |
| 命名空间隔离 | 通过前缀避免跨模块命名冲突 |
| 枚举不可变 | 已发布的枚举值不得改变，只允许追加 |
| 版本可追踪 | 破坏性变更需在消息文件和本文档中同步注明 |

---

## 2. Topic / Service / Action 命名规范

### 2.1 总体格式

```
/<模块名>/<子模块名>/<功能描述>
```

- 全小写字母，单词间用下划线（`_`）连接（snake_case）。
- 第一段为**模块名**，与 ROS2 节点命名空间对齐。
- 不超过 4 段路径层级（避免过深嵌套）。

### 2.2 模块前缀对照表

| 模块前缀 | 对应节点 / 功能 |
|---------|---------------|
| `/head_camera` | `head_camera_node`，Orbbec Gemini 305 |
| `/wrist_camera` | `wrist_camera_node`，Orbbec Gemini 330 |
| `/perception` | `perception_node`，场景语义感知 |
| `/slam` | `slam_node`，RK3588 建图与定位 |
| `/nav` | `nav_node`，路径规划与导航 |
| `/fr5` | `arm_driver_node` / `arm_planner_node`，FR5 机械臂 |
| `/arm` | `arm_planner_node`，机械臂运动规划层 |
| `/skill` | `skill_executor_node`，技能库执行 |
| `/task` | `task_manager_node`，任务调度 |
| `/vla` | `vla_inference_node`，VLA 模型推理 |
| `/tool` | `tool_controller_node`，清洁工具控制 |
| `/quest` | `quest_teleop_node`，Meta Quest 3s 数据采集 |
| `/system` | `system_monitor_node`，系统健康监控 |

### 2.3 Topic 命名示例

```
# 好的命名
/head_camera/color/image_raw
/perception/scene_objects
/fr5/joint_states
/task/current_task
/skill/execution_state
/vla/action_proposal

# 不好的命名（禁止）
/cam1/img           # 模糊缩写
/PERCEPTION/STAIN   # 大写字母
/robot/camera/head/orbbec/color  # 层级过深
/task-manager/status  # 连字符（应用下划线）
```

### 2.4 Service 命名约定

- 格式：`/<模块名>/<动词_名词>` 或 `/<模块名>/<动词_对象>`
- 动词使用祈使形式：`set_`, `get_`, `detect_`, `locate_`, `switch_`, `move_to_`

```
/perception/detect_scene
/perception/locate_target
/fr5/set_gripper
/fr5/move_to_pose
/nav/navigate_to
/task/pause
/task/resume
/task/abort
/tool/spray
/tool/switch_head
/system/set_mode
```

### 2.5 Action 命名约定

- 格式：`/<模块名>/<动词_名词>`
- Action 名称通常比 Service 更长，因为描述的是异步、长时间任务

```
/arm/execute_trajectory
/arm/clean_surface
/arm/move_to_pose_planned
/nav/navigate_and_clean
/task/execute_cleaning_plan
/vla/plan_task
```

---

## 3. ROS2 包命名规范

### 3.1 包名格式

- 全小写，单词间下划线分隔，以项目名 `robot_` 为前缀（或 `fr5_`、`orbbec_` 等第三方前缀）。

| 包名 | 功能 |
|------|------|
| `robot_interfaces` | 所有自定义消息/服务/Action 定义 |
| `robot_perception` | 感知算法节点 |
| `robot_slam` | SLAM 建图定位（RK3588 端） |
| `robot_navigation` | 路径规划、Nav2 配置 |
| `robot_task_manager` | 任务调度、行为树引擎 |
| `robot_skill_lib` | 技能库（原子清洁动作） |
| `robot_vla` | VLA 模型加载与推理 |
| `robot_arm_planner` | MoveIt2 规划封装 |
| `robot_tool_ctrl` | 清洁工具控制 |
| `robot_monitor` | 系统监控与告警 |
| `fr5_ros2_driver` | Fairino FR5 ROS2 驱动 |
| `orbbec_camera` | Orbbec SDK ROS2 封装 |
| `quest2ros2` | Meta Quest 3s ROS2 桥接 |

### 3.2 节点名格式

```
<功能描述>_node
```

例：`perception_node`、`arm_driver_node`、`task_manager_node`

---

## 4. 消息（Message）命名规范

### 4.1 .msg 文件命名

- **大驼峰（UpperCamelCase / PascalCase）**，无下划线，无连字符。
- 文件名即消息类型名，应为名词或名词短语。

```
SceneObject.msg          ✓
SceneObjectArray.msg     ✓
TaskStatus.msg           ✓
stain_map.msg            ✗（小写）
Stain-Region.msg         ✗（连字符）
```

### 4.2 .msg 字段命名

- **蛇形命名（snake_case）**，全小写。
- 布尔字段以 `is_`、`has_`、`enable_`、`requires_` 前缀。
- 数组字段以 `_list` 或使用复数形式。
- 带单位的字段在末尾注明，如 `_sec`、`_m`、`_n`、`_ml`、`_percent`、`_cm2`。

```
# 正确
float32 confidence
bool is_spraying
bool requires_cleaning
float32 spray_duration_sec
float32 area_cm2
geometry_msgs/PoseStamped[] waypoints   # 复数形式数组

# 错误
float32 Confidence         # 大写
bool spraying              # 缺少语义前缀
float32 duration           # 缺少单位
```

### 4.3 枚举常量命名

- **大写蛇形（UPPER_SNAKE_CASE）**，以类型名为前缀。
- 必须从 `0` 开始，`0` 值通常为 `UNKNOWN` 或 `NONE` 或 `IDLE`。
- 已发布常量不可修改，新增常量只能追加。

```
# TaskType 枚举
uint8 TASK_IDLE           = 0    # 0 = IDLE/UNKNOWN
uint8 TASK_CLEAN_TOILET   = 1
uint8 TASK_CLEAN_URINAL   = 2
uint8 TASK_CLEAN_SINK     = 3
uint8 TASK_CLEAN_FLOOR    = 4
uint8 TASK_CLEAN_WALL     = 5
uint8 TASK_FULL_CLEAN     = 6
# 新增时追加，如 TASK_CLEAN_MIRROR = 7
```

---

## 5. 枚举清单

本节汇总项目中全部枚举定义，所有代码须引用此处规范，**不得自行重新定义**。

### 5.1 `ObjectType` — 场景物体类型

| 常量名 | 值 | 说明 |
|-------|-----|------|
| `OBJECT_UNKNOWN` | 0 | 未知物体 |
| `OBJECT_TOILET` | 1 | 马桶 |
| `OBJECT_URINAL` | 2 | 小便池 |
| `OBJECT_SINK` | 3 | 洗手台 |
| `OBJECT_FLOOR` | 4 | 地面 |
| `OBJECT_WALL` | 5 | 墙面 |
| `OBJECT_MIRROR` | 6 | 镜子 |
| `OBJECT_FAUCET` | 7 | 水龙头 |
| `OBJECT_OBSTACLE` | 8 | 障碍物（非清洁目标） |

> **定义位置**: `robot_interfaces/msg/SceneObject.msg`

---

### 5.2 `StainType` — 污渍类型

| 常量名 | 值 | 说明 |
|-------|-----|------|
| `STAIN_UNKNOWN` | 0 | 未知污渍 |
| `STAIN_WATER` | 1 | 水渍 |
| `STAIN_SOAP` | 2 | 皂液 |
| `STAIN_URINE` | 3 | 尿渍 |
| `STAIN_FECES` | 4 | 粪渍 |
| `STAIN_RUST` | 5 | 锈迹 |
| `STAIN_MOLD` | 6 | 霉斑 |

> **定义位置**: `robot_interfaces/msg/StainRegion.msg`

---

### 5.3 `TaskType` — 任务类型

| 常量名 | 值 | 说明 |
|-------|-----|------|
| `TASK_IDLE` | 0 | 空闲 |
| `TASK_CLEAN_TOILET` | 1 | 清洁马桶 |
| `TASK_CLEAN_URINAL` | 2 | 清洁小便池 |
| `TASK_CLEAN_SINK` | 3 | 清洁洗手台 |
| `TASK_CLEAN_FLOOR` | 4 | 清洁地面 |
| `TASK_CLEAN_WALL` | 5 | 清洁墙面 |
| `TASK_FULL_CLEAN` | 6 | 全厕清洁（组合任务） |

> **定义位置**: `robot_interfaces/msg/TaskStatus.msg`

---

### 5.4 `TaskStatusCode` — 任务执行状态码

| 常量名 | 值 | 说明 |
|-------|-----|------|
| `STATUS_IDLE` | 0 | 未激活 |
| `STATUS_PLANNING` | 1 | 规划中（路径/策略） |
| `STATUS_NAVIGATING` | 2 | 导航移动中 |
| `STATUS_EXECUTING` | 3 | 正在执行清洁动作 |
| `STATUS_VERIFYING` | 4 | 验证清洁结果 |
| `STATUS_PAUSED` | 5 | 已暂停（等待恢复） |
| `STATUS_COMPLETED` | 6 | 成功完成 |
| `STATUS_FAILED` | 7 | 执行失败 |
| `STATUS_ABORTED` | 8 | 被中止（外部命令） |

> **定义位置**: `robot_interfaces/msg/TaskStatus.msg`

---

### 5.5 `SkillType` — 技能类型

| 常量名 | 值 | 说明 |
|-------|-----|------|
| `SKILL_UNKNOWN` | 0 | 未知 |
| `SKILL_NAVIGATE` | 1 | 导航到站位 |
| `SKILL_SCAN_SCENE` | 2 | 扫描/感知场景 |
| `SKILL_APPROACH_TARGET` | 3 | 靠近目标物体 |
| `SKILL_SPRAY_DETERGENT` | 4 | 喷洒清洁剂 |
| `SKILL_WIPE_SURFACE` | 5 | 擦拭表面（机械臂） |
| `SKILL_SCRUB_SURFACE` | 6 | 刷洗表面 |
| `SKILL_FLUSH_WATER` | 7 | 冲水（马桶/小便池） |
| `SKILL_SQUEEGEE_FLOOR` | 8 | 刮水（地面刮板） |
| `SKILL_VERIFY_CLEAN` | 9 | 二次检测验证清洁结果 |
| `SKILL_STOW_ARM` | 10 | 收臂到安全位置 |
| `SKILL_CHANGE_TOOL` | 11 | 更换清洁工具头 |
| `SKILL_TELEOP` | 12 | 远程遥控模式 |

> **定义位置**: `robot_interfaces/msg/SkillState.msg`，在 [06_task_skill_bt_spec.md](06_task_skill_bt_spec.md) 中有详细字段规范。

---

### 5.6 `SkillStateCode` — 技能执行状态码

| 常量名 | 值 | 说明 |
|-------|-----|------|
| `SKILL_IDLE` | 0 | 未激活 |
| `SKILL_RUNNING` | 1 | 运行中 |
| `SKILL_SUCCESS` | 2 | 成功完成 |
| `SKILL_FAILURE` | 3 | 执行失败 |
| `SKILL_PREEMPTED` | 4 | 被抢占（高优先级任务介入） |

> **定义位置**: `robot_interfaces/msg/SkillState.msg`

---

### 5.7 `ToolType` — 末端清洁工具类型

| 常量名 | 值 | 说明 |
|-------|-----|------|
| `TOOL_NONE` | 0 | 无工具 / 裸爪 |
| `TOOL_BRUSH` | 1 | 刷头（坐便刷） |
| `TOOL_SPONGE` | 2 | 海绵擦 |
| `TOOL_SPRAY` | 3 | 喷头（清洁剂喷雾） |
| `TOOL_SCRUBBER` | 4 | 硬毛刷（顽固污渍） |
| `TOOL_SQUEEGEE` | 5 | 刮水板 |

> **定义位置**: `robot_interfaces/msg/GripperState.msg`

---

### 5.8 `SystemMode` — 系统运行模式

| 常量名 | 值 | 说明 |
|-------|-----|------|
| `MODE_STANDBY` | 0 | 待机（低功耗） |
| `MODE_CLEANING` | 1 | 自主清洁模式 |
| `MODE_TELEOP` | 2 | 遥控模式（Quest 3s） |
| `MODE_DATA_COLLECT` | 3 | 数据采集 / 示教模式 |
| `MODE_EMERGENCY` | 4 | 紧急停机 |

> **定义位置**: `robot_interfaces/srv/SetSystemMode.srv`

---

### 5.9 `AlertSeverity` — 告警严重等级

| 常量名 | 值 | 说明 |
|-------|-----|------|
| `SEVERITY_INFO` | 0 | 信息提示（无需干预） |
| `SEVERITY_WARNING` | 1 | 警告（不影响主任务） |
| `SEVERITY_ERROR` | 2 | 错误（需人工处理） |
| `SEVERITY_CRITICAL` | 3 | 致命（立即停机） |

> **定义位置**: `robot_interfaces/msg/SystemAlert.msg`

---

### 5.10 `CleanMode` — 清洁动作模式

| 常量名 | 值 | 说明 |
|-------|-----|------|
| `CLEAN_AUTO` | 0 | 自动选择（由感知结果决定） |
| `CLEAN_WIPE_HORIZONTAL` | 1 | 水平擦拭（适合台面/镜面） |
| `CLEAN_WIPE_VERTICAL` | 2 | 垂直擦拭（适合墙面） |
| `CLEAN_SCRUB` | 3 | 刷洗（适合顽固污渍） |
| `CLEAN_FLUSH` | 4 | 冲水清洗 |
| `CLEAN_SQUEEGEE` | 5 | 刮水 |

> **定义位置**: `robot_interfaces/action/CleanSurface.action`

---

### 5.11 `CleanQuality` — 清洁质量档位

| 常量名 | 值 | 说明 |
|-------|-----|------|
| `QUALITY_QUICK` | 0 | 快速清洁（单遍，满足基本卫生） |
| `QUALITY_STANDARD` | 1 | 标准清洁（多遍，正常运营） |
| `QUALITY_DEEP` | 2 | 深度清洁（全覆盖，适合特殊场合） |

> **定义位置**: `robot_interfaces/action/ExecuteCleaningPlan.action`

---

### 5.12 `NavigationPhase` — 导航与清洁执行阶段

| 常量名 | 值 | 说明 |
|-------|-----|------|
| `PHASE_NAVIGATING` | 0 | 导航移动阶段 |
| `PHASE_CLEANING` | 1 | 清洁执行阶段 |
| `PHASE_VERIFYING` | 2 | 验证核查阶段 |

> **定义位置**: `robot_interfaces/action/NavigateAndClean.action`

---

## 6. 坐标系与 TF 命名规范

### 6.1 坐标系命名

所有 TF 坐标系名称采用 **snake_case**，结构为：

```
<设备或身体部位>_<坐标系语义>_frame
```

| 坐标系名称 | 说明 |
|-----------|------|
| `map` | 全局地图坐标系（SLAM 原点） |
| `odom` | 里程计坐标系（累计漂移） |
| `base_link` | 机器人底盘中心 |
| `base_footprint` | 底盘在地面的投影（Nav2 规范） |
| `head_camera_link` | Gemini 305 相机光心 |
| `head_camera_depth_optical_frame` | Gemini 305 深度光学坐标系 |
| `head_camera_color_optical_frame` | Gemini 305 彩色光学坐标系 |
| `wrist_camera_link` | Gemini 330 相机光心 |
| `wrist_camera_depth_optical_frame` | Gemini 330 深度光学坐标系 |
| `wrist_camera_color_optical_frame` | Gemini 330 彩色光学坐标系 |
| `fr5_base_link` | FR5 机械臂基座（安装点） |
| `fr5_flange` | FR5 法兰盘 |
| `fr5_tool0` | FR5 工具坐标系原点（末端工具安装基准） |
| `cleaning_tool_tip` | 当前安装清洁工具的末端点 |

### 6.2 TF 树结构（简化）

```
map
└── odom
    └── base_footprint
        └── base_link
            ├── head_camera_link
            │   ├── head_camera_depth_optical_frame
            │   └── head_camera_color_optical_frame
            └── fr5_base_link
                └── fr5_link1 → ... → fr5_flange
                    └── fr5_tool0
                        └── cleaning_tool_tip
```

---

## 7. 参数（Parameter）命名规范

### 7.1 节点参数格式

参数名使用 **snake_case**，按功能分组使用点号（`.`）：

```
perception.confidence_threshold        # 感知置信度阈值
perception.camera_hint                 # 使用相机提示
arm.max_velocity_scale                 # 机械臂最大速度比例
arm.max_acceleration_scale
task.auto_replan                       # 任务失败后自动重规划
task.verify_after_each                 # 每段清洁后验证
vla.model_path                        # VLA 模型文件路径
vla.inference_device                   # "cuda:0" | "cpu"
vla.requires_confirmation              # 是否强制人工确认
tool.detergent_volume_per_spray_ml     # 每次喷雾量
slam.map_frame                         # SLAM 地图坐标系名
nav.goal_tolerance_m                   # 导航目标容差
```

### 7.2 参数默认值约定

所有节点参数**必须**在 `package/config/*.yaml` 中声明默认值，不得仅在代码中硬编码。

---

## 8. 日志（Log）规范

### 8.1 日志级别使用场景

| 级别 | 使用场景 |
|------|---------|
| `DEBUG` | 调试信息，发布版禁用 |
| `INFO` | 正常流程里程碑，如任务开始/完成 |
| `WARN` | 非预期但可恢复，如重试、降级 |
| `ERROR` | 执行失败，需要上层处理 |
| `FATAL` | 节点无法继续运行 |

### 8.2 日志格式模板

```
[<节点名>] [<模块>] <动作>: <可读描述> (<关键参数>)
```

示例：
```
[task_manager_node] [TaskFSM] Transition: STATUS_NAVIGATING -> STATUS_EXECUTING (task_id=TASK_CLEAN_TOILET_001)
[skill_executor_node] [SkillLib] Started: SKILL_WIPE_SURFACE, target=sink_01, force=3.5N
[arm_driver_node] [FR5Driver] Error: joint_3 velocity limit exceeded (vel=120 deg/s, limit=100 deg/s)
```

---

## 9. 版本管理规范

### 9.1 接口版本规则

| 变更类型 | 操作 |
|---------|------|
| 新增字段（追加） | 只更新注释说明，包版本次版本号 +1 |
| 新增枚举值（追加） | 只更新本文档，包版本次版本号 +1 |
| 重命名字段 | 破坏性变更，包版本主版本号 +1，并告知所有使用者 |
| 删除字段 | 破坏性变更，先废弃（注释），两个版本后删除 |
| 修改枚举值 | **严禁**，会导致编译后的节点语义错误 |

### 9.2 废弃字段标注示例

```
float32 confidence        # 置信度
bool requires_cleaning
# DEPRECATED since v1.2: use cleanliness_level instead
uint8 dirty_level         # 废弃字段，保留至 v2.0
uint8 cleanliness_level   # 清洁度（替代 dirty_level）
```

---

## 10. 参考

- [01_ros2_node_architecture.md](01_ros2_node_architecture.md) — Topic / Service / Action 完整列表
- [03_custom_messages.md](03_custom_messages.md) — 消息文件原始定义
- [06_task_skill_bt_spec.md](06_task_skill_bt_spec.md) — 技能与任务字段规范
- [ROS2 接口设计指南](https://docs.ros.org/en/humble/Concepts/About-ROS-Interfaces.html)
- [ROS2 命名规范 REP-144](https://www.ros.org/reps/rep-0144.html)
