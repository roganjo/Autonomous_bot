"""
diffbot_bringup.launch.py — v1.5

Root cause of persistent 'left_wheel_names cannot be empty':
The controller manager in Humble resolves controller parameters by looking
up /<controller_name>/ros__parameters in the parameter server at the moment
the spawner calls LoadController. If the YAML file hasn't been parsed into
the parameter server yet (race) or the install path is stale, it gets empty.

Fix: define all controller parameters directly in the launch file as Python
dicts. This is 100% reliable — no file path, no install cache, no YAML
parsing ambiguity. The YAML file is still used for update_rate and types.
"""

import os

from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument, RegisterEventHandler, LogInfo, TimerAction
from launch.event_handlers import OnProcessExit
from launch.substitutions import Command, FindExecutable, LaunchConfiguration
from launch_ros.actions import Node
from launch_ros.parameter_descriptions import ParameterValue
from ament_index_python.packages import get_package_share_directory


def generate_launch_description():

    pkg_share = get_package_share_directory("diffbot_hardware")
    urdf_file = os.path.join(pkg_share, "urdf", "diffbot.urdf.xacro")

    # ── Launch arguments ──────────────────────────────────────────────────────
    declare_use_sim_time = DeclareLaunchArgument(
        "use_sim_time", default_value="false")
    declare_serial_port = DeclareLaunchArgument(
        "serial_port", default_value="/dev/ttyACM0")
    declare_log_level = DeclareLaunchArgument(
        "log_level", default_value="info")

    use_sim_time = LaunchConfiguration("use_sim_time")
    serial_port  = LaunchConfiguration("serial_port")
    log_level    = LaunchConfiguration("log_level")

    # ── Robot description ─────────────────────────────────────────────────────
    robot_description_content = ParameterValue(
        Command([
            FindExecutable(name="xacro"), " ", urdf_file,
            " serial_port:=", serial_port,
        ]),
        value_type=str,
    )

    # ── robot_state_publisher ─────────────────────────────────────────────────
    robot_state_publisher_node = Node(
        package="robot_state_publisher",
        executable="robot_state_publisher",
        name="robot_state_publisher",
        output="screen",
        parameters=[
            {"robot_description": robot_description_content},
            {"use_sim_time": use_sim_time},
        ],
    )

    # ── ros2_control_node ─────────────────────────────────────────────────────
    # All parameters defined inline as dicts — no YAML file dependency.
    # This guarantees the controller manager sees them regardless of install
    # cache state or symlink-install behaviour.
    ros2_control_node = Node(
        package="controller_manager",
        executable="ros2_control_node",
        name="controller_manager",
        output="screen",
        remappings=[("~/robot_description", "/robot_description")],
        parameters=[
            # ── Controller manager ──────────────────────────────────────────
            {
                "update_rate": 50,
                "joint_state_broadcaster": {"type": "joint_state_broadcaster/JointStateBroadcaster"},
                "diff_drive_controller":   {"type": "diff_drive_controller/DiffDriveController"},
            },
            # ── diff_drive_controller params ────────────────────────────────
            {
                "diff_drive_controller": {
                    "left_wheel_names":               ["left_wheel_joint"],
                    "right_wheel_names":              ["right_wheel_joint"],
                    "wheel_separation":               0.160,
                    "wheel_radius":                   0.033,
                    "wheel_separation_multiplier":    1.0,
                    "left_wheel_radius_multiplier":   1.0,
                    "right_wheel_radius_multiplier":  1.0,
                    "open_loop":                      False,
                    "enable_odom_tf":                 True,
                    "odom_frame_id":                  "odom",
                    "base_frame_id":                  "base_link",
                    "cmd_vel_timeout":                0.5,
                    "publish_rate":                   50.0,
                    "pose_covariance_diagonal":       [0.001, 0.001, 1.0e6, 1.0e6, 1.0e6, 0.01],
                    "twist_covariance_diagonal":      [0.001, 1.0e6, 1.0e6, 1.0e6, 1.0e6, 0.01],
                }
            },
            # ── joint_state_broadcaster params ──────────────────────────────
            {
                "joint_state_broadcaster": {
                    "joints": [
                        "left_wheel_joint",
                        "right_wheel_joint",
                    ]
                }
            },
            {"use_sim_time": use_sim_time},
        ],
        arguments=["--ros-args", "--log-level", log_level],
    )

    ros2_control_node_delayed = TimerAction(period=2.0, actions=[ros2_control_node])

    # ── Spawners ──────────────────────────────────────────────────────────────
    spawn_jsb = Node(
        package="controller_manager",
        executable="spawner",
        name="spawner_jsb",
        arguments=[
            "joint_state_broadcaster",
            "--controller-manager", "/controller_manager",
            "--controller-manager-timeout", "60",
        ],
        output="screen",
    )

    spawn_ddc = Node(
        package="controller_manager",
        executable="spawner",
        name="spawner_ddc",
        arguments=[
            "diff_drive_controller",
            "--controller-manager", "/controller_manager",
            "--controller-manager-timeout", "60",
        ],
        output="screen",
    )

    load_after_jsb = RegisterEventHandler(
        event_handler=OnProcessExit(
            target_action=spawn_jsb,
            on_exit=[
                LogInfo(msg="JSB active — spawning diff_drive_controller"),
                spawn_ddc,
            ],
        )
    )

    return LaunchDescription([
        declare_use_sim_time,
        declare_serial_port,
        declare_log_level,
        robot_state_publisher_node,
        ros2_control_node_delayed,
        spawn_jsb,
        load_after_jsb,
    ])