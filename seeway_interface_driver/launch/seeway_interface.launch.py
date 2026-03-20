import os
from ament_index_python.packages import get_package_share_directory
from launch import LaunchDescription
from launch.actions import (
    DeclareLaunchArgument,
    EmitEvent,
    RegisterEventHandler,
    TimerAction,
)
from launch.event_handlers import OnProcessStart
from launch.substitutions import LaunchConfiguration
from launch_ros.actions import LifecycleNode
from launch_ros.events.lifecycle import ChangeState
from lifecycle_msgs.msg import Transition
import launch


def generate_launch_description():
    pkg_driver = get_package_share_directory('seeway_interface_driver')
    config_file = os.path.join(pkg_driver, 'config', 'seeway_interface.yaml')

    # -----------------------------------------------------------------------
    # Launch arguments – allow overriding any transport parameter on the CLI
    # -----------------------------------------------------------------------
    transport_arg = DeclareLaunchArgument(
        'transport', default_value='tcp',
        description='Transport type: tcp | serial')

    tcp_mode_arg = DeclareLaunchArgument(
        'tcp_mode', default_value='server',
        description='TCP mode: server (Jetson listens) | client (Jetson connects)')

    tcp_port_arg = DeclareLaunchArgument(
        'tcp_port', default_value='9000',
        description='TCP port')

    tcp_host_arg = DeclareLaunchArgument(
        'tcp_host', default_value='192.168.1.50',
        description='T113i IP address (used in tcp client mode)')

    tcp_bind_address_arg = DeclareLaunchArgument(
        'tcp_bind_address', default_value='0.0.0.0',
        description='Bind address for tcp server mode')

    tcp_reconnect_ms_arg = DeclareLaunchArgument(
        'tcp_reconnect_ms', default_value='1000',
        description='Milliseconds between TCP client reconnect attempts')

    serial_device_arg = DeclareLaunchArgument(
        'serial_device', default_value='/dev/ttyACM0',
        description='Serial / USB-CDC device path')

    serial_baudrate_arg = DeclareLaunchArgument(
        'serial_baudrate', default_value='115200',
        description='Serial baud rate')

    # -----------------------------------------------------------------------
    # Driver node (LifecycleNode so we can drive state transitions)
    # -----------------------------------------------------------------------
    driver_node = LifecycleNode(
        package='seeway_interface_driver',
        executable='seeway_driver_node_exe',
        name='seeway_interface_driver',
        namespace='',
        output='screen',
        parameters=[
            config_file,
            {
                'transport':          LaunchConfiguration('transport'),
                'tcp.mode':           LaunchConfiguration('tcp_mode'),
                'tcp.port':           LaunchConfiguration('tcp_port'),
                'tcp.host':           LaunchConfiguration('tcp_host'),
                'tcp.bind_address':   LaunchConfiguration('tcp_bind_address'),
                'tcp.reconnect_ms':   LaunchConfiguration('tcp_reconnect_ms'),
                'serial.device':      LaunchConfiguration('serial_device'),
                'serial.baudrate':    LaunchConfiguration('serial_baudrate'),
            },
        ],
    )

    # -----------------------------------------------------------------------
    # Automatic lifecycle management:
    #   1) configure  – sent 1 s after the process starts
    #   2) activate   – sent 3 s after the process starts
    #      (assumes configure completes within 2 s)
    # -----------------------------------------------------------------------
    configure_event = EmitEvent(
        event=ChangeState(
            lifecycle_node_matcher=launch.events.matches_action(driver_node),
            transition_id=Transition.TRANSITION_CONFIGURE,
        )
    )

    activate_event = EmitEvent(
        event=ChangeState(
            lifecycle_node_matcher=launch.events.matches_action(driver_node),
            transition_id=Transition.TRANSITION_ACTIVATE,
        )
    )

    configure_on_start = RegisterEventHandler(
        OnProcessStart(
            target_action=driver_node,
            on_start=[
                TimerAction(period=1.0, actions=[configure_event]),
                TimerAction(period=3.0, actions=[activate_event]),
            ],
        )
    )

    return LaunchDescription([
        # Arguments
        transport_arg,
        tcp_mode_arg,
        tcp_port_arg,
        tcp_host_arg,
        tcp_bind_address_arg,
        tcp_reconnect_ms_arg,
        serial_device_arg,
        serial_baudrate_arg,

        # Node + lifecycle handler
        driver_node,
        configure_on_start,
    ])
