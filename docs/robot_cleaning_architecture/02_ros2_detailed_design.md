# ROS2 节点、Topic、Action、Service、数据流详细设计

> **版本**: v1.0  
> **文档编号**: 02  
> **适用读者**: ROS2 软件工程师、算法工程师  
> **前置阅读**: [01_ros2_node_architecture.md](01_ros2_node_architecture.md)

---

## 1. 节点详细设计

### 1.1 `perception_node`（感知节点）

**包名**: `robot_perception`  
**运行平台**: x86（4090 / Thor）  
**线程模型**: 多回调组（Reentrant CallbackGroup）

#### 订阅

| Topic | 类型 | 处理逻辑 |
|-------|------|---------|
| `/head_camera/color/image_raw` | `sensor_msgs/Image` | 送入场景分割模型（SegFormer / YOLOv8-seg） |
| `/head_camera/depth/image_raw` | `sensor_msgs/Image` | 与 RGB 融合，生成带深度的语义点云 |
| `/wrist_camera/color/image_raw` | `sensor_msgs/Image` | 局部污渍检测（精细分类） |
| `/wrist_camera/depth/image_raw` | `sensor_msgs/Image` | 腕部近距离深度，用于接触点定位 |
| `/fr5/end_effector_pose` | `geometry_msgs/PoseStamped` | 更新工具坐标系，配合腕部相机外参 |

#### 发布

| Topic | 类型 | 触发条件 |
|-------|------|---------|
| `/perception/scene_objects` | `robot_interfaces/SceneObjectArray` | 每 200 ms（5 Hz）定时 |
| `/perception/stain_map` | `robot_interfaces/StainMap` | 每 200 ms（5 Hz）定时 |
| `/perception/surface_normal` | `geometry_msgs/PoseArray` | 收到清洁目标请求后触发 |

#### 提供 Service

| 服务名 | 类型 | 逻辑 |
|--------|------|------|
| `/perception/detect_scene` | `robot_interfaces/DetectScene` | 同步完整场景检测，返回物体列表和分割掩码 |
| `/perception/locate_target` | `robot_interfaces/LocateTarget` | 定位指定类别目标（马桶/洗手台等），返回 3D 位姿 |

#### 关键参数

```yaml
perception_node:
  ros__parameters:
    model_path: "/opt/robot/models/segformer_toilet.onnx"
    stain_model_path: "/opt/robot/models/stain_detector.onnx"
    head_camera_frame: "head_camera_link"
    wrist_camera_frame: "wrist_camera_link"
    detection_confidence_threshold: 0.65
    stain_area_threshold_cm2: 2.0
    publish_rate_hz: 5.0
```

---

### 1.2 `task_manager_node`（任务管理节点）

**包名**: `robot_task_manager`  
**运行平台**: x86  
**核心机制**: BehaviorTree.CPP v4 行为树引擎

#### 订阅

| Topic | 类型 | 用途 |
|-------|------|------|
| `/perception/scene_objects` | `robot_interfaces/SceneObjectArray` | 更新任务队列中的清洁目标 |
| `/perception/stain_map` | `robot_interfaces/StainMap` | 实时调整清洁优先级 |
| `/skill/execution_state` | `robot_interfaces/SkillState` | 监控技能执行进度 |
| `/vla/action_proposal` | `robot_interfaces/ActionProposal` | 接收 VLA 高层指令 |
| `/system/alert` | `robot_interfaces/SystemAlert` | 响应系统告警（暂停/中止） |

#### 发布

| Topic | 类型 | 用途 |
|-------|------|------|
| `/task/current_task` | `robot_interfaces/TaskStatus` | 广播当前任务状态 |

#### 调用 Action

| Action | 用途 |
|--------|------|
| `/nav/navigate_and_clean` | 驱动底盘到清洁点 |
| `/task/execute_cleaning_plan` | 对外暴露的整体清洁方案 |
| `/vla/plan_task` | 触发 VLA 规划 |

#### 提供 Service

| 服务 | 说明 |
|------|------|
| `/task/pause` | 暂停行为树 |
| `/task/resume` | 恢复行为树 |
| `/task/abort` | 中止并回归安全状态 |

#### 状态机模型（简化）

