# 轮臂式厕所清洁机器人 — 状态机 / 行为树设计文档

> **版本**: v1.0  
> **文档编号**: 08  
> **适用读者**: 软件工程师、算法工程师  
> **前置阅读**: [06_task_skill_bt_spec.md](06_task_skill_bt_spec.md), [07_vla_hybrid_control.md](07_vla_hybrid_control.md)

---

## 1. 设计目标

本文档定义轮臂式厕所清洁机器人的**完整状态机（FSM）与行为树（BT）设计**，覆盖：

1. 系统级顶层状态机（System FSM）
2. 任务级状态机（Task FSM，见 06 文档，此处补充细节）
3. 技能级状态机（Skill FSM）
4. 各清洁场景的完整行为树 XML
5. 状态机与行为树的协同机制
6. 异常处理与安全行为

---

## 2. 系统顶层状态机（System FSM）

系统顶层状态机由 `system_monitor_node` 和 `task_manager_node` 协同维护。

```
                   ┌─────────────────────────────────────┐
                   │         System FSM 状态图             │
                   └─────────────────────────────────────┘

  [上电启动]
       │
       ▼
  SYS_BOOT ──────── 自检完成 ──────────► SYS_STANDBY
                                              │
                               ┌──────────────┼─────────────────┐
                               │              │                 │
                        /task/start    /system/set_mode     /quest/trigger
                    (外部调度指令)      (MODE_TELEOP)      (data_collect)
                               │              │                 │
                               ▼              ▼                 ▼
                         SYS_CLEANING   SYS_TELEOP        SYS_DATA_COLLECT
                               │              │                 │
                        任务完成/失败   Quest 断开/手动退出   采集结束
                               │              │                 │
                               └──────────────┴─────────────────┘
                                              │
                                              ▼
                                        SYS_STANDBY

  ──── 任意状态接收 SEVERITY_CRITICAL 告警 → SYS_EMERGENCY ────
  ──── SYS_EMERGENCY 手动复位 → SYS_STANDBY ────
```

### 2.1 系统状态定义

| 状态 | 值 | 说明 |
|------|-----|------|
| `SYS_BOOT` | — | 启动自检，等待所有节点就绪 |
| `SYS_STANDBY` | `MODE_STANDBY=0` | 待机，所有节点运行但机器人静止 |
| `SYS_CLEANING` | `MODE_CLEANING=1` | 自主清洁模式，任务管理器主导 |
| `SYS_TELEOP` | `MODE_TELEOP=2` | 遥控模式，Quest 3s 操控 |
| `SYS_DATA_COLLECT` | `MODE_DATA_COLLECT=3` | 数据采集模式，记录所有话题 |
| `SYS_EMERGENCY` | `MODE_EMERGENCY=4` | 紧急停机，所有运动立即停止 |

### 2.2 SYS_BOOT 自检清单

| 检查项 | 超时 | 失败处理 |
|--------|------|---------|
| `arm_driver_node` 心跳 | 5s | 重试 3 次，失败报 FATAL |
| `head_camera_node` 图像流 | 5s | 报 ERROR，降级可用 |
| `wrist_camera_node` 图像流 | 5s | 报 ERROR，降级可用 |
| `slam_node` 地图就绪 | 30s | 报 ERROR（无地图则导航不可用） |
| FR5 关节零位校验 | 10s | 报 ERROR，等待人工校零 |
| 工具系统水压检查 | 5s | 报 WARNING（可继续） |
| 清洁剂液位检查 | 5s | 报 WARNING（可继续） |

---

## 3. 任务状态机补充设计

> 基础状态转换定义见 [06_task_skill_bt_spec.md 第 2 节](06_task_skill_bt_spec.md)，此处补充**并发任务管理**与**优先级抢占**机制。

### 3.1 任务队列管理

```cpp
// robot_task_manager/include/task_manager/task_queue.hpp

class TaskQueue {
public:
    // 入队（按优先级排序）
    void enqueue(const Task& task);

    // 出队（取最高优先级任务）
    Task dequeue();

    // 抢占：插入更高优先级任务，暂停当前任务
    void preempt(const Task& high_priority_task);

    // 恢复被抢占的任务
    void resumePreempted();

private:
    std::priority_queue<Task, std::vector<Task>, TaskPriorityComparator> queue_;
    std::stack<Task> preempted_stack_;  // LIFO 恢复
};
```

