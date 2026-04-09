# Task / Skill / BT 状态机字段规范

> **版本**: v1.0  
> **文档编号**: 06  
> **适用读者**: ROS2 软件工程师、算法工程师  
> **前置阅读**: [03_custom_messages.md](03_custom_messages.md), [05_interface_naming_conventions.md](05_interface_naming_conventions.md)

---

## 1. 设计目标

本文档定义轮臂式厕所清洁机器人三层执行体系的**数据结构与状态机字段规范**：

```
┌─────────────────────────────────────────────────────────────┐
│  层级 3 — Task（任务层）                                      │
│  由 VLA 或外部指令触发，对应一个清洁场景（马桶、地面…）         │
│  内部由行为树（BehaviorTree.CPP v4）驱动多个 Skill           │
├─────────────────────────────────────────────────────────────┤
│  层级 2 — Skill（技能层）                                     │
│  原子可复用清洁动作，对应 SkillType 枚举                        │
│  内部可包含有限状态机（FSM）或简单序列                          │
├─────────────────────────────────────────────────────────────┤
│  层级 1 — Primitive（原语层）                                 │
│  底层运动指令：关节轨迹、笛卡尔运动、速度指令、工具控制          │
└─────────────────────────────────────────────────────────────┘
```

---

## 2. Task（任务）字段规范

### 2.1 Task 数据结构

```cpp
// robot_task_manager/include/task_manager/task_types.hpp

struct Task {
    // ── 标识字段 ──────────────────────────────────
    std::string task_id;          // UUID v4，格式: "TASK-XXXXXXXX"
    TaskType    task_type;        // 见枚举 5.3（05_interface_naming_conventions.md）
    std::string description;      // 可读描述，如 "清洁马桶 #A3"
    int         priority;         // 优先级 [0-9]，数值越大越高

    // ── 目标字段 ──────────────────────────────────
    std::string        target_object_id;   // 目标物体 ID（如 "toilet_01"）
    geometry_msgs::PoseStamped station_pose;  // 清洁站位目标点
    CleanQuality       quality;            // 清洁质量档位

    // ── 状态字段 ──────────────────────────────────
    TaskStatusCode     status;             // 当前状态码
    std::string        current_step;       // 当前执行步骤描述
    float              progress_percent;   // 整体进度 [0.0, 100.0]

    // ── 时间字段 ──────────────────────────────────
    rclcpp::Time  created_at;
    rclcpp::Time  started_at;
    rclcpp::Time  completed_at;
    double        timeout_sec;            // 最大执行时限，默认 300s

    // ── 结果字段 ──────────────────────────────────
    bool        success;
    std::string failure_reason;
    float       cleaned_area_percent;    // 清洁覆盖率（验证后填写）
};
```

### 2.2 Task ID 格式约定

```
TASK-{8位大写十六进制}-{TaskType枚举值字符串}
示例:
  TASK-A3F19C02-TOILET
  TASK-00B7E411-FLOOR
  TASK-FF102233-FULL_CLEAN
```

### 2.3 Task 状态机（FSM）

```
              ┌──────────────────────────────────────────────────┐
              │                   任务 FSM                        │
              └──────────────────────────────────────────────────┘

  [创建任务]
      │
      ▼
  STATUS_IDLE ──── trigger ────► STATUS_PLANNING
                                      │
                          规划成功     │    规划失败
                     ┌───────────────┤───────────────────┐
                     ▼               │                   ▼
             STATUS_NAVIGATING    STATUS_FAILED ◄── 超时
                     │
                导航到站位
                     │
                     ▼
             STATUS_EXECUTING
             (行为树运行中)
                     │
                  ┌──┴──────────────────────┐
                  │                         │
               success                   failure / abort
                  │                         │
                  ▼                         ▼
          STATUS_VERIFYING          STATUS_FAILED / STATUS_ABORTED
                  │
              验证通过
                  │
                  ▼
          STATUS_COMPLETED

  ── 任意状态可接收 /task/pause → STATUS_PAUSED ──
  ── STATUS_PAUSED 接收 /task/resume → 恢复前一状态 ──
  ── 任意状态可接收 /task/abort → STATUS_ABORTED ──
```

### 2.4 Task 转换触发条件

