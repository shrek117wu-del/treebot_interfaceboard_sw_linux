#!/usr/bin/env bash
# =============================================================================
# monitor_gpu_resources.sh
#
# 持续监控 GPU / CPU / 内存资源（每0.5秒采样），输出到 CSV。
#
# 用法：
#   ./monitor_gpu_resources.sh [output_csv]
#   默认输出：/tmp/resource_monitor.csv
#
# CSV 字段：
#   timestamp, cpu_pct_all, mem_used_mb, mem_total_mb,
#   gpu_util_pct, gpu_mem_used_mb, gpu_mem_total_mb,
#   gpu_power_w, gpu_temp_c, pcie_tx_mbps, pcie_rx_mbps
# =============================================================================

set -euo pipefail

OUTPUT_CSV="${1:-/tmp/resource_monitor.csv}"

# ── 彩色日志 ──────────────────────────────────────────────────────────────────
log_info() { echo -e "\033[32m[INFO]\033[0m  $*"; }
log_warn() { echo -e "\033[33m[WARN]\033[0m  $*"; }

log_info "GPU 资源监控已启动"
log_info "  输出文件：${OUTPUT_CSV}"
log_info "  采样间隔：0.5 秒"
log_info "  Ctrl+C 退出"

# ── 写入 CSV 表头 ─────────────────────────────────────────────────────────────
echo "timestamp,cpu_pct_all,mem_used_mb,mem_total_mb,\
gpu_util_pct,gpu_mem_used_mb,gpu_mem_total_mb,\
gpu_power_w,gpu_temp_c,pcie_tx_mbps,pcie_rx_mbps" \
  > "${OUTPUT_CSV}"

# ── 检查 nvidia-smi 是否可用 ─────────────────────────────────────────────────
if ! command -v nvidia-smi &>/dev/null; then
  log_warn "nvidia-smi 未找到，GPU 字段将填 0"
fi

# ── PCIe 吞吐量辅助函数（通过 nvidia-smi dmon）────────────────────────────────
# 使用全局变量保存上一次的 PCIe 累计值（字节），用于计算差分带宽
PREV_PCIE_TX=0
PREV_PCIE_RX=0
PREV_PCIE_TS=0

get_pcie_mbps() {
  # nvidia-smi dmon 单次采样（-s t = PCIe吞吐，-c 1）
  local tx_mbps=0 rx_mbps=0
  if command -v nvidia-smi &>/dev/null; then
    local line
    line=$(nvidia-smi dmon -s t -c 1 2>/dev/null | tail -1 || echo "")
    # dmon 输出列：# gpu  txpci  rxpci（MB/s）
    if [[ -n "${line}" && "${line}" != "#"* ]]; then
      # shellcheck disable=SC2086
      tx_mbps=$(echo $line | awk '{print $2}')
      rx_mbps=$(echo $line | awk '{print $3}')
    fi
  fi
  echo "${tx_mbps:-0} ${rx_mbps:-0}"
}

# ── 主采样循环 ────────────────────────────────────────────────────────────────
while true; do
  TS=$(date +%Y-%m-%dT%H:%M:%S.%3N)

  # ── CPU 利用率（所有核心平均）────────────────────────────────────────────
  # top -bn1 输出 "%Cpu(s):  X.X us, ..."，取 idle 字段反推
  CPU_IDLE=$(top -bn1 2>/dev/null \
    | grep -E "^(%Cpu|Cpu)" \
    | head -1 \
    | awk '{for(i=1;i<=NF;i++) if($i=="id,") {print $(i-1); exit}}' \
    || echo "0.0")
  # 防止空值
  CPU_IDLE=${CPU_IDLE:-0.0}
  CPU_PCT=$(echo "scale=1; 100.0 - ${CPU_IDLE}" | bc 2>/dev/null || echo "0.0")

  # ── 内存（free -m）───────────────────────────────────────────────────────
  MEM_LINE=$(free -m 2>/dev/null | grep "^Mem:" || echo "Mem: 0 0 0")
  MEM_TOTAL=$(echo "${MEM_LINE}" | awk '{print $2}')
  MEM_USED=$( echo "${MEM_LINE}" | awk '{print $3}')

  # ── GPU（nvidia-smi）──────────────────────────────────────────────────────
  GPU_UTIL=0; GPU_MEM_USED=0; GPU_MEM_TOTAL=0; GPU_POWER=0; GPU_TEMP=0

  if command -v nvidia-smi &>/dev/null; then
    GPU_LINE=$(nvidia-smi \
      --query-gpu=utilization.gpu,memory.used,memory.total,power.draw,temperature.gpu \
      --format=csv,noheader,nounits 2>/dev/null | head -1 || echo "0,0,0,0,0")
    IFS=',' read -r GPU_UTIL GPU_MEM_USED GPU_MEM_TOTAL GPU_POWER GPU_TEMP \
      <<< "${GPU_LINE}"
    # 去除可能的空格
    GPU_UTIL=$(echo    "${GPU_UTIL}"     | tr -d ' ')
    GPU_MEM_USED=$(echo "${GPU_MEM_USED}" | tr -d ' ')
    GPU_MEM_TOTAL=$(echo "${GPU_MEM_TOTAL}" | tr -d ' ')
    GPU_POWER=$(echo   "${GPU_POWER}"    | tr -d ' ')
    GPU_TEMP=$(echo    "${GPU_TEMP}"     | tr -d ' ')
  fi

  # ── PCIe 带宽（MB/s）─────────────────────────────────────────────────────
  read -r PCIE_TX PCIE_RX <<< "$(get_pcie_mbps)"

  # ── 写入 CSV ──────────────────────────────────────────────────────────────
  echo "${TS},${CPU_PCT},${MEM_USED},${MEM_TOTAL},\
${GPU_UTIL},${GPU_MEM_USED},${GPU_MEM_TOTAL},\
${GPU_POWER},${GPU_TEMP},${PCIE_TX},${PCIE_RX}" \
    >> "${OUTPUT_CSV}"

  sleep 0.5
done
