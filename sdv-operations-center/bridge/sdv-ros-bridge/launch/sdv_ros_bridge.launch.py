from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument
from launch.substitutions import LaunchConfiguration, PathJoinSubstitution
from launch_ros.actions import Node
from launch_ros.substitutions import FindPackageShare


def generate_launch_description():
    config = PathJoinSubstitution([
        FindPackageShare('sdv_ros_bridge'),
        'config',
        'bridge_params.yaml',
    ])

    return LaunchDescription([
        DeclareLaunchArgument('params_file', default_value=config),
        Node(
            package='sdv_ros_bridge',
            executable='sdv_ros_bridge_node',
            name='sdv_ros_bridge_node',
            output='screen',
            parameters=[LaunchConfiguration('params_file')],
        ),
    ])