### 3.2 任务优先级策略

| 优先级 | 值 | 任务场景 |
|--------|-----|---------|
| EMERGENCY | 9 | 障碍物闯入，立即停机 |
| HIGH | 7-8 | 人工紧急任务（值班主管发起） |
| NORMAL | 4-6 | 调度平台排班任务 |
| LOW | 1-3 | 后台自动补充任务（如补喷清洁剂） |
| BACKGROUND | 0 | 系统维护（日志清理等） |

---

## 4. 行为树完整设计

本项目行为树结构分为三层：

```
根树（CleaningOrchestrator）
  └── 任务分发子树（TaskDispatcher）
        ├── 马桶清洁子树（CleanToilet）
        ├── 小便池清洁子树（CleanUrinal）
        ├── 洗手台清洁子树（CleanSink）
        ├── 墙面清洁子树（CleanWall）
        └── 地面清洁子树（CleanFloor）
```

### 4.1 根树：CleaningOrchestrator

```xml
<root main_tree_to_execute="CleaningOrchestrator"
      BTCPP_format="4">

  <BehaviorTree ID="CleaningOrchestrator">
    <Sequence name="orchestrator_main">

      <!-- 系统初始化检查 -->
      <SubTree ID="SystemPreCheck" _autoremap="true"/>

      <!-- VLA 高层规划（置信度足够则自动执行，否则等待确认） -->
      <Fallback name="vla_or_rule_based">
        <SubTree ID="VLAPlanning" _autoremap="true"/>
        <SubTree ID="RuleBasedPlanning" _autoremap="true"/>
      </Fallback>

      <!-- 主清洁循环 -->
      <ForEach input_port="{task_sequence}" output_port="{current_task}">
        <SubTree ID="ExecuteOneTask" _autoremap="true"/>
      </ForEach>

      <!-- 完成后收臂并返回停靠站 -->
      <SubTree ID="ReturnToBase" _autoremap="true"/>

    </Sequence>
  </BehaviorTree>

</root>
```

### 4.2 系统预检子树

```xml
<BehaviorTree ID="SystemPreCheck">
  <Sequence name="pre_check">

    <Condition ID="CheckArmReady"
               timeout_sec="5.0"/>

    <Condition ID="CheckCamerasReady"
               camera_hint="both"
               allow_partial="true"/>

    <Condition ID="CheckToolWaterLevel"
               min_level_ml="500"/>

    <Condition ID="CheckDetergentLevel"
               min_level_ml="200"/>

    <!-- 如果工具液位低，发出警告但不阻止（Fallback 保证继续） -->
    <Fallback name="detergent_check_soft">
      <Condition ID="CheckDetergentLevel" min_level_ml="200"/>
      <Action    ID="PublishWarning"
                 message="清洁剂不足，建议补充"
                 severity="SEVERITY_WARNING"/>
    </Fallback>

    <!-- 确认机器人在起始位 -->
    <Condition ID="CheckAtHomePosition"/>

    <Action ID="PublishSystemStatus" message="预检通过，开始清洁任务"/>

  </Sequence>
</BehaviorTree>
```

### 4.3 马桶清洁子树（详细版）

