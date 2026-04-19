#!/usr/bin/env bash
# =============================================================================
# orbbec_fps_diagnose.sh
#
# Orbbec 多相机 FPS 不足深度诊断脚本
# 适用场景：usbfs_memory_mb 已设为 128，FPS 依然远低于 30 fps
#
# 诊断层级（按优先级排列）：
#   L1  USB 物理层     — 总线速度、拓扑、同控制器冲突
#   L2  内核/驱动层   — xhci/UVC 驱动参数、中断计数、dmesg 错误
#   L3  SDK/参数层    — OrbbecSDK backend、节点 FPS 参数是否生效
#   L4  ROS2 层       — QoS、话题带宽、参数实际值
#   L5  进程竞争层    — CPU 占用、IRQ 亲和性、调度
#   L6  进阶隔离测试  — 单流 / 单相机 / 双相机逐步测试指引
#
# 用法：
#   bash orbbec_fps_diagnose.sh [--output-dir DIR] [--ros2-source PATH]
#
# 输出目录（默认）：/tmp/orbbec_diag_YYYYMMDD_HHMMSS/
#   diag_report.txt       — 结构化文本报告（可直接发给支持工程师）
#   usb_topology.txt      — lsusb -t 完整输出
#   dmesg_usb.txt         — 最近 USB/UVC 相关 dmesg
#   ros2_params_*.txt     — 相机节点参数快照（如 ROS2 已运行）
#   proc_stats.txt        — 进程 CPU/内存快照
#   irq_counts.txt        — USB 中断计数
#   orbbec_sdk_config.txt — OrbbecSDKConfig.xml 内容快照
#
# 依赖（均为常见系统包，缺少时脚本会跳过对应项而非中止）：
#   usbutils（lsusb）、util-linux（lscpu）、procps（ps/top）
#   可选：ros2（如需节点参数抓取）、v4l-utils（v4l2-ctl）
# =============================================================================

set -uo pipefail

# ─── 颜色输出 ─────────────────────────────────────────────────────────────────
# 写入终端（带颜色）并追加到报告文件（无颜色转义序列）
_info()  { local msg="[INFO]  $*"; echo -e "\033[32m${msg}\033[0m"; echo "${msg}" >> "${REPORT}"; }
_warn()  { local msg="[WARN]  $*"; echo -e "\033[33m${msg}\033[0m"; echo "${msg}" >> "${REPORT}"; }
_error() { local msg="[ERRO]  $*"; echo -e "\033[31m${msg}\033[0m"; echo "${msg}" >> "${REPORT}"; }
_head()  { local msg="══ $* ══"; echo -e "\033[36m\n${msg}\033[0m"; printf '\n%s\n' "${msg}" >> "${REPORT}"; }
_sep()   { local sep="────────────────────────────────────────────────────────────"; echo "${sep}"; echo "${sep}" >> "${REPORT}"; }
# _tee: 将命令输出写到终端 AND 报告文件（替代管道 tee -a "${REPORT}"）
_tee()   { tee -a "${REPORT}"; }

# ─── 参数解析 ─────────────────────────────────────────────────────────────────
OUTPUT_DIR=""
ROS2_SOURCE="/opt/ros/humble/setup.bash"

while [[ $# -gt 0 ]]; do
    case "$1" in
        --output-dir)  OUTPUT_DIR="$2"; shift 2 ;;
        --ros2-source) ROS2_SOURCE="$2"; shift 2 ;;
        -h|--help)
            grep '^#' "$0" | head -40 | sed 's/^# \{0,1\}//'
            exit 0
            ;;
        *) echo "未知参数: $1" >&2; exit 1 ;;
    esac
done

if [[ -z "${OUTPUT_DIR}" ]]; then
    OUTPUT_DIR="/tmp/orbbec_diag_$(date +%Y%m%d_%H%M%S)"
fi
mkdir -p "${OUTPUT_DIR}"
REPORT="${OUTPUT_DIR}/diag_report.txt"

# 文件头
{
echo "========================================================================"
echo " Orbbec 多相机 FPS 诊断报告"
echo " 生成时间  : $(date '+%Y-%m-%d %H:%M:%S')"
echo " 主机名    : $(hostname)"
echo " 内核版本  : $(uname -r)"
echo " 输出目录  : ${OUTPUT_DIR}"
echo "========================================================================"
} | tee "${REPORT}"

