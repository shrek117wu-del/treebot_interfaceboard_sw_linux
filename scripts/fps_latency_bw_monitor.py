#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
fps_latency_bw_monitor.py  —  双相机四路图像流并行 FPS / 延迟 / USB 带宽诊断

功能：
  1. 并行订阅 4 路话题，统计实际帧率（FPS，300帧滑动窗口）
  2. 端到端延迟（系统 wall-clock 时间 vs 消息 header.stamp）：
       mean / p95 / max
  3. USB 带宽估算：按消息像素尺寸（step × height）累计每秒字节数
  4. 采样时长通过 --duration 指定（默认 60 秒）
  5. 结果落盘到
       /tmp/fps_latency_bw_diag_YYYYMMDD_HHMMSS/per_interval_stats.csv
       /tmp/fps_latency_bw_diag_YYYYMMDD_HHMMSS/final_summary.csv

依赖：
  Python3 stdlib  + rclpy + sensor_msgs（均随 ROS2 Humble 安装，无需 pip 安装额外包）

用法：
  python3 fps_latency_bw_monitor.py [--duration 60] [--output-dir /tmp/mydir]
  (或通过 run_fps_latency_monitor.sh 一键调用)
"""

import argparse
import csv
import json
import os
import signal
import sys
import time
import threading
from collections import deque
from typing import Dict, List, Tuple

import rclpy
from rclpy.node import Node
from rclpy.qos import QoSProfile, QoSReliabilityPolicy, QoSHistoryPolicy
from sensor_msgs.msg import Image

# ─── 监控话题配置 ─────────────────────────────────────────────────────────────
# 话题路径 → 流名称 的映射，可根据实际部署修改
TOPICS: List[Tuple[str, str]] = [
    ("/head_camera/color/image_raw",  "head_color"),
    ("/head_camera/depth/image_raw",  "head_depth"),
    ("/wrist_camera/color/image_raw", "wrist_color"),
    ("/wrist_camera/depth/image_raw", "wrist_depth"),
]

# ─── 统计窗口参数 ─────────────────────────────────────────────────────────────
WINDOW          = 300   # 滑动窗口帧数：FPS/延迟/带宽均在此窗口内计算
REPORT_INTERVAL = 5.0   # 控制台打印 + CSV 写入周期（秒）

# 端到端延迟超过此阈值的帧被计为严重延迟帧（"丢帧"）
DROP_FRAME_THRESHOLD_MS = 500.0


# ─────────────────────────────────────────────────────────────────────────────
# 工具函数（纯 stdlib，无第三方依赖）
# ─────────────────────────────────────────────────────────────────────────────

def _percentile(data: list, pct: float) -> float:
    """线性插值分位数，行为与 numpy.percentile(data, pct) 一致。

    算法：
        idx = pct/100 × (n-1)，取相邻两个整数索引做线性插值。

    参数:
        data: 数值列表（内部自动排序，原始列表不受影响）
        pct:  百分比，如 95.0 → p95

    返回:
        分位数值；data 为空时返回 0.0
    """
    if not data:
        return 0.0
    s = sorted(data)
    n = len(s)
    idx = (pct / 100.0) * (n - 1)
    lo  = int(idx)
    hi  = lo + 1
    if hi >= n:
        return float(s[-1])
    frac = idx - lo
    return float(s[lo] * (1.0 - frac) + s[hi] * frac)


def _mean(data: list) -> float:
    """安全均值；data 为空时返回 0.0。"""
    return sum(data) / len(data) if data else 0.0


def _read_mem_mb() -> Tuple[float, float]:
    """从 /proc/meminfo 读取内存，返回 (used_mb, total_mb)。

    MemTotal  − MemAvailable = 已用内存（与 free -m 中 "used" 列一致）。
    如果读取失败（非 Linux 环境），返回 (0.0, 0.0)。
    """
    total_kb = avail_kb = 0
    try:
        with open("/proc/meminfo") as f:
            for line in f:
                if line.startswith("MemTotal:"):
                    total_kb = int(line.split()[1])
                elif line.startswith("MemAvailable:"):
                    avail_kb = int(line.split()[1])
    except OSError:
        pass
    return (total_kb - avail_kb) / 1024.0, total_kb / 1024.0


# ─────────────────────────────────────────────────────────────────────────────
# 单流统计容器
# ─────────────────────────────────────────────────────────────────────────────

class StreamStats:
    """单路图像流的实时统计容器（线程安全：GIL 保护 deque 追加操作）。"""

    def __init__(self, name: str) -> None:
        self.name = name
        # ── 滑动窗口 ──────────────────────────────────────────────────────────
        # 接收时刻（wall-clock nanoseconds，由 time.time_ns() 获取）
        self._recv_ns:  deque = deque(maxlen=WINDOW)
        # 端到端延迟（毫秒）= 接收时刻 − 消息时间戳
        self._lat_ms:   deque = deque(maxlen=WINDOW)
        # 每帧原始图像字节数（step × height）
        self._bw_bytes: deque = deque(maxlen=WINDOW)
        # ── 累计计数器 ────────────────────────────────────────────────────────
        self.total_frames = 0        # 自启动以来的总帧数
        self.drop_frames  = 0        # 延迟超过阈值的帧数（视为严重滞后）

    # ─── 数据写入 ─────────────────────────────────────────────────────────────

    def record(self, recv_ns: int, stamp_ns: int, frame_bytes: int) -> None:
        """记录一帧到达事件。

        参数:
            recv_ns:     本机接收该消息的 wall-clock 时间（nanoseconds）
            stamp_ns:    消息 header.stamp 转换后的纳秒时间戳
            frame_bytes: 原始图像字节数（msg.step × msg.height）
        """
        self._recv_ns.append(recv_ns)
        lat_ms = (recv_ns - stamp_ns) / 1_000_000.0     # ns → ms
        self._lat_ms.append(lat_ms)
        self._bw_bytes.append(frame_bytes)
        self.total_frames += 1
        if lat_ms > DROP_FRAME_THRESHOLD_MS:             # 超过阈值视为严重延迟
            self.drop_frames += 1

    # ─── 计算属性（只读，每次调用均基于当前窗口内容）────────────────────────

    @property
    def fps(self) -> float:
        """滑动窗口内的当前帧率（frames/second）。"""
        if len(self._recv_ns) < 2:
            return 0.0
        span_s = (self._recv_ns[-1] - self._recv_ns[0]) / 1_000_000_000.0
        return (len(self._recv_ns) - 1) / span_s if span_s > 0.0 else 0.0

    @property
    def latency_mean_ms(self) -> float:
        return _mean(list(self._lat_ms))

    @property
    def latency_p95_ms(self) -> float:
        return _percentile(list(self._lat_ms), 95.0)

    @property
    def latency_max_ms(self) -> float:
        return max(self._lat_ms) if self._lat_ms else 0.0

    @property
    def bandwidth_mbps(self) -> float:
        """当前窗口内的平均 USB 数据带宽（MB/s）。

        计算方式：窗口内所有帧字节数之和 ÷ 窗口时间跨度。
        """
        if len(self._recv_ns) < 2 or not self._bw_bytes:
            return 0.0
        span_s = (self._recv_ns[-1] - self._recv_ns[0]) / 1_000_000_000.0
        return sum(self._bw_bytes) / span_s / (1024.0 * 1024.0) if span_s > 0.0 else 0.0

    @property
    def frame_size_kb(self) -> float:
        """单帧平均字节数（KB）。"""
        return _mean(list(self._bw_bytes)) / 1024.0 if self._bw_bytes else 0.0

    @property
    def drop_rate(self) -> float:
        """丢帧率 = drop_frames / total_frames。"""
        return self.drop_frames / self.total_frames if self.total_frames else 0.0


# ─────────────────────────────────────────────────────────────────────────────
# ROS2 监控节点
# ─────────────────────────────────────────────────────────────────────────────

class FpsLatencyBwNode(Node):
    """并行订阅双相机 4 路话题，定期打印诊断报告并写入 CSV。"""

    def __init__(self, output_dir: str) -> None:
        super().__init__("fps_latency_bw_monitor")
        self._output_dir = output_dir

        # QoS：BEST_EFFORT + depth=1，与相机驱动默认 QoS 保持一致
        # （RELIABLE QoS 会在消费者较慢时积压队列，导致延迟虚高）
        qos = QoSProfile(
            reliability=QoSReliabilityPolicy.BEST_EFFORT,
            history=QoSHistoryPolicy.KEEP_LAST,
            depth=1,
        )

        # 为每路话题创建独立的统计容器
        self._stats: Dict[str, StreamStats] = {
            name: StreamStats(name) for _, name in TOPICS
        }

        # 并行订阅 4 路图像话题
        # lambda 捕获 n=name 以避免 Python 闭包晚绑定问题
        self._subs = []
        for topic, name in TOPICS:
            sub = self.create_subscription(
                Image,
                topic,
                lambda msg, n=name: self._image_cb(msg, n),
                qos,
            )
            self._subs.append(sub)
            self.get_logger().info(f"  订阅: {topic}  →  {name}")

        # 初始化输出文件
        os.makedirs(output_dir, exist_ok=True)
        self._interval_csv_path = os.path.join(output_dir, "per_interval_stats.csv")
        self._csv_fields = [
            "timestamp", "stream",
            "fps",
            "latency_mean_ms", "latency_p95_ms", "latency_max_ms",
            "bandwidth_mbps", "frame_size_kb",
            "drop_rate",
        ]
        # 写 CSV 表头
        with open(self._interval_csv_path, "w", newline="") as fh:
            csv.DictWriter(fh, fieldnames=self._csv_fields).writeheader()

        # 定时报告（每 REPORT_INTERVAL 秒）
        self.create_timer(REPORT_INTERVAL, self._report_cb)
        self.get_logger().info(f"FpsLatencyBwNode 已启动  →  {output_dir}")

    # ─── 图像回调 ─────────────────────────────────────────────────────────────

    def _image_cb(self, msg: Image, name: str) -> None:
        """图像消息回调：仅提取元数据，不解码像素，开销极低。

        提取内容：
          - recv_ns:    接收时刻 (wall-clock, ns)
          - stamp_ns:   消息时间戳 (sec*1e9 + nanosec)
          - frame_bytes: 原始图像字节数 (step × height)
        """
        recv_ns     = time.time_ns()
        stamp_ns    = msg.header.stamp.sec * 1_000_000_000 + msg.header.stamp.nanosec
        frame_bytes = msg.step * msg.height          # 实际像素数据字节数
        self._stats[name].record(recv_ns, stamp_ns, frame_bytes)

    # ─── 定时报告 ─────────────────────────────────────────────────────────────

    def _report_cb(self) -> None:
        """每 REPORT_INTERVAL 秒输出一次诊断表格并追加写入 CSV。"""
        ts = time.strftime("%Y-%m-%dT%H:%M:%S")
        mem_used, mem_total = _read_mem_mb()

        sep  = "─" * 90
        hdr  = (f"  {'流':12s} {'FPS':>6}  {'均值延迟ms':>10} "
                f"{'P95ms':>7} {'最大ms':>7}  {'带宽MB/s':>9} {'帧大小KB':>9} {'丢帧率':>7}")

        self.get_logger().info(sep)
        self.get_logger().info(
            f"[DiagReport] {ts}  内存: {mem_used:.0f}/{mem_total:.0f} MB"
        )
        self.get_logger().info(hdr)

        rows = []
        for name, s in self._stats.items():
            rows.append({
                "timestamp":       ts,
                "stream":          name,
                "fps":             f"{s.fps:.2f}",
                "latency_mean_ms": f"{s.latency_mean_ms:.3f}",
                "latency_p95_ms":  f"{s.latency_p95_ms:.3f}",
                "latency_max_ms":  f"{s.latency_max_ms:.3f}",
                "bandwidth_mbps":  f"{s.bandwidth_mbps:.3f}",
                "frame_size_kb":   f"{s.frame_size_kb:.1f}",
                "drop_rate":       f"{s.drop_rate:.4f}",
            })
            self.get_logger().info(
                f"  {name:12s} {s.fps:>6.1f}"
                f"  {s.latency_mean_ms:>10.2f}"
                f" {s.latency_p95_ms:>7.2f} {s.latency_max_ms:>7.2f}"
                f"  {s.bandwidth_mbps:>9.3f} {s.frame_size_kb:>9.1f}"
                f" {s.drop_rate:>7.2%}"
            )

        # 追加写入 per_interval_stats.csv
        with open(self._interval_csv_path, "a", newline="") as fh:
            writer = csv.DictWriter(fh, fieldnames=self._csv_fields)
            for row in rows:
                writer.writerow(row)

        self.get_logger().info(sep)

    # ─── 终态汇总 ─────────────────────────────────────────────────────────────

    def write_final_summary(self) -> None:
        """测量结束时将每路流的整体汇总写入 final_summary.csv 和 final_summary.json。

        汇总维度：
          - fps_last:           最后一个统计窗口的帧率
          - latency_mean_ms:    全窗口均值延迟
          - latency_p95_ms:     全窗口 p95 延迟
          - latency_max_ms:     全窗口最大延迟
          - bandwidth_mbps:     全窗口均值 USB 带宽
          - frame_size_kb:      单帧均值字节（KB）
          - total_frames:       自启动以来累计帧数
          - drop_rate:          严重延迟帧比率
        """
        summary_csv  = os.path.join(self._output_dir, "final_summary.csv")
        summary_json = os.path.join(self._output_dir, "final_summary.json")
        total_bw = 0.0

        fields = [
            "stream", "fps_last",
            "latency_mean_ms", "latency_p95_ms", "latency_max_ms",
            "bandwidth_mbps", "frame_size_kb",
            "total_frames", "drop_rate",
        ]
        json_out = {}

        with open(summary_csv, "w", newline="") as fh:
            writer = csv.DictWriter(fh, fieldnames=fields)
            writer.writeheader()
            for name, s in self._stats.items():
                row = {
                    "stream":          name,
                    "fps_last":        f"{s.fps:.2f}",
                    "latency_mean_ms": f"{s.latency_mean_ms:.3f}",
                    "latency_p95_ms":  f"{s.latency_p95_ms:.3f}",
                    "latency_max_ms":  f"{s.latency_max_ms:.3f}",
                    "bandwidth_mbps":  f"{s.bandwidth_mbps:.3f}",
                    "frame_size_kb":   f"{s.frame_size_kb:.1f}",
                    "total_frames":    s.total_frames,
                    "drop_rate":       f"{s.drop_rate:.4f}",
                }
                writer.writerow(row)
                total_bw += s.bandwidth_mbps
                json_out[name] = {k: row[k] for k in row}

        json_out["_total_bandwidth_mbps"] = round(total_bw, 3)

        with open(summary_json, "w") as fh:
            json.dump(json_out, fh, indent=2, ensure_ascii=False)

        print(f"\n[summary] final_summary.csv  → {summary_csv}")
        print(f"[summary] final_summary.json → {summary_json}")
        print(f"[summary] 全流 USB 总带宽: {total_bw:.2f} MB/s")

        # 控制台打印结构化表格
        sep = "─" * 90
        print(sep)
        print(f"  {'流':12s} {'FPS':>6}  {'均值延迟ms':>10} {'P95ms':>7} {'最大ms':>7}"
              f"  {'带宽MB/s':>9} {'帧大小KB':>9} {'丢帧率':>7}")
        print(sep)
        for name, s in self._stats.items():
            print(f"  {name:12s} {s.fps:>6.1f}"
                  f"  {s.latency_mean_ms:>10.2f}"
                  f" {s.latency_p95_ms:>7.2f} {s.latency_max_ms:>7.2f}"
                  f"  {s.bandwidth_mbps:>9.3f} {s.frame_size_kb:>9.1f}"
                  f" {s.drop_rate:>7.2%}")
        print(sep)
        print(f"  {'全流合计带宽':12s} {total_bw:>9.3f} MB/s")
        print(sep)


# ─────────────────────────────────────────────────────────────────────────────
# 入口
# ─────────────────────────────────────────────────────────────────────────────

def parse_args(argv=None):
    """解析命令行参数。"""
    parser = argparse.ArgumentParser(
        description="双相机 4 路图像流 FPS / 延迟 / USB 带宽并行诊断",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=(
            "示例:\n"
            "  python3 fps_latency_bw_monitor.py --duration 60\n"
            "  python3 fps_latency_bw_monitor.py --duration 120 "
            "--output-dir /tmp/my_test\n"
        ),
    )
    parser.add_argument(
        "--duration", "-d",
        type=int,
        default=60,
        metavar="SECONDS",
        help="采样时长（秒），默认 60",
    )
    parser.add_argument(
        "--output-dir", "-o",
        type=str,
        default="",
        metavar="DIR",
        help="输出目录（默认: /tmp/fps_latency_bw_diag_YYYYMMDD_HHMMSS）",
    )
    return parser.parse_args(argv)


def main(argv=None):
    args = parse_args(argv)

    # 确定输出目录
    if args.output_dir:
        output_dir = args.output_dir
    else:
        ts_tag  = time.strftime("%Y%m%d_%H%M%S")
        output_dir = f"/tmp/fps_latency_bw_diag_{ts_tag}"
    os.makedirs(output_dir, exist_ok=True)

    print(f"[monitor] 输出目录: {output_dir}")
    print(f"[monitor] 采样时长: {args.duration} 秒")
    print(f"[monitor] 监控话题: {[t for t, _ in TOPICS]}")

    # 初始化 ROS2（传入原始 sys.argv，让 rclpy 过滤 ROS2 专用参数）
    rclpy.init(args=sys.argv)
    node = FpsLatencyBwNode(output_dir)

    # 用独立线程执行 spin，主线程负责计时退出
    spin_thread = threading.Thread(
        target=rclpy.spin,
        args=(node,),
        daemon=True,      # 随主线程退出
    )
    spin_thread.start()

    # 等待采样时长结束（可被 Ctrl-C 中断）
    try:
        print(f"[monitor] 正在采集，{args.duration} 秒后自动结束 …（Ctrl-C 可提前退出）")
        time.sleep(args.duration)
    except KeyboardInterrupt:
        print("\n[monitor] 用户中断")

    # 输出最终汇总
    print("\n[monitor] 采样结束，生成汇总报告 …")
    node.write_final_summary()

    # 清理资源
    node.destroy_node()
    rclpy.shutdown()
    spin_thread.join(timeout=2.0)
    print(f"[monitor] 完成。所有日志已保存到 {output_dir}/")


if __name__ == "__main__":
    main()