```
IDLE
  │ 接收 VLA 建议 / 外部指令
  ▼
PLANNING          ← 感知场景 + 生成清洁计划
  │
  ▼
EXECUTING_TASK    ← 行为树逐区域推进
  │ 检测到区域完成
  ▼
VERIFYING         ← 二次污渍检测
  │ 通过 / 失败
  ├──▶ EXECUTING_TASK (补清)
  └──▶ NEXT_TARGET / IDLE
```

---

### 1.3 `skill_executor_node`（技能执行节点）

**包名**: `robot_skill_lib`  
**运行平台**: x86  
**职责**: 将高层技能描述翻译为机械臂 + 工具的具体动作序列

#### 技能列表

| 技能 ID | 名称 | 参数 |
|---------|------|------|
| `SKILL_WIPE_HORIZONTAL` | 水平擦拭 | 起始点, 终止点, 力度, 往返次数 |
| `SKILL_WIPE_VERTICAL` | 竖向擦拭 | 同上 |
| `SKILL_WIPE_CIRCULAR` | 画圆擦拭 | 中心点, 半径, 圈数 |
| `SKILL_SPRAY_DETERGENT` | 喷洒清洁剂 | 位置, 喷雾量（mL） |
| `SKILL_SCRUB_TOILET_BOWL` | 马桶内壁刷洗 | 目标区域 ROI |
| `SKILL_FLUSH_RINSE` | 冲水/漂洗 | 持续时间 |
| `SKILL_MOVE_ARM_SAFE` | 机械臂安全收回 | — |
| `SKILL_APPROACH_TARGET` | 接近目标点 | 目标位姿, 接近向量 |

#### Action 服务端

| Action | 说明 |
|--------|------|
| `/arm/clean_surface` | 接收清洁面参数，编排子技能序列，带逐步进度反馈 |

#### 调用 Action（作为 Client）

| Action | 说明 |
|--------|------|
| `/arm/move_to_pose_planned` | MoveIt2 规划移动 |
| `/arm/execute_trajectory` | 直接轨迹执行 |

---

### 1.4 `vla_inference_node`（VLA 推理节点）

**包名**: `robot_vla`  
**运行平台**: x86（必须 GPU）  
**模型**: 基于 OpenVLA / RoboFlamingo 微调，输入语言指令 + 视觉帧，输出动作 token 序列

#### 数据流

```
语言指令（ROS param / 外部 API）
            +
/head_camera/color/image_raw  ──▶  VLA 模型推理  ──▶  /vla/action_proposal
/perception/scene_objects     ──▶                ──▶  /task/execute_cleaning_plan (action)
```

#### Action 服务端

| Action | 输入 | 输出 |
|--------|------|------|
| `/vla/plan_task` | 自然语言指令 + 场景快照 | 结构化任务序列 `TaskPlan` |

#### 关键参数

```yaml
vla_inference_node:
  ros__parameters:
    model_path: "/opt/robot/models/vla_toilet_v1.ckpt"
    inference_backend: "tensorrt"  # tensorrt | torch
    max_sequence_length: 64
    temperature: 0.1
    gpu_device_id: 0
```

---

### 1.5 `arm_planner_node`（机械臂规划节点）

**包名**: `robot_arm_planner`  
**运行平台**: x86  
**依赖**: MoveIt2，FR5 URDF  

#### 核心功能

1. **IK 求解**: 使用 KDL / TRAC-IK 求解器，支持冗余消解。
2. **碰撞检测**: 基于 FCL 库，实时检测机械臂与环境碰撞。
3. **轨迹优化**: Time-Optimal Path Parameterization (TOPP-RA)。
4. **力控模式**: 通过 FR5 力矩传感器接口实现末端力控清洁。

#### Action 服务端

| Action | 类型 | 说明 |
|--------|------|------|
| `/arm/move_to_pose_planned` | `robot_interfaces/MoveToPosePlanned` | 规划 + 执行，带中间进度 |

#### 调用 Action（作为 Client）

| Action | 说明 |
|--------|------|
| `/arm/execute_trajectory` | 将规划轨迹发给驱动节点执行 |

---

## 2. 数据流端到端追踪

### 场景：清洁马桶