# ─────────────────────────────────────────────────────────────────────────────
# L1：USB 物理层诊断
# ─────────────────────────────────────────────────────────────────────────────
_head "L1  USB 物理层诊断"

# 1a. 全局拓扑
_info "[L1-1] USB 完整拓扑 (lsusb -t)"
if command -v lsusb &>/dev/null; then
    lsusb -t 2>/dev/null | tee "${OUTPUT_DIR}/usb_topology.txt" | tee -a "${REPORT}"
    echo "" >> "${REPORT}"

    # 1b. Orbbec 设备检测
    _info "[L1-2] Orbbec 设备列表 (VID 2bc5)"
    ORBBEC_LINES=$(lsusb 2>/dev/null | grep -i "2bc5" || true)
    if [[ -z "${ORBBEC_LINES}" ]]; then
        _warn "  ❌ 未检测到任何 Orbbec 设备（VID 2bc5）！"
        _warn "     请确认 USB 连接、udev 规则、供电是否正常。"
    else
        echo "${ORBBEC_LINES}" | tee -a "${REPORT}"
        ORBBEC_COUNT=$(echo "${ORBBEC_LINES}" | wc -l)
        _info "  检测到 ${ORBBEC_COUNT} 台 Orbbec 设备"
    fi

    # 1c. 检查每台 Orbbec 设备的 USB 速度
    _info "[L1-3] 逐台 Orbbec 设备速度检查"
    while IFS= read -r line; do
        [[ -z "${line}" ]] && continue
        BUS=$(echo "${line}" | awk '{print $2}')
        DEV=$(echo "${line}" | awk '{print $4}' | tr -d ':')
        SPEED_FILE="/sys/bus/usb/devices/${BUS}-${DEV}/speed"
        # 尝试从 sysfs 查找该设备的速度
        # lsusb 格式：Bus NNN Device DDD: ID VVVV:PPPP  Description
        BUS_NUM=$(echo "${line}" | grep -oP 'Bus \K[0-9]+')
        DEV_NUM=$(echo "${line}" | grep -oP 'Device \K[0-9]+')
        # 在 sysfs 中查找匹配的 devnum
        SPEED="未知"
        DEVPATH=""
        while IFS= read -r sysdev; do
            DEVNUM=$(cat "${sysdev}/devnum" 2>/dev/null || echo "0")
            BUSNUM=$(cat "${sysdev}/busnum" 2>/dev/null || echo "0")
            if [[ "${DEVNUM}" == "${DEV_NUM}" && "${BUSNUM}" == "${BUS_NUM}" ]]; then
                SPEED=$(cat "${sysdev}/speed" 2>/dev/null || echo "未知")
                DEVPATH="${sysdev}"
                break
            fi
        done < <(find /sys/bus/usb/devices -maxdepth 1 -name '[0-9]*' 2>/dev/null)

        # 速度判断
        case "${SPEED}" in
            5000)  SPEED_LABEL="✅ SuperSpeed USB3.2 Gen 1 (5 Gbps)" ;;
            10000) SPEED_LABEL="✅ SuperSpeed+ USB3.2 Gen 2 (10 Gbps)" ;;
            20000) SPEED_LABEL="✅ SuperSpeed+ USB3.2 Gen 2×2 (20 Gbps)" ;;
            480)   SPEED_LABEL="⚠️  HighSpeed USB2.0 (480 Mbps) — 带宽严重不足！" ;;
            12)    SPEED_LABEL="❌ FullSpeed USB1.1 (12 Mbps) — 无法使用！" ;;
            *)     SPEED_LABEL="？未知速度 ${SPEED}" ;;
        esac

        _info "  Bus${BUS_NUM} Dev${DEV_NUM}: ${SPEED} Mbps  →  ${SPEED_LABEL}"
        if [[ "${SPEED}" == "480" ]]; then
            _warn "  ⚠️  相机连接到 USB2.0 端口！"
            _warn "     单台相机 4 流同时传输需要约 70~80 MB/s，USB2.0 上限 ~60 MB/s。"
            _warn "     解决：换插 USB3.x 蓝色接口，或检查线缆/接口是否兼容 USB3。"
        fi

        # 获取该设备在哪个 xhci 控制器下
        if [[ -n "${DEVPATH}" ]]; then
            CTRLR=$(readlink -f "${DEVPATH}" 2>/dev/null | grep -oP 'pci[^/]+' | head -1 || echo "未知")
            _info "  └─ USB 控制器路径片段: ${CTRLR}"
        fi
    done <<< "${ORBBEC_LINES}"

    # 1d. 两台相机是否在同一 USB Root Hub 上（关键冲突点）
    _info "[L1-4] 检查两台 Orbbec 相机是否在同一 USB 根端口"
    ORBBEC_BUSES=$(lsusb 2>/dev/null | grep -i "2bc5" | awk '{print $2}' | sort -u)
    ORBBEC_BUS_COUNT=$(echo "${ORBBEC_BUSES}" | grep -c '[0-9]' 2>/dev/null || true)
    ORBBEC_DEV_COUNT=$(lsusb 2>/dev/null | grep -ic "2bc5" 2>/dev/null || true)
    if [[ "${ORBBEC_DEV_COUNT}" -lt 2 ]]; then
        _info "  仅检测到 ${ORBBEC_DEV_COUNT} 台 Orbbec 设备，跳过总线冲突检查"
    elif [[ "${ORBBEC_BUS_COUNT}" -le 1 ]]; then
        _warn "  ⚠️  两台 Orbbec 相机在同一个 USB 总线 (Bus ${ORBBEC_BUSES})！"
        _warn "     物理带宽共享：单 USB3.0 5Gbps 被两台相机瓜分，约 2.5Gbps/台。"
        _warn "     两台相机 4 流合计需要约 640 Mbps，理论上同一总线勉强够，"
        _warn "     但 xHCI 调度开销和 ISO 带宽预留会进一步压缩实际可用带宽。"
        _warn "     建议：将两台相机分插到主板上两个独立 USB 控制器的接口。"
    else
        _info "  ✅ 两台 Orbbec 相机分布在不同 USB 总线（总线号：${ORBBEC_BUSES//$'\n'/, }）"
    fi
