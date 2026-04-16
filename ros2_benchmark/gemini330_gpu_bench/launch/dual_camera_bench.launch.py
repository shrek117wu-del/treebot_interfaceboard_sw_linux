# ros2_benchmark/gemini330_gpu_bench/launch/dual_camera_bench.launch.py
#
# ROS2 Launch 文件：同时启动双相机节点 + 基准测试节点
#   - wrist_camera  (Orbbec Gemini 330, namespace: wrist_camera)
#   - head_camera   (Orbbec Gemini 305, namespace: head_camera)
#   - camera_benchmark_node（延迟 3 秒等待相机初始化）
#
# 用法：
#   ros2 launch gemini330_gpu_bench dual_camera_bench.launch.py

from launch import LaunchDescription
from launch.actions import TimerAction
from launch_ros.actions import Node


def generate_launch_description():

    # ── Wrist Camera：Orbbec Gemini 330 ─────────────────────────────────────
    # USB 3.x 接 x86，1280×720 @ 30fps，彩色 + 深度
    wrist_camera_node = Node(
        package="orbbec_camera",
        executable="orbbec_camera_node",
        name="wrist_camera_node",
        namespace="wrist_camera",
        output="screen",
        parameters=[{
            "product_name":          "gemini330",
            # 彩色流配置
            "color_width":           1280,
            "color_height":          720,
            "color_fps":             30,
            "enable_color":          True,
            # 深度流配置
            "depth_width":           1280,
            "depth_height":          720,
            "depth_fps":             30,
            "enable_depth":          True,
            # 关闭点云（降低 CPU/带宽占用）
            "enable_point_cloud":    False,
            # 使用系统时间作为时间戳域，便于延迟测量
            "timestamp_domain":      "system",
            # 硬件深度对齐
            "enable_hardware_d2c":   True,
        }],
        # 所有话题 QoS 覆盖为 best_effort
        remappings=[],
        ros_arguments=[
            "--ros-args",
            "--qos-reliability-override", "best_effort",
        ],
    )

    # ── Head Camera：Orbbec Gemini 305 ──────────────────────────────────────
    # USB 3.x 接 x86，640×480 @ 15fps，彩色 + 深度
    head_camera_node = Node(
        package="orbbec_camera",
        executable="orbbec_camera_node",
        name="head_camera_node",
        namespace="head_camera",
        output="screen",
        parameters=[{
            "product_name":          "gemini305",
            # 彩色流配置（降分辨率节省带宽）
            "color_width":           640,
            "color_height":          480,
            "color_fps":             15,
            "enable_color":          True,
            # 深度流配置
            "depth_width":           640,
            "depth_height":          480,
            "depth_fps":             15,
            "enable_depth":          True,
            # 关闭点云
            "enable_point_cloud":    False,
            # 系统时间戳域
            "timestamp_domain":      "system",
            # 硬件深度对齐
            "enable_hardware_d2c":   True,
        }],
        remappings=[],
        ros_arguments=[
            "--ros-args",
            "--qos-reliability-override", "best_effort",
        ],
    )

    # ── Camera Benchmark 节点（延迟 3 秒启动，等待相机初始化完成）───────────
    camera_benchmark_node = Node(
        package="gemini330_gpu_bench",
        executable="camera_benchmark_node",
        name="camera_benchmark_node",
        output="screen",
    )

    delayed_benchmark = TimerAction(
        period=3.0,
        actions=[camera_benchmark_node],
    )

    return LaunchDescription([
        wrist_camera_node,
        head_camera_node,
        delayed_benchmark,
    ])