| 源状态 | 目标状态 | 触发条件 |
|--------|---------|---------|
| `STATUS_IDLE` | `STATUS_PLANNING` | 接收到新任务请求 |
| `STATUS_PLANNING` | `STATUS_NAVIGATING` | 站位规划成功，机器人需移动 |
| `STATUS_PLANNING` | `STATUS_EXECUTING` | 已在目标站位，直接执行 |
| `STATUS_NAVIGATING` | `STATUS_EXECUTING` | Nav2 导航目标达成 |
| `STATUS_EXECUTING` | `STATUS_VERIFYING` | 行为树返回 SUCCESS |
| `STATUS_VERIFYING` | `STATUS_COMPLETED` | 二次感知确认清洁达标 |
| `STATUS_VERIFYING` | `STATUS_EXECUTING` | 二次感知发现遗漏，重新执行（最多重试 2 次） |
| 任意 | `STATUS_PAUSED` | 收到 `/task/pause` 服务请求 |
| `STATUS_PAUSED` | 前一状态 | 收到 `/task/resume` 服务请求 |
| 任意 | `STATUS_ABORTED` | 收到 `/task/abort` 或安全告警 CRITICAL |
| 任意 | `STATUS_FAILED` | 超时 / 行为树返回 FAILURE 且重试耗尽 |

---

## 3. Skill（技能）字段规范

### 3.1 Skill 数据结构

```cpp
// robot_skill_lib/include/skill_lib/skill_base.hpp

struct SkillContext {
    // ── 标识字段 ──────────────────────────────────
    std::string   skill_id;         // "SKILL-{8HEX}-{SkillType}"
    SkillType     skill_type;       // 见枚举 5.5（05_interface_naming_conventions.md）
    std::string   parent_task_id;   // 归属的 Task ID

    // ── 输入参数（由 Task 层或 BT 传入）────────────
    std::string              target_object_id;
    geometry_msgs::PoseStamped  target_pose;
    CleanMode                clean_mode;
    float                    target_force_n;    // 期望接触力 (N)
    uint8_t                  max_passes;        // 最大擦拭遍数
    float                    speed_scale;       // 速度比例 [0.1, 1.0]
    CleanQuality             quality;

    // ── 状态字段 ──────────────────────────────────
    SkillStateCode  state;
    float           progress_percent;
    std::string     message;

    // ── 结果字段 ──────────────────────────────────
    bool    success;
    float   actual_force_n;
    float   cleaned_area_percent;
    uint8_t passes_completed;
    double  execution_time_sec;
    std::string failure_reason;

    // ── 时间字段 ──────────────────────────────────
    rclcpp::Time started_at;
    rclcpp::Time completed_at;
    double       timeout_sec;       // 技能超时，默认 60s
};
```

### 3.2 Skill 注册规范

每个技能实现类需在技能库中注册，格式：

```cpp
// robot_skill_lib/src/skill_registry.cpp
#include "skill_lib/skills/wipe_surface_skill.hpp"
#include "skill_lib/skills/spray_detergent_skill.hpp"
// ... 其他技能头文件

void SkillRegistry::registerAll() {
    register_skill<WipeSurfaceSkill>(SkillType::SKILL_WIPE_SURFACE);
    register_skill<SprayDetergentSkill>(SkillType::SKILL_SPRAY_DETERGENT);
    register_skill<ScrubSurfaceSkill>(SkillType::SKILL_SCRUB_SURFACE);
    register_skill<NavigateSkill>(SkillType::SKILL_NAVIGATE);
    register_skill<ScanSceneSkill>(SkillType::SKILL_SCAN_SCENE);
    register_skill<ApproachTargetSkill>(SkillType::SKILL_APPROACH_TARGET);
    register_skill<FlushWaterSkill>(SkillType::SKILL_FLUSH_WATER);
    register_skill<SqueegeFloorSkill>(SkillType::SKILL_SQUEEGEE_FLOOR);
    register_skill<VerifyCleanSkill>(SkillType::SKILL_VERIFY_CLEAN);
    register_skill<StowArmSkill>(SkillType::SKILL_STOW_ARM);
    register_skill<ChangeToolSkill>(SkillType::SKILL_CHANGE_TOOL);
}
```

### 3.3 技能基类接口（C++）