else
    _warn "  lsusb 未安装，跳过 USB 拓扑检查（sudo apt install usbutils）"
fi

# ─────────────────────────────────────────────────────────────────────────────
# L2：内核 / 驱动层诊断
# ─────────────────────────────────────────────────────────────────────────────
_head "L2  内核 / 驱动层诊断"

# 2a. usbfs_memory_mb
_info "[L2-1] usbfs_memory_mb 当前值"
USBFS="/sys/module/usbcore/parameters/usbfs_memory_mb"
if [[ -f "${USBFS}" ]]; then
    USBFS_VAL=$(cat "${USBFS}")
    if [[ "${USBFS_VAL}" -lt 128 ]]; then
        _warn "  ⚠️  usbfs_memory_mb = ${USBFS_VAL}（建议 ≥ 128）"
        _warn "     执行：echo 128 | sudo tee ${USBFS}"
    else
        _info "  ✅ usbfs_memory_mb = ${USBFS_VAL}（已满足）"
    fi
else
    _warn "  ${USBFS} 不存在，跳过"
fi

# 2b. 内核版本
_info "[L2-2] 内核版本"
KVER=$(uname -r)
_info "  内核：${KVER}"
# 判断内核是否过老（5.4 以下 UVC 多流存在已知 bug）
KMAJ=$(echo "${KVER}" | cut -d. -f1)
KMIN=$(echo "${KVER}" | cut -d. -f2)
if [[ "${KMAJ}" -lt 5 ]] || [[ "${KMAJ}" -eq 5 && "${KMIN}" -lt 4 ]]; then
    _warn "  ⚠️  内核版本 < 5.4，UVC 驱动可能存在多流并发问题，建议升级。"
fi

# 2c. UVC 驱动参数
_info "[L2-3] UVC 驱动已加载模块及参数"
if lsmod 2>/dev/null | grep -q "uvcvideo"; then
    _info "  ✅ uvcvideo 模块已加载"
    modinfo uvcvideo 2>/dev/null | grep -E 'version|parm' | tee -a "${REPORT}" || true
    # 检查 uvcvideo quirks
    if [[ -d "/sys/module/uvcvideo/parameters" ]]; then
        ls /sys/module/uvcvideo/parameters/ 2>/dev/null | while read -r p; do
            VAL=$(cat "/sys/module/uvcvideo/parameters/${p}" 2>/dev/null || echo "?")
            _info "    uvcvideo.${p} = ${VAL}"
        done
    fi
