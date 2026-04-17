#!/usr/bin/env bash
# =============================================================================
# run_fps_latency_monitor.sh
#
# 一键脚本：并行监控双相机 4 路图像流的实际帧率 / 端到端延迟 / USB 带宽。
#
# 用法：
#   bash run_fps_latency_monitor.sh [采样时长_秒]
#   默认采样时长：60 秒
#
# 输出目录：/tmp/fps_latency_bw_diag_YYYYMMDD_HHMMSS/
#   per_interval_stats.csv  — 每 5 秒采样一行（全部流）
#   final_summary.csv       — 测量结束后的汇总表
#   final_summary.json      — JSON 格式汇总（便于程序解析）
#   usb_topology.txt        — lsusb -t 拓扑快照
#   system_metrics.csv      — 系统负载/内存每秒快照
#
# 依赖：
#   - Python3 + rclpy + sensor_msgs（随 ROS2 Humble 安装）
#   - lsusb（usb-utils，可选，用于拓扑检查）
#   - bc（可选，用于 CPU% 精确计算）
# =============================================================================

set -euo pipefail

# ─── 参数 ────────────────────────────────────────────────────────────────────
DURATION=${1:-60}
DIAG_DIR="/tmp/fps_latency_bw_diag_$(date +%Y%m%d_%H%M%S)"

# ─── 彩色输出 ─────────────────────────────────────────────────────────────────
_info()  { echo -e "\033[32m[INFO]\033[0m  $*"; }
_warn()  { echo -e "\033[33m[WARN]\033[0m  $*"; }
_error() { echo -e "\033[31m[ERROR]\033[0m $*" >&2; }
_sep()   { echo -e "\033[36m$(printf '─%.0s' {1..70})\033[0m"; }

# ─── 创建输出目录 ─────────────────────────────────────────────────────────────
mkdir -p "${DIAG_DIR}"
_sep
_info "输出目录 : ${DIAG_DIR}"
_info "采样时长 : ${DURATION} 秒"
_sep

# ─── Step 1：USB 拓扑检查 ─────────────────────────────────────────────────────
_info "[ 1/5 ] USB 拓扑检查"
if command -v lsusb &>/dev/null; then
    lsusb -t 2>/dev/null | tee "${DIAG_DIR}/usb_topology.txt" || true
    _info "Orbbec 设备（VID 2bc5）："
    ORBBEC_LIST=$(lsusb 2>/dev/null | grep -i "2bc5" || true)
    if [ -n "${ORBBEC_LIST}" ]; then
        echo "${ORBBEC_LIST}"
    else
        _warn "  未检测到 Orbbec 设备 — 请确认 USB 连接或 udev 权限"
    fi
else
    _warn "  lsusb 未安装（sudo apt install usbutils），跳过拓扑检查"
fi

# ─── Step 2：设置 usbfs 缓冲（双相机同时传输需要更大缓冲）───────────────────
_info "[ 2/5 ] 设置 usbfs_memory_mb"
USBFS="/sys/module/usbcore/parameters/usbfs_memory_mb"
if [ -f "${USBFS}" ]; then
    CURRENT_VAL=$(cat "${USBFS}" 2>/dev/null || echo "?")
    if [ "${CURRENT_VAL}" -lt 128 ] 2>/dev/null; then
        if echo 128 | sudo tee "${USBFS}" > /dev/null 2>&1; then
            _info "  usbfs_memory_mb 已由 ${CURRENT_VAL} 调整为 128"
        else
            _warn "  无 sudo 权限，usbfs_memory_mb 当前值: ${CURRENT_VAL}"
            _warn "  如出现 ENOMEM 错误，请手动执行："
            _warn "    echo 128 | sudo tee ${USBFS}"
        fi
    else
        _info "  usbfs_memory_mb 当前值: ${CURRENT_VAL}（已满足要求）"
    fi
else
    _warn "  ${USBFS} 不存在，跳过设置"
fi

# ─── Step 3：后台系统监控（每秒采样：loadavg + 内存 + 温度）─────────────────
_info "[ 3/5 ] 启动后台系统监控"
SYS_CSV="${DIAG_DIR}/system_metrics.csv"
echo "timestamp,loadavg_1m,loadavg_5m,loadavg_15m,mem_used_mb,mem_total_mb,temp_c" \
    > "${SYS_CSV}"

