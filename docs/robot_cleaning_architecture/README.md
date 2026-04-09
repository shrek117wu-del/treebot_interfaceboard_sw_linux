# 轮臂式厕所清洁机器人 — 软件架构与产品规划文档索引

> **版本**: v1.1  
> **适用人群**: 机器人软件工程师、产品经理、系统架构师、算法工程师  
> **文档状态**: 完整版（内部评审用）

---

## 目录说明

本目录 `docs/robot_cleaning_architecture/` 收录了轮臂式厕所清洁机器人项目的**全套软件架构与产品规划文档**，涵盖：

- ROS2 软件节点架构设计
- 接口（Topic / Service / Action / Message）详细定义
- 自定义消息包初始化模板
- 命名与枚举规范
- 任务状态机与行为树设计
- VLA + 技能库混合控制方案
- 产品立项与研发管理规划

---

## 系统背景概览

| 硬件模块 | 型号 / 方案 |
|---------|-----------|
| 主控计算单元（前期） | x86 + NVIDIA RTX 4090 |
| 主控计算单元（量产） | x86 + NVIDIA Thor |
| 机械臂 | Fairino FR5 |
| 腕部相机（Wrist Camera） | Orbbec Gemini 330 |
| 头部相机（Head Camera） | Orbbec Gemini 305 |
| 数据采集 | Meta Quest 3s / Quest2ROS2 |
| 底盘 | RK3588 实现 SLAM |
| 任务场景 | 洗手台、马桶、小便池、地面、墙面清洁 |

---

## 文档列表与阅读顺序

> 建议按以下顺序阅读，从架构总览 → 接口设计 → 规范文档 → 控制算法 → 项目管理。

| 编号 | 文件名 | 主题 | 适合读者 |
|------|--------|------|---------|
| 01 | [01_ros2_node_architecture.md](01_ros2_node_architecture.md) | ROS2 软件节点架构图 + Topic/Service/Action 设计文档 | 架构师、软件工程师 |
| 02 | [02_ros2_detailed_design.md](02_ros2_detailed_design.md) | ROS2 节点、Topic、Action、Service、数据流详细设计 | 软件工程师、算法工程师 |
| 03 | [03_custom_messages.md](03_custom_messages.md) | ROS2 自定义消息 .msg/.srv/.action 文件详细定义草案 | 软件工程师 |
| 04 | [04_robot_interfaces_package.md](04_robot_interfaces_package.md) | robot_interfaces 包的 package.xml + CMakeLists.txt + 目录初始化模板 | 软件工程师 |
| 05 | [05_interface_naming_conventions.md](05_interface_naming_conventions.md) | ROS2 接口枚举规范与命名规范文档 | 全体开发人员 |
| 06 | [06_task_skill_bt_spec.md](06_task_skill_bt_spec.md) | Task / Skill / BT 状态机字段规范 | 软件工程师、算法工程师 |
| 07 | [07_vla_hybrid_control.md](07_vla_hybrid_control.md) | VLA + 技能库混合控制架构详细方案 | 算法工程师、架构师 |
| 08 | [08_state_machine_bt_design.md](08_state_machine_bt_design.md) | 轮臂式厕所清洁机器人状态机/行为树设计文档 | 软件工程师、算法工程师 |
| 09 | [09_product_management.md](09_product_management.md) | 产品立项/研发管理版：模块拆解、团队分工、里程碑和招聘画像 | 产品经理、技术负责人 |
| 10 | [10_glossary.md](10_glossary.md) | 术语表与缩写说明 | 全体人员（参考用） |

---

## 文档关系图

```
┌──────────────────────────────────────────────────────────┐
│              01 ROS2 节点架构图（总览）                   │
│        ┌──────────┴──────────┐                           │
│        ↓                     ↓                           │
│  02 详细节点设计         05 命名与枚举规范                │
│        ↓                     ↓                           │
│  03 自定义消息定义       06 Task/Skill/BT 字段规范        │
│        ↓                     ↓                           │
│  04 接口包初始化模板     08 状态机/行为树设计             │
│                              ↓                           │
│                    07 VLA + 技能库混合控制方案            │
└──────────────────────────────────────────────────────────┘
                              ↓
               09 产品立项 / 研发管理规划
```

---

## 版本历史

| 版本 | 日期 | 变更说明 |
|------|------|---------|
| v1.0 | 2026-04-09 | 初稿，涵盖文档 01~04 及 README 索引 |
| v1.1 | 2026-04-09 | 完成文档 05~10：命名规范、技能规范、VLA 混合控制、状态机/行为树设计、产品管理、术语表 |

---

## 维护说明

- 文档由架构/产品团队共同维护。
- 每次重大接口或架构变更时，需同步更新相关文档并在此 README 中注明版本变化。
- 术语统一参考 [10_glossary.md](10_glossary.md)。
