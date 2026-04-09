# 术语表与缩写说明

> **版本**: v1.0  
> **文档编号**: 10  
> **适用读者**: 全体人员（参考用）  
> **说明**: 本术语表为轮臂式厕所清洁机器人项目的统一术语参考，所有文档、代码注释、会议沟通均须使用此处定义的术语，避免歧义。

---

## 1. 英文缩写与术语

| 缩写 / 术语 | 全称 | 中文说明 |
|------------|------|---------|
| **BT** | Behavior Tree | 行为树，一种用于机器人任务规划的层次化决策树结构 |
| **FSM** | Finite State Machine | 有限状态机，用于描述系统离散状态和转换逻辑 |
| **VLA** | Vision-Language-Action | 视觉-语言-动作模型，一类多模态基础模型，输入图像+语言指令，输出机器人动作 |
| **DDS** | Data Distribution Service | 数据分发服务，ROS2 底层通信中间件协议 |
| **IK** | Inverse Kinematics | 逆运动学，给定末端位姿求关节角度 |
| **FK** | Forward Kinematics | 正运动学，给定关节角度求末端位姿 |
| **MPC** | Model Predictive Control | 模型预测控制，一种高级控制算法 |
| **SLAM** | Simultaneous Localization and Mapping | 同时定位与建图 |
| **CPP** | Coverage Path Planning | 覆盖路径规划，用于地面清扫路径生成 |
| **LoRA** | Low-Rank Adaptation | 大模型参数高效微调方法 |
| **QLoRA** | Quantized Low-Rank Adaptation | 量化版 LoRA，节省显存 |
| **BOM** | Bill of Materials | 物料清单 |
| **WBS** | Work Breakdown Structure | 工作分解结构，项目管理中的模块拆解 |
| **DoF** / **6DoF** | Degrees of Freedom | 自由度，6DoF 指6个自由度（3平移+3旋转） |
| **EEF** | End-Effector | 末端执行器，机械臂末端安装的工具 |
| **IMU** | Inertial Measurement Unit | 惯性测量单元，测量加速度和角速度 |
| **GPIO** | General-Purpose Input/Output | 通用输入输出，嵌入式控制接口 |
| **TCP** | Tool Center Point | 工具中心点，机械臂末端工具的参考点 |
| **TF** | Transform | ROS 坐标变换系统，管理机器人各部件坐标系关系 |
| **URDF** | Unified Robot Description Format | 统一机器人描述格式，XML 格式的机器人模型文件 |
| **XACRO** | XML Macros | URDF 的宏扩展格式，支持参数化定义 |
| **API** | Application Programming Interface | 应用程序编程接口 |
| **CI/CD** | Continuous Integration/Continuous Delivery | 持续集成/持续交付 |
| **PR** | Pull Request | 代码合并请求 |
| **RMSE** | Root Mean Square Error | 均方根误差，模型精度评估指标 |
| **mAP** | mean Average Precision | 平均精度均值，目标检测模型评估指标 |
| **FPS** | Frames Per Second | 帧率，图像/视频每秒帧数 |
| **Hz** | Hertz | 赫兹，频率单位，1 Hz = 每秒1次 |
| **ms** | millisecond | 毫秒，1 ms = 0.001 秒 |
| **VRAM** | Video Random Access Memory | 显存，GPU 的专用内存 |
| **TOPS** | Tera Operations Per Second | 算力单位，每秒万亿次运算 |
| **INT8/FP16/FP32** | 8-bit Integer / 16-bit Float / 32-bit Float | 模型量化精度格式 |
| **ONNX** | Open Neural Network Exchange | 开放神经网络交换格式，AI 模型跨框架部署标准 |
| **SDK** | Software Development Kit | 软件开发工具包 |
| **ROS2** | Robot Operating System 2 | 机器人操作系统第2版，开源机器人软件框架 |
| **Nav2** | Navigation2 | ROS2 导航框架，包含路径规划、障碍物避让等 |
| **MoveIt2** | — | ROS2 机械臂运动规划框架 |
| **OMPL** | Open Motion Planning Library | 开放运动规划库，MoveIt2 的规划算法后端 |
| **PCL** | Point Cloud Library | 点云处理开源库 |
| **SAM** | Segment Anything Model | Meta 开源的图像分割基础模型 |
| **YOLOv8** | You Only Look Once v8 | 实时目标检测模型（Ultralytics） |
| **MCAP** | — | 机器人数据包文件格式（ROS2 推荐，替代 .bag） |
| **UUID** | Universally Unique Identifier | 通用唯一标识符，用于唯一标识任务/技能 |
| **JSON** | JavaScript Object Notation | 轻量级数据交换格式 |
| **YAML** | YAML Ain't Markup Language | 配置文件格式，ROS2 参数文件常用 |
| **XML** | Extensible Markup Language | 可扩展标记语言，行为树配置文件格式 |
| **IPX5** | — | 防水等级，防任意方向低压喷水 |
| **ISO 10218** | — | 工业机器人安全标准 |

