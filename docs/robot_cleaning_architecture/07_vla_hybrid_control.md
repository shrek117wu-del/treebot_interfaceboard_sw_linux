# VLA + 技能库混合控制架构详细方案

> **版本**: v1.0  
> **文档编号**: 07  
> **适用读者**: 算法工程师、系统架构师  
> **前置阅读**: [01_ros2_node_architecture.md](01_ros2_node_architecture.md), [06_task_skill_bt_spec.md](06_task_skill_bt_spec.md)

---

## 1. 架构总览

轮臂式厕所清洁机器人采用 **VLA（Vision-Language-Action）模型 + 技能库** 两级混合控制架构，兼顾高层语义推理的灵活性与底层运动控制的精准性和安全性。

```
┌────────────────────────────────────────────────────────────────────┐
│                       外部指令层                                    │
│  自然语言 / REST API / 远程调度平台                                  │
└───────────────────────────────┬────────────────────────────────────┘
                                │ 语言指令 + 场景图像
                                ▼
┌────────────────────────────────────────────────────────────────────┐
│                   VLA 推理层（x86 GPU）                             │
│                                                                    │
│  输入：RGB 图像 + 自然语言指令 + 上下文                              │
│  模型：OpenVLA / π₀ / RoboFlamingo（可选）                          │
│  输出：任务序列 ActionProposal（高层语义动作列表）                    │
│                                                                    │
│  /vla/plan_task (Action)                                           │
│  /vla/action_proposal (Topic，实时建议）                            │
└───────────────────────────────┬────────────────────────────────────┘
                                │ ActionProposal
                                ▼
┌────────────────────────────────────────────────────────────────────┐
│                  任务管理层 task_manager_node                        │
│                                                                    │
│  功能：解析 ActionProposal → 生成 Task 队列 → 驱动行为树              │
│  引擎：BehaviorTree.CPP v4                                          │
│  输出：Skill 调用请求                                                │
│                                                                    │
│  /task/execute_cleaning_plan (Action)                              │
│  /task/current_task (Topic)                                        │
└───────────────────────────────┬────────────────────────────────────┘
                                │ SkillContext
                                ▼
┌────────────────────────────────────────────────────────────────────┐
│               技能执行层 skill_executor_node                         │
│                                                                    │
│  功能：原子清洁动作实现（导航、感知、喷雾、擦拭、刷洗、刮水、验证）     │
│  /skill/execution_state (Topic)                                    │
│  /arm/clean_surface (Action)                                       │
└─────────────────┬───────────────────────────┬──────────────────────┘
                  │                           │
                  ▼                           ▼
┌──────────────────────┐          ┌─────────────────────────┐
│  运动规划层           │          │  底层控制层              │
│  arm_planner_node    │          │  arm_driver_node (FR5)   │
│  MoveIt2 + IK 规划   │──────►   │  关节轨迹执行            │
│  /arm/execute_traj   │          │  力控反馈                 │
└──────────────────────┘          └──────────────┬──────────┘
                                                 │
                                                 ▼
                                     ┌──────────────────────┐
                                     │  FR5 机械臂硬件       │
                                     │  TCP/IP 通信          │
                                     └──────────────────────┘
```

---

## 2. VLA 模型选型与集成

### 2.1 候选模型对比

| 模型 | 参数量 | 推理延迟 (A100) | 输出类型 | 适用阶段 |
|------|--------|---------------|---------|---------|
| **OpenVLA-7B** | 7B | ~150 ms | 关节角度 delta / 语言动作 | 前期科研验证 |
| **π₀ (pi-zero)** | ~3B | ~80 ms | 语言动作序列 | 前期集成测试 |
| **RoboFlamingo** | 3B | ~100 ms | 语言动作 token | 前期集成测试 |
| **自训 VLA（微调版）** | 7B+ | ~200 ms (Thor) | 结构化任务序列 | 量产最终目标 |

> **当前选型**：前期以 **OpenVLA-7B** 为基础，在 x86 + RTX 4090 上部署验证；量产阶段切换至 x86 + Thor（NVIDIA Thor SoC），结合量化（INT8/FP8）将推理延迟控制在 100ms 以内。

### 2.2 VLA 模型输入规范

