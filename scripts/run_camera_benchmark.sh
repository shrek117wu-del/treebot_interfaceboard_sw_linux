#!/usr/bin/env bash
# =============================================================================
# run_camera_benchmark.sh
#
# 完整压测脚本：启动双相机 ROS2 节点 + 基准测试节点，
# 同时采集系统资源指标，测试结束后生成 summary.json。
#
# 用法：
#   ./run_camera_benchmark.sh [duration_seconds]
#   默认测试时长：120 秒
#
# 输出目录格式：/tmp/camera_bench_YYYYMMDD_HHMMSS
# =============================================================================

set -euo pipefail

# ── 参数解析 ──────────────────────────────────────────────────────────────────
DURATION=${1:-120}
REPORT_DIR="/tmp/camera_bench_$(date +%Y%m%d_%H%M%S)"
SYSTEM_METRICS_CSV="${REPORT_DIR}/system_metrics.csv"
CAMERA_BENCH_CSV="/tmp/camera_benchmark.csv"
SUMMARY_JSON="${REPORT_DIR}/summary.json"

# ── 彩色日志函数 ──────────────────────────────────────────────────────────────
log_info()  { echo -e "\033[32m[INFO]\033[0m  $*"; }
log_warn()  { echo -e "\033[33m[WARN]\033[0m  $*"; }
log_error() { echo -e "\033[31m[ERROR]\033[0m $*" >&2; }

# ── 创建输出目录 ──────────────────────────────────────────────────────────────
mkdir -p "${REPORT_DIR}"
log_info "报告目录：${REPORT_DIR}"
log_info "测试时长：${DURATION} 秒"

# ── 1. 检查 USB 拓扑（过滤 Orbbec VID 2bc5）─────────────────────────────────
log_info "── USB 拓扑检查 ──────────────────────────────────────────────"
if command -v lsusb &>/dev/null; then
  lsusb -t 2>/dev/null | tee "${REPORT_DIR}/usb_topology.txt" || true
  log_info "Orbbec 设备（VID 2bc5）："
  lsusb 2>/dev/null | grep -i "2bc5" || log_warn "  未找到 Orbbec 设备，请检查 USB 连接"
else
  log_warn "lsusb 未安装，跳过 USB 拓扑检查"
fi

# ── 2. 设置 usbfs_memory_mb = 128（避免大数据流 ENOMEM）─────────────────────
log_info "── 设置 usbfs_memory_mb ───────────────────────────────────────"
USBFS_PATH="/sys/module/usbcore/parameters/usbfs_memory_mb"
if [ -w "${USBFS_PATH}" ]; then
  echo 128 | sudo tee "${USBFS_PATH}" > /dev/null
  log_info "usbfs_memory_mb 已设为 128"
else
  log_warn "无法写入 ${USBFS_PATH}，请手动执行："
  log_warn "  echo 128 | sudo tee ${USBFS_PATH}"
fi

# ── 3. 后台系统监控（每1秒采样）─────────────────────────────────────────────
log_info "── 启动系统监控（每1秒）──────────────────────────────────────"
echo "timestamp,loadavg_1m,loadavg_5m,loadavg_15m,mem_used_mb,mem_total_mb,temp_c" \
  > "${SYSTEM_METRICS_CSV}"

{
  while true; do
    TS=$(date +%Y-%m-%dT%H:%M:%S)
    # loadavg
    read -r LA1 LA5 LA15 _ < /proc/loadavg
    # 内存
    MEM_TOTAL=$(awk '/MemTotal/  {printf "%.0f", $2/1024}' /proc/meminfo)
    MEM_AVAIL=$(awk '/MemAvailable/ {printf "%.0f", $2/1024}' /proc/meminfo)
    MEM_USED=$(( MEM_TOTAL - MEM_AVAIL ))
    # CPU 温度（通用路径，若无则为 N/A）
    TEMP="N/A"
    for f in /sys/class/thermal/thermal_zone*/temp; do
      if [ -r "$f" ]; then
        RAW=$(cat "$f" 2>/dev/null || echo 0)
        TEMP=$(echo "scale=1; $RAW / 1000" | bc 2>/dev/null || echo "N/A")
        break
      fi
    done
    echo "${TS},${LA1},${LA5},${LA15},${MEM_USED},${MEM_TOTAL},${TEMP}" \
      >> "${SYSTEM_METRICS_CSV}"
    sleep 1
  done
} &
MONITOR_PID=$!
log_info "  系统监控 PID: ${MONITOR_PID}"

# ── 4. 后台尝试读取 USB debugfs 带宽日志 ─────────────────────────────────────
USB_BW_LOG="${REPORT_DIR}/usb_bandwidth.log"
{
  DEBUGFS_USB="/sys/kernel/debug/usb"
  if [ -d "${DEBUGFS_USB}" ]; then
    while true; do
      TS=$(date +%Y-%m-%dT%H:%M:%S)
      echo "=== ${TS} ===" >> "${USB_BW_LOG}"
      # 尝试读取 xhci 或 ehci 带宽文件
      find "${DEBUGFS_USB}" -name "bandwidth" 2>/dev/null \
        -exec cat {} \; >> "${USB_BW_LOG}" 2>/dev/null || true
      sleep 5
    done
  else
    echo "USB debugfs 不可用（需要 root 权限或 debugfs 挂载）" > "${USB_BW_LOG}"
  fi
} &
USB_MONITOR_PID=$!