```xml
<BehaviorTree ID="CleanToilet">
  <Sequence name="toilet_clean_main">

    <!-- ── 阶段 1：导航到马桶区域 ──────────────── -->
    <Timeout msec="120000" name="navigate_timeout">
      <Action ID="SkillNavigate"
              target_object_id="{target_object_id}"
              goal_tolerance_m="0.3"
              skill_id="{skill_nav_id}"/>
    </Timeout>

    <!-- ── 阶段 2：精确感知定位 ───────────────── -->
    <Timeout msec="15000" name="scan_timeout">
      <Action ID="SkillScanScene"
              camera_hint="head"
              target_type="1"
              target_object_id="{target_object_id}"
              skill_id="{skill_scan_id}"/>
    </Timeout>

    <!-- ── 阶段 3：确认目标存在 ───────────────── -->
    <Condition ID="CheckObjectDetected"
               target_object_id="{target_object_id}"
               min_confidence="0.75"/>

    <!-- ── 阶段 4：靠近目标 ──────────────────── -->
    <Timeout msec="30000" name="approach_timeout">
      <Action ID="SkillApproachTarget"
              target_object_id="{target_object_id}"
              approach_distance_m="0.15"
              skill_id="{skill_approach_id}"/>
    </Timeout>

    <!-- ── 阶段 5：换刷头 ────────────────────── -->
    <Fallback name="tool_selection">
      <Condition ID="CheckCurrentTool" expected_tool="1"/>  <!-- TOOL_BRUSH -->
      <Timeout msec="30000">
        <Action ID="SkillChangeTool"
                target_tool="1"
                skill_id="{skill_change_id}"/>
      </Timeout>
    </Fallback>

    <!-- ── 阶段 6：喷洒清洁剂 ─────────────────── -->
    <Timeout msec="15000" name="spray_timeout">
      <Action ID="SkillSprayDetergent"
              spray_volume_ml="30"
              spray_duration_sec="5"
              spray_pattern="0"
              skill_id="{skill_spray_id}"/>
    </Timeout>

    <!-- ── 阶段 7：等待浸润 ──────────────────── -->
    <Delay delay_msec="30000">
      <AlwaysSuccess/>
    </Delay>

    <!-- ── 阶段 8：刷洗（带重试机制） ───────────── -->
    <RetryUntilSuccessful num_attempts="3" name="scrub_with_retry">
      <Sequence name="scrub_and_verify">

        <Timeout msec="120000" name="scrub_timeout">
          <Action ID="SkillScrubSurface"
                  target_object_id="{target_object_id}"
                  clean_mode="3"
                  target_force_n="8.0"
                  max_passes="3"
                  speed_scale="0.5"
                  cleaned_area_percent="{cleaned_area_percent}"
                  skill_id="{skill_scrub_id}"/>
        </Timeout>

        <!-- 验证清洁结果 -->
        <Timeout msec="20000" name="verify_timeout">
          <Action ID="SkillVerifyClean"
                  target_object_id="{target_object_id}"
                  min_coverage="0.85"
                  cleaned_area_percent="{cleaned_area_percent}"
                  skill_id="{skill_verify_id}"/>
        </Timeout>

        <!-- 检查是否达标 -->
        <Condition ID="CheckCleaningQuality"
                   cleaned_area_percent="{cleaned_area_percent}"
                   threshold="0.85"/>

      </Sequence>
    </RetryUntilSuccessful>

    <!-- ── 阶段 9：冲水 ──────────────────────── -->
    <Timeout msec="20000" name="flush_timeout">
      <Action ID="SkillFlushWater"
              target_object_id="{target_object_id}"
              skill_id="{skill_flush_id}"/>
    </Timeout>

    <!-- ── 阶段 10：收臂 ─────────────────────── -->
    <Timeout msec="15000" name="stow_timeout">
      <Action ID="SkillStowArm"
              skill_id="{skill_stow_id}"/>
    </Timeout>

    <!-- 记录任务结果 -->
    <Action ID="RecordTaskResult"
            task_id="{task_id}"
            success="true"
            cleaned_area_percent="{cleaned_area_percent}"/>

  </Sequence>
</BehaviorTree>
```

### 4.4 洗手台清洁子树

