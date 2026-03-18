我在开发一个机器人系统，Jetson Orin NX 运行ros2，frcobot机械臂(frcobot_ros) 和Gemini 330（OrbbecSDK_ROS2）连接到NX。rk3588作为移动地盘的主控并运行导航等功能。全志t113i作为接口板，连接一些sensor和电磁阀，和小的执行器，电池控制器等，并强制控制frcobot机械臂、底盘、Jetson Orin NX的电源，。Jetson Orin NX 运行主控程序，并于云端链接，和rk3588，全志t113i交互数据。全志t113i 运行linux系统，我需要开发一套软件框架，全志t113i链接Jetson Orin NX ，Jetson Orin NX 上的ros2程序，可以读取接口板上的数据，和发送命令给接口板，控制全志t113i连接的一些sensor和电磁阀，和小的执行器，电池控制器等。全志t113i也可以自主接受来自外部键盘、touch、硬件gpio等输入，执行一些任务，如强制关闭电源，机械臂归位等。请给出完整的代码方案


T113i 与 Jetson ROS2 全套通讯与控制框架代码已完全编写完毕！

完成内容：

T113i 侧： 完成了守护进程（seeway_interface_daemon）的搭建，补齐了 main 函数，集成传感器读取、GPIO 控制、电源管理、系统任务执行以及硬件事件回传，并配好了跨平台编译的 CMakeLists.txt 和运行配置文件。
Jetson ROS2 seeway_interface_msgs： 定义了 5个 Topic 消息和 4个 Service 接口。
Jetson ROS2 seeway_interface_driver： 实现了一个基于 TCP 的 Lifecycle Node，它作为网桥自动解析底层的二进制协议，将其双向映射到上述的 Topics 和 Services 上。
Jetson ROS2 seeway_interface_hardware： 开发了标准的 ros2_control 硬件接口插件 (SystemInterface)，这使得后续你只需要在 URDF 里注册这个插件，就能无缝在 ROS2 控制器框架里直接读取 T113i 的 8路模拟量和诸多开关量，同时能映射下发运动库命令到 T113i 的 PWM 和 Digital Outs。
附上了配套的 CMake 脚本、package.xml、以及启动 Launch / yaml 参数配置。
我已经在 walkthrough.md 中编写了包含架构说明和交叉编译/启动指令的说明文档指南。代码已全数生成在您的工程目录中。

---

## ⚠️ Breaking Change: Interface Naming Scheme Renamed

> **This change is introduced in this PR and is NOT backward-compatible.**
> All users must update their URDF `<ros2_control>` blocks and controller YAML files
> to use the new interface names shown below.

### Old → New Mapping

#### State Interfaces

| Old Name          | New Name                    |
|-------------------|-----------------------------|
| `adc_0`           | `analog_in/0`               |
| `adc_1`           | `analog_in/1`               |
| `adc_2`           | `analog_in/2`               |
| `adc_3`           | `analog_in/3`               |
| `adc_4`           | `analog_in/4`               |
| `adc_5`           | `analog_in/5`               |
| `adc_6`           | `analog_in/6`               |
| `adc_7`           | `analog_in/7`               |
| `temperature`     | `environment/temperature_c` |
| `humidity`        | `environment/humidity_pct`  |
| `gpio_in_0`       | `digital_in/0`              |
| `gpio_in_1`       | `digital_in/1`              |
| ...               | ...                         |
| `gpio_in_31`      | `digital_in/31`             |

#### Command Interfaces

| Old Name          | New Name        |
|-------------------|-----------------|
| `gpio_out_0`      | `digital_out/0` |
| `gpio_out_1`      | `digital_out/1` |
| ...               | ...             |
| `gpio_out_31`     | `digital_out/31`|
| `pwm_out_0`       | `pwm_out/0`     |
| `pwm_out_1`       | `pwm_out/1`     |
| `pwm_out_2`       | `pwm_out/2`     |
| `pwm_out_3`       | `pwm_out/3`     |

