# Orbbec 双相机四流 FPS 不足诊断手册

> **适用场景**：`usbfs_memory_mb` 已设为 128，FPS 依然仅 12~17，  
> 全流合计带宽约 42 MB/s，远低于期望 30 fps / ≈80 MB/s。

---

## 一、现象确认与数据记录

运行监控脚本，记录基线数据：

```bash
bash scripts/run_fps_latency_monitor.sh 60
```

典型"FPS 不足"基线特征：

| 指标 | 正常值 | 问题值（实测） |
|------|-------|--------------|
| 每流 FPS | ≈ 30 | 12–17 |
| 全流带宽 | ≈ 80 MB/s | 40–45 MB/s |
| 丢帧率 | 0% | 0%（帧未被丢弃，而是根本没产出） |
| 端到端延迟 | ≤ 50 ms | 25–40 ms（正常） |

> **关键结论**：丢帧率为 0% 但 FPS 远低于 30，说明帧未在传输层丢失，  
> 而是**相机侧或驱动侧根本没有以 30 fps 产出帧**。瓶颈在传输链上游。

---

## 二、非 usbfs_memory 原因全景图

```
物理层                 驱动层                  SDK/参数层               ROS2层
─────────────────     ──────────────────────  ─────────────────────   ──────────────────
USB2.0 接口         → uvcvideo quirks 限速  → LibUVC 后端多流锁    → color_fps 参数未生效
同一 USB 控制器     → xhci 带宽分配不足     → V4L2 Backend 未选   → QoS RELIABLE 积压
USB Hub 降速        → 内核版本 < 5.4 bug    → color_fps 参数未传  → enable_ir 意外开启
线缆/供电不稳       → UVC 中断单核瓶颈      → SDK Config 不存在  → 同 serial_number 抢占
两相机共根端口      → IRQ 亲和性未优化      → sync_mode 耦合色深 → topic bw 未实测
```

---

## 三、逐层诊断方法与操作命令

### L1 — USB 物理层

**最高优先级，解决后 FPS 可能直接翻倍。**

#### 1.1 确认每台相机接在 USB3.x 接口（5000 Mbps）

```bash
lsusb -t
```

期望输出中 Orbbec 设备（VID 2bc5）所在行的速度列为 **5000M**：

```
/:  Bus 02.Port 1: Dev 1, Class=root_hub, Driver=xhci_hcd/6p, 5000M
    |__ Port 3: Dev 4, If 0, Class=Video, Driver=uvcvideo, 5000M   ← ✅
```

若显示 **480M**（USB2.0）：

- 换插主板蓝色 USB3 接口
- 替换支持 USB3 的数据线（线缆向下兼容时会降速）
- 检查主板 USB 控制器是否启用 SS 模式（BIOS 设置）

#### 1.2 两台相机是否共享同一 USB 根控制器

```bash
# 查看两台相机分别在哪条总线
lsusb | grep "2bc5"
# 例：
#   Bus 002 Device 004: ID 2bc5:0673  ← head_camera
#   Bus 002 Device 005: ID 2bc5:0660  ← wrist_camera  ← 同一总线！
```

若两台均在 **Bus 002**：

```bash
# 查看 Bus 002 对应哪个 PCIe 控制器
cat /sys/bus/usb/devices/usb2/../../uevent | grep PCI_ID
# 或
lspci | grep -iE "USB|xhci"
```

**解决方案**：

| 方案 | 说明 |
|------|------|
| 分插到主板不同控制器接口 | 如 USB-A 和 USB-C 可能属于不同控制器 |
| 加装 PCIe USB3.x 扩展卡 | 完全独立带宽，推荐工业部署首选 |
| 使用外供电 USB3 Hub（单相机一个） | 每台相机独占 Hub 上行口，但共享根仍有限制 |

#### 1.3 USB 错误实时监控

```bash
# 持续监控 USB 相关内核日志
sudo dmesg -w | grep -iE "usb|uvc|xhci|ENOMEM|URB|reset|timeout"
```

| 关键词 | 含义 | 解决方向 |
|--------|------|---------|
| `ENOMEM` | usbfs 缓冲不足 | 确认 `usbfs_memory_mb ≥ 128` |
| `URB timeout` | ISO 传输超时，控制器过载 | 分离 USB 控制器 |
| `USB disconnect` | 供电不稳或线缆故障 | 更换线缆、有源 Hub |
| `speed mismatch` | 端口速度协商失败 | 换 USB3 接口或线缆 |

---

### L2 — 内核 / 驱动层

#### 2.1 内核版本

```bash
uname -r
```

- **≥ 5.15**：推荐，UVC 多流支持完善
- **5.4–5.14**：基本可用，偶有 UVC quirk 问题
- **< 5.4**：存在已知 UVC 多设备并发 bug，建议升级

#### 2.2 UVC 驱动参数

```bash
# 查看 uvcvideo 当前参数
cat /sys/module/uvcvideo/parameters/* 2>/dev/null || true
# 查看 quirks 是否影响帧率
modinfo uvcvideo | grep parm
```