```
① head_camera_node 发布 /head_camera/color/image_raw (30Hz)
        │
② perception_node 检测到"马桶"物体，发布 /perception/scene_objects
        │
③ task_manager_node 接收，将"马桶清洁"加入任务队列
        │
④ task_manager_node 调用 /perception/locate_target (service) 获取马桶 3D 位姿
        │
⑤ task_manager_node 通过行为树调用 Action /nav/navigate_and_clean
   nav_node 驱动底盘移动到马桶前方指定站位
        │
⑥ task_manager_node 调用 Action /arm/clean_surface → skill_executor_node
        │
⑦ skill_executor_node 拆解为：
   a. SKILL_MOVE_ARM_SAFE → 安全展开机械臂
   b. SKILL_SPRAY_DETERGENT → 喷洒清洁剂（/tool/spray service）
   c. SKILL_SCRUB_TOILET_BOWL → 调用 /arm/move_to_pose_planned (MoveIt2)
      + /arm/execute_trajectory (FR5 驱动)
   d. SKILL_FLUSH_RINSE → 冲水控制
   e. SKILL_MOVE_ARM_SAFE → 收回机械臂
        │
⑧ perception_node 二次扫描：wrist_camera 检测残余污渍
   → 如仍有污渍，重复步骤 ⑥（力度增大）
        │
⑨ task_manager_node 标记"马桶清洁完成"，更新 /task/current_task
```

---

## 3. QoS 配置建议

| 数据类型 | 可靠性 | 持久性 | 历史深度 | 说明 |
|---------|--------|--------|---------|------|
| 相机图像 | Best Effort | Volatile | 1 | 高频，丢帧可接受 |
| 点云 | Best Effort | Volatile | 1 | 高频大数据 |
| 任务状态 | Reliable | Transient Local | 5 | 晚连接节点需接收最新状态 |
| 告警 | Reliable | Volatile | 10 | 不允许丢失 |
| 控制指令（速度/轨迹） | Reliable | Volatile | 1 | 实时性优先 |
| Service 调用 | Reliable | — | — | 默认 |
| Action 调用 | Reliable | — | — | 默认 |

---

## 4. 跨平台通信（x86 ↔ RK3588）

两台机器运行同一 ROS2 域（`ROS_DOMAIN_ID` 相同），通过 **Fast DDS over Ethernet** 自动发现。

```
x86 主控                          RK3588 底盘
─────────────────────────────────────────────────────
task_manager_node ──▶  /nav/navigate_and_clean ──▶  nav_node (RK3588)
                                                          │
                    /slam/map ◀──────────────────── slam_node (RK3588)
                    /slam/pose ◀─────────────────── slam_node (RK3588)
                    /odom ◀──────────────────────── 底盘驱动
```

**网络要求**:
- 千兆以太网，延迟 < 1 ms（同一局域网）
- 建议划分独立 VLAN 隔离机器人网络

---

## 5. 启动文件结构（Launch）

```
robot_bringup/
├── launch/
│   ├── full_system.launch.py          # 全系统启动
│   ├── perception_only.launch.py      # 仅感知模块调试
│   ├── arm_only.launch.py             # 仅机械臂调试
│   ├── nav_only.launch.py             # 仅导航调试
│   └── teleop_data_collect.launch.py  # Quest 示教采集模式
├── config/
│   ├── perception_params.yaml
│   ├── arm_planner_params.yaml
│   ├── nav_params.yaml
│   ├── task_manager_params.yaml
│   └── vla_params.yaml
```

---

## 6. 关键依赖版本矩阵

| 软件 | 版本 | 说明 |
|------|------|------|
| ROS2 | Humble (LTS) | 主控 + RK3588 |
| MoveIt2 | 2.5.x | 机械臂规划 |
| Nav2 | Humble | 底盘导航 |
| BehaviorTree.CPP | 4.x | 行为树引擎 |
| OrbbecSDK_ROS2 | ≥ 1.8 | 相机驱动 |
| ONNX Runtime | ≥ 1.16 | 感知模型推理 |
| TensorRT | ≥ 8.6 | VLA GPU 推理 |
| Fast DDS | 2.x | ROS2 默认 DDS |