```xml
<BehaviorTree ID="CleanSink">
  <Sequence name="sink_clean_main">

    <!-- 导航 -->
    <Timeout msec="120000">
      <Action ID="SkillNavigate"
              target_object_id="{target_object_id}"
              skill_id="{skill_nav_id}"/>
    </Timeout>

    <!-- 感知 + 靠近 -->
    <Action ID="SkillScanScene" camera_hint="head" target_type="3"
            skill_id="{skill_scan_id}"/>
    <Action ID="SkillApproachTarget"
            target_object_id="{target_object_id}"
            approach_distance_m="0.20"
            skill_id="{skill_approach_id}"/>

    <!-- 换海绵工具 -->
    <Fallback>
      <Condition ID="CheckCurrentTool" expected_tool="2"/>  <!-- TOOL_SPONGE -->
      <Action ID="SkillChangeTool" target_tool="2" skill_id="{skill_change_id}"/>
    </Fallback>

    <!-- 喷雾 + 等待 -->
    <Action ID="SkillSprayDetergent"
            spray_volume_ml="20"
            spray_duration_sec="3"
            skill_id="{skill_spray_id}"/>
    <Delay delay_msec="20000"><AlwaysSuccess/></Delay>

    <!-- 台面擦拭（水平光栅） -->
    <RetryUntilSuccessful num_attempts="2">
      <Sequence>
        <Action ID="SkillWipeSurface"
                target_object_id="{target_object_id}"
                clean_mode="1"
                target_force_n="5.0"
                max_passes="2"
                cleaned_area_percent="{cleaned_area_percent}"
                skill_id="{skill_wipe_id}"/>
        <Condition ID="CheckCleaningQuality"
                   cleaned_area_percent="{cleaned_area_percent}"
                   threshold="0.80"/>
      </Sequence>
    </RetryUntilSuccessful>

    <!-- 水龙头区域单独处理（精细动作） -->
    <Fallback>
      <Condition ID="CheckObjectDetected"
                 target_object_id="faucet_near_sink"
                 min_confidence="0.6"/>
      <AlwaysSuccess/>
    </Fallback>

    <!-- 收臂 -->
    <Action ID="SkillStowArm" skill_id="{skill_stow_id}"/>

  </Sequence>
</BehaviorTree>
```

### 4.5 地面清洁子树

```xml
<BehaviorTree ID="CleanFloor">
  <Sequence name="floor_clean_main">

    <!-- 生成地面清洁区域（由 task_manager 注入 Blackboard） -->
    <Action ID="GenerateFloorZones"
            map_frame="map"
            zone_width_m="0.5"
            zones="{floor_zones}"/>

    <!-- 换刮水板 -->
    <Fallback>
      <Condition ID="CheckCurrentTool" expected_tool="5"/>  <!-- TOOL_SQUEEGEE -->
      <Action ID="SkillChangeTool" target_tool="5" skill_id="{skill_change_id}"/>
    </Fallback>

    <!-- 遍历每个地面区域 -->
    <ForEach input_port="{floor_zones}" output_port="{current_zone}">
      <Sequence name="clean_one_zone">

        <!-- 导航到区域中心 -->
        <Action ID="SkillNavigate"
                target_pose="{current_zone.center_pose}"
                skill_id="{skill_nav_id}"/>

        <!-- 刮水清洁 -->
        <Timeout msec="60000">
          <Action ID="SkillSqueegeFloor"
                  target_zone="{current_zone}"
                  speed_scale="0.6"
                  skill_id="{skill_squeegee_id}"/>
        </Timeout>

      </Sequence>
    </ForEach>

    <!-- 全地面验证 -->
    <Action ID="SkillScanScene"
            camera_hint="head"
            target_type="4"
            skill_id="{skill_scan_id}"/>

    <!-- 收臂 -->
    <Action ID="SkillStowArm" skill_id="{skill_stow_id}"/>

  </Sequence>
</BehaviorTree>
```

### 4.6 小便池清洁子树