| 输入域 | 格式 | 来源 | 说明 |
|--------|------|------|------|
| 场景图像 | `sensor_msgs/Image`，RGB，640×480，JPEG 压缩 | `head_camera_node` | 头部 Gemini 305 实时图像 |
| 自然语言指令 | UTF-8 字符串，最长 256 字节 | 外部 API / 调度平台 | 如 "请清洁马桶 A3" |
| 场景上下文 | `robot_interfaces/SceneObjectArray` | `perception_node` | 感知到的物体列表（可选辅助） |
| 历史动作 | `string[]`，最近 5 步 | Blackboard | 用于多步决策连续性 |

### 2.3 VLA 模型输出规范

VLA 输出经过结构化解析，映射到 `ActionProposal.msg`：

```
# VLA 原始输出（JSON 格式字符串，内部中间格式）
{
  "proposal_id": "VLA-20260409-001",
  "confidence": 0.92,
  "requires_confirmation": false,
  "proposed_task_sequence": [
    "SKILL_NAVIGATE:target=toilet_01",
    "SKILL_SCAN_SCENE:camera=head",
    "SKILL_APPROACH_TARGET:target=toilet_01",
    "SKILL_SPRAY_DETERGENT:volume_ml=30",
    "SKILL_SCRUB_SURFACE:mode=SCRUB,force=8.0N,passes=3",
    "SKILL_FLUSH_WATER:target=toilet_01",
    "SKILL_VERIFY_CLEAN:target=toilet_01,min_coverage=0.85",
    "SKILL_STOW_ARM"
  ]
}
```

### 2.4 VLA → ActionProposal 解析规则

`vla_inference_node` 内部 `VLAOutputParser` 模块负责将 JSON 格式的模型输出解析为 `ActionProposal.msg`：

```python
# robot_vla/robot_vla/vla_output_parser.py

class VLAOutputParser:
    SKILL_TOKEN_MAP = {
        "SKILL_NAVIGATE":         SkillType.SKILL_NAVIGATE,
        "SKILL_SCAN_SCENE":       SkillType.SKILL_SCAN_SCENE,
        "SKILL_APPROACH_TARGET":  SkillType.SKILL_APPROACH_TARGET,
        "SKILL_SPRAY_DETERGENT":  SkillType.SKILL_SPRAY_DETERGENT,
        "SKILL_WIPE_SURFACE":     SkillType.SKILL_WIPE_SURFACE,
        "SKILL_SCRUB_SURFACE":    SkillType.SKILL_SCRUB_SURFACE,
        "SKILL_FLUSH_WATER":      SkillType.SKILL_FLUSH_WATER,
        "SKILL_SQUEEGEE_FLOOR":   SkillType.SKILL_SQUEEGEE_FLOOR,
        "SKILL_VERIFY_CLEAN":     SkillType.SKILL_VERIFY_CLEAN,
        "SKILL_STOW_ARM":         SkillType.SKILL_STOW_ARM,
        "SKILL_CHANGE_TOOL":      SkillType.SKILL_CHANGE_TOOL,
    }

    def parse(self, raw_json: str) -> ActionProposal:
        data = json.loads(raw_json)
        proposal = ActionProposal()
        proposal.proposal_id = data["proposal_id"]
        proposal.confidence = data["confidence"]
        proposal.requires_confirmation = data["requires_confirmation"]
        proposal.proposed_task_sequence = data["proposed_task_sequence"]
        proposal.natural_language_instruction = self._last_instruction
        return proposal
```

---

## 3. VLA 与技能库的控制权交接协议

### 3.1 控制模式定义

| 模式 | 触发条件 | 控制权 | 说明 |
|------|---------|--------|------|
| `VLA_PLAN` | 收到语言指令 | VLA → task_manager | VLA 规划任务序列，任务管理器执行 |
| `VLA_DIRECT` | 实验性，未来支持 | VLA → skill_executor | VLA 直接输出关节增量（不推荐量产） |
| `SKILL_ONLY` | 无 GPU / VLA 离线 | 预设规则 → task_manager | 纯技能库规则执行，无 VLA |
| `TELEOP` | Quest 3s 触发 | 操作员 → arm_planner | 遥控模式，VLA 禁用 |

### 3.2 控制权交接流程（VLA_PLAN 模式）

```
外部指令                VLA节点              任务管理器          技能执行器
    │                      │                      │                  │
    │── VLAPlanTask.Goal ──►│                      │                  │
    │                      │                      │                  │
    │              [模型推理 ~150ms]               │                  │
    │                      │                      │                  │
    │                      │── ActionProposal ────►│                  │
    │                      │  (Topic 广播)         │                  │
    │                      │                      │[解析成 Task 队列] │
    │                      │                      │── SkillContext ──►│
    │                      │                      │                  │[执行 Skill]
    │                      │── VLAPlanTask.Result ─►                  │
    │◄─────────────────────────────────────────────│                  │
    │  (任务完成回调)        │                      │                  │
```