else
    _warn "  uvcvideo 模块未加载（如使用 V4L2 后端则必须加载）"
fi

# 2d. xhci 驱动
_info "[L2-4] xhci-hcd 驱动信息"
if lsmod 2>/dev/null | grep -q "xhci_hcd"; then
    _info "  ✅ xhci_hcd 已加载"
    modinfo xhci_hcd 2>/dev/null | grep -E 'version|description' | tee -a "${REPORT}" || true
else
    _warn "  xhci_hcd 未加载（纯 USB3 需要此驱动）"
fi

# 2e. USB 相关 dmesg 错误
_info "[L2-5] 最近 USB/UVC dmesg 错误（最近 200 行，过滤关键词）"
DMESG_OUT="${OUTPUT_DIR}/dmesg_usb.txt"
dmesg 2>/dev/null | grep -iE "usb|uvc|xhci|ehci|orbbec|video" \
    | tail -200 | tee "${DMESG_OUT}" | tee -a "${REPORT}" || true

# 提取关键错误
ERRORS=$(grep -iE "error|fail|overflow|reset|disconnect|timeout|ENOMEM|URB" \
    "${DMESG_OUT}" 2>/dev/null || true)
if [[ -n "${ERRORS}" ]]; then
    _warn "  ⚠️  发现 USB/UVC 错误信息："
    echo "${ERRORS}" | tee -a "${REPORT}"
    _warn "  常见错误含义："
    _warn "    ENOMEM      → usbfs_memory_mb 不足（已设 128 则排除）"
    _warn "    URB timeout → USB 控制器超载或 ISO 带宽不足"
    _warn "    disconnect  → 供电不稳 / 线缆问题"
    _warn "    reset       → 设备枚举失败，检查 USB 版本"
else
    _info "  ✅ 未见 USB/UVC 错误信息"
fi

# 2f. USB 中断计数（判断 xhci 中断是否集中在单一 CPU 核心）
_info "[L2-6] USB 中断计数（/proc/interrupts）"
IRQ_OUT="${OUTPUT_DIR}/irq_counts.txt"
grep -iE "xhci|ehci|usb" /proc/interrupts 2>/dev/null | tee "${IRQ_OUT}" | tee -a "${REPORT}" || true
# 检查是否只有单核在处理 USB 中断
if [[ -s "${IRQ_OUT}" ]]; then
    CORE_COUNT=$(lscpu 2>/dev/null | grep "^CPU(s):" | awk '{print $2}' || echo "?")
    _info "  CPU 核心总数: ${CORE_COUNT}"
    _info "  如上表中 USB IRQ 数值仅集中在少数核心，可设置 IRQ 亲和性分散负载："
    _info "    # 查找 xhci IRQ 号：grep xhci /proc/interrupts | head -3"
    _info "    # 设置亲和性（将 IRQ_NUM 换成实际值）："
    _info "    # echo ff | sudo tee /proc/irq/IRQ_NUM/smp_affinity"
fi

# ─────────────────────────────────────────────────────────────────────────────
# L3：Orbbec SDK / 驱动参数层诊断
# ─────────────────────────────────────────────────────────────────────────────
_head "L3  Orbbec SDK / 驱动参数诊断"

