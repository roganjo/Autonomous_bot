"""
Gazebo (classic) simulation.

Starts:
  robot_state_publisher  URDF (sim_mode:=true, use_sim_time:=true)
  twist_mux              /cmd_vel (Nav2), /cmd_vel_joy, /cmd_vel_key -> /diff_cont/cmd_vel_unstamped
  gazebo                 world:=worlds/turtlebot.world by default
  spawn_entity          robot from /robot_description. The gazebo_ros2_control plugin in the
                         URDF starts controller_manager inside Gazebo with my_controllers.yaml
  spawners               joint_broad, diff_cont (after the robot is spawned)
  joystick               (use_joystick:=false, default)

The simulated lidar (lidar.xacro) publishes /scan in laser_frame, same as the real YDLIDAR.

Example:
  ros2 launch articubot_one launch_sim.launch.py world:=$(ros2 pkg prefix articubot_one)/share/articubot_one/worlds/obstacles.world
"""

import os

from ament_index_python.packages import get_package_share_directory

from launch import LaunchDescription
from launch.actions import (
    IncludeLaunchDescription,
    RegisterEventHandler,
    DeclareLaunchArgument
)
from launch.conditions import IfCondition
from launch.launch_description_sources import PythonLaunchDescriptionSource
from launch.event_handlers import OnProcessExit
from launch.substitutions import LaunchConfiguration
from launch_ros.actions import Node


def generate_launch_description():

    package_name = 'articubot_one'
    pkg_share = get_package_share_directory(package_name)

    world = LaunchConfiguration('world')
    use_joystick = LaunchConfiguration('use_joystick')

    declare_world = DeclareLaunchArgument(
        'world',
        default_value=os.path.join(pkg_share, 'worlds', 'turtlebot.world'),
        description='Gazebo world file')
    declare_use_joystick = DeclareLaunchArgument(
        'use_joystick', default_value='false',
        description='Start joy_node + teleop_twist_joy')

    # ================== ROBOT STATE PUBLISHER ==================
    rsp = IncludeLaunchDescription(
        PythonLaunchDescriptionSource(
            os.path.join(pkg_share, 'launch', 'rsp.launch.py')
        ),
        launch_arguments={
            'use_sim_time': 'true',
            'use_ros2_control': 'true',
            'sim_mode': 'true',
        }.items()
    )

    # ================== JOYSTICK (OPTIONAL) ==================
    joystick = IncludeLaunchDescription(
        PythonLaunchDescriptionSource(
            os.path.join(pkg_share, 'launch', 'joystick.launch.py')
        ),
        launch_arguments={'use_sim_time': 'true'}.items(),
        condition=IfCondition(use_joystick)
    )

    # ================== TWIST MUX ==================
    twist_mux_params = os.path.join(pkg_share, 'config', 'twist_mux.yaml')

    twist_mux = Node(
        package="twist_mux",
        executable="twist_mux",
        parameters=[twist_mux_params, {'use_sim_time': True}],
        remappings=[('/cmd_vel_out', '/diff_cont/cmd_vel_unstamped')]
    )

    # ================== GAZEBO ==================
    gazebo = IncludeLaunchDescription(
        PythonLaunchDescriptionSource(
            os.path.join(
                get_package_share_directory("gazebo_ros"),
                "launch",
                "gazebo.launch.py"
            )
        ),
        launch_arguments={"world": world}.items()
    )

    # ================== SPAWN ROBOT ==================
    spawn_entity = Node(
        package='gazebo_ros',
        executable='spawn_entity.py',
        arguments=[
            '-topic', 'robot_description',
            '-entity', 'my_bot',
            '-x', '-1.5',
            '-y', '0.5',
            '-z', '0.1',
            '-Y', '0'
        ],
        output='screen'
    )

    # ================== CONTROLLER SPAWNERS ==================
    joint_broad_spawner = Node(
        package="controller_manager",
        executable="spawner",
        arguments=["joint_broad", "--controller-manager", "/controller_manager"],
    )

    diff_drive_spawner = Node(
        package="controller_manager",
        executable="spawner",
        arguments=["diff_cont", "--controller-manager", "/controller_manager"],
    )

    # ================== DELAY CONTROLLERS UNTIL ROBOT SPAWNS ==================
    delayed_controllers = RegisterEventHandler(
        event_handler=OnProcessExit(
            target_action=spawn_entity,
            on_exit=[
                joint_broad_spawner,
                diff_drive_spawner,
            ],
        )
    )

    # ================== LAUNCH EVERYTHING ==================
    return LaunchDescription([
        declare_world,
        declare_use_joystick,
        rsp,
        joystick,
        twist_mux,
        gazebo,
        spawn_entity,
        delayed_controllers
    ])