---

## Interface Naming Reference

The `seeway_interface_hardware` plugin exposes the following hardware interfaces
(component name = `<hardware name>` as declared in the URDF):

### State Interfaces

| Interface Name              | Description                  | Index |
|-----------------------------|------------------------------|-------|
| `analog_in/0` … `analog_in/7` | ADC raw values (8 channels) | 0–7   |
| `environment/temperature_c` | Ambient temperature (°C)     | –     |
| `environment/humidity_pct`  | Relative humidity (%)        | –     |
| `digital_in/0` … `digital_in/31` | Digital input pin states | 0–31  |

### Command Interfaces

| Interface Name                    | Description                    | Index |
|-----------------------------------|--------------------------------|-------|
| `digital_out/0` … `digital_out/31` | Digital output pin commands   | 0–31  |
| `pwm_out/0` … `pwm_out/3`         | PWM duty-cycle commands (0–1000 per-mil) | 0–3 |

---

## Prerequisites (ROS 2 Humble)

```bash
sudo apt install ros-humble-ros2-control ros-humble-ros2-controllers
source /opt/ros/humble/setup.bash
```

---

## Example URDF Snippet (`ros2_control` block)

```xml
<ros2_control name="seeway_board" type="system">
  <hardware>
    <plugin>seeway_interface_hardware/SeewayHardwareInterface</plugin>
  </hardware>

  <!-- Analog inputs (ADC 0-7) -->
  <joint name="seeway_board">
    <state_interface name="analog_in/0"/>
    <state_interface name="analog_in/1"/>
    <state_interface name="analog_in/2"/>
    <state_interface name="analog_in/3"/>
    <state_interface name="analog_in/4"/>
    <state_interface name="analog_in/5"/>
    <state_interface name="analog_in/6"/>
    <state_interface name="analog_in/7"/>

    <!-- Environment sensors -->
    <state_interface name="environment/temperature_c"/>
    <state_interface name="environment/humidity_pct"/>

    <!-- Digital inputs (GPIO bank 0, 32 pins) -->
    <state_interface name="digital_in/0"/>
    <state_interface name="digital_in/1"/>
    <!-- ... digital_in/2 through digital_in/31 ... -->
    <state_interface name="digital_in/31"/>

    <!-- Digital outputs (GPIO bank 0, 32 pins) -->
    <command_interface name="digital_out/0"/>
    <command_interface name="digital_out/1"/>
    <!-- ... digital_out/2 through digital_out/31 ... -->
    <command_interface name="digital_out/31"/>

    <!-- PWM outputs (4 channels) -->
    <command_interface name="pwm_out/0"/>
    <command_interface name="pwm_out/1"/>
    <command_interface name="pwm_out/2"/>
    <command_interface name="pwm_out/3"/>
  </joint>
</ros2_control>
```

---

## Example Controller YAML (`seeway_ros2_control.yaml`)

```yaml
controller_manager:
  ros__parameters:
    update_rate: 100  # Hz

    joint_state_broadcaster:
      type: joint_state_broadcaster/JointStateBroadcaster

    # Example: ForwardCommandController driving digital_out/0
    seeway_digital_out_controller:
      type: forward_command_controller/ForwardCommandController

seeway_digital_out_controller:
  ros__parameters:
    joints:
      - seeway_board
    interface_name: digital_out/0

# Example: ForwardCommandController driving pwm_out/0
seeway_pwm_controller:
  ros__parameters:
    joints:
      - seeway_board
    interface_name: pwm_out/0
```

> **Note:** When using `ForwardCommandController`, you must also declare the matching
> `<command_interface>` in the URDF `<joint>` block above.

---

## Build Instructions (ROS 2 Humble)

```bash
cd ~/ros2_ws
colcon build --packages-select seeway_interface_msgs seeway_interface_driver seeway_interface_hardware
source install/setup.bash
```
