#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
camera_benchmark_node.py

ROS2 节点：订阅 Wrist Camera (Gemini 330) 和 Head Camera (Gemini 305)
的 color/depth 话题，统计 FPS、延迟（mean/p95/p99）、丢帧率，
每5秒打印报告并写入 /tmp/camera_benchmark.csv。

适用硬件：
  - Wrist Camera: Orbbec Gemini 330  (USB 3.x → x86)
  - Head Camera:  Orbbec Gemini 305  (USB 3.x → x86)
  - 主控:         x86 + RTX 4090 / x86 + Thor
  - ROS2:         Humble (Ubuntu 22.04)
"""

import csv
import os
import time
from collections import deque
from typing import Dict, List

import numpy as np
import psutil
import rclpy
from rclpy.node import Node
from rclpy.qos import QoSProfile, QoSReliabilityPolicy, QoSHistoryPolicy
from sensor_msgs.msg import Image

# ─── 常量 ────────────────────────────────────────────────────────────────────
WINDOW_SIZE = 300          # 滑动窗口帧数
REPORT_INTERVAL_SEC = 5.0  # 报告周期（秒）
CSV_PATH = "/tmp/camera_benchmark.csv"

# 订阅话题：(话题路径, 流名称)
TOPICS: List[tuple] = [
    ("/wrist_camera/color/image_raw", "wrist_color"),
    ("/wrist_camera/depth/image_raw", "wrist_depth"),
    ("/head_camera/color/image_raw",  "head_color"),
    ("/head_camera/depth/image_raw",  "head_depth"),
]

CSV_FIELDS = [
    "timestamp", "stream", "fps",
    "latency_ms", "p95_latency_ms", "p99_latency_ms",
    "drop_rate", "cpu_pct", "mem_rss_mb",
]


class StreamStats:
    """单个相机流的统计数据容器（滑动窗口）。"""

    def __init__(self, name: str):
        self.name = name
        # 接收时间戳（wall-clock，纳秒）
        self.recv_times: deque = deque(maxlen=WINDOW_SIZE)
        # 延迟样本（毫秒）
        self.latencies_ms: deque = deque(maxlen=WINDOW_SIZE)
        # 总帧数（用于计算丢帧率）
        self.total_frames: int = 0
        # 延迟异常帧数（latency > 500ms 视为"丢帧"或严重延迟）
        self.drop_frames: int = 0

    def record(self, recv_ns: int, stamp_ns: int) -> None:
        """记录一帧的接收时间和延迟。"""
        self.recv_times.append(recv_ns)
        latency_ms = (recv_ns - stamp_ns) / 1e6
        self.latencies_ms.append(latency_ms)
        self.total_frames += 1
        if latency_ms > 500.0:
            self.drop_frames += 1

    @property
    def fps(self) -> float:
        """用滑动窗口内的时间跨度计算 FPS。"""
        if len(self.recv_times) < 2:
            return 0.0
        span_s = (self.recv_times[-1] - self.recv_times[0]) / 1e9
        if span_s <= 0:
            return 0.0
        return (len(self.recv_times) - 1) / span_s

    @property
    def mean_latency_ms(self) -> float:
        if not self.latencies_ms:
            return 0.0
        return float(np.mean(list(self.latencies_ms)))

    @property
    def p95_latency_ms(self) -> float:
        if not self.latencies_ms:
            return 0.0
        return float(np.percentile(list(self.latencies_ms), 95))

    @property
    def p99_latency_ms(self) -> float:
        if not self.latencies_ms:
            return 0.0
        return float(np.percentile(list(self.latencies_ms), 99))

    @property
    def drop_rate(self) -> float:
        """丢帧率 = drop_frames / total_frames。"""
        if self.total_frames == 0:
            return 0.0
        return self.drop_frames / self.total_frames


class CameraBenchmarkNode(Node):
    """
    订阅双相机 4 路话题，每 5 秒打印并记录 FPS / 延迟 / 丢帧率报告。
    """

    def __init__(self):
        super().__init__("camera_benchmark_node")
        self.get_logger().info("CameraBenchmarkNode 初始化中 …")

        # QoS：BEST_EFFORT + depth=1（匹配相机驱动默认设置）
        qos = QoSProfile(
            reliability=QoSReliabilityPolicy.BEST_EFFORT,
            history=QoSHistoryPolicy.KEEP_LAST,
            depth=1,
        )

        # 每个流的统计对象
        self._stats: Dict[str, StreamStats] = {
            stream: StreamStats(stream) for _, stream in TOPICS
        }

        # 创建订阅
        self._subs = []
        for topic, stream in TOPICS:
            sub = self.create_subscription(
                Image,
                topic,
                lambda msg, s=stream: self._image_callback(msg, s),
                qos,
            )
            self._subs.append(sub)
            self.get_logger().info(f"  订阅: {topic} → {stream}")

        # 初始化 CSV
        self._init_csv()

        # psutil 进程对象（用于 CPU / 内存）
        self._process = psutil.Process(os.getpid())

        # 每 5 秒打印报告
        self.create_timer(REPORT_INTERVAL_SEC, self._report_callback)
        self.get_logger().info("CameraBenchmarkNode 已启动，等待图像话题 …")

    # ─── 私有方法 ─────────────────────────────────────────────────────────────

    def _init_csv(self) -> None:
        """初始化 CSV 文件，写入表头（若文件已存在则追加）。"""
        write_header = not os.path.exists(CSV_PATH)
        self._csv_file = open(CSV_PATH, "a", newline="", buffering=1)
        self._csv_writer = csv.DictWriter(self._csv_file, fieldnames=CSV_FIELDS)
        if write_header:
            self._csv_writer.writeheader()
            self._csv_file.flush()

    def _image_callback(self, msg: Image, stream: str) -> None:
        """图像消息回调：记录接收时间和消息头时间戳。"""
        recv_ns = time.time_ns()
        # 将 ROS Header stamp 转换为纳秒
        stamp_ns = msg.header.stamp.sec * 10**9 + msg.header.stamp.nanosec
        self._stats[stream].record(recv_ns, stamp_ns)

    def _get_system_metrics(self):
        """读取当前进程的 CPU 和 RSS 内存。"""
        cpu_pct = self._process.cpu_percent(interval=None)
        mem_rss_mb = self._process.memory_info().rss / (1024 * 1024)
        return cpu_pct, mem_rss_mb

    def _report_callback(self) -> None:
        """每 5 秒触发：打印统计报告并写入 CSV。"""
        ts = time.strftime("%Y-%m-%dT%H:%M:%S")
        cpu_pct, mem_rss_mb = self._get_system_metrics()

        separator = "─" * 72
        self.get_logger().info(separator)
        self.get_logger().info(
            f"[CameraBench] 报告时间: {ts} | "
            f"进程 CPU: {cpu_pct:.1f}% | 内存 RSS: {mem_rss_mb:.1f} MB"
        )
        self.get_logger().info(
            f"{'流名称':<14} {'FPS':>6} {'延迟(ms)':>10} "
            f"{'P95(ms)':>9} {'P99(ms)':>9} {'丢帧率':>8}"
        )

        for stream, stats in self._stats.items():
            fps = stats.fps
            lat = stats.mean_latency_ms
            p95 = stats.p95_latency_ms
            p99 = stats.p99_latency_ms
            dr = stats.drop_rate

            self.get_logger().info(
                f"  {stream:<12} {fps:>6.1f} {lat:>10.2f} "
                f"{p95:>9.2f} {p99:>9.2f} {dr:>7.1%}"
            )

            # 写入 CSV
            self._csv_writer.writerow({
                "timestamp":       ts,
                "stream":          stream,
                "fps":             f"{fps:.2f}",
                "latency_ms":      f"{lat:.3f}",
                "p95_latency_ms":  f"{p95:.3f}",
                "p99_latency_ms":  f"{p99:.3f}",
                "drop_rate":       f"{dr:.4f}",
                "cpu_pct":         f"{cpu_pct:.1f}",
                "mem_rss_mb":      f"{mem_rss_mb:.1f}",
            })

        self._csv_file.flush()
        self.get_logger().info(separator)

    def destroy_node(self):
        """节点销毁时关闭 CSV 文件。"""
        if hasattr(self, "_csv_file") and not self._csv_file.closed:
            self._csv_file.close()
        super().destroy_node()


def main(args=None):
    rclpy.init(args=args)
    node = CameraBenchmarkNode()
    try:
        rclpy.spin(node)
    except KeyboardInterrupt:
        pass
    finally:
        node.destroy_node()
        rclpy.shutdown()


if __name__ == "__main__":
    main()