如果相机有已知 quirk 问题，可通过加载时参数临时屏蔽：

```bash
# 临时重新加载（慎用，会断开所有 UVC 设备）
sudo modprobe -r uvcvideo && sudo modprobe uvcvideo quirks=0
```

#### 2.3 USB 中断亲和性优化

单核处理所有 USB 中断会成为瓶颈（4 路 30fps 的中断密度很高）：

```bash
# 查看 xhci 中断分布在哪些 CPU 核心
grep -i xhci /proc/interrupts

# 示例输出：
#  37:    50000    0    0    0   PCI-MSI  524288-edge  xhci_hcd  ← 只有 CPU0 在处理！

# 设置亲和性（让所有核心均分 USB 中断，假设 IRQ 号为 37，8 核机器设 0xff）
sudo sh -c 'echo ff > /proc/irq/37/smp_affinity'
```

---

### L3 — Orbbec SDK / 驱动参数层

#### 3.1 指定 V4L2 后端（最常见优化项）

**LibUVC 后端在多流/多设备时存在互斥锁导致帧率下降，V4L2 后端无此问题。**

```bash
# 创建/修改 OrbbecSDK 配置文件
mkdir -p ~/.ros
cat > ~/.ros/OrbbecSDKConfig.xml <<'EOF'
<?xml version="1.0" encoding="UTF-8"?>
<OrbbecSDKConfig>
    <!-- V4L2 后端：多流/多设备场景性能最优 -->
    <UvcBackend>V4L2</UvcBackend>
</OrbbecSDKConfig>
EOF
```

或在节点启动参数中显式指定：

```bash
ros2 run orbbec_camera orbbec_camera_node \
    --ros-args -p uvc_backend:=V4L2 ...
```

#### 3.2 确认节点 FPS 参数实际生效

```bash
# 节点启动后立即验证参数（用实际节点名替换）
ros2 param get /head_camera color_fps
ros2 param get /head_camera depth_fps
ros2 param get /wrist_camera color_fps
```

如果返回值 ≠ 30，重新启动时显式传参：

```bash
ros2 run orbbec_camera orbbec_camera_node \
    --ros-args \
    -p camera_name:=head_camera \
    -p serial_number:=<YOUR_SN_HERE> \
    -p color_fps:=30 \
    -p depth_fps:=30 \
    -p color_width:=640 \
    -p color_height:=480 \
    -p depth_width:=640 \
    -p depth_height:=480 \
    -p enable_color:=true \
    -p enable_depth:=true \
    -p enable_ir:=false \
    -p enable_point_cloud:=false \
    -p uvc_backend:=V4L2 \
    __ns:=/head_camera
```

#### 3.3 SerialNumber 冲突检查

两节点使用相同（或不指定）serial_number 时，SDK 会让两节点竞争同一物理设备。

```bash
# 查找每台相机的序列号
lsusb -v 2>/dev/null | grep -A5 "2bc5" | grep iSerial
# 或通过 OrbbecViewer 图形工具查看
```

**节点必须使用各自唯一的 serial_number 参数。**

---

### L4 — ROS2 层

#### 4.1 QoS 配置

监控节点应使用 `BEST_EFFORT + depth=1`，与相机驱动默认 QoS 匹配：

```python
from rclpy.qos import QoSProfile, QoSReliabilityPolicy, QoSHistoryPolicy
qos = QoSProfile(
    reliability=QoSReliabilityPolicy.BEST_EFFORT,
    history=QoSHistoryPolicy.KEEP_LAST,
    depth=1,
)
```

若订阅者使用 `RELIABLE`，相机驱动（`BEST_EFFORT`）与订阅者不兼容，  
导致数据完全不流通，`ros2 topic hz` 显示 0。

检查命令：

```bash
ros2 topic info --verbose /head_camera/color/image_raw
```

#### 4.2 关闭不需要的流

每增加一路流都消耗 USB 带宽。关闭不用的流：

```bash
-p enable_ir:=false
-p enable_point_cloud:=false
-p enable_accel:=false
-p enable_gyro:=false
```

#### 4.3 同步模式

硬件同步（`sync_mode:=software_triggering` 或 `hardware_triggering`）  
会让深度和色彩流相互等待，有效帧率降至约一半：

```bash
# 关闭同步，各流独立最大帧率
-p sync_mode:=close
```

---

### L5 — 进程竞争 / CPU 调度

#### 5.1 相机进程 CPU 占用

```bash
# 实时监控相机相关进程
watch -n1 'ps aux --sort=-%cpu | grep -iE "orbbec|component_container|ros2" | grep -v grep | head -10'
```

若单进程 CPU 超过 80%，考虑：

- 关闭点云计算（`enable_point_cloud:=false`）
- 降低分辨率（640×480 替代 1280×720）
- 使用独立 launch 进程（两台相机各一个进程，防止同进程 GIL/线程竞争）

#### 5.2 CPU 频率调速

Linux 默认省电策略可能降低 CPU 频率，导致驱动线程处理速度不足：