# 3a. OrbbecSDKConfig.xml backend 设置
_info "[L3-1] OrbbecSDKConfig.xml 后端设置"
SDK_CONFIG_PATHS=(
    "${HOME}/.ros/OrbbecSDKConfig.xml"
    "/etc/orbbec/OrbbecSDKConfig.xml"
    "${HOME}/ros2_ws/install/orbbec_camera/share/orbbec_camera/config/OrbbecSDKConfig.xml"
)
FOUND_SDK_CFG=0
for cfg_path in "${SDK_CONFIG_PATHS[@]}"; do
    if [[ -f "${cfg_path}" ]]; then
        _info "  找到 SDK 配置：${cfg_path}"
        cat "${cfg_path}" | tee "${OUTPUT_DIR}/orbbec_sdk_config.txt" | tee -a "${REPORT}"
        FOUND_SDK_CFG=1
        # 检查 backend
        if grep -iq "LibUVC" "${cfg_path}"; then
            _warn "  ⚠️  后端设置为 LibUVC！"
            _warn "     LibUVC 在多流/多设备场景下存在带宽共享和帧率限制问题。"
            _warn "     建议修改为：<UvcBackend>V4L2</UvcBackend>"
        elif grep -iq "V4L2" "${cfg_path}"; then
            _info "  ✅ 后端已设置为 V4L2"
        else
            _warn "  ? 未找到 UvcBackend 配置项，将使用默认值（通常为 V4L2）"
        fi
        break
    fi
done
if [[ "${FOUND_SDK_CFG}" -eq 0 ]]; then
    _warn "  未找到 OrbbecSDKConfig.xml（已搜索路径：${SDK_CONFIG_PATHS[*]}）"
    _warn "  可能使用驱动内置默认后端，建议显式创建配置文件并指定 V4L2："
    _warn "    mkdir -p ~/.ros"
    _warn "    echo '<OrbbecSDKConfig><UvcBackend>V4L2</UvcBackend></OrbbecSDKConfig>' > ~/.ros/OrbbecSDKConfig.xml"
fi

# 3b. /dev/video* 节点与权限
_info "[L3-2] /dev/video* 节点列表及权限"
ls -la /dev/video* 2>/dev/null | tee -a "${REPORT}" || _warn "  未找到 /dev/video* 节点"
GROUPS_OUT=$(groups 2>/dev/null || echo "")
if echo "${GROUPS_OUT}" | grep -q "video"; then
    _info "  ✅ 当前用户已在 video 组"
else
    _warn "  ⚠️  当前用户不在 video 组！可能导致 uvc_open -6 (EACCES) 错误。"
    _warn "     执行：sudo usermod -aG video \$USER && newgrp video"
fi

# 3c. v4l2-ctl 设备能力（如已安装）
_info "[L3-3] V4L2 设备能力（v4l2-ctl，如已安装）"
if command -v v4l2-ctl &>/dev/null; then
    for dev in /dev/video*; do
        # 只检查支持 Video Capture 的设备
        CAPS=$(v4l2-ctl -d "${dev}" --info 2>/dev/null | grep -i "Video Capture" || true)
        if [[ -n "${CAPS}" ]]; then
            _info "  ${dev}:"
            v4l2-ctl -d "${dev}" --list-formats-ext 2>/dev/null \
                | grep -E "Pixel|Size|Interval" | head -20 | tee -a "${REPORT}" || true
        fi
    done
else
    _warn "  v4l2-ctl 未安装（sudo apt install v4l-utils），跳过 V4L2 能力检查"
fi

# ─────────────────────────────────────────────────────────────────────────────
# L4：ROS2 层诊断
# ─────────────────────────────────────────────────────────────────────────────
_head "L4  ROS2 层诊断"

# 尝试加载 ROS2 环境
ROS2_AVAILABLE=0
if [[ -f "${ROS2_SOURCE}" ]]; then
    # shellcheck source=/dev/null
    source "${ROS2_SOURCE}" 2>/dev/null && ROS2_AVAILABLE=1
fi
WS_SETUP="${HOME}/ros2_ws/install/setup.bash"
if [[ "${ROS2_AVAILABLE}" -eq 1 && -f "${WS_SETUP}" ]]; then
    # shellcheck source=/dev/null
    source "${WS_SETUP}" 2>/dev/null || true
fi

if [[ "${ROS2_AVAILABLE}" -eq 0 ]]; then
    _warn "  ROS2 环境不可用（${ROS2_SOURCE} 不存在），跳过 L4 层检查"
