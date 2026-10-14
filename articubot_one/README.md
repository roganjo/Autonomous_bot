# articubot_one – the robot package

This is the package you launch. It holds everything that describes the autonomous bot to ROS: its shape and joints
(the URDF), how its wheels are controlled, which program gets to drive it, and the settings for mapping and
navigation. It also has the Gazebo worlds for simulation and the saved map.

**New to the project?** Start with the [workspace README](../README.md). It takes you through installing, building,
simulation, the hardware and navigation in order. This file explains the package in more depth: what each file
does, which values you may need to change, and why things are set up the way they are.

The name `articubot_one` comes from the open-source robot template this package started from. It is the autonomous
bot's package; the name has no other meaning.

---

## Contents

1. [Folder layout](#1-folder-layout)
2. [The robot model (URDF)](#2-the-robot-model-urdf)
3. [Wheel control (ros2_control)](#3-wheel-control-ros2_control)
4. [Who drives the robot (twist_mux)](#4-who-drives-the-robot-twist_mux)
5. [Launch files](#5-launch-files)
6. [Config files: what each one does and what you may change](#6-config-files-what-each-one-does-and-what-you-may-change)
7. [Mapping and navigation in this package](#7-mapping-and-navigation-in-this-package)
8. [Common changes](#8-common-changes)

---

## 1. Folder layout

```
articubot_one/
├── description/                 ← the robot model (URDF, written in xacro)
│   ├── robot.urdf.xacro         top-level file; includes the others
│   ├── robot_core.xacro         chassis, wheels, caster; robot dimensions live here
│   ├── ros2_control.xacro       which hardware drives the wheels (ESP32 or Gazebo)
│   ├── gazebo_control.xacro     alternative Gazebo drive plugin (only if use_ros2_control:=false)
│   ├── lidar.xacro              where the lidar sits + simulated lidar for Gazebo
│   ├── face.xacro               two "eyes" on the front, cosmetic only
│   └── inertial_macros.xacro    helpers for mass/inertia (needed by Gazebo)
├── config/                      ← settings (YAML) and RViz layouts
│   ├── my_controllers.yaml      wheel controller diff_cont + joint_broad
│   ├── twist_mux.yaml           priority of keyboard / joystick / Nav2
│   ├── joystick.yaml            joystick buttons and speeds
│   ├── mapper_params_online_async.yaml   slam_toolbox (mapping / localization)
│   ├── nav2_params.yaml         AMCL, map_server and all Nav2 servers
│   ├── drive_bot.rviz           RViz: robot + scan, Fixed Frame odom (for driving)
│   ├── map.rviz                 RViz: map + robot + scan, Fixed Frame map (for SLAM and navigation)
│   ├── main.rviz                RViz: robot + scan, Fixed Frame odom
│   ├── view_bot.rviz            RViz: robot model only, Fixed Frame base_link
│   └── gazebo_params.yaml, gaz_ros2_ctl_use_sim.yaml, empty.yaml   not used by any launch file
├── launch/                      ← launch files (section 5)
├── map/                         ← saved maps (turtlebot_map = map of the Gazebo turtlebot.world)
└── worlds/                      ← Gazebo worlds: turtlebot (default), obstacles, empty
```

Because the workspace is built with `colcon build --symlink-install`, changes to files in `description/`, `config/`,
`launch/`, `map/` and `worlds/` take effect the next time you start the launch file. No rebuild needed.

## 2. The robot model (URDF)

The URDF tells ROS what the robot looks like and where each part is. RViz uses it to draw the robot, Gazebo uses it
to simulate it, and `robot_state_publisher` uses it to publish the TF frames that SLAM and Nav2 need.

### Dimensions

All in `description/robot_core.xacro` (and `lidar.xacro` for the lidar). The robot's front is the +x direction,
where the face is.

| Item | Value | xacro property |
|---|---|---|
| Wheel radius | 0.054 m | `wheel_radius` |
| Wheel separation (centre to centre) | 0.30 m | 2 × `wheel_offset_y` (0.15) |
| Chassis size (L × W × H) | 0.33 × 0.24 × 0.135 m | `chassis_length`, `chassis_width`, `chassis_height` |
| Chassis position | its back edge is 0.226 m behind the wheel axle | `wheel_offset_x` |
| Caster wheel | radius 0.015 m, behind the drive wheels | `caster_wheel_*` |
| Lidar (`laser_frame`) | 0.25 m forward from the chassis back edge, 0.212 m up | `laser_joint` in `lidar.xacro` |

Measure your real robot and correct these if they differ. Wheel radius and separation must also be changed in two
other files; see [section 8](#8-common-changes).

### Frames (TF)

```
map ──► odom ──► base_link ──┬──► base_footprint
 │        │                  ├──► left_wheel, right_wheel   (move with the wheels)
 │        │                  └──► chassis ──┬──► laser_frame
 │        │                                 ├──► face_link
 │        │                                 └──► caster_wheel
 │        └ published by diff_cont (wheel odometry)
 └ published by slam_toolbox or AMCL (corrects odometry drift)
```

| Frame | Where it is | Published by |
|---|---|---|
| `map` | fixed point in the room, origin of the map | slam_toolbox or AMCL |
| `odom` | where the robot started; drifts slowly against `map` | diff_cont |
| `base_link` | midpoint between the two drive wheels; the robot turns around this point | (robot origin) |
| `base_footprint` | same place as `base_link` in this model (zero offset); used by slam_toolbox and AMCL | robot_state_publisher |
| `chassis`, `laser_frame`, `face_link`, `caster_wheel` | fixed parts of the robot | robot_state_publisher |
| `left_wheel`, `right_wheel` | rotate as the wheels turn | robot_state_publisher, from `/joint_states` |

To see the live tree: `ros2 run tf2_tools view_frames` (creates `frames.pdf`).

### xacro arguments

`robot.urdf.xacro` takes three arguments. The launch files set them for you.

| Argument | Default | Meaning |
|---|---|---|
| `use_ros2_control` | `true` | `true`: wheels driven through ros2_control (normal). `false`: Gazebo's own diff-drive plugin from `gazebo_control.xacro` (simulation only, not used by the launch files). |
| `sim_mode` | `false` | `false`: real ESP32 hardware. `true`: Gazebo hardware. |
| `serial_port` | `/dev/ttyUSB0` | ESP32 port, passed on to the hardware plugin. |

**Careful with comments in `.xacro` files.** xacro copies comments into the generated URDF, and in simulation
`gazebo_ros2_control` passes the whole URDF to ROS as a command-line parameter, which is read as YAML. A comment
containing an apostrophe (`'`), a colon followed by a space (`: `), a space followed by `#`, or a non-ASCII character
(e.g. `─`, `→`, `°`) makes it fail with `parser error Couldn't parse parameter override rule`, and no controllers
load. Write comments in plain ASCII without those. Quick check:

```bash
xacro ~/autonomous_bot/src/articubot_one/description/robot.urdf.xacro sim_mode:=true > /tmp/sim.urdf
python3 -c "import yaml; yaml.safe_load(open('/tmp/sim.urdf')); print('OK')"
```

To check the model on its own:

```bash
xacro ~/autonomous_bot/src/articubot_one/description/robot.urdf.xacro sim_mode:=false > /tmp/robot.urdf
check_urdf /tmp/robot.urdf
ros2 launch articubot_one rsp.launch.py &
rviz2 -d ~/autonomous_bot/src/articubot_one/config/view_bot.rviz
```

## 3. Wheel control (ros2_control)

ros2_control sits between "drive at this speed" and the motors. It has two layers:

- a **controller** (`diff_cont`) that does the maths for a two-wheeled robot, and
- a **hardware interface** that actually moves the wheels: the ESP32 on the real robot, Gazebo in simulation.

### Joints

`description/ros2_control.xacro` declares the same two joints for the real robot and for simulation:

| Joint | Command (what the controller sets) | State (what the hardware reports) |
|---|---|---|
| `left_wheel_joint` | velocity, −10 … +10 rad/s | position, velocity |
| `right_wheel_joint` | velocity, −10 … +10 rad/s | position, velocity |

Which hardware is used depends on `sim_mode`:

- `sim_mode:=false` (real robot) → plugin `diffbot_hardware/DiffBotHardware` from the
  [diffbot_hardware](../diffbot_hardware/README.md) package. Its settings are in the `<hardware>` block of
  `ros2_control.xacro`: `serial_port` (from the launch argument), `baud_rate` 115200, `ticks_per_rev` 2520,
  `wheel_radius` 0.054, `max_speed_rad_s` 10.0, `read_timeout_ms` 5, `vel_filter_alpha` 0.3.
- `sim_mode:=true` (Gazebo) → `gazebo_ros2_control/GazeboSystem`. The `gazebo_ros2_control` plugin at the bottom of
  `ros2_control.xacro` starts the controller manager inside Gazebo and loads `my_controllers.yaml`.

### Controllers

Defined in `config/my_controllers.yaml`, running at 50 Hz:

| Controller | Type | Listens to | Publishes |
|---|---|---|---|
| `diff_cont` | `diff_drive_controller/DiffDriveController` | `/diff_cont/cmd_vel_unstamped` (Twist: forward speed m/s, turn rate rad/s) | `/diff_cont/odom` and TF `odom → base_link` |
| `joint_broad` | `joint_state_broadcaster/JointStateBroadcaster` | the wheel joint states | `/joint_states` |

Important `diff_cont` settings:

| Setting | Value | Meaning |
|---|---|---|
| `wheel_separation` | 0.3 | must equal the URDF |
| `wheel_radius` | 0.054 | must equal the URDF |
| `cmd_vel_timeout` | 0.1 s | stop the wheels if no command arrives for this long |
| `open_loop` | false | odometry comes from the encoders, not from the commands |
| `enable_odom_tf` | true | publish `odom → base_link` |

Check that the controllers are running: `ros2 control list_controllers` → both `active`.

## 4. Who drives the robot (twist_mux)

Three programs can send speed commands. `twist_mux` (config `config/twist_mux.yaml`) picks one and forwards it to
`diff_cont`:

| Topic | Priority | From |
|---|---|---|
| `/cmd_vel_joy` | 100 (highest) | joystick (`use_joystick:=true`) |
| `/cmd_vel_key` | 90 | `ros2 run teleop_twist_keyboard teleop_twist_keyboard --ros-args -r cmd_vel:=/cmd_vel_key` |
| `/cmd_vel` | 10 (lowest) | Nav2 |

The highest-priority topic that published in the last 0.5 s wins. So you can always take control away from Nav2
with the keyboard or joystick. The output goes to `/diff_cont/cmd_vel_unstamped`.

**Common mistake:** running the keyboard teleop without `-r cmd_vel:=/cmd_vel_key`. It then publishes on
`/cmd_vel`, at Nav2's priority. It still drives the robot, but Nav2 and the keyboard fight each other.

## 5. Launch files

| File | Starts | Main arguments (default) |
|---|---|---|
| `launch_robot.launch.py` | robot_state_publisher, twist_mux, ros2_control_node (DiffBotHardware), spawners for both controllers, **YDLIDAR X2 driver**, optional joystick | `serial_port` (/dev/ttyUSB0), `use_lidar` (true), `lidar_port` (/dev/ttyUSB1), `lidar_params_file` (X2.yaml), `use_joystick` (false) |
| `launch_sim.launch.py` | robot_state_publisher (`sim_mode:=true`), twist_mux, Gazebo, spawn_entity, both controllers after spawn, optional joystick | `world` (worlds/turtlebot.world), `use_joystick` (false) |
| `rsp.launch.py` | robot_state_publisher only | `use_sim_time`, `use_ros2_control`, `sim_mode`, `serial_port` |
| `joystick.launch.py` | joy_node + teleop_twist_joy → `/cmd_vel_joy` | `use_sim_time` |
| `online_async_launch.py` | slam_toolbox (online, asynchronous) | `use_sim_time` (**true**), `params_file` |
| `localization_launch.py` | map_server + AMCL | `map` (map/turtlebot_map.yaml), `use_sim_time` (false), `params_file` (nav2_params.yaml) |
| `navigation_launch.py` | Nav2: controller, planner, behaviors, bt_navigator, smoother, velocity_smoother, waypoint_follower → `/cmd_vel` | `use_sim_time` (false), `params_file` (nav2_params.yaml) |

Note the different `use_sim_time` defaults: always pass it explicitly (`false` on the real robot, `true` in Gazebo).

### What happens when you start `launch_robot.launch.py`

1. robot_state_publisher, twist_mux and the lidar driver start immediately.
2. After 3 s, `ros2_control_node` starts. It loads `DiffBotHardware`, which opens the ESP32 serial port and sends a
   zero speed.
3. 2 s after that, the spawners load and activate `joint_broad` and `diff_cont`.

The lidar driver is started as a plain node, not through `ydlidar_launch.py` from the driver package. That file also
publishes its own `base_link → laser_frame` transform, which would clash with `laser_joint` in the URDF.
The lidar node restarts every 3 s if its port is missing, so the robot comes up even with the lidar unplugged.

### Quick commands

```bash
# real robot (port names: workspace README, step 10)
ros2 launch articubot_one launch_robot.launch.py serial_port:=/dev/esp32 lidar_port:=/dev/lidar
# simulation
ros2 launch articubot_one launch_sim.launch.py
# view the model only
ros2 launch articubot_one rsp.launch.py & rviz2 -d ~/autonomous_bot/src/articubot_one/config/view_bot.rviz
# SLAM / localization / navigation (use_sim_time:=true in simulation)
ros2 launch articubot_one online_async_launch.py use_sim_time:=false
ros2 launch articubot_one localization_launch.py use_sim_time:=false map:=<path>/my_map.yaml
ros2 launch articubot_one navigation_launch.py use_sim_time:=false
```

## 6. Config files: what each one does and what you may change

| File | What it controls | Things you may need to change |
|---|---|---|
| `my_controllers.yaml` | `diff_cont` and `joint_broad` | `wheel_separation`, `wheel_radius` if your robot differs |
| `twist_mux.yaml` | priorities of the speed sources | normally nothing |
| `joystick.yaml` | joystick axes, buttons, speeds | `enable_button` (6), `enable_turbo_button` (7), `axis_linear.x` (1), `axis_angular.yaw` (0), `scale_linear.x` (0.5 m/s), `scale_angular.yaw` (0.5 rad/s), to match your controller |
| `mapper_params_online_async.yaml` | slam_toolbox | `mode` (`mapping` or `localization`), `map_file_name`, `map_start_at_dock`; see section 7 |
| `nav2_params.yaml` | AMCL, map_server, Nav2 | `robot_radius` (0.26 m), speed limits (0.26 m/s, 1.0 rad/s) |
| `*.rviz` | RViz window layouts | nothing; add displays in RViz and use File → Save Config As if you want your own |

Which slam_toolbox and Nav2 values to adjust for the real robot, with suggested first values, is explained in
[workspace README, step 14](../README.md#14-tune-slam_toolbox-and-nav2-for-your-robot).

Things to know about these files:

- `mapper_params_online_async.yaml` has `max_laser_range: 8.0` to match the YDLIDAR X2 range, and
  `base_frame: base_footprint`.
- `nav2_params.yaml` uses `robot_radius: 0.26` because the chassis corner is 0.256 m from `base_link`.
  `velocity_smoother` limits Nav2 to 0.26 m/s forward and 1.0 rad/s turning. Raise these slowly once navigation works.

## 7. Mapping and navigation in this package

The full step-by-step procedure is in the workspace README, [step 12 (make a map)](../README.md#12-make-a-map) and
[step 13 (navigate)](../README.md#13-navigate-on-the-map). What you need to know about this package:

**Two kinds of saved map.** slam_toolbox saves its own format (`.posegraph` + `.data`), which it can load again for
localization. AMCL and Nav2's map_server use an image map (`.pgm` + `.yaml`). The `map/` folder has both for
`turtlebot_map`. Save both when you make your own map.

**slam_toolbox has two modes**, set with `mode:` in `mapper_params_online_async.yaml`:

| Mode | What it does | `map_file_name` |
|---|---|---|
| `mapping` | builds a new map while you drive | comment it out for a new map (if set, it continues the old map) |
| `localization` | loads a saved map and finds the robot on it | path to the saved map, **without** extension |

The file is currently set to `mode: localization` with
`map_file_name: $(env AUTONOMOUS_BOT_WS)/src/articubot_one/map/turtlebot_map`. `AUTONOMOUS_BOT_WS` is set in
`~/.bashrc` (see step 5 of the main README) and is expanded by `online_async_launch.py`, so no username is needed. The map is of the Gazebo `turtlebot.world`, so it only fits the simulation.

`map_start_at_dock: true` means "assume the robot starts where the map was started". Put the robot at the place where
you started mapping before launching localization.

**Localization choices for navigation:**

- slam_toolbox in `localization` mode (`online_async_launch.py`), or
- AMCL (`localization_launch.py map:=...yaml`). AMCL needs you to set the starting pose with **2D Pose Estimate** in RViz.

Run only one of them at a time. Both publish the `map → odom` transform.

## 8. Common changes

**Different wheel size or wheel spacing.** Measure them, then change all of:

- `description/robot_core.xacro`: `wheel_radius`, `wheel_offset_y` (half the separation)
- `description/ros2_control.xacro`: `wheel_radius` in the real-robot `<hardware>` block
- `config/my_controllers.yaml`: `wheel_radius`, `wheel_separation`

**Different encoder or gearbox.** Measure `ticks_per_rev` (see [diffbot_hardware commissioning](../diffbot_hardware/README.md#commissioning-first-power-up-wheels-off-the-ground)),
then set it in `ros2_control.xacro` **and** in the firmware `TICKS_PER_REV`.

**Lidar mounted somewhere else.** Edit `laser_joint` in `description/lidar.xacro`: `xyz` is the position from the
chassis origin in metres (x forward, y left, z up), `rpy` the rotation in radians. If the scan in RViz is turned
compared to the real walls, change the last `rpy` number (yaw), e.g. `3.14159` if the lidar faces backwards.

**Robot is bigger than before, or carries luggage that sticks out.** Update `robot_radius` (or use a `footprint`
polygon) in `config/nav2_params.yaml`, in both costmaps, so Nav2 keeps enough distance from walls. How to measure it:
[workspace README, step 14.1](../README.md#141-how-to-work-out-the-robots-size-from-base_link).

**Different joystick.** Run `ros2 run joy joy_node` and `ros2 topic echo /joy`, press buttons and move sticks to find
their numbers, then update `config/joystick.yaml`.