### 3.3 安全降级策略

```
VLA 推理超时（> 500ms）
    ├── 一级降级：使用上一次有效 ActionProposal
    └── 二级降级：切换 SKILL_ONLY 模式，执行预设清洁规则

VLA 置信度 < 0.7
    ├── requires_confirmation = true
    └── 等待人工确认（通过 /task/resume 触发）

VLA 节点崩溃
    └── system_monitor 检测心跳超时 → 发布 SystemAlert CRITICAL
        └── task_manager 收到告警 → 暂停当前任务
```

---

## 4. 数据采集与模型训练流程

### 4.1 采集架构（Meta Quest 3s）

```
Meta Quest 3s
    │ 6DoF 手部位姿（90 Hz）
    │ 触发器按钮事件
    │
    ▼
quest_teleop_node
    │ /quest/hand_pose_right (90 Hz)
    │ /quest/trigger_event (事件驱动)
    │
    ▼
arm_planner_node（遥控模式，IK 反解）
    │ /arm/move_to_pose_planned (Action)
    │
    ▼
arm_driver_node (FR5)
    │ 关节轨迹执行
    │
    ▼ (同步记录)
数据采集节点 (robot_data_collector)
    │
    ├── 录制话题: /head_camera/color/image_raw
    │             /wrist_camera/color/image_raw
    │             /fr5/joint_states
    │             /fr5/end_effector_pose
    │             /quest/hand_pose_right
    │             /quest/trigger_event
    │             /tool/status
    │
    └── 写入 ROS2 bag 文件 (mcap 格式)
        命名规范: {场景}_{操作员}_{YYYYMMDD_HHMMSS}.mcap
        示例: toilet_wipe_op01_20260409_143200.mcap
```

### 4.2 数据集格式规范

采集完成后，通过数据处理流水线转换为 VLA 训练格式：

```python
# /data/processing/convert_to_lerobot.py
# 转换为 LeRobot 数据格式（兼容 OpenVLA 微调）

Dataset Schema:
  observation.images.head_camera:    (H, W, 3) uint8 RGB
  observation.images.wrist_camera:   (H, W, 3) uint8 RGB
  observation.state.joint_positions: (7,) float32  # FR5 7轴
  observation.state.end_effector:    (7,) float32  # xyz + quaternion
  observation.state.tool_type:       int32
  action.joint_delta:                (7,) float32  # 关节角增量
  action.task_label:                 str           # 任务标签
  language_instruction:              str           # 采集时注解
  timestamp:                         float64
```

### 4.3 VLA 微调流程

```
1. 原始数据采集（Meta Quest 3s 示教）
   └── 目标：每个清洁场景 ≥ 200 集轨迹

2. 数据标注与过滤
   ├── 自动过滤：去除力限触发、碰撞帧
   └── 人工标注：添加语言指令注解

3. 数据增强
   ├── 图像：颜色抖动、随机裁剪、亮度变化
   └── 轨迹：噪声注入 (σ=0.001 rad)

4. 微调训练（基于 OpenVLA-7B）
   ├── 硬件：x86 + RTX 4090（研发），多卡集群（大规模）
   ├── 框架：HuggingFace Transformers + LoRA
   ├── 学习率：1e-4（LoRA），1e-5（全量）
   └── 评估指标：动作预测误差 RMSE、任务成功率

5. 模型评估与部署
   ├── 仿真验证：Isaac Sim / Gazebo
   ├── 真机验证：10 轮次完整清洁测试
   └── 部署：ONNX Runtime / TensorRT（Thor 量产）
```

---

## 5. VLA 推理节点实现规范

### 5.1 节点参数配置

```yaml
# robot_vla/config/vla_params.yaml

vla_inference_node:
  ros__parameters:
    # 模型配置
    model_path: "/opt/robot_models/openvla_toilet_v1.pt"
    model_type: "openvla"         # openvla | pi_zero | custom
    inference_device: "cuda:0"    # cuda:0 | cpu
    precision: "fp16"             # fp32 | fp16 | int8

    # 推理配置
    max_inference_time_ms: 500    # 超时阈值，超过则降级
    confidence_threshold: 0.70    # 低于此值需人工确认
    history_length: 5             # 历史动作长度

    # 输入图像配置
    image_width: 640
    image_height: 480
    image_topic: "/head_camera/color/image_raw"

    # 输出配置
    requires_confirmation_default: false
    publish_action_proposal: true
```