else
    # 确认 ROS2 daemon 是否在运行（避免挂起等待）
    ROS2_DAEMON_RUNNING=0
    if timeout 1 ros2 daemon status 2>/dev/null | grep -q "running"; then
        ROS2_DAEMON_RUNNING=1
    fi

    if [[ "${ROS2_DAEMON_RUNNING}" -eq 0 ]]; then
        _warn "  ROS2 daemon 未运行（ros2 daemon status 无响应）"
        _warn "  如需检查节点参数，请先启动相机节点，再重新运行此脚本。"
    else
        # 4a. 活跃相机节点
        _info "[L4-1] 当前活跃 ROS2 相机节点"
        NODES=$(timeout 5 ros2 node list 2>/dev/null | grep -iE "camera|orbbec" || true)
        if [[ -z "${NODES}" ]]; then
            _warn "  ⚠️  未发现活跃的相机节点（请先启动相机节点再运行诊断，或忽略此项）"
        else
            echo "${NODES}" | tee -a "${REPORT}"

            # 4b. 逐节点抓取关键参数
            _info "[L4-2] 相机节点关键参数（color_fps / depth_fps / serial_number / uvc_backend）"
            while IFS= read -r node; do
                [[ -z "${node}" ]] && continue
                _info "  节点: ${node}"
                PARAMS_FILE="${OUTPUT_DIR}/ros2_params_$(echo "${node}" | tr '/' '_').txt"
                timeout 5 ros2 param list "${node}" 2>/dev/null > "${PARAMS_FILE}" || true
                for key in color_fps depth_fps ir_fps color_width color_height \
                           depth_width depth_height serial_number uvc_backend \
                           enable_color enable_depth enable_ir sync_mode; do
                    VAL=$(timeout 3 ros2 param get "${node}" "${key}" 2>/dev/null || echo "（未设置）")
                    _info "    ${key}: ${VAL}"
                    # 关键参数警告
                    case "${key}" in
                        color_fps|depth_fps)
                            NUM=$(echo "${VAL}" | grep -oP '[0-9]+' | head -1 || true)
                            if [[ -n "${NUM}" && "${NUM}" -lt 30 ]]; then
                                _warn "    ⚠️  ${key} = ${NUM}（< 30）！驱动可能按此低帧率工作。"
                            fi
                            ;;
                        uvc_backend)
                            if echo "${VAL}" | grep -iq "LibUVC"; then
                                _warn "    ⚠️  uvc_backend = LibUVC，建议改为 V4L2。"
                            fi
                            ;;
                    esac
                done
            done <<< "${NODES}"
        fi

        # 4c. 话题 QoS 检查
        _info "[L4-3] 图像话题 QoS 信息"
        for topic in /head_camera/color/image_raw /head_camera/depth/image_raw \
                     /wrist_camera/color/image_raw /wrist_camera/depth/image_raw; do
            INFO=$(timeout 4 ros2 topic info --verbose "${topic}" 2>/dev/null | head -30 || true)
            if [[ -n "${INFO}" ]]; then
                _info "  ${topic}:"
                echo "${INFO}" | tee -a "${REPORT}"
            fi
        done

        # 4d. 话题实时帧率（快速采样，各路并行，最多等 4 秒）
        _info "[L4-4] 话题帧率快速采样（每路最多 4 秒，并行）"
        DIAG_TOPICS=(
            "/head_camera/color/image_raw"
            "/head_camera/depth/image_raw"
            "/wrist_camera/color/image_raw"
            "/wrist_camera/depth/image_raw"
        )
        declare -A HZ_PIDS
        declare -A HZ_TMPFILES
        for t in "${DIAG_TOPICS[@]}"; do
            TMPF=$(mktemp)
            HZ_TMPFILES[$t]="${TMPF}"
            # timeout 确保最多等 4 秒
            timeout 4 bash -c "source '${ROS2_SOURCE}' 2>/dev/null; \
                ros2 topic hz '${t}' --window 90 2>&1 | head -5" \
                > "${TMPF}" 2>&1 &
            HZ_PIDS[$t]=$!
        done
        # 等待所有并行采样结束（最长 timeout 时间）
        for t in "${DIAG_TOPICS[@]}"; do
            wait "${HZ_PIDS[$t]}" 2>/dev/null || true
            RATE=$(grep -oP 'average rate: \K[0-9.]+' "${HZ_TMPFILES[$t]}" || echo "N/A")
            _info "  $(printf '%-45s' "${t}")  平均帧率: ${RATE} Hz"
            rm -f "${HZ_TMPFILES[$t]}" 2>/dev/null || true
        done
    fi
fi