(
    while true; do
        TS=$(date +%Y-%m-%dT%H:%M:%S)
        # 负载均值（从 /proc/loadavg 直接读取，无需外部命令）
        read -r LA1 LA5 LA15 _ < /proc/loadavg
        # 内存（KB → MB，从 /proc/meminfo 读取）
        MEM_TOT=$(awk '/^MemTotal:/    { printf "%.0f", $2/1024 }' /proc/meminfo)
        MEM_AVL=$(awk '/^MemAvailable:/{ printf "%.0f", $2/1024 }' /proc/meminfo)
        MEM_USED=$(( MEM_TOT - MEM_AVL ))
        # CPU 热区温度（取第一个 thermal_zone；不可读则填 N/A）
        TEMP="N/A"
        for zone_temp in /sys/class/thermal/thermal_zone*/temp; do
            if [ -r "${zone_temp}" ]; then
                RAW=$(cat "${zone_temp}" 2>/dev/null || echo "0")
                TEMP=$(awk "BEGIN{printf \"%.1f\", ${RAW}/1000}")
                break
            fi
        done
        echo "${TS},${LA1},${LA5},${LA15},${MEM_USED},${MEM_TOT},${TEMP}" \
            >> "${SYS_CSV}"
        sleep 1
    done
) &
SYS_MON_PID=$!
_info "  系统监控 PID: ${SYS_MON_PID}"

# ─── Step 4：source ROS2 环境 ─────────────────────────────────────────────────
_info "[ 4/5 ] 加载 ROS2 环境"
ROS2_SETUP="/opt/ros/humble/setup.bash"
if [ ! -f "${ROS2_SETUP}" ]; then
    _error "ROS2 Humble 未找到: ${ROS2_SETUP}"
    _error "请先安装 ROS2 Humble 或修改脚本中的 ROS2_SETUP 路径"
    kill "${SYS_MON_PID}" 2>/dev/null || true
    exit 1
fi
# shellcheck source=/dev/null
source "${ROS2_SETUP}"
_info "  ROS2 Humble 已加载"

# 加载用户工作空间（可选）
WS_SETUP="${HOME}/ros2_ws/install/setup.bash"
if [ -f "${WS_SETUP}" ]; then
    # shellcheck source=/dev/null
    source "${WS_SETUP}"
    _info "  工作空间 overlay 已加载: ${WS_SETUP}"
fi

# 定位 Python 脚本（同目录或 PATH 中查找）
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
MONITOR_PY="${SCRIPT_DIR}/fps_latency_bw_monitor.py"
if [ ! -f "${MONITOR_PY}" ]; then
    _error "fps_latency_bw_monitor.py 未找到: ${MONITOR_PY}"
    kill "${SYS_MON_PID}" 2>/dev/null || true
    exit 1
fi

# ─── Step 5：运行 Python 监控脚本 ────────────────────────────────────────────
_info "[ 5/5 ] 启动 FPS/延迟/带宽监控（${DURATION} 秒）"
_sep

python3 "${MONITOR_PY}" \
    --duration "${DURATION}" \
    --output-dir "${DIAG_DIR}" \
    2>&1 | tee "${DIAG_DIR}/monitor.log"

PYTHON_EXIT=${PIPESTATUS[0]}

# ─── 停止后台监控 ─────────────────────────────────────────────────────────────
kill "${SYS_MON_PID}" 2>/dev/null || true
wait "${SYS_MON_PID}" 2>/dev/null || true

_sep
if [ "${PYTHON_EXIT}" -eq 0 ]; then
    _info "测量完成 ✓"
else
    _warn "Python 监控脚本退出码: ${PYTHON_EXIT}"
fi

_info "结果目录: ${DIAG_DIR}/"
_info "  per_interval_stats.csv  — 每 5 秒采样数据"
_info "  final_summary.csv       — 汇总表（可用 Excel/Python 分析）"
_info "  final_summary.json      — JSON 格式汇总"
_info "  system_metrics.csv      — 系统资源快照"
_info "  monitor.log             — 完整控制台日志"
_sep