---

## 2. 中文术语

| 术语 | 解释 |
|------|------|
| **轮臂式机器人** | 本项目机器人形态：移动轮式底盘 + 6自由度机械臂组合，兼具移动性和操作能力 |
| **主控计算单元** | 承担 AI 推理、ROS2 主节点运行的 x86 服务器（前期配 RTX 4090，量产配 Thor） |
| **机械臂** | 本项目采用 Fairino FR5 6自由度协作机械臂 |
| **腕部相机** | 安装在机械臂末端附近的相机，本项目为 Orbbec Gemini 330，用于近距离感知 |
| **头部相机** | 安装在机器人头部/顶部的相机，本项目为 Orbbec Gemini 305，用于全局场景感知 |
| **底盘** | 移动平台，搭载 RK3588 芯片，负责 SLAM 建图和自主导航 |
| **清洁工具头** | 安装在机械臂末端的清洁工具，包括刷头、海绵、喷头、刮板等，可自动更换 |
| **示教** | 操作员通过 Meta Quest 3s 遥控机械臂完成清洁动作，记录轨迹用于 VLA 训练的过程 |
| **技能** | 可复用的原子清洁动作单元，如"擦拭表面"、"喷洒清洁剂"、"冲水"等（对应 Skill 层） |
| **技能库** | 所有技能的集合与管理框架（robot_skill_lib 包），行为树通过调用技能库执行清洁动作 |
| **任务** | 对应一个清洁场景（如"清洁马桶"），由多个技能组成，通过行为树调度（对应 Task 层） |
| **任务管理器** | `task_manager_node`，负责任务队列管理、行为树引擎驱动、VLA 输出解析 |
| **行为树** | BehaviorTree.CPP v4 实现的层次化任务决策树，用于组合技能完成复杂清洁流程 |
| **黑板** | BT Blackboard，行为树各节点共享数据的内存区域 |
| **状态机** | 有限状态机（FSM），描述任务或技能在不同阶段的状态与转换逻辑 |
| **感知节点** | `perception_node`，处理相机图像，输出场景物体检测结果、污渍图等 |
| **污渍图** | `StainMap.msg`，描述目标物体表面污渍分布的热力图或掩码图 |
| **清洁站位** | 机器人底盘移动到的清洁准备位置，与清洁目标保持安全距离，机械臂可以到达目标 |
| **覆盖率** | `cleaned_area_percent`，表示已清洁面积占目标总面积的百分比 |
| **阻抗控制** | 一种力控策略，通过调节机械臂刚度控制接触力，适合需要稳定接触力的擦拭动作 |
| **力限保护** | 当机械臂接触力超过预设阈值时自动停止运动的安全机制 |
| **急停** | 紧急停机，所有运动立即停止，进入安全状态 |
| **降级** | 当主功能（如 VLA 推理）不可用时，切换到备用方案（如规则驱动）继续运行 |
| **心跳** | 节点定期向系统发送的存活信号，`system_monitor_node` 通过心跳检测节点是否正常 |
| **自检** | 系统启动时对各模块进行的功能和状态验证，全部通过才进入正常工作模式 |
| **全厕清洁** | `TASK_FULL_CLEAN`，按顺序清洁整个卫生间的所有目标（洗手台、马桶、小便池、地面、墙面） |
| **量化** | 将模型参数从 FP32 降低精度到 FP16/INT8，减少显存占用和推理延迟 |
| **微调** | Fine-tuning，在预训练大模型基础上用项目特定数据进行适配训练 |
| **集轨迹** | Episode，一次完整的示教操作记录（从任务开始到完成的完整过程） |
| **里程计** | Odometry，机器人通过编码器等传感器估计自身位移的方法，有累积误差 |
| **占用栅格地图** | Occupancy Grid Map，SLAM 输出的二维地图，记录空间中各栅格是否被占用 |
| **法向量** | Surface Normal，垂直于物体表面的单位向量，用于清洁路径规划（确定接触方向） |
| **弓形路径** | Boustrophedon，来回往复的光栅扫描路径，常用于覆盖清洁 |
| **节点命名空间** | ROS2 Node Namespace，用于隔离不同实例或功能组的节点/话题前缀 |
| **话题** | ROS2 Topic，发布-订阅模式的异步通信通道 |
| **服务** | ROS2 Service，请求-响应模式的同步通信接口 |
| **动作** | ROS2 Action，带中间反馈的长时间异步任务接口 |
| **接口包** | `robot_interfaces`，定义项目所有自定义 .msg/.srv/.action 的 ROS2 包 |
| **参数文件** | YAML 格式的节点配置文件，存放在 `config/` 目录下 |
| **启动文件** | Launch File，Python 格式的 ROS2 节点批量启动脚本（`.launch.py`） |
| **工作空间** | ROS2 Workspace，存放所有 ROS2 包源码和编译产物的目录（通常为 `~/robot_ws`） |
| **colcon** | ROS2 官方构建工具，用于编译工作空间中的所有包 |
| **ament** | ROS2 构建系统（ament_cmake / ament_python），替代 ROS1 的 catkin |
| **Fast DDS** | ROS2 默认 DDS 实现（eProsima Fast DDS），负责节点间网络通信 |
| **CycloneDDS** | 另一个常用的 ROS2 DDS 实现，部分场景性能优于 Fast DDS |

