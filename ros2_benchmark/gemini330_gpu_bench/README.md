# Gemini 330 / 305 双相机 GPU 基准测试套件

> **适用环境**：x86 + RTX 4090（前期）/ x86 + Thor（最终）·ROS2 Humble · Ubuntu 22.04

---

## 1. 系统要求

### 1.1 硬件

| 设备 | 型号 | 接口 |
|------|------|------|
| Wrist Camera | Orbbec Gemini 330 | USB 3.x → x86 |
| Head Camera | Orbbec Gemini 305 | USB 3.x → x86 |
| GPU | NVIDIA RTX 4090 / Thor | PCIe |

> 两台相机建议连接到**不同的 USB Host Controller**，避免争用同一条 USB 总线带宽。

### 1.2 软件依赖

```bash
# ROS2 Humble
sudo apt install ros-humble-desktop

# Orbbec 相机驱动（OrbbecSDK_ROS2 >= 1.8）
# 参见：https://github.com/orbbec/OrbbecSDK_ROS2

# CUDA（>= 12.0）
# 参见：https://developer.nvidia.com/cuda-downloads

# Python 依赖
pip3 install psutil numpy

# 系统工具
sudo apt install lsusb bc
```

### 1.3 固件版本要求

| 相机 | 最低固件版本 |
|------|-------------|
| Orbbec Gemini 330 | ≥ 1.6.00 |
| Orbbec Gemini 305 | ≥ 1.0.30 |

固件版本检查：OrbbecViewer → 设备信息。

---

## 2. 已知 USB 问题与解决方案

### 2.1 usbfs_memory 不足（ENOMEM / 图像丢失）

**现象**：驱动报错 `usbfs: USBDEVFS_SUBMITURB` 失败，或 `/var/log/syslog` 出现 `Cannot allocate memory`。

**解决**：
```bash
# 临时生效
echo 128 | sudo tee /sys/module/usbcore/parameters/usbfs_memory_mb

# 永久生效：在 /etc/rc.local 或 systemd unit 中添加上述命令
```

### 2.2 V4L2 后端稳定性

相比默认 LibUVC 后端，V4L2 在 Linux 内核 5.15+ 上更稳定。配置文件已默认开启：
```yaml
uvc_backend: "V4L2"
```

若遇到 `/dev/video*` 权限问题：
```bash
sudo usermod -aG video $USER
# 重新登录后生效
```

### 2.3 USB Host Controller 拓扑

使用 `lsusb -t` 查看 USB 拓扑，确保两台相机连接在不同的 xHCI 控制器下：

```
/:  Bus 04.Port 1: Dev 1  ← 控制器 A（接 Gemini 330）
/:  Bus 02.Port 1: Dev 1  ← 控制器 B（接 Gemini 305）
```

### 2.4 udev 权限配置

OrbbecSDK_ROS2 安装时会自动添加 udev 规则。若未自动添加：
```bash
# 将 udev 规则文件复制到 /etc/udev/rules.d/
sudo cp <orbbec_ws>/src/OrbbecSDK_ROS2/orbbec_camera/scripts/99-obsensor-libusb.rules \
       /etc/udev/rules.d/
sudo udevadm control --reload-rules && sudo udevadm trigger
```

### 2.5 USB 3.0 带宽预算

| 流 | 分辨率 | FPS | 理论带宽 |
|----|--------|-----|---------|
| Wrist Color (RGB8) | 1280×720 | 30 | ≈ 66 MB/s |
| Wrist Depth (16bit) | 1280×720 | 30 | ≈ 44 MB/s |
| Head Color (RGB8) | 640×480 | 15 | ≈ 14 MB/s |
| Head Depth (16bit) | 640×480 | 15 | ≈ 9 MB/s |
| **合计** | | | **≈ 133 MB/s** |

USB 3.0 理论带宽 ≈ 400 MB/s，双相机在同一控制器下也应够用，但建议分开以保证稳定性。

---

## 3. 构建步骤

```bash
# 进入 ROS2 工作空间
cd ~/ros2_ws

# 将包放到 src 目录（或已存在）
# cp -r <repo>/ros2_benchmark/gemini330_gpu_bench src/

# 构建
colcon build --packages-select gemini330_gpu_bench \
  --cmake-args -DCMAKE_BUILD_TYPE=RelWithDebInfo

# 加载环境
source install/setup.bash
```

---

## 4. 运行方式

### 4.1 运行 Python 基准测试节点（FPS/延迟统计）

```bash
# 方式1：使用 launch 文件（推荐，同时启动双相机）
ros2 launch gemini330_gpu_bench dual_camera_bench.launch.py

# 方式2：单独运行基准测试节点（相机已独立运行）
ros2 run gemini330_gpu_bench camera_benchmark_node
```

