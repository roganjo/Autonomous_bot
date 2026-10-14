import os

from ament_index_python.packages import get_package_share_directory

from launch import LaunchDescription
from launch.substitutions import LaunchConfiguration, Command
from launch.actions import DeclareLaunchArgument
from launch_ros.actions import Node
from launch_ros.parameter_descriptions import ParameterValue


def generate_launch_description():

    use_sim_time = LaunchConfiguration('use_sim_time')
    use_ros2_control = LaunchConfiguration('use_ros2_control')
    sim_mode = LaunchConfiguration('sim_mode')
    serial_port = LaunchConfiguration('serial_port')

    # Process the URDF file
    pkg_path = get_package_share_directory('articubot_one')
    xacro_file = os.path.join(pkg_path, 'description', 'robot.urdf.xacro')
    robot_description_config = ParameterValue(
        Command(['xacro ', xacro_file,
                 ' use_ros2_control:=', use_ros2_control,
                 ' sim_mode:=', sim_mode,
                 ' serial_port:=', serial_port]),
        value_type=str)

    # Create a robot_state_publisher node
    params = {'robot_description': robot_description_config, 'use_sim_time': use_sim_time}
    node_robot_state_publisher = Node(
        package='robot_state_publisher',
        executable='robot_state_publisher',
        output='screen',
        parameters=[params]
    )

    # Launch!
    return LaunchDescription([
        DeclareLaunchArgument(
            'use_sim_time',
            default_value='false',
            description='Use sim time if true'),
        DeclareLaunchArgument(
            'use_ros2_control',
            default_value='true',
            description='Use ros2_control if true'),
        DeclareLaunchArgument(
            'sim_mode',
            default_value='false',
            description='true: Gazebo hardware, false: ESP32 hardware (DiffBotHardware)'),
        DeclareLaunchArgument(
            'serial_port',
            default_value='/dev/ttyUSB0',
            description='ESP32 serial port (real robot only)'),

        node_robot_state_publisher
    ])