# ─────────────────────────────────────────────────────────────────────────────
# L5：进程竞争 / CPU 调度诊断
# ─────────────────────────────────────────────────────────────────────────────
_head "L5  进程竞争 / CPU 调度诊断"

# 5a. CPU 架构和核心数
_info "[L5-1] CPU 信息"
lscpu 2>/dev/null | grep -E "Architecture|CPU\(s\)|MHz|Model name|NUMA" \
    | tee -a "${REPORT}" || true

# 5b. 系统负载
_info "[L5-2] 当前系统负载"
uptime | tee -a "${REPORT}"

# 5c. 相机进程 CPU 占用
_info "[L5-3] 相机进程 CPU/内存占用（orbbec/camera/ros2）"
PROC_FILE="${OUTPUT_DIR}/proc_stats.txt"
ps aux --sort=-%cpu 2>/dev/null \
    | grep -iE "orbbec|camera|uvc|ros2|component_container" \
    | grep -v grep | tee "${PROC_FILE}" | tee -a "${REPORT}" || true

if [[ ! -s "${PROC_FILE}" ]]; then
    _warn "  未发现相机相关进程（如节点未运行则正常）"
fi

# 5d. 调度策略
_info "[L5-4] 关键进程调度策略（chrt）"
if command -v chrt &>/dev/null; then
    while IFS= read -r line; do
        PID=$(echo "${line}" | awk '{print $2}')
        SCHED=$(chrt -p "${PID}" 2>/dev/null | head -1 || echo "unknown")
        echo "  PID ${PID}: ${SCHED}" | tee -a "${REPORT}"
    done < "${PROC_FILE}"
else
    _warn "  chrt 不可用，跳过调度策略检查"
fi

# 5e. USB 控制器 PCI 信息（判断是否独立通道）
_info "[L5-5] USB 控制器 PCI 列表"
if command -v lspci &>/dev/null; then
    lspci 2>/dev/null | grep -iE "USB|xhci|ehci" | tee -a "${REPORT}" || true
    USB_CTRLR_COUNT=$(lspci 2>/dev/null | grep -icE "xhci|USB" || true)
    _info "  共检测到 ${USB_CTRLR_COUNT} 个 USB PCI 控制器条目"
    if [[ "${USB_CTRLR_COUNT}" -eq 0 ]]; then
        _info "  （虚拟机或无 PCI USB 控制器环境，跳过控制器独立性检查）"
    elif [[ "${USB_CTRLR_COUNT}" -eq 1 ]]; then
        _warn "  ⚠️  只检测到 1 个 USB 控制器！"
        _warn "     双相机必须共享同一控制器的带宽，强烈建议："
        _warn "     1. 使用主板独立的多个 USB3 根口（不同控制器）"
        _warn "     2. 或加装独立 PCIe USB3.x 扩展卡（独立带宽）"
    else
        _info "  ✅ 检测到 ${USB_CTRLR_COUNT} 个 USB 控制器，可将两台相机分配到不同控制器"
    fi
else
    _warn "  lspci 未安装，跳过 PCI 控制器检查（sudo apt install pciutils）"
fi

# ─────────────────────────────────────────────────────────────────────────────
# L6：进阶隔离测试指引
# ─────────────────────────────────────────────────────────────────────────────
_head "L6  进阶隔离测试操作指引"

tee -a "${REPORT}" <<'GUIDE'
以下是逐步隔离定位 FPS 瓶颈的推荐操作序列（每步对比前一步）：

────────────────────────────────────────────────────────────────────────────
步骤 1：单相机 · 单流（baseline）
  目的：确认单台相机单路能否达到 30 fps
  命令：
    ros2 run orbbec_camera orbbec_camera_node \
      --ros-args -p camera_name:=test \
                 -p serial_number:=<SN> \
                 -p color_fps:=30 \
                 -p depth_fps:=30 \
                 -p enable_color:=true \
                 -p enable_depth:=false  __ns:=/test
    # 另一个终端监测帧率：
    ros2 topic hz /test/color/image_raw --window 90

  判断：
    ✅ FPS ≈ 30       → 单相机本身正常，问题在多流/双相机
    ❌ FPS < 25       → 单相机即异常，排查 USB 速度、驱动参数、SDK 配置