---

## 3. 硬件产品术语

| 名称 | 型号/品牌 | 说明 |
|------|----------|------|
| **FR5** | Fairino（法奥意威）FR5 | 6自由度协作机械臂，额定负载 5kg，工作半径 924mm，本项目机械臂选型 |
| **Gemini 330** | Orbbec Gemini 330 | 结构光深度相机（RGB-D），腕部相机，用于近距离工作区感知 |
| **Gemini 305** | Orbbec Gemini 305 | 结构光深度相机（RGB-D），头部相机，用于全局场景感知 |
| **Quest 3s** | Meta Quest 3s | 混合现实头显，6DoF 手部追踪，用于示教数据采集 |
| **RK3588** | Rockchip RK3588 | 高性能嵌入式 SoC，搭载底盘，承担 SLAM 和导航计算 |
| **RTX 4090** | NVIDIA GeForce RTX 4090 | 研发阶段主控 GPU，24GB VRAM，用于 VLA 推理和感知加速 |
| **Thor** | NVIDIA Drive Thor | 量产阶段目标主控 SoC，专为自动驾驶/机器人设计，高集成度 |
| **Quest2ROS2** | — | 开源 ROS2 桥接工具，将 Meta Quest 2/3s 手部数据转发为 ROS2 话题 |

---

## 4. 模型与算法术语

| 术语 | 全称 / 描述 |
|------|------------|
| **OpenVLA** | Open Vision-Language-Action Model，开源 VLA 基础模型，7B 参数，基于 Prismatic VLM |
| **π₀** / **pi-zero** | Physical Intelligence 发布的 VLA 模型，适合机器人灵巧操作 |
| **RoboFlamingo** | 基于 Flamingo 架构的机器人 VLA 模型 |
| **LeRobot** | HuggingFace 开源的机器人学习库，提供标准化数据格式和训练工具 |
| **Cartographer** | Google 开源 SLAM 算法，支持 2D/3D 建图 |
| **slam_toolbox** | ROS2 常用 SLAM 库，支持持续建图和重定位 |
| **OMPL** | 开放运动规划库，MoveIt2 的路径规划后端，支持多种采样规划算法 |
| **YOLOv8** | Ultralytics YOLO 第8版，快速精准的目标检测/分割模型 |
| **SAM** | Segment Anything Model，Meta 开源的零样本图像分割模型 |
| **TensorRT** | NVIDIA 高性能深度学习推理优化库，支持 INT8/FP16 量化 |
| **BehaviorTree.CPP** | 开源 C++ 行为树库，本项目采用 v4 版本 |
| **Groot2** | BehaviorTree.CPP 配套的可视化调试工具 |
| **Isaac Sim** | NVIDIA 开源机器人物理仿真平台，支持光追渲染和 RTX 传感器仿真 |
| **Gazebo Harmonic** | 开源机器人仿真平台（Ignition Gazebo），ROS2 官方支持 |