```cpp
// robot_skill_lib/include/skill_lib/skill_base.hpp

class SkillBase {
public:
    explicit SkillBase(const std::string& skill_name);
    virtual ~SkillBase() = default;

    // 主接口（由行为树节点调用）
    virtual BT::NodeStatus tick() = 0;

    // 生命周期钩子（可选覆写）
    virtual void onStart() {}    // 首次 tick 前
    virtual void onSuccess() {}  // 返回 SUCCESS 后
    virtual void onFailure() {}  // 返回 FAILURE 后
    virtual void onHalt() {}     // 被中断时（BT Halt）

    // 状态查询
    SkillStateCode getState() const;
    float getProgress() const;
    const SkillContext& getContext() const;

protected:
    SkillContext ctx_;
    rclcpp::Node::SharedPtr node_;

    // 工具方法
    void setProgress(float percent, const std::string& message = "");
    void publishState();  // 发布到 /skill/execution_state
};
```

### 3.4 Skill 状态机

```
  [BT tick 首次调用]
        │
        ▼
  SKILL_IDLE ──► onStart() ──► SKILL_RUNNING
                                    │
                         ┌──────────┼──────────┐
                         │          │          │
                      success    failure    halt
                         │          │          │
                         ▼          ▼          ▼
                    SKILL_SUCCESS  SKILL_FAILURE  SKILL_PREEMPTED
                         │
                    onSuccess()
```

---

## 4. 行为树（BT）节点字段规范

本项目采用 **BehaviorTree.CPP v4**（`behaviortree_cpp_v4`）作为行为树运行引擎。

### 4.1 BT 节点类型对照

| BT 节点类型 | 说明 | 示例使用场景 |
|------------|------|------------|
| `SyncActionNode` | 同步动作（立即返回） | 设置参数、发布话题 |
| `StatefulActionNode` | 有状态异步动作（多 tick） | 执行 Skill，等待 Action 结果 |
| `ConditionNode` | 条件判断（返回 SUCCESS/FAILURE） | 检测物体是否存在 |
| `SequenceNode` | 顺序执行，任意子节点失败则整体失败 | 清洁流程主干 |
| `FallbackNode` | 备选执行，子节点失败才尝试下一个 | 工具选择备选链 |
| `RetryNode` | 失败重试，可限制次数 | 清洁质量未达标时重做 |
| `TimeoutNode` | 超时限制包装 | 限制技能执行时间 |
| `ForceFailureNode` / `ForceSuccessNode` | 强制翻转返回值 | 调试/模拟场景 |
| `SubTreeNode` | 内联子树 | 模块化清洁流程 |

### 4.2 BT 输入/输出端口（Port）命名规范

端口名使用 **snake_case**，并配有类型注释：

```cpp
// 示例：WipeSurfaceSkill BT 节点端口定义
static BT::PortsList providedPorts() {
    return {
        // 输入端口
        BT::InputPort<std::string>("target_object_id",  "清洁目标 ID"),
        BT::InputPort<uint8_t>   ("clean_mode",         "清洁模式，见 CleanMode 枚举"),
        BT::InputPort<float>     ("target_force_n",     "期望接触力 (N)"),
        BT::InputPort<uint8_t>   ("max_passes",         "最大擦拭遍数"),
        BT::InputPort<float>     ("speed_scale",        "速度比例 [0.1, 1.0]"),

        // 输出端口
        BT::OutputPort<float>    ("cleaned_area_percent", "清洁覆盖率"),
        BT::OutputPort<bool>     ("stain_removed",         "污渍是否消除"),
    };
}
```

### 4.3 BT Blackboard 键名规范

Blackboard 中的键名遵循 **全小写 snake_case**，并建议按模块加前缀：

| 键名 | 类型 | 说明 |
|------|------|------|
| `task_id` | `string` | 当前任务 ID |
| `task_type` | `uint8_t` | 当前任务类型 |
| `target_object_id` | `string` | 当前清洁目标 ID |
| `scene_objects` | `SceneObjectArray` | 最新感知结果 |
| `current_stain_map` | `StainMap` | 最新污渍图 |
| `station_pose` | `PoseStamped` | 清洁站位 |
| `arm_current_pose` | `PoseStamped` | 机械臂末端当前位姿 |
| `tool_type` | `uint8_t` | 当前工具类型 |
| `clean_mode` | `uint8_t` | 清洁模式 |
| `quality_level` | `uint8_t` | 质量档位 |
| `cleaned_area_percent` | `float` | 已清洁覆盖率 |
| `retry_count` | `int` | 当前重试次数 |
| `vla_proposal` | `ActionProposal` | VLA 最新建议 |
| `last_skill_result` | `bool` | 上一个技能执行结果 |