```xml
<BehaviorTree ID="CleanUrinal">
  <Sequence name="urinal_clean_main">

    <!-- 导航 + 感知 -->
    <Action ID="SkillNavigate"
            target_object_id="{target_object_id}"
            skill_id="{skill_nav_id}"/>
    <Action ID="SkillScanScene" camera_hint="wrist" target_type="2"
            skill_id="{skill_scan_id}"/>
    <Action ID="SkillApproachTarget"
            target_object_id="{target_object_id}"
            approach_distance_m="0.15"
            skill_id="{skill_approach_id}"/>

    <!-- 换硬毛刷 -->
    <Fallback>
      <Condition ID="CheckCurrentTool" expected_tool="4"/>  <!-- TOOL_SCRUBBER -->
      <Action ID="SkillChangeTool" target_tool="4" skill_id="{skill_change_id}"/>
    </Fallback>

    <!-- 喷雾（消毒液） -->
    <Action ID="SkillSprayDetergent"
            spray_volume_ml="25"
            spray_duration_sec="4"
            skill_id="{skill_spray_id}"/>
    <Delay delay_msec="25000"><AlwaysSuccess/></Delay>

    <!-- 刷洗（弓形路径，自上而下） -->
    <RetryUntilSuccessful num_attempts="2">
      <Sequence>
        <Action ID="SkillScrubSurface"
                target_object_id="{target_object_id}"
                clean_mode="3"
                target_force_n="6.0"
                max_passes="2"
                skill_id="{skill_scrub_id}"/>
        <Condition ID="CheckCleaningQuality"
                   cleaned_area_percent="{cleaned_area_percent}"
                   threshold="0.80"/>
      </Sequence>
    </RetryUntilSuccessful>

    <!-- 冲水 -->
    <Action ID="SkillFlushWater"
            target_object_id="{target_object_id}"
            skill_id="{skill_flush_id}"/>

    <!-- 收臂 -->
    <Action ID="SkillStowArm" skill_id="{skill_stow_id}"/>

  </Sequence>
</BehaviorTree>
```

### 4.7 墙面清洁子树

```xml
<BehaviorTree ID="CleanWall">
  <Sequence name="wall_clean_main">

    <!-- 感知墙面脏污区域 -->
    <Action ID="SkillScanScene" camera_hint="head" target_type="5"
            skill_id="{skill_scan_id}"/>

    <!-- 如果无明显污渍，跳过墙面清洁 -->
    <Fallback name="skip_if_clean">
      <Condition ID="CheckStainPresent"
                 target_type="5"
                 min_coverage="0.05"/>
      <Sequence name="skip_sequence">
        <Action ID="PublishInfo" message="墙面清洁度达标，跳过"/>
        <AlwaysSuccess/>
      </Sequence>
    </Fallback>

    <!-- 导航到墙面清洁站位 -->
    <Action ID="SkillNavigate"
            target_object_id="wall_zone_01"
            skill_id="{skill_nav_id}"/>

    <!-- 换海绵工具 -->
    <Fallback>
      <Condition ID="CheckCurrentTool" expected_tool="2"/>
      <Action ID="SkillChangeTool" target_tool="2" skill_id="{skill_change_id}"/>
    </Fallback>

    <!-- 喷雾 -->
    <Action ID="SkillSprayDetergent"
            spray_volume_ml="15"
            spray_duration_sec="3"
            skill_id="{skill_spray_id}"/>

    <!-- 垂直擦拭 -->
    <Action ID="SkillWipeSurface"
            target_object_id="wall_zone_01"
            clean_mode="2"
            target_force_n="4.0"
            max_passes="2"
            skill_id="{skill_wipe_id}"/>

    <!-- 收臂 -->
    <Action ID="SkillStowArm" skill_id="{skill_stow_id}"/>

  </Sequence>
</BehaviorTree>
```

---

## 5. 异常处理行为树

### 5.1 全局异常监控（ReactiveSequence）

```xml
<BehaviorTree ID="SafetyMonitor">
  <ReactiveSequence name="safety_reactive">

    <!-- 高优先级安全条件（任意失败立即中止） -->
    <Condition ID="CheckEmergencyStop"/>
    <Condition ID="CheckArmForceLimit" max_force_n="15.0"/>
    <Condition ID="CheckObstacleProximity" min_distance_m="0.3"/>
    <Condition ID="CheckBatteryLevel" min_level_percent="10"/>

    <!-- 主任务（只有以上所有条件满足才允许执行） -->
    <SubTree ID="CleaningOrchestrator" _autoremap="true"/>

  </ReactiveSequence>
</BehaviorTree>
```

> `ReactiveSequence`：每次 tick 都从第一个子节点重新评估，若前置安全条件失败，主任务被 Halt。

### 5.2 力限触发处理