────────────────────────────────────────────────────────────────────────────
步骤 2：单相机 · 双流（color + depth）
  目的：评估同一相机色彩 + 深度流并发是否存在内部竞争
  命令：
    ros2 run orbbec_camera orbbec_camera_node \
      --ros-args -p camera_name:=test \
                 -p serial_number:=<SN> \
                 -p color_fps:=30 \
                 -p depth_fps:=30 \
                 -p enable_color:=true \
                 -p enable_depth:=true  __ns:=/test
    ros2 topic hz /test/color/image_raw --window 90 &
    ros2 topic hz /test/depth/image_raw --window 90

  判断：
    ✅ 两路均 ≈ 30 fps → 单相机双流正常
    ❌ 均降至 15~17   → 相机固件帧率被节流（深度 + 色彩 MIPI 分时）
                       → 检查相机固件版本；或切换 sync_mode

────────────────────────────────────────────────────────────────────────────
步骤 3：双相机 · 各单流（分控制器）
  目的：排除 USB 总线竞争
  操作：
    - 将两台相机插到主板上不同 USB 控制器的接口（lspci 确认不同 BDF）
    - 各启动一个节点，各监测 color 帧率

  判断：
    ✅ 各 ≈ 30 fps    → USB 独立控制器时正常，共享控制器是瓶颈
    ❌ 依然 < 25      → 问题与 USB 带宽无关，聚焦 SDK/驱动参数

────────────────────────────────────────────────────────────────────────────
步骤 4：双相机 · 四流（最终目标）
  前提：步骤 1~3 均 ≥ 25 fps 才值得继续
  额外优化建议：
    a. OrbbecSDKConfig.xml 确保 <UvcBackend>V4L2</UvcBackend>
    b. 每个节点显式指定 serial_number（防止设备被另一节点抢占）
    c. QoS 使用 BEST_EFFORT + depth=1（降低队列积压）
    d. 关闭不需要的流（enable_ir:=false, enable_point_cloud:=false）
    e. 禁用硬件同步模式（sync_mode:=close）以获得最大各自帧率

────────────────────────────────────────────────────────────────────────────
关键诊断命令速查表（现场工程师）

  # USB 速度验证（每台相机必须显示 5000M）
  lsusb -t | grep -A2 "2bc5"

  # USB 错误（实时）
  dmesg -w | grep -iE "usb|uvc|xhci"

  # 帧率实时监测（4 路并行）
  for t in head_camera/color head_camera/depth wrist_camera/color wrist_camera/depth; do
    ros2 topic hz /${t}/image_raw --window 120 2>&1 | grep rate &
  done

  # 节点实际参数确认
  ros2 param get /head_camera color_fps
  ros2 param get /head_camera depth_fps
  ros2 param get /wrist_camera color_fps

  # USB 总线利用率（间接）
  cat /sys/kernel/debug/usb/devices 2>/dev/null | grep -E "^[TB]"

  # 相机进程实时 CPU
  watch -n1 'ps aux | grep -iE "orbbec|component_container" | grep -v grep'
GUIDE

# ─────────────────────────────────────────────────────────────────────────────
# 最终摘要
# ─────────────────────────────────────────────────────────────────────────────
_head "诊断完成 · 结果摘要"

{
echo ""
echo "诊断结果已保存到："
echo "  ${OUTPUT_DIR}/diag_report.txt        — 完整报告"
echo "  ${OUTPUT_DIR}/usb_topology.txt       — USB 拓扑"
echo "  ${OUTPUT_DIR}/dmesg_usb.txt          — USB/UVC 内核日志"
echo "  ${OUTPUT_DIR}/irq_counts.txt         — USB 中断计数"
echo "  ${OUTPUT_DIR}/orbbec_sdk_config.txt  — SDK 配置（如找到）"
echo "  ${OUTPUT_DIR}/proc_stats.txt         — 进程 CPU/内存"
echo ""
echo "提交工单时请将整个目录打包：tar czf orbbec_diag.tar.gz ${OUTPUT_DIR}"
} | tee -a "${REPORT}"

echo ""
echo -e "\033[32m[DONE]\033[0m  orbbec_fps_diagnose.sh 执行完成"
echo -e "       报告: \033[33m${REPORT}\033[0m"