---

## 5. 各清洁任务的典型行为树结构

### 5.1 马桶清洁（`TASK_CLEAN_TOILET`）BT XML 示例

```xml
<root main_tree_to_execute="CleanToilet">
  <BehaviorTree ID="CleanToilet">
    <Sequence name="toilet_clean_sequence">

      <!-- 1. 导航到马桶区域 -->
      <Action ID="SkillNavigate"
              target_object_id="toilet_01"
              skill_id="{skill_id_nav}"/>

      <!-- 2. 扫描场景，获取精确位置 -->
      <Action ID="SkillScanScene"
              camera_hint="head"
              target_type="OBJECT_TOILET"
              skill_id="{skill_id_scan}"/>

      <!-- 3. 靠近马桶 -->
      <Action ID="SkillApproachTarget"
              target_object_id="{target_object_id}"
              skill_id="{skill_id_approach}"/>

      <!-- 4. 喷洒清洁剂 -->
      <Action ID="SkillSprayDetergent"
              spray_volume_ml="30"
              skill_id="{skill_id_spray}"/>

      <!-- 5. 等待 30 秒（清洁剂浸润） -->
      <Delay delay_msec="30000">
        <AlwaysSuccess/>
      </Delay>

      <!-- 6. 刷洗马桶（带重试） -->
      <RetryUntilSuccessful num_attempts="3">
        <Sequence>
          <Action ID="SkillScrubSurface"
                  target_object_id="{target_object_id}"
                  clean_mode="CLEAN_SCRUB"
                  target_force_n="8.0"
                  max_passes="3"
                  skill_id="{skill_id_scrub}"/>
          <Action ID="SkillVerifyClean"
                  target_object_id="{target_object_id}"
                  min_coverage="0.85"
                  cleaned_area_percent="{cleaned_area_percent}"
                  skill_id="{skill_id_verify}"/>
        </Sequence>
      </RetryUntilSuccessful>

      <!-- 7. 冲水 -->
      <Action ID="SkillFlushWater"
              target_object_id="{target_object_id}"
              skill_id="{skill_id_flush}"/>

      <!-- 8. 收臂到安全位 -->
      <Action ID="SkillStowArm"
              skill_id="{skill_id_stow}"/>

    </Sequence>
  </BehaviorTree>
</root>
```

### 5.2 地面清洁（`TASK_CLEAN_FLOOR`）BT XML 示例

```xml
<root main_tree_to_execute="CleanFloor">
  <BehaviorTree ID="CleanFloor">
    <Sequence name="floor_clean_sequence">

      <!-- 1. 全厂扫描，生成污渍热力图 -->
      <Action ID="SkillScanScene"
              camera_hint="head"
              target_type="OBJECT_FLOOR"
              skill_id="{skill_id_scan}"/>

      <!-- 2. 更换刮水板工具 -->
      <Action ID="SkillChangeTool"
              target_tool="TOOL_SQUEEGEE"
              skill_id="{skill_id_change}"/>

      <!-- 3. 遍历清洁区域（由任务规划器生成 waypoints） -->
      <SubTree ID="FloorWipeZones"/>

      <!-- 4. 验证 -->
      <Action ID="SkillVerifyClean"
              target_object_id="floor_zone_all"
              min_coverage="0.90"
              cleaned_area_percent="{cleaned_area_percent}"
              skill_id="{skill_id_verify}"/>

      <!-- 5. 收臂 -->
      <Action ID="SkillStowArm" skill_id="{skill_id_stow}"/>

    </Sequence>
  </BehaviorTree>

  <BehaviorTree ID="FloorWipeZones">
    <Sequence name="zone_wipe">
      <Action ID="SkillSqueegeFloor"
              clean_mode="CLEAN_SQUEEGEE"
              speed_scale="0.6"
              skill_id="{skill_id_squeegee}"/>
    </Sequence>
  </BehaviorTree>
</root>
```