```xml
<BehaviorTree ID="ForceLimitRecovery">
  <Sequence name="force_limit_recovery">
    <!-- 1. 立即停止当前运动 -->
    <Action ID="EmergencyStopArm"/>
    <!-- 2. 退到安全位 -->
    <Action ID="SkillStowArm" skill_id="{skill_stow_id}"/>
    <!-- 3. 等待 2 秒稳定 -->
    <Delay delay_msec="2000"><AlwaysSuccess/></Delay>
    <!-- 4. 上报告警 -->
    <Action ID="PublishAlert"
            severity="2"
            alert_code="ARM_FORCE_LIMIT"
            description="机械臂力限触发，已退到安全位"/>
    <!-- 5. 标记当前技能失败（由父树 Retry 决定是否重试） -->
    <AlwaysFailure/>
  </Sequence>
</BehaviorTree>
```

### 5.3 感知失败恢复

```xml
<BehaviorTree ID="PerceptionRecovery">
  <Fallback name="perception_fallback">
    <!-- 第一次尝试：重新扫描 -->
    <Action ID="SkillScanScene"
            camera_hint="head"
            force_redetect="true"
            skill_id="{skill_scan_id}"/>
    <!-- 第二次尝试：切换腕部相机 -->
    <Action ID="SkillScanScene"
            camera_hint="wrist"
            force_redetect="true"
            skill_id="{skill_scan_id}"/>
    <!-- 最终：发出警告，使用上次已知位置 -->
    <Sequence>
      <Action ID="PublishWarning"
              message="感知失败，使用历史目标位置"/>
      <Action ID="UseLastKnownTargetPose"
              target_object_id="{target_object_id}"/>
    </Sequence>
  </Fallback>
</BehaviorTree>
```

---

## 6. 行为树节点清单

### 6.1 Condition 节点

| 节点 ID | 输入端口 | 说明 |
|--------|---------|------|
| `CheckArmReady` | `timeout_sec` | 检查机械臂节点心跳 |
| `CheckCamerasReady` | `camera_hint`, `allow_partial` | 检查相机图像流 |
| `CheckToolWaterLevel` | `min_level_ml` | 检查清洁液液位 |
| `CheckDetergentLevel` | `min_level_ml` | 检查清洁剂液位 |
| `CheckAtHomePosition` | — | 检查机器人在待机位 |
| `CheckObjectDetected` | `target_object_id`, `min_confidence` | 检查物体是否被感知到 |
| `CheckCurrentTool` | `expected_tool` | 检查当前工具类型 |
| `CheckCleaningQuality` | `cleaned_area_percent`, `threshold` | 检查清洁达标率 |
| `CheckStainPresent` | `target_type`, `min_coverage` | 检查是否存在污渍 |
| `CheckEmergencyStop` | — | 急停按钮状态 |
| `CheckArmForceLimit` | `max_force_n` | 机械臂接触力检查 |
| `CheckObstacleProximity` | `min_distance_m` | 障碍物近距离检测 |
| `CheckBatteryLevel` | `min_level_percent` | 电池电量检查 |

### 6.2 Action 节点

| 节点 ID | 输入端口 | 输出端口 | 说明 |
|--------|---------|---------|------|
| `SkillNavigate` | `target_object_id` / `target_pose`, `goal_tolerance_m` | — | 导航到目标 |
| `SkillScanScene` | `camera_hint`, `target_type`, `force_redetect` | `target_object_id` | 感知场景 |
| `SkillApproachTarget` | `target_object_id`, `approach_distance_m` | — | 靠近目标 |
| `SkillSprayDetergent` | `spray_volume_ml`, `spray_duration_sec`, `spray_pattern` | — | 喷雾 |
| `SkillWipeSurface` | `target_object_id`, `clean_mode`, `target_force_n`, `max_passes` | `cleaned_area_percent` | 擦拭 |
| `SkillScrubSurface` | `target_object_id`, `clean_mode`, `target_force_n`, `max_passes` | `cleaned_area_percent` | 刷洗 |
| `SkillFlushWater` | `target_object_id` | — | 冲水 |
| `SkillSqueegeFloor` | `target_zone`, `speed_scale` | — | 地面刮水 |
| `SkillVerifyClean` | `target_object_id`, `min_coverage` | `cleaned_area_percent` | 验证清洁结果 |
| `SkillStowArm` | — | — | 收臂到安全位 |
| `SkillChangeTool` | `target_tool` | — | 更换工具 |
| `GenerateFloorZones` | `map_frame`, `zone_width_m` | `floor_zones` | 生成地面清洁分区 |
| `VLAPlanTask` | `instruction`, `execute_after_plan` | `task_sequence` | VLA 规划 |
| `EmergencyStopArm` | — | — | 紧急停止机械臂 |
| `UseLastKnownTargetPose` | `target_object_id` | — | 使用历史位置 |
| `PublishAlert` | `severity`, `alert_code`, `description` | — | 发布告警 |
| `PublishWarning` | `message` | — | 发布警告日志 |
| `PublishInfo` | `message` | — | 发布信息日志 |
| `RecordTaskResult` | `task_id`, `success`, `cleaned_area_percent` | — | 记录任务结果 |

