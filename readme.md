���ڿ���һ��������ϵͳ��Jetson Orin NX ����ros2��frcobot��е��(frcobot_ros) ��Gemini 330��OrbbecSDK_ROS2�����ӵ�NX��rk3588��Ϊ�ƶ����̵����ز����е����ȹ��ܡ�ȫ־t113i��Ϊ�ӿڰ壬����һЩsensor�͵�ŷ�����С��ִ��������ؿ������ȣ���ǿ�ƿ���frcobot��е�ۡ����̡�Jetson Orin NX�ĵ�Դ����Jetson Orin NX �������س��򣬲����ƶ����ӣ���rk3588��ȫ־t113i�������ݡ�ȫ־t113i ����linuxϵͳ������Ҫ����һ��������ܣ�ȫ־t113i����Jetson Orin NX ��Jetson Orin NX �ϵ�ros2���򣬿��Զ�ȡ�ӿڰ��ϵ����ݣ��ͷ���������ӿڰ壬����ȫ־t113i���ӵ�һЩsensor�͵�ŷ�����С��ִ��������ؿ������ȡ�ȫ־t113iҲ�����������������ⲿ���̡�touch��Ӳ��gpio�����룬ִ��һЩ������ǿ�ƹرյ�Դ����е�۹�λ�ȡ�����������Ĵ��뷽��


T113i �� Jetson ROS2 ȫ��ͨѶ����ƿ�ܴ�������ȫ��д��ϣ�

������ݣ�

T113i �ࣺ ������ػ����̣�seeway_interface_daemon���Ĵ�������� main ���������ɴ�������ȡ��GPIO ���ơ���Դ������ϵͳ����ִ���Լ�Ӳ���¼��ش���������˿�ƽ̨����� CMakeLists.txt �����������ļ���
Jetson ROS2 seeway_interface_msgs�� ������ 5�� Topic ��Ϣ�� 4�� Service �ӿڡ�
Jetson ROS2 seeway_interface_driver�� ʵ����һ������ TCP �� Lifecycle Node������Ϊ�����Զ������ײ�Ķ�����Э�飬����˫��ӳ�䵽������ Topics �� Services �ϡ�
Jetson ROS2 seeway_interface_hardware�� �����˱�׼�� ros2_control Ӳ���ӿڲ�� (SystemInterface)����ʹ�ú�����ֻ��Ҫ�� URDF ��ע���������������޷��� ROS2 �����������ֱ�Ӷ�ȡ T113i �� 8·ģ��������࿪������ͬʱ��ӳ���·��˶������ T113i �� PWM �� Digital Outs��
���������׵� CMake �ű���package.xml���Լ����� Launch / yaml �������á�
���Ѿ��� walkthrough.md �б�д�˰����ܹ�˵���ͽ������/����ָ���˵���ĵ�ָ�ϡ�������ȫ�����������Ĺ���Ŀ¼�С�

## ROS 2 Humble Build Prerequisites (Jetson / Ubuntu 22.04)

### Required packages

Before building, install the `ros2_control` development packages:

```bash
sudo apt update
sudo apt install ros-humble-ros2-control ros-humble-ros2-controllers
```

### Source the ROS 2 environment

Always source the ROS 2 setup script before building or running anything:

```bash
source /opt/ros/humble/setup.bash
```

### Build the packages

```bash
cd ~/ros2_ws
colcon build --packages-select seeway_interface_msgs seeway_interface_driver seeway_interface_hardware
source install/setup.bash
```

### Troubleshooting

**CMake cannot find `hardware_interface` (`hardware_interfaceConfig.cmake`)**

This means the `ros2_control` dev packages are not installed. Verify with:

```bash
ros2 pkg prefix hardware_interface
```

If this command fails, run the install step above and re-source `/opt/ros/humble/setup.bash`.

**Plugin not found at runtime**

Ensure the library path in `seeway_hardware_interface.xml` matches the built shared library name (`seeway_interface_hardware`). The XML entry must be:

```xml
<library path="seeway_interface_hardware">
  ...
</library>
```

**`controller_manager` not found**

The `controller_manager` package ships with `ros-humble-ros2-controllers`. Install it with the command above.