输出文件：`/tmp/camera_benchmark.csv`

### 4.2 运行 C++ GPU Upload 测试节点

```bash
ros2 run gemini330_gpu_bench gpu_upload_node
```

输出文件：`/tmp/gpu_upload_bench.csv`

### 4.3 使用完整压测脚本

```bash
chmod +x scripts/run_camera_benchmark.sh
./scripts/run_camera_benchmark.sh 120   # 测试 120 秒
```

### 4.4 持续 GPU 资源监控

```bash
chmod +x scripts/monitor_gpu_resources.sh
./scripts/monitor_gpu_resources.sh /tmp/gpu_monitor.csv
```

---

## 5. 预期资源占用（双相机场景）

| 指标 | 预期范围 | 说明 |
|------|---------|------|
| CPU（主控进程） | 15 % ～ 35 % | 双相机解压 + ROS2 发布 |
| 系统 RAM | 800 MB ～ 1.5 GB | 含 ROS2 DDS 缓冲 + 图像队列 |
| GPU 显存（上传节点） | 500 MB ～ 2 GB | 取决于 buffer 池大小 |
| GPU 利用率（上传节点） | 1 % ～ 5 % | cudaMemcpyAsync 本身开销极小 |
| GPU 利用率（含推理） | 30 % ～ 80 % | 若叠加 TensorRT 推理 |
| USB 带宽（单控制器） | ≈ 133 MB/s | 建议 < 300 MB/s |

> **注意**：上述数据基于 RTX 4090 + Ubuntu 22.04 + ROS2 Humble + OrbbecSDK 1.8 测试。
> Thor 平台数据需实测。

---

## 6. 结果文件说明

| 文件路径 | 说明 |
|---------|------|
| `/tmp/camera_benchmark.csv` | Python 基准节点输出：FPS/延迟/丢帧率 |
| `/tmp/gpu_upload_bench.csv` | C++ GPU upload 节点输出：上传耗时/带宽 |
| `/tmp/camera_bench_YYYYMMDD_HHMMSS/` | 压测脚本报告目录 |
| `  system_metrics.csv` | 系统负载/内存/温度（1秒采样） |
| `  camera_benchmark.csv` | 测试期间的相机基准数据副本 |
| `  summary.json` | 各流 FPS/延迟/丢帧率汇总 |
| `  usb_topology.txt` | 测试前 USB 拓扑快照 |
| `/tmp/resource_monitor.csv` | GPU 持续监控脚本输出 |

### 6.1 `camera_benchmark.csv` 字段

| 字段 | 说明 |
|------|------|
| `timestamp` | 报告时间（ISO 8601） |
| `stream` | 流名称（wrist_color / wrist_depth / head_color / head_depth） |
| `fps` | 当前滑动窗口 FPS |
| `latency_ms` | 平均端到端延迟（ms） |
| `p95_latency_ms` | P95 延迟（ms） |
| `p99_latency_ms` | P99 延迟（ms） |
| `drop_rate` | 丢帧率（0~1） |
| `cpu_pct` | 进程 CPU 占用（%） |
| `mem_rss_mb` | 进程 RSS 内存（MB） |

### 6.2 `gpu_upload_bench.csv` 字段

| 字段 | 说明 |
|------|------|
| `timestamp` | 报告时间 |
| `stream` | 流名称 |
| `fps` | 当前滑动窗口 FPS |
| `avg_upload_us` | 平均 GPU 上传耗时（微秒） |
| `avg_latency_ms` | 平均端到端延迟（ms） |
| `gpu_mem_used_mb` | GPU 已用显存（MB） |
| `gpu_util_pct` | GPU 利用率（%） |
| `cpu_pct` | 进程 CPU 占用（%） |
| `ram_used_mb` | 系统已用 RAM（MB） |

---

## 7. 常见问题

**Q：相机无法打开，提示 "device not found"？**
A：检查 udev 规则和 USB 连接。运行 `lsusb | grep 2bc5` 确认设备可见。

**Q：FPS 远低于设定值（如 30fps 只有 10fps）？**
A：检查 usbfs_memory_mb 设置，以及是否有 USB 带宽冲突。

**Q：`colcon build` 提示找不到 CUDA？**
A：确保 CUDA >= 12.0 已安装，并设置 `CUDA_HOME` 环境变量：
```bash
export CUDA_HOME=/usr/local/cuda
export PATH=$CUDA_HOME/bin:$PATH
```

**Q：GPU upload 节点报 CUDA 错误？**
A：检查 GPU 驱动版本（`nvidia-smi`），确保驱动支持 CUDA 12.0+。