### 5.2 节点主逻辑（伪代码）

```python
# robot_vla/robot_vla/vla_inference_node.py

class VLAInferenceNode(Node):
    def __init__(self):
        super().__init__('vla_inference_node')
        # 加载模型
        self.model = VLAModel.load(self.get_parameter('model_path').value)
        # ROS2 接口
        self.action_server = ActionServer(self, VLAPlanTask, '/vla/plan_task',
                                          self._execute_plan_callback)
        self.proposal_pub = self.create_publisher(ActionProposal,
                                                  '/vla/action_proposal', 10)
        self.image_sub = self.create_subscription(Image,
                          self.get_parameter('image_topic').value,
                          self._image_callback, 10)

    def _execute_plan_callback(self, goal_handle):
        goal = goal_handle.request
        start_time = self.get_clock().now()

        # 获取最新图像（如 goal 未附带）
        image = goal.scene_image if goal.scene_image.data else self._latest_image

        # 执行推理
        feedback = VLAPlanTask.Feedback()
        feedback.status_message = "正在推理..."
        feedback.inference_progress_percent = 0.0
        goal_handle.publish_feedback(feedback)

        raw_output = self.model.infer(
            image=image,
            instruction=goal.instruction,
            context=goal.scene_context,
            history=self._action_history[-5:],
        )

        # 解析输出
        proposal = self._parser.parse(raw_output)
        proposal.header.stamp = self.get_clock().now().to_msg()

        # 发布建议
        self.proposal_pub.publish(proposal)

        # 构造结果
        result = VLAPlanTask.Result()
        result.success = True
        result.proposal = proposal
        result.task_steps = proposal.proposed_task_sequence
        elapsed = (self.get_clock().now() - start_time).nanoseconds / 1e9
        result.inference_time_sec = elapsed

        goal_handle.succeed()
        return result
```

---

## 6. 技能库与实时运动控制

### 6.1 力控清洁技能（`SKILL_WIPE_SURFACE`）实现要点

擦拭类技能采用 **阻抗控制（Impedance Control）** 实现稳定的接触力：

```
目标力 F_target（由 SkillContext.target_force_n 传入）
         │
         ▼
┌─────────────────────────────────────────────────┐
│  阻抗控制器（笛卡尔空间）                          │
│                                                 │
│  F_err = F_target - F_actual（力传感器）          │
│  δx = (1/K_s) × F_err   (刚度系数 K_s)           │
│  x_ref = x_surface + δx (法向补偿）              │
│                                                 │
│  输出：修正后的末端位姿 x_ref                      │
└────────────────────┬────────────────────────────┘
                     │ 修正位姿
                     ▼
            MoveIt2 IK 求解
                     │ 关节角
                     ▼
            FR5 关节轨迹执行
                     │
                     ▼
            力传感器反馈（FR5 末端力矩）
```

### 6.2 清洁路径规划

| 清洁对象 | 路径策略 | 路径点来源 |
|---------|---------|-----------|
| 马桶坐圈 | 环绕闭合曲线 | 预定义 + 感知法向量修正 |
| 洗手台台面 | 光栅扫描（Boustrophedon） | 感知 BoundingBox 生成 |
| 墙面 | 垂直光栅扫描 | 感知 BoundingBox + 法向量 |
| 地面 | 覆盖路径规划（CPP） | Nav2 地图 + 障碍物避让 |
| 小便池 | 顶部到底部弓形路径 | 预定义 + 感知高度调整 |

### 6.3 技能库与 MoveIt2 集成

