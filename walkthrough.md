# Treebot Interface Board – Walkthrough

This document explains how to build, configure, and run the Jetson-side ROS 2
driver, the T113i daemon, and how to do a loopback test on a single machine.

---

## Repository layout

```
seeway_interface_msgs/      ROS 2 message & service definitions
seeway_interface_driver/    Lifecycle node: protocol ↔ ROS 2 bridge
seeway_interface_hardware/  ros2_control SystemInterface plugin
t113i_daemon/               Standalone C++ daemon for Allwinner T113i
```

---

## 1  Building the Jetson (ROS 2) packages

### Prerequisites

| Tool | Version |
|------|---------|
| ROS 2 | Humble or Iron |
| colcon | any recent |
| C++ | 17 |

```bash
# Inside your ROS 2 workspace
cd ~/ros2_ws
colcon build --packages-select \
    seeway_interface_msgs \
    seeway_interface_driver \
    seeway_interface_hardware
source install/setup.bash
```

---

## 2  Starting the driver on Jetson

The driver is a **lifecycle node** shipped as a composable component.  
Parameter files live in `seeway_interface_driver/config/`.

### 2.1  TCP server mode (Jetson listens, T113i connects)

```bash
ros2 run seeway_interface_driver seeway_driver_node_exe \
  --ros-args \
  --param transport:=tcp \
  --param tcp.mode:=server \
  --param tcp.port:=9000 \
  --param tcp.bind_address:=0.0.0.0 \
  --param ack_timeout_ms:=300 \
  --param ack_retries:=1
```

Then transition the lifecycle node to `active`:

```bash
ros2 lifecycle set /seeway_interface_driver configure
ros2 lifecycle set /seeway_interface_driver activate
```

### 2.2  TCP client mode (T113i acts as server)

```bash
ros2 run seeway_interface_driver seeway_driver_node_exe \
  --ros-args \
  --param transport:=tcp \
  --param tcp.mode:=client \
  --param tcp.host:=192.168.1.50 \
  --param tcp.port:=9000 \
  --param tcp.reconnect_ms:=1000
```

### 2.3  Serial / USB-CDC mode

```bash
ros2 run seeway_interface_driver seeway_driver_node_exe \
  --ros-args \
  --param transport:=serial \
  --param serial.device:=/dev/ttyACM0 \
  --param serial.baudrate:=115200
```

### 2.4  Using a launch file with YAML parameters

```bash
ros2 launch seeway_interface_driver seeway_interface.launch.py
```

Edit `seeway_interface_driver/config/seeway_interface.yaml` before launching.

---

## 3  Building the T113i daemon

The daemon is a plain CMake project – no ROS 2 required on the T113i.

```bash
cd t113i_daemon
mkdir build && cd build
cmake ..
make -j$(nproc)
```

The binary is `seeway_interface_daemon`.

---

## 4  Running the T113i daemon

### 4.1  Default configuration path

```bash
# Copy the example config (first-time setup)
sudo mkdir -p /etc/seeway_interface
sudo cp config/daemon.conf /etc/seeway_interface/daemon.conf

# Edit as needed (transport, IPs, GPIO pins, ADC channels, power rails)
sudo nano /etc/seeway_interface/daemon.conf

# Start the daemon (reads /etc/seeway_interface/daemon.conf by default)
sudo ./seeway_interface_daemon
```

### 4.2  Custom configuration path

```bash
./seeway_interface_daemon /path/to/custom.conf
```

If the configuration file cannot be opened or parsed, the daemon prints an
error and exits with code 1.

### 4.3  daemon.conf quick reference