### 5.3 全厕清洁（`TASK_FULL_CLEAN`）BT XML 示例

```xml
<root main_tree_to_execute="FullClean">
  <BehaviorTree ID="FullClean">
    <Sequence name="full_clean_sequence">

      <!-- 0. VLA 高层规划（可选，确认后执行） -->
      <Fallback>
        <Action ID="VLAPlanTask"
                instruction="清洁整个卫生间"
                execute_after_plan="false"
                skill_id="{skill_id_vla}"/>
        <AlwaysSuccess/>  <!-- VLA 不可用时跳过 -->
      </Fallback>

      <!-- 按顺序执行各区域清洁子树 -->
      <SubTree ID="CleanSink"/>
      <SubTree ID="CleanUrinal"/>
      <SubTree ID="CleanToilet"/>
      <SubTree ID="CleanWall"/>
      <SubTree ID="CleanFloor"/>

    </Sequence>
  </BehaviorTree>
</root>
```

---

## 6. 技能超时与容错策略

### 6.1 超时设定参考

| 技能 | 默认超时 (s) | 说明 |
|------|------------|------|
| `SKILL_NAVIGATE` | 120 | 依赖地图规模，可调 |
| `SKILL_SCAN_SCENE` | 15 | 感知单次完整扫描 |
| `SKILL_APPROACH_TARGET` | 30 | 精对准靠近 |
| `SKILL_SPRAY_DETERGENT` | 10 | 喷雾操作 |
| `SKILL_WIPE_SURFACE` | 90 | 多遍擦拭含力控 |
| `SKILL_SCRUB_SURFACE` | 120 | 刷洗（含力控） |
| `SKILL_FLUSH_WATER` | 20 | 冲水 |
| `SKILL_SQUEEGEE_FLOOR` | 180 | 地面刮水（面积大） |
| `SKILL_VERIFY_CLEAN` | 20 | 二次感知验证 |
| `SKILL_STOW_ARM` | 15 | 回到安全位 |
| `SKILL_CHANGE_TOOL` | 30 | 工具更换 |

### 6.2 容错策略矩阵

| 失败场景 | 处理策略 |
|---------|---------|
| 导航超时 | 发出 WARNING，上报 `STATUS_FAILED`，等待人工复位 |
| 感知置信度过低 | 重新触发 `SKILL_SCAN_SCENE`，最多 2 次 |
| 机械臂力限触发 | 立即停止当前技能，退到安全位，上报 ERROR |
| 工具更换失败 | 发出 CRITICAL 告警，终止任务 |
| 验证未通过 | 重新执行清洁技能，最多重试 `task.max_verify_retry`（默认 2）次 |
| BT 节点超时 | `TimeoutNode` 返回 FAILURE → RetryUntilSuccessful 逻辑处理 |
| 网络断联（RK3588） | 底盘原地停止，上层任务 PAUSE，等待重连 |

---

## 7. 消息字段与 BT 端口映射关系

| 消息/字段 | BT Blackboard 键 | 方向 |
|----------|-----------------|------|
| `TaskStatus.task_id` | `task_id` | Task → BB |
| `TaskStatus.task_type` | `task_type` | Task → BB |
| `SceneObjectArray.objects[0].object_id` | `target_object_id` | Perception → BB |
| `SkillState.state` | `last_skill_result` | Skill → BB |
| `CleanSurface.Result.cleaned_area_percent` | `cleaned_area_percent` | Skill → BB |
| `ActionProposal.proposed_task_sequence` | `vla_proposal` | VLA → BB |
| `StainMap` | `current_stain_map` | Perception → BB |

---

## 8. 参考

- [05_interface_naming_conventions.md](05_interface_naming_conventions.md) — 枚举常量完整定义
- [07_vla_hybrid_control.md](07_vla_hybrid_control.md) — VLA 如何生成 Task
- [08_state_machine_bt_design.md](08_state_machine_bt_design.md) — 详细行为树设计文档
- [BehaviorTree.CPP v4 文档](https://www.behaviortree.dev/)
- [03_custom_messages.md](03_custom_messages.md) — 消息原始定义
