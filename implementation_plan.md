# Seeway Interface Board — Complete Software Framework

## Background

机器人系统由以下节点组成：

| 节点 | 角色 |
|------|------|
| **全志 T113i** | 接口板，运行 Linux，控制传感器/电磁阀/执行器/电池，强制管理所有节点电源 |
| **Jetson Orin NX** | 主控，运行 ROS2，连接 frcobot 机械臂 + Gemini 330 相机 |
| **RK3588** | 底盘主控，运行导航 |
| **云端** | 与 Jetson 通信 |

T113i 与 Jetson 之间通过 **TCP（Ethernet）** 或 **UART** 传输自定义二进制帧协议。

---

## Architecture Overview

```
┌─────────────────────────────────────────────────────┐
│               Allwinner T113i (Linux)               │
│                                                     │
│  ┌────────────┐  ┌──────────┐  ┌────────────────┐  │
│  │SensorReader│  │GpioCtrl  │  │PowerManager    │  │
│  │ ADC/I2C    │  │DO/PWM    │  │NX/Arm/Chassis  │  │
│  └─────┬──────┘  └────┬─────┘  └───────┬────────┘  │
│        │             │                 │            │
│  ┌─────▼─────────────▼─────────────────▼────────┐  │
│  │              TaskExecutor                     │  │
│  │  arm_home / estop / shutdown / valve control  │  │
│  └─────────────────────┬─────────────────────────┘  │
│                        │                            │
│  ┌─────────────────────▼─────────────────────────┐  │
│  │   SerialComm (UART or TCP client → Jetson)    │  │
│  │   binary protocol: AA55 framing + CRC16        │  │
│  └───────────────────────────────────────────────┘  │
│                        ▲                            │
│  ┌─────────────────────┴─────────────────────────┐  │
│  │  InputHandler (evdev: keyboard / touch / GPIO)│  │
│  └───────────────────────────────────────────────┘  │
└─────────────────────────────────────────────────────┘
                         │ TCP / UART
┌─────────────────────────────────────────────────────┐
│               Jetson Orin NX (ROS2)                 │
│                                                     │
│  ┌────────────────────────────────────────────────┐ │
│  │  seeway_interface_driver (Lifecycle Node)      │ │
│  │  TCP Server → FrameCodec → ROS2 pub/sub        │ │
│  │                                                │ │
│  │  Topics published:                             │ │
│  │    /seeway/sensor_data   (SensorData)          │ │
│  │    /seeway/gpio_status   (GpioStatus)          │ │
│  │    /seeway/battery_status(BatteryStatus)       │ │
│  │    /seeway/system_status (SystemStatus)        │ │
│  │    /seeway/input_event   (InputEvent)          │ │
│  │                                                │ │
│  │  Services:                                     │ │
│  │    /seeway/set_gpio      (SetGpio)             │ │
│  │    /seeway/set_pwm       (SetPwm)              │ │
│  │    /seeway/power_control (PowerControl)        │ │
│  │    /seeway/send_task     (SendTask)            │ │
│  └────────────────────────────────────────────────┘ │
│                                                     │
│  ┌────────────────────────────────────────────────┐ │
│  │  seeway_interface_hardware (ros2_control plugin│ │
│  │  HardwareInterface → reads topics, calls srvs  │ │
│  │  Exposes StateInterfaces + CommandInterfaces    │ │
│  └────────────────────────────────────────────────┘ │
└─────────────────────────────────────────────────────┘
```

---

## Proposed Changes

### T113i Daemon (C++)

#### [MODIFY] [main.cpp](file:///d:/WorkingProject/Antigravity_workspace/seeway_interfaceB1/t113i_daemon/src/main.cpp) [NEW]
守护进程入口：解析配置，初始化所有模块，注册协议处理回调，运行主循环，处理 SIGTERM/SIGINT。

#### [NEW] [CMakeLists.txt](file:///d:/WorkingProject/Antigravity_workspace/seeway_interfaceB1/t113i_daemon/CMakeLists.txt)
交叉编译配置：目标 arm-linux-gnueabihf，C++17，静态链接 pthread。

#### [NEW] [daemon.conf](file:///d:/WorkingProject/Antigravity_workspace/seeway_interfaceB1/t113i_daemon/config/daemon.conf)
示例配置文件（transport, device, host, GPIO map, ADC channels）。

---

### Jetson ROS2 — `seeway_interface_msgs`

自定义消息和服务接口包。

#### [NEW] SensorData.msg, GpioStatus.msg, BatteryStatus.msg, SystemStatus.msg, InputEvent.msg
#### [NEW] SetGpio.srv, SetPwm.srv, PowerControl.srv, SendTask.srv
#### [NEW] CMakeLists.txt + package.xml

---

### Jetson ROS2 — `seeway_interface_driver`

ROS2 Lifecycle Node，作为 TCP 服务器接收 T113i 连接，解析帧后发布 ROS2 话题，并提供服务接口让其他节点发送命令到 T113i。

#### [NEW] driver_node.hpp / driver_node.cpp
#### [NEW] CMakeLists.txt + package.xml

---

### Jetson ROS2 — `seeway_interface_hardware`

`ros2_control` hardware interface plugin，订阅 driver 话题，将传感器和 GPIO 状态暴露为 `StateInterface`，将命令通过服务发送给 T113i。

#### [NEW] seeway_hardware_interface.hpp / seeway_hardware_interface.cpp
#### [NEW] CMakeLists.txt + package.xml

---

### Config & Launch

#### [NEW] seeway_interface.yaml — 参数配置（IP、端口、话题名）
#### [NEW] seeway_ros2_control.yaml — ros2_control 控制器配置
#### [NEW] seeway_interface.launch.py — 启动文件

---

## Verification Plan

### Code Review (Manual)
1. 审查协议帧编解码一致性（T113i [FrameCodec](file:///d:/WorkingProject/Antigravity_workspace/seeway_interfaceB1/t113i_daemon/include/serial_comm.h#80-98) vs Jetson [FrameCodec](file:///d:/WorkingProject/Antigravity_workspace/seeway_interfaceB1/t113i_daemon/include/serial_comm.h#80-98)）
2. 审查 CRC16 计算覆盖范围是否一致
3. 确认所有 `#pragma pack(push,1)` 结构体对齐

### Build Verification
```bash
# T113i side (cross-compile on Linux host)
cd t113i_daemon && mkdir build && cd build
cmake .. -DCMAKE_TOOLCHAIN_FILE=../arm-linux-gnueabihf.cmake
make -j4

# Jetson side (colcon build)
cd ros2_ws
colcon build --packages-select seeway_interface_msgs seeway_interface_driver seeway_interface_hardware
```

### Functional Test (Loopback)
在同一机器上运行 T113i daemon（TCP模式连接 localhost:9000）和 Jetson driver node，观察：
- `ros2 topic echo /seeway/sensor_data` 能收到周期数据
- `ros2 service call /seeway/set_gpio` 能回传 ACK
- `ros2 service call /seeway/send_task` 触发 TaskExecutor