---

## 5. 接口状态码速查

| 状态码类型 | 值域 | 参考文档 |
|-----------|------|---------|
| `TaskType` | 0~6 | [05_interface_naming_conventions.md § 5.3](05_interface_naming_conventions.md) |
| `TaskStatusCode` | 0~8 | [05_interface_naming_conventions.md § 5.4](05_interface_naming_conventions.md) |
| `SkillType` | 0~12 | [05_interface_naming_conventions.md § 5.5](05_interface_naming_conventions.md) |
| `SkillStateCode` | 0~4 | [05_interface_naming_conventions.md § 5.6](05_interface_naming_conventions.md) |
| `ObjectType` | 0~8 | [05_interface_naming_conventions.md § 5.1](05_interface_naming_conventions.md) |
| `StainType` | 0~6 | [05_interface_naming_conventions.md § 5.2](05_interface_naming_conventions.md) |
| `ToolType` | 0~5 | [05_interface_naming_conventions.md § 5.7](05_interface_naming_conventions.md) |
| `SystemMode` | 0~4 | [05_interface_naming_conventions.md § 5.8](05_interface_naming_conventions.md) |
| `AlertSeverity` | 0~3 | [05_interface_naming_conventions.md § 5.9](05_interface_naming_conventions.md) |
| `CleanMode` | 0~5 | [05_interface_naming_conventions.md § 5.10](05_interface_naming_conventions.md) |
| `CleanQuality` | 0~2 | [05_interface_naming_conventions.md § 5.11](05_interface_naming_conventions.md) |

---

## 6. 外部参考链接汇总

| 资源 | 链接 |
|------|------|
| Fairino FR5 ROS2 驱动文档 | https://fairino-doc-zhs.readthedocs.io/3.9.3/ROSGuide/ros2guide.html |
| Fairino FR5 URDF | https://github.com/FAIR-INNOVATION/frcobot_ros2/tree/main/fairino_description/urdf |
| Quest2ROS2 开源项目 | https://github.com/Taokt/Quest2ROS2 |
| Orbbec SDK ROS2 | https://github.com/orbbec/OrbbecSDK_ROS2 |
| ROS2 Humble 文档 | https://docs.ros.org/en/humble/ |
| Nav2 导航框架 | https://navigation.ros.org/ |
| MoveIt2 文档 | https://moveit.picknik.ai/ |
| BehaviorTree.CPP v4 | https://www.behaviortree.dev/ |
| OpenVLA 论文 | https://arxiv.org/abs/2406.09246 |
| LeRobot | https://github.com/huggingface/lerobot |
| NVIDIA Isaac Sim | https://developer.nvidia.com/isaac-sim |
| NVIDIA TensorRT | https://developer.nvidia.com/tensorrt |

---

## 7. 文档版本历史

| 文档编号 | 文件名 | 版本 | 最后更新 | 说明 |
|---------|--------|------|---------|------|
| 01 | `01_ros2_node_architecture.md` | v1.0 | 2026-04-09 | 初稿 |
| 02 | `02_ros2_detailed_design.md` | v1.0 | 2026-04-09 | 初稿 |
| 03 | `03_custom_messages.md` | v1.0 | 2026-04-09 | 初稿 |
| 04 | `04_robot_interfaces_package.md` | v1.0 | 2026-04-09 | 初稿 |
| 05 | `05_interface_naming_conventions.md` | v1.0 | 2026-04-09 | 初稿 |
| 06 | `06_task_skill_bt_spec.md` | v1.0 | 2026-04-09 | 初稿 |
| 07 | `07_vla_hybrid_control.md` | v1.0 | 2026-04-09 | 初稿 |
| 08 | `08_state_machine_bt_design.md` | v1.0 | 2026-04-09 | 初稿 |
| 09 | `09_product_management.md` | v1.0 | 2026-04-09 | 初稿 |
| 10 | `10_glossary.md` | v1.0 | 2026-04-09 | 初稿（本文档） |