---

## 7. BT 与 FSM 协同机制

行为树与状态机并非对立，而是**协同**工作：

```
任务 FSM（task_manager）
    │
    │ STATUS_EXECUTING 状态下
    │ 启动行为树引擎
    ▼
行为树（BehaviorTree.CPP）
    │
    │ 每个叶节点 Action 对应一个 Skill
    ▼
技能 FSM（skill_executor）
    │
    │ SKILL_RUNNING 状态下
    │ 调用 MoveIt2 / 底层控制
    ▼
底层运动原语
```

**数据流向**：

- `task_manager` 将 `Task` 参数写入 BT Blackboard
- BT 节点从 Blackboard 读取输入，写出结果
- `skill_executor` 读取 `SkillContext`，写出 `SkillState`
- `task_manager` 订阅 `/skill/execution_state` 更新任务进度

---

## 8. 行为树文件管理

### 8.1 文件目录结构

```
robot_task_manager/
└── behavior_trees/
    ├── orchestrator/
    │   └── cleaning_orchestrator.xml
    ├── tasks/
    │   ├── clean_toilet.xml
    │   ├── clean_urinal.xml
    │   ├── clean_sink.xml
    │   ├── clean_wall.xml
    │   ├── clean_floor.xml
    │   └── full_clean.xml
    ├── recovery/
    │   ├── force_limit_recovery.xml
    │   ├── perception_recovery.xml
    │   └── navigation_recovery.xml
    └── monitors/
        └── safety_monitor.xml
```

### 8.2 BT 加载配置

```yaml
# robot_task_manager/config/bt_config.yaml

task_manager_node:
  ros__parameters:
    bt_file_map:
      TASK_CLEAN_TOILET:  "tasks/clean_toilet.xml"
      TASK_CLEAN_URINAL:  "tasks/clean_urinal.xml"
      TASK_CLEAN_SINK:    "tasks/clean_sink.xml"
      TASK_CLEAN_WALL:    "tasks/clean_wall.xml"
      TASK_CLEAN_FLOOR:   "tasks/clean_floor.xml"
      TASK_FULL_CLEAN:    "orchestrator/cleaning_orchestrator.xml"

    bt_directory: "/opt/robot_ws/install/robot_task_manager/share/robot_task_manager/behavior_trees"
    enable_groot2_monitoring: true        # 调试时启用 Groot2 可视化
    groot2_port: 1667
    bt_tick_rate_hz: 10.0
```

---

## 9. 参考

- [06_task_skill_bt_spec.md](06_task_skill_bt_spec.md) — Task / Skill 数据结构规范
- [07_vla_hybrid_control.md](07_vla_hybrid_control.md) — VLA 与技能库集成
- [05_interface_naming_conventions.md](05_interface_naming_conventions.md) — 枚举常量
- [BehaviorTree.CPP v4 XML 参考](https://www.behaviortree.dev/docs/learn-the-basics/BT_basics)
- [Groot2 行为树可视化调试工具](https://www.behaviortree.dev/groot)
- [Nav2 BT Navigator](https://navigation.ros.org/tutorials/docs/using_groot.html)