```bash
# 查看当前策略
cat /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor | sort -u

# 设置为性能模式（生产环境推荐）
echo performance | sudo tee /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor
```

---

## 四、一键诊断脚本使用方法

```bash
# 基本运行（需提前启动相机节点以获取 L4 ROS2 层数据）
bash scripts/orbbec_fps_diagnose.sh

# 指定输出目录
bash scripts/orbbec_fps_diagnose.sh --output-dir /tmp/my_diag

# 指定非标准 ROS2 路径
bash scripts/orbbec_fps_diagnose.sh --ros2-source /opt/ros/iron/setup.bash
```

脚本输出文件说明：

| 文件 | 内容 |
|------|------|
| `diag_report.txt` | 完整结构化报告（含所有诊断结论） |
| `usb_topology.txt` | `lsusb -t` 完整 USB 拓扑树 |
| `dmesg_usb.txt` | USB/UVC/xhci 相关内核日志 |
| `irq_counts.txt` | USB 控制器中断计数（每核分布） |
| `orbbec_sdk_config.txt` | OrbbecSDKConfig.xml 内容快照 |
| `proc_stats.txt` | 相机进程 CPU/内存占用快照 |
| `ros2_params_*.txt` | 每个相机节点的完整参数列表 |

提交技术支持工单时打包所有文件：

```bash
tar czf orbbec_diag_$(date +%Y%m%d).tar.gz /tmp/orbbec_diag_*
```

---

## 五、关键参数与日志优先级速查

| 优先级 | 参数 / 日志 | 如何获取 | 意义 |
|--------|------------|---------|------|
| ⭐⭐⭐ | USB 总线速度 | `lsusb -t` Speed 列 | 是否 USB3（5000M） |
| ⭐⭐⭐ | 两台相机是否同 Bus | `lsusb` Bus 列 | 共享带宽瓶颈 |
| ⭐⭐⭐ | UvcBackend | `~/.ros/OrbbecSDKConfig.xml` | V4L2 vs LibUVC |
| ⭐⭐⭐ | `color_fps` / `depth_fps` 实际值 | `ros2 param get` | 参数是否生效 |
| ⭐⭐ | USB dmesg 错误 | `dmesg | grep usb` | 枚举/传输层错误 |
| ⭐⭐ | xhci IRQ 分布 | `/proc/interrupts` | 中断单核瓶颈 |
| ⭐⭐ | `serial_number` 参数 | `ros2 param get` | 设备抢占风险 |
| ⭐ | 内核版本 | `uname -r` | UVC 驱动兼容性 |
| ⭐ | CPU governor | `/sys/cpufreq` | 频率节流 |
| ⭐ | `sync_mode` 参数 | `ros2 param get` | 色深耦合降帧 |

---

## 六、配置文件模板

### 双相机 YAML 参数模板

```yaml
# config/dual_gemini_v4l2.yaml
# 使用方法：ros2 run orbbec_camera orbbec_camera_node --ros-args --params-file dual_gemini_v4l2.yaml __ns:=/head_camera

head_camera:
  ros__parameters:
    camera_name: head_camera
    serial_number: "305XXXXX"          # 替换为实际序列号
    color_width: 640
    color_height: 480
    color_fps: 30
    depth_width: 640
    depth_height: 480
    depth_fps: 30
    enable_color: true
    enable_depth: true
    enable_ir: false
    enable_point_cloud: false
    uvc_backend: V4L2
    sync_mode: close

wrist_camera:
  ros__parameters:
    camera_name: wrist_camera
    serial_number: "330YYYYY"          # 替换为实际序列号
    color_width: 640
    color_height: 480
    color_fps: 30
    depth_width: 640
    depth_height: 480
    depth_fps: 30
    enable_color: true
    enable_depth: true
    enable_ir: false
    enable_point_cloud: false
    uvc_backend: V4L2
    sync_mode: close
```

### 一键启动双相机命令

```bash
# Terminal 1：head_camera
ros2 run orbbec_camera orbbec_camera_node \
    --ros-args --params-file config/dual_gemini_v4l2.yaml \
    __ns:=/head_camera

# Terminal 2：wrist_camera
ros2 run orbbec_camera orbbec_camera_node \
    --ros-args --params-file config/dual_gemini_v4l2.yaml \
    __ns:=/wrist_camera

# Terminal 3：监控帧率
bash scripts/run_fps_latency_monitor.sh 60
```

---

## 七、预期改善效果

| 优化项 | 预期 FPS 改善 |
|--------|-------------|
| USB2 → USB3 接口 | +100%（从 ~15 → ~30） |
| 双相机分离到独立 USB 控制器 | +30~50%（视带宽饱和程度） |
| LibUVC → V4L2 后端 | +20~50%（多流场景） |
| `color_fps:=30` 显式设参 | +0~100%（参数未生效时可达满额） |
| 关闭 IR/点云/IMU 流 | +10~20%（释放带宽余量） |
| IRQ 亲和性优化 | +5~15%（高负载场景） |
| CPU governor=performance | +5~10%（低频机器） |