```ini
[communication]
transport=tcp               # tcp | uart
server_ip=192.168.1.10      # Jetson IP (tcp client mode)
server_port=9000
# uart_device=/dev/ttyS3
# baud_rate=115200

[gpio]
# bank,pin,linux_num,is_output,active_low,label
pin_0=0,0,10,true,false,valve_1
pin_1=0,1,11,true,false,valve_2
pin_2=2,0,64,false,false,estop_btn

[adc]
# index,sysfs_path,scale,offset,unit
ch_0=0,/sys/bus/iio/devices/iio:device0/in_voltage0_raw,0.001,0.0,V

[power]
# on_cmd_id(hex),off_cmd_id(hex),gpio_num,active_low,min_on_ms,sequence_delay_ms
nx_rail=0x01,0x02,20,true,5000,1000
```

---

## 5  Loopback test (Jetson and T113i on the same machine)

A loopback test lets you verify the full ROS 2 ↔ daemon stack without
physical hardware.

### 5.1  Start the daemon in TCP-client mode on localhost

```bash
# daemon.conf:
#   transport=tcp
#   server_ip=127.0.0.1
#   server_port=9000

./seeway_interface_daemon /path/to/daemon.conf
```

### 5.2  Start the Jetson driver in TCP-server mode on localhost

```bash
ros2 run seeway_interface_driver seeway_driver_node_exe \
  --ros-args \
  --param transport:=tcp \
  --param tcp.mode:=server \
  --param tcp.port:=9000 \
  --param tcp.bind_address:=127.0.0.1

ros2 lifecycle set /seeway_interface_driver configure
ros2 lifecycle set /seeway_interface_driver activate
```

### 5.3  Verify connectivity

```bash
# Watch sensor data arriving from the daemon's mock sensor
ros2 topic echo /seeway/sensor_data

# Check system status
ros2 topic echo /seeway/system_status

# Send a GPIO command and check the ACK result
ros2 service call /seeway/set_gpio seeway_interface_msgs/srv/SetGpio \
  "{bank: 0, pin_mask: 1, pin_states: 1}"

# Send a task command
ros2 service call /seeway/send_task seeway_interface_msgs/srv/SendTask \
  "{task_id: 2, arg: 0, name: 'estop_test'}"
```

---

## 6  Key parameters reference

| Parameter | Default | Description |
|-----------|---------|-------------|
| `transport` | `tcp` | `tcp` or `serial` |
| `tcp.mode` | `server` | `server` or `client` |
| `tcp.port` | `9000` | TCP port |
| `tcp.host` | `192.168.1.50` | Remote host (client mode) |
| `tcp.reconnect_ms` | `1000` | Reconnect interval (client mode) |
| `serial.device` | `/dev/ttyACM0` | Serial device path |
| `serial.baudrate` | `115200` | Baud rate |
| `ack_timeout_ms` | `300` | Timeout waiting for MSG_ACK (ms) |
| `ack_retries` | `0` | Number of retransmit attempts on ACK timeout |

---

## 7  Architecture notes

```
Jetson (ROS 2)                         T113i
┌──────────────────────┐               ┌──────────────────────┐
│  seeway_interface_   │               │  seeway_interface_   │
│  driver (lifecycle)  │◄──TCP/UART───►│  daemon              │
│                      │               │                      │
│  /seeway/sensor_data │               │  GpioController      │
│  /seeway/gpio_status │               │  SensorReader        │
│  /seeway/set_gpio    │               │  PowerManager        │
│  /seeway/send_task   │               │  InputHandler        │
└──────────┬───────────┘               │  TaskExecutor        │
           │ ros2_control              └──────────────────────┘
┌──────────▼───────────┐
│  seeway_interface_   │
│  hardware (plugin)   │
│  (StateInterface,    │
│   CommandInterface)  │
└──────────────────────┘
```

### Communication reliability

- Every outbound command (`SetGpio`, `SetPwm`) is matched to an `MSG_ACK`
  from the T113i using the frame sequence number.  If no ACK arrives within
  `ack_timeout_ms` the call is retried up to `ack_retries` times.
- `SendTask` waits up to 5 seconds for `MSG_TASK_RESPONSE`.  Concurrent
  `SendTask` calls are isolated by sequence number.
- The ros2_control `read()` loop never calls `spin_some()`; a dedicated
  `SingleThreadedExecutor` thread handles all subscription callbacks.