```cpp
// robot_skill_lib/src/skills/wipe_surface_skill.cpp

BT::NodeStatus WipeSurfaceSkill::tick() {
    if (status_ == SkillStateCode::SKILL_IDLE) {
        onStart();
        // 1. 获取感知结果（从 Blackboard）
        auto scene = getInput<SceneObjectArray>("scene_objects");
        auto target_id = getInput<std::string>("target_object_id");

        // 2. 生成清洁路径
        auto waypoints = path_planner_->generateWipePath(
            scene->findById(target_id), ctx_.clean_mode);

        // 3. 发送 MoveIt2 规划 Action
        auto goal = MoveToPosePlanned::Goal();
        goal.target_pose = waypoints[0];
        goal.velocity_scale = ctx_.speed_scale;
        goal.cartesian_path = true;
        goal.eef_step = 0.005;  // 5mm 步长

        moveit_client_->async_send_goal(goal, callbacks_);
        status_ = SkillStateCode::SKILL_RUNNING;
    }

    // 检查执行进度
    if (moveit_result_ready_) {
        if (moveit_success_) {
            setProgress(100.0, "擦拭完成");
            status_ = SkillStateCode::SKILL_SUCCESS;
            return BT::NodeStatus::SUCCESS;
        } else {
            status_ = SkillStateCode::SKILL_FAILURE;
            return BT::NodeStatus::FAILURE;
        }
    }

    // 发布中间进度
    setProgress(current_waypoint_idx_ * 100.0 / total_waypoints_);
    return BT::NodeStatus::RUNNING;
}
```

---

## 7. 系统性能指标（目标）

| 指标 | 目标值 | 当前状态 |
|------|--------|---------|
| VLA 推理延迟（4090） | ≤ 200 ms | 待验证 |
| VLA 推理延迟（Thor） | ≤ 100 ms | 量产目标 |
| 技能执行循环频率 | ≥ 10 Hz | 依赖 MoveIt2 规划 |
| 力控带宽 | ≥ 125 Hz | FR5 原生支持 |
| 任务成功率（单场景） | ≥ 95% | 训练目标 |
| 全厕清洁时间 | ≤ 20 分钟 | 工程目标 |
| 端到端感知→动作延迟 | ≤ 500 ms | 系统目标 |

---

## 8. 计算资源分配

### 8.1 前期（x86 + RTX 4090）

| 进程 / 节点 | 计算资源 | 说明 |
|------------|---------|------|
| VLA 推理 | GPU 全量（~20 GB VRAM） | OpenVLA-7B FP16 |
| 感知（分割+检测） | GPU 共享（~4 GB） | YOLOv8 + SAM |
| MoveIt2 规划 | CPU 6核 | OMPL 规划 |
| Nav2 导航（RK3588） | CPU 4核（板端） | 底盘独立运行 |
| ROS2 DDS | CPU 2核 | Fast DDS |

### 8.2 量产（x86 + NVIDIA Thor）

| 进程 / 节点 | 计算资源 | 说明 |
|------------|---------|------|
| VLA 推理 | Thor GPU（≥ 2000 TOPS） | INT8 量化，TensorRT 加速 |
| 感知 | Thor GPU 共享 | 与 VLA 分时共享 |
| MoveIt2 | Thor CPU 大核 4核 | ARM64 |
| 系统监控 | Thor CPU 小核 | 低功耗后台 |

---

## 9. 仿真验证环境

在真机部署前，VLA + 技能库联合在以下仿真环境中验证：

| 仿真平台 | 用途 | 配置要点 |
|---------|------|---------|
| Isaac Sim 4.x | 物理仿真 + GPU 渲染 | FR5 URDF 导入，接触力仿真 |
| Gazebo Harmonic | 集成测试 | ROS2 Bridge，Nav2 集成 |
| MoveIt2 RViz | 运动规划可视化 | 碰撞检测调试 |

仿真数据也可用于 VLA 数据增强：

```bash
# 在 Isaac Sim 中录制仿真 bag
ros2 bag record -o sim_toilet_wipe_01 \
  /head_camera/color/image_raw \
  /wrist_camera/color/image_raw \
  /fr5/joint_states \
  /fr5/end_effector_pose
```

---

## 10. 参考

- [06_task_skill_bt_spec.md](06_task_skill_bt_spec.md) — Skill 字段与 BT 规范
- [08_state_machine_bt_design.md](08_state_machine_bt_design.md) — 完整行为树设计
- [OpenVLA 论文](https://arxiv.org/abs/2406.09246)
- [π₀ (Physical Intelligence)](https://www.physicalintelligence.company/blog/pi0)
- [BehaviorTree.CPP v4](https://www.behaviortree.dev/)
- [MoveIt2 阻抗控制](https://moveit.picknik.ai/main/doc/examples/realtime_servo/realtime_servo_tutorial.html)
- [FR5 ROS2 驱动](https://fairino-doc-zhs.readthedocs.io/3.9.3/ROSGuide/ros2guide.html)
- [Quest2ROS2](https://github.com/Taokt/Quest2ROS2)
