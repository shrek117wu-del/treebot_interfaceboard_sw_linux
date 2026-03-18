我在开发一个机器人系统，Jetson Orin NX 运行ros2，frcobot机械臂(frcobot_ros) 和Gemini 330（OrbbecSDK_ROS2）连接到NX。rk3588作为移动地盘的主控并运行导航等功能。全志t113i作为接口板，连接一些sensor和电磁阀，和小的执行器，电池控制器等，并强制控制frcobot机械臂、底盘、Jetson Orin NX的电源，。Jetson Orin NX 运行主控程序，并于云端链接，和rk3588，全志t113i交互数据。全志t113i 运行linux系统，我需要开发一套软件框架，全志t113i链接Jetson Orin NX ，Jetson Orin NX 上的ros2程序，可以读取接口板上的数据，和发送命令给接口板，控制全志t113i连接的一些sensor和电磁阀，和小的执行器，电池控制器等。全志t113i也可以自主接受来自外部键盘、touch、硬件gpio等输入，执行一些任务，如强制关闭电源，机械臂归位等。请给出完整的代码方案


T113i 与 Jetson ROS2 全套通讯与控制框架代码已完全编写完毕！

完成内容：

T113i 侧： 完成了守护进程（seeway_interface_daemon）的搭建，补齐了 main 函数，集成传感器读取、GPIO 控制、电源管理、系统任务执行以及硬件事件回传，并配好了跨平台编译的 CMakeLists.txt 和运行配置文件。
Jetson ROS2 seeway_interface_msgs： 定义了 5个 Topic 消息和 4个 Service 接口。
Jetson ROS2 seeway_interface_driver： 实现了一个基于 TCP 的 Lifecycle Node，它作为网桥自动解析底层的二进制协议，将其双向映射到上述的 Topics 和 Services 上。
Jetson ROS2 seeway_interface_hardware： 开发了标准的 ros2_control 硬件接口插件 (SystemInterface)，这使得后续你只需要在 URDF 里注册这个插件，就能无缝在 ROS2 控制器框架里直接读取 T113i 的 8路模拟量和诸多开关量，同时能映射下发运动库命令到 T113i 的 PWM 和 Digital Outs。
附上了配套的 CMake 脚本、package.xml、以及启动 Launch / yaml 参数配置。
我已经在 walkthrough.md 中编写了包含架构说明和交叉编译/启动指令的说明文档指南。代码已全数生成在您的工程目录中。