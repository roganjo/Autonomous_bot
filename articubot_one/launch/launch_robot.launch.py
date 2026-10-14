"""
Real-robot bring-up.

Starts:
  robot_state_publisher  URDF (sim_mode:=false) -> /robot_description, /tf_static, /tf for the wheels
  twist_mux              /cmd_vel (Nav2), /cmd_vel_joy, /cmd_vel_key -> /diff_cont/cmd_vel_unstamped
  ros2_control_node      loads DiffBotHardware (serial to the ESP32) + controllers
  spawners               joint_broad, diff_cont
  ydlidar driver         /scan in laser_frame            (use_lidar:=true, default)
  joystick               joy_node + teleop_twist_joy     (use_joystick:=false, default)

Example:
  ros2 launch articubot_one launch_robot.launch.py serial_port:=/dev/ttyUSB0 lidar_port:=/dev/ttyUSB1
"""

import os

from ament_index_python.packages import get_package_share_directory
from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument, IncludeLaunchDescription, TimerAction
from launch.actions import RegisterEventHandler
from launch.conditions import IfCondition
from launch.event_handlers import OnProcessStart
from launch.launch_description_sources import PythonLaunchDescriptionSource
from launch.substitutions import Command, FindExecutable, LaunchConfiguration, PathJoinSubstitution
from launch_ros.actions import Node
from launch_ros.parameter_descriptions import ParameterValue
from launch_ros.substitutions import FindPackageShare


def generate_launch_description():

    package_name = 'articubot_one'
    pkg_share = get_package_share_directory(package_name)

    serial_port = LaunchConfiguration('serial_port')
    use_lidar = LaunchConfiguration('use_lidar')
    lidar_port = LaunchConfiguration('lidar_port')
    lidar_params_file = LaunchConfiguration('lidar_params_file')
    use_joystick = LaunchConfiguration('use_joystick')

    declare_serial_port = DeclareLaunchArgument(
        'serial_port', default_value='/dev/ttyUSB0',
        description='ESP32 serial port. Prefer a /dev/serial/by-id/... path.')
    declare_use_lidar = DeclareLaunchArgument(
        'use_lidar', default_value='true',
        description='Start the YDLIDAR driver')
    declare_lidar_port = DeclareLaunchArgument(
        'lidar_port', default_value='/dev/ttyUSB1',
        description='YDLIDAR serial port. Prefer a /dev/serial/by-id/... path.')
    declare_lidar_params_file = DeclareLaunchArgument(
        'lidar_params_file',
        default_value=os.path.join(
            get_package_share_directory('ydlidar_ros2_driver'), 'params', 'X2.yaml'),
        description='YDLIDAR model parameters (default: X2)')
    declare_use_joystick = DeclareLaunchArgument(
        'use_joystick', default_value='false',
        description='Start joy_node + teleop_twist_joy')

    # ── robot_description (URDF via xacro) ───────────────────────────────────
    # Same xacro arguments as rsp.launch.py below, so controller_manager and
    # robot_state_publisher see an identical URDF. Passed to controller_manager
    # as a parameter (Humble reads it from there), so there is no topic race.
    robot_description_content = ParameterValue(
        Command([
            FindExecutable(name='xacro'), ' ',
            PathJoinSubstitution([
                FindPackageShare(package_name), 'description', 'robot.urdf.xacro'
            ]),
            ' use_ros2_control:=true sim_mode:=false serial_port:=', serial_port,
        ]),
        value_type=str)
    robot_description = {'robot_description': robot_description_content}

    # ── robot_state_publisher ─────────────────────────────────────────────────
    rsp = IncludeLaunchDescription(
        PythonLaunchDescriptionSource([
            os.path.join(pkg_share, 'launch', 'rsp.launch.py')
        ]),
        launch_arguments={
            'use_sim_time': 'false',
            'use_ros2_control': 'true',
            'sim_mode': 'false',
            'serial_port': serial_port,
        }.items()
    )

    # ── twist_mux ─────────────────────────────────────────────────────────────
    twist_mux_params = os.path.join(pkg_share, 'config', 'twist_mux.yaml')
    twist_mux = Node(
        package='twist_mux',
        executable='twist_mux',
        parameters=[twist_mux_params],
        remappings=[('/cmd_vel_out', '/diff_cont/cmd_vel_unstamped')]
    )

    # ── joystick (optional) ───────────────────────────────────────────────────
    joystick = IncludeLaunchDescription(
        PythonLaunchDescriptionSource([
            os.path.join(pkg_share, 'launch', 'joystick.launch.py')
        ]),
        launch_arguments={'use_sim_time': 'false'}.items(),
        condition=IfCondition(use_joystick)
    )

    # ── controller_manager ────────────────────────────────────────────────────
    controller_params_file = os.path.join(pkg_share, 'config', 'my_controllers.yaml')

    controller_manager = Node(
        package='controller_manager',
        executable='ros2_control_node',
        parameters=[
            robot_description,
            controller_params_file,
        ],
        output='screen',
    )

    # Delay controller_manager 3 s so robot_state_publisher is up first
    delayed_controller_manager = TimerAction(period=3.0, actions=[controller_manager])

    # ── Controller spawners ───────────────────────────────────────────────────
    joint_broad_spawner = Node(
        package='controller_manager',
        executable='spawner',
        arguments=['joint_broad', '--controller-manager', '/controller_manager'],
    )

    diff_drive_spawner = Node(
        package='controller_manager',
        executable='spawner',
        arguments=['diff_cont', '--controller-manager', '/controller_manager'],
    )

    # OnProcessStart fires when the process starts, not when the hardware is
    # active, so wait 2 s more for on_configure + on_activate (serial open).
    delayed_controller_spawner = RegisterEventHandler(
        event_handler=OnProcessStart(
            target_action=controller_manager,
            on_start=[
                TimerAction(period=2.0, actions=[
                    joint_broad_spawner,
                    diff_drive_spawner,
                ])
            ],
        )
    )

    # ── YDLIDAR ───────────────────────────────────────────────────────────────
    # Started as a plain node (not ydlidar_launch.py) because that launch file
    # also publishes a static base_link -> laser_frame TF, which would fight
    # the laser_joint already in the URDF. Node name must match the top-level
    # key in the params file. The driver exits if the port cannot be opened,
    # so respawn it until the lidar is plugged in.
    lidar = Node(
        package='ydlidar_ros2_driver',
        executable='ydlidar_ros2_driver_node',
        name='ydlidar_ros2_driver_node',
        output='screen',
        emulate_tty=True,
        respawn=True,
        respawn_delay=3.0,
        parameters=[
            lidar_params_file,
            {'port': lidar_port, 'frame_id': 'laser_frame'},
        ],
        condition=IfCondition(use_lidar)
    )

    return LaunchDescription([
        declare_serial_port,
        declare_use_lidar,
        declare_lidar_port,
        declare_lidar_params_file,
        declare_use_joystick,
        rsp,
        twist_mux,
        joystick,
        delayed_controller_manager,
        delayed_controller_spawner,
        lidar,
    ])
