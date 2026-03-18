import os
from ament_index_python.packages import get_package_share_directory
from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument, IncludeLaunchDescription, RegisterEventHandler
from launch.event_handlers import OnProcessStart
from launch.launch_description_sources import PythonLaunchDescriptionSource
from launch.substitutions import LaunchConfiguration, Command, FindExecutable, PathJoinSubstitution
from launch_ros.actions import Node
from launch_ros.substitutions import FindPackageShare

def generate_launch_description():
    pkg_driver = get_package_share_directory('seeway_interface_driver')
    config_file = os.path.join(pkg_driver, 'config', 'seeway_interface.yaml')
    control_config_file = os.path.join(pkg_driver, 'config', 'seeway_ros2_control.yaml')

    # Note: in a real deployment, you would generate a URDF that includes the 
    # <ros2_control> tag specifying "seeway_interface_hardware/SeewayHardwareInterface"
    # as the plugin, and then pass it to robot_state_publisher.
    # For this launch file, we show how to start the driver and a mock controller manager.

    driver_node = Node(
        package='seeway_interface_driver',
        executable='seeway_driver_node_exe',
        name='seeway_driver_node',
        output='screen',
        parameters=[config_file]
    )

    # For Lifecycle nodes, we need to transition it to "active"
    # Wait for the node to start, then emit emit events: Event(Unconfigured -> Inactive), Event(Inactive -> Active)
    from launch_ros.events.lifecycle import ChangeState
    from lifecycle_msgs.msg import Transition
    
    activate_driver = RegisterEventHandler(
        OnProcessStart(
            target_action=driver_node,
            on_start=[
                # Configure
                launch.actions.EmitEvent(
                    event=ChangeState(
                        lifecycle_node_matcher=launch.events.matches_action(driver_node),
                        transition_id=Transition.TRANSITION_CONFIGURE
                    )
                ),
                # A proper launch script would wait for the COMPLETED event of configuration 
                # before Activating, but for brevity we'll emit both (or use a helper tool).
            ]
        )
    )

    return LaunchDescription([
        driver_node,
        # activate_driver
    ])