# ── 5. 清空旧的 camera_benchmark.csv ─────────────────────────────────────────
rm -f "${CAMERA_BENCH_CSV}"

# ── 6. source ROS2 环境 ───────────────────────────────────────────────────────
ROS2_SETUP="/opt/ros/humble/setup.bash"
if [ -f "${ROS2_SETUP}" ]; then
  # shellcheck source=/dev/null
  source "${ROS2_SETUP}"
  log_info "ROS2 Humble 环境已加载"
else
  log_error "ROS2 环境文件未找到：${ROS2_SETUP}"
  exit 1
fi

# 尝试加载工作空间 overlay
WS_SETUP="${HOME}/ros2_ws/install/setup.bash"
if [ -f "${WS_SETUP}" ]; then
  # shellcheck source=/dev/null
  source "${WS_SETUP}"
  log_info "工作空间 overlay 已加载：${WS_SETUP}"
fi

# ── 7. 启动 dual_camera_bench.launch.py ──────────────────────────────────────
log_info "── 启动 dual_camera_bench.launch.py ──────────────────────────"
ros2 launch gemini330_gpu_bench dual_camera_bench.launch.py \
  > "${REPORT_DIR}/ros2_launch.log" 2>&1 &
LAUNCH_PID=$!
log_info "  ROS2 launch PID: ${LAUNCH_PID}"

# ── 8. 等待测试完成 ───────────────────────────────────────────────────────────
log_info "── 压测运行中，持续 ${DURATION} 秒 … ──────────────────────────"
sleep "${DURATION}"

# ── 9. 停止所有后台进程 ───────────────────────────────────────────────────────
log_info "── 停止所有进程 ──────────────────────────────────────────────"
kill "${LAUNCH_PID}"  2>/dev/null || true
kill "${MONITOR_PID}" 2>/dev/null || true
kill "${USB_MONITOR_PID}" 2>/dev/null || true
wait "${LAUNCH_PID}"  2>/dev/null || true
wait "${MONITOR_PID}" 2>/dev/null || true
log_info "  所有进程已终止"

# 复制 camera_benchmark.csv 到报告目录
if [ -f "${CAMERA_BENCH_CSV}" ]; then
  cp "${CAMERA_BENCH_CSV}" "${REPORT_DIR}/camera_benchmark.csv"
fi

# ── 10. 内嵌 Python：解析 CSV 生成 summary.json ──────────────────────────────
log_info "── 生成 summary.json ────────────────────────────────────────"

export REPORT_DIR CAMERA_BENCH_CSV SUMMARY_JSON
python3 - <<'PYEOF'
import csv, json, os, sys
from collections import defaultdict

csv_path     = os.path.join(os.environ.get("REPORT_DIR", "/tmp"), "camera_benchmark.csv")
summary_path = os.path.join(os.environ.get("REPORT_DIR", "/tmp"), "summary.json")

# 若文件不存在则早退
if not os.path.exists(csv_path):
    print(f"[Python] 找不到 CSV：{csv_path}", file=sys.stderr)
    sys.exit(0)

streams = defaultdict(lambda: {"fps_list": [], "latency_list": [], "drop_rate_list": []})

with open(csv_path) as f:
    reader = csv.DictReader(f)
    for row in reader:
        s = row["stream"]
        streams[s]["fps_list"].append(float(row["fps"]))
        streams[s]["latency_list"].append(float(row["latency_ms"]))
        streams[s]["drop_rate_list"].append(float(row["drop_rate"]))

summary = {}
for s, d in streams.items():
    fps_list = d["fps_list"]
    lat_list = d["latency_list"]
    dr_list  = d["drop_rate_list"]
    summary[s] = {
        "fps_avg":           round(sum(fps_list) / len(fps_list), 2),
        "fps_min":           round(min(fps_list), 2),
        "latency_avg_ms":    round(sum(lat_list) / len(lat_list), 3),
        "latency_max_ms":    round(max(lat_list), 3),
        "drop_rate_avg_pct": round(sum(dr_list) / len(dr_list) * 100.0, 4),
    }

with open(summary_path, "w") as f:
    json.dump(summary, f, indent=2, ensure_ascii=False)

print(f"[Python] summary.json → {summary_path}")
PYEOF

# ── 11. 打印 summary ──────────────────────────────────────────────────────────
if [ -f "${SUMMARY_JSON}" ]; then
  log_info "── Summary ───────────────────────────────────────────────────"
  cat "${SUMMARY_JSON}"
  echo
fi

log_info "── 压测完成 ──────────────────────────────────────────────────"
log_info "  报告目录：${REPORT_DIR}"
log_info "  系统监控：${SYSTEM_METRICS_CSV}"
log_info "  相机数据：${REPORT_DIR}/camera_benchmark.csv"
log_info "  汇总报告：${SUMMARY_JSON}"
