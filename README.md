# Autonomous Bot

A two-wheeled robot that carries luggage. It can be driven with a keyboard or joystick, build a map of a room
with its lidar, and then drive itself to a point you click on that map.

This guide assumes you are new to ROS 2 and robotics. Follow it from top to bottom and you will go from an
empty laptop to a robot that navigates on its own. Every command you need is written out. When something is
specific to this robot (a pin number, a port name, a file to edit) it says so.

## Clone the repository

This repository is the `src/` folder of a ROS 2 workspace, so it must be cloned **into** a folder named `src`
inside the workspace root. The workspace root is `~/autonomous_bot`:

```bash
mkdir -p ~/autonomous_bot
cd ~/autonomous_bot
git clone https://github.com/roganjo/Autonomous_bot.git src
ls ~/autonomous_bot/src
# README.md  YDLidar-SDK  articubot_one  diffbot_hardware  ydlidar_ros2_driver
```

Do not clone it as `~/autonomous_bot/Autonomous_bot` or straight into `~/autonomous_bot`. `colcon build` is run
from `~/autonomous_bot` and every path in this guide assumes the code is at `~/autonomous_bot/src`.

---

## Contents

- [Clone the repository](#clone-the-repository)
- [How the documentation is organised](#how-the-documentation-is-organised)
- [The project in 13 steps](#the-project-in-14-steps)
- **Part A – Understand the robot**
  - [A1. Hardware you need](#a1-hardware-you-need)
  - [A2. ROS 2 words used in this guide](#a2-ros-2-words-used-in-this-guide)
  - [A3. What is in this workspace](#a3-what-is-in-this-workspace)
  - [A4. How the pieces work together](#a4-how-the-pieces-work-together)
  - [A5. Launch files](#a5-launch-files)
- **Part B – Set up the PC**
  - [1. Install Ubuntu 22.04 and ROS 2 Humble](#1-install-ubuntu-2204-and-ros-2-humble)
  - [2. Put the code in place](#2-put-the-code-in-place)
  - [3. Install the dependencies](#3-install-the-dependencies)
  - [4. Build and install the YDLidar SDK](#4-build-and-install-the-ydlidar-sdk)
  - [5. Build the workspace](#5-build-the-workspace)
- **Part C – Try it in simulation**
  - [6. Drive the robot in Gazebo](#6-drive-the-robot-in-gazebo)
- **Part D – The real robot**
  - [7. Build and wire the hardware](#7-build-and-wire-the-hardware) (layout, connections, from simulation to real)
  - [8. Flash the ESP32 with the Arduino IDE](#8-flash-the-esp32-with-the-arduino-ide)
  - [9. Commission the drive base](#9-commission-the-drive-base)
  - [10. Serial ports: which one is the ESP32 and which one is the lidar](#10-serial-ports-which-one-is-the-esp32-and-which-one-is-the-lidar)
  - [11. Drive the real robot](#11-drive-the-real-robot)
- **Part E – Maps and autonomous navigation**
  - [12. Make a map](#12-make-a-map)
  - [13. Navigate on the map](#13-navigate-on-the-map)
  - [14. Tune slam_toolbox and Nav2 for your robot](#14-tune-slam_toolbox-and-nav2-for-your-robot)
- **Reference**
  - [15. Values that must agree](#15-values-that-must-agree)
  - [16. Troubleshooting](#16-troubleshooting)
  - [17. Verification status](#17-verification-status)

---

## How the documentation is organised

There is one README per folder. Read them in this order:

| # | File | What you learn from it |
|---|---|---|
| 1 | **This file** (`src/README.md`) | The whole project, step by step. Start here and come back here. |
| 2 | [articubot_one/README.md](articubot_one/README.md) | The robot model (URDF), every config file, every launch file, mapping and navigation in detail. |
| 3 | [diffbot_hardware/README.md](diffbot_hardware/README.md) | The electronics: wiring, the ESP32 firmware, the serial protocol, commissioning, hardware troubleshooting. |
| 4 | [ydlidar_ros2_driver/README.md](ydlidar_ros2_driver/README.md) | Vendor documentation for the lidar ROS driver. Only the note at the top matters for this project. |
| 5 | [YDLidar-SDK/README.md](YDLidar-SDK/README.md) | Vendor documentation for the lidar library. Only the note at the top matters for this project. |

You do not need to read the C++ or Python code to complete the project. You will edit a few settings in
config files and in the firmware; each one is pointed out where it is needed.

---

## The project in 14 steps

Tick these off as you go. Each step links to its section.

| Step | What you do | Done when |
|---|---|---|
| [1](#1-install-ubuntu-2204-and-ros-2-humble) | Install Ubuntu 22.04 and ROS 2 Humble | `ros2 --help` works |
| [2](#2-put-the-code-in-place) | Put this folder at `~/autonomous_bot/src` | `ls ~/autonomous_bot/src` shows the four package folders |
| [3](#3-install-the-dependencies) | Install the dependencies | the check in 3.3 prints only `OK` lines |
| [4](#4-build-and-install-the-ydlidar-sdk) | Build the lidar library | `/usr/local/lib/libydlidar_sdk.a` exists |
| [5](#5-build-the-workspace) | Build the ROS packages | `colcon build` finishes with no failed packages |
| [6](#6-drive-the-robot-in-gazebo) | Drive the robot in simulation | the robot moves in Gazebo when you press keys |
| [7](#7-build-and-wire-the-hardware) | Build and wire the robot | wiring matches the pin table |
| [8](#8-flash-the-esp32-with-the-arduino-ide) | Flash the ESP32 | Serial Monitor shows `# Ready` and `E0,0` lines |
| [9](#9-commission-the-drive-base) | Check motor and encoder directions | both wheels go forward on `W2,2` and both counts go up |
| [10](#10-serial-ports-which-one-is-the-esp32-and-which-one-is-the-lidar) | Give the ESP32 and lidar fixed port names | `/dev/esp32` and `/dev/lidar` exist |
| [11](#11-drive-the-real-robot) | Drive the real robot from the keyboard | robot drives, `/scan` at ~10 Hz |
| [12](#12-make-a-map) | Make a map of your room | map files saved in `articubot_one/map/` |
| [13](#13-navigate-on-the-map) | Navigate autonomously | robot drives to a goal you click in RViz |
| [14](#14-tune-slam_toolbox-and-nav2-for-your-robot) | Tune the parameters for your robot | robot reaches goals smoothly, no false obstacles |

Do step 6 (simulation) before touching the hardware. It proves the software side works, and you will know what
"correct" looks like before you power the motors.

---

# Part A – Understand the robot

## A1. Hardware you need

| Part | Quantity | Used for | Notes |
|---|---|---|---|
| Laptop or mini-PC with **Ubuntu 22.04** | 1 | runs ROS 2, rides on the robot | 2 free USB ports (ESP32 + lidar) |
| **ESP32 DevKit** (classic ESP32, ESP32-WROOM-32) | 1 | low-level motor control, reads encoders | not ESP32-S2/S3/C3: the pin numbers are for the classic chip |
| **BTS7960** motor driver board (43 A H-bridge) | 2 | one per motor | |
| DC gear motor **with quadrature encoder** | 2 | left and right drive wheels | the code assumes 2520 encoder ticks per wheel turn; you will measure yours in step 9 |
| Wheels, radius **0.054 m** (108 mm diameter) | 2 | | if yours differ, change the values listed in [section 15](#15-values-that-must-agree) |
| Caster wheel | 1 | third support point | |
| **YDLIDAR X2** with its USB adapter board | 1 | 360° laser scanner for mapping | |
| Motor battery | 1 | powers the BTS7960 boards | voltage to match your motors (often 12 V) |
| USB data cables | 2 | PC ↔ ESP32, PC ↔ lidar | some cables only charge; use data cables |
| Pull-up resistors 4.7 kΩ | 4 | encoder signals | only if your encoders are open-collector |
| Optional: game controller | 1 | joystick driving | any controller `joy` recognises |

The chassis dimensions in the robot model are 0.33 × 0.24 × 0.135 m with the wheels 0.30 m apart.

## A2. ROS 2 words used in this guide

You will see these words everywhere. You don't need to understand them deeply to finish the project.

| Word | What it means here |
|---|---|
| **ROS 2** | The robotics framework the PC software is built on. We use the **Humble** release, which runs on Ubuntu 22.04. |
| **Workspace** | The folder `~/autonomous_bot`. Source code lives in `src/`; building creates `build/`, `install/` and `log/` next to it. |
| **Package** | One folder in `src/` with a `package.xml`. This workspace has three ROS packages plus one plain library. |
| **colcon** | The build tool. `colcon build` compiles every package in the workspace. |
| **`source install/setup.bash`** | Tells the current terminal where the built packages are. **Run it in every new terminal**, or ROS will say "package not found". |
| **Node** | A running program, for example the lidar driver. |
| **Topic** | A named channel that nodes publish messages on, for example `/scan` (laser data) or `/cmd_vel` (speed commands). |
| **Launch file** | A Python file that starts many nodes at once. You start one with `ros2 launch <package> <file>`. |
| **Parameter / YAML file** | Settings for a node, usually stored in a `.yaml` file in `config/`. |
| **URDF / xacro** | The robot's description: its parts, sizes and how they are connected. Written in xacro (XML with variables) in `articubot_one/description/`. |
| **TF (transforms)** | Where every part of the robot is, relative to every other part, updated live. Frames used here: `map`, `odom`, `base_link`, `chassis`, `laser_frame`. |
| **ros2_control** | The ROS framework that connects controllers to hardware. |
| **Controller** | Here `diff_cont`: turns "drive at 0.2 m/s, turn at 0.5 rad/s" into a speed for each wheel, and turns wheel rotation back into odometry. |
| **Hardware plugin** | Here `DiffBotHardware`: the bridge between ros2_control and the ESP32 over USB serial. |
| **Odometry** | The robot's estimate of how far it has moved, from counting wheel rotations. It drifts over time. |
| **SLAM** | Building a map while working out where you are on it. Done by **slam_toolbox**. |
| **AMCL** | Working out where you are on an existing map. An alternative to slam_toolbox's localization mode. |
| **Nav2** | The navigation stack: plans a path to a goal and drives the robot along it, avoiding obstacles. |
| **RViz** | The 3D viewer: shows the robot model, laser scan, map, planned path. You also click goals in it. |
| **Gazebo** | The physics simulator. Runs a virtual copy of the robot. |
| **`use_sim_time`** | `true` when running in Gazebo (use the simulator's clock), `false` on the real robot. Getting this wrong is a common cause of "nothing happens". |

## A3. What is in this workspace

```
~/autonomous_bot/                  ← the workspace
└── src/                        ← this folder
    ├── README.md               ← this guide
    ├── YDLidar-SDK/            ← lidar library (plain C++, not ROS)
    ├── ydlidar_ros2_driver/    ← ROS node that publishes /scan from the lidar
    ├── diffbot_hardware/       ← ros2_control plugin + ESP32 firmware
    └── articubot_one/          ← the robot: model, configs, launch files, maps, worlds  ← you launch this
```

| Folder | What it does | Needed for |
|---|---|---|
| `YDLidar-SDK/` | Vendor C/C++ library that talks to YDLIDAR sensors. Plain CMake, **not** a ROS package. Built and installed to `/usr/local` first (step 4). | real robot |
| `ydlidar_ros2_driver/` | ROS 2 node that uses the SDK and publishes `/scan`. Per-model settings in `params/`; the robot uses `X2.yaml`. | real robot |
| `diffbot_hardware/` | The **ros2_control hardware plugin** `diffbot_hardware/DiffBotHardware`: opens the ESP32 serial port, sends wheel speed commands, reads encoder ticks. Also holds the **ESP32 firmware** (`firmware/diffbot_firmware/`). | real robot (must still be built for simulation, because `articubot_one` depends on it) |
| `articubot_one/` | Robot model, controller config, launch files for the real robot and Gazebo, SLAM Toolbox and Nav2 config, saved map. **This is the package you launch.** | everything |

Build order: `YDLidar-SDK` → `ydlidar_ros2_driver`, and `diffbot_hardware` → `articubot_one`. colcon works out the
order of the ROS packages by itself; you only build the SDK by hand.

`diffbot_hardware` also contains its own `launch/`, `urdf/` and `config/` folders. They are a standalone bench test
for a smaller robot and are **not** used by `articubot_one`. Ignore them unless you are testing the plugin alone.

## A4. How the pieces work together

### In plain words

1. You (keyboard or joystick) or Nav2 publish a speed: "go forward at 0.2 m/s, turn at 0.5 rad/s".
2. **twist_mux** picks one source if several are talking. The joystick wins over the keyboard, the keyboard wins over Nav2.
3. **diff_cont** converts that into a speed for each wheel in rad/s.
4. **DiffBotHardware** sends the wheel speeds to the ESP32 over USB, 50 times a second, as text like `W3.70,3.70`.
5. The **ESP32** turns that into PWM signals for the two **BTS7960** boards, which drive the motors.
6. The ESP32 counts **encoder** pulses and sends them back as `E12345,12340`, 50 times a second.
7. **diff_cont** turns the wheel rotation into **odometry**: the robot's position as seen from its wheels.
8. Separately, the **lidar** publishes `/scan`. **slam_toolbox** combines scan and odometry to build a map and
   correct the odometry drift. **Nav2** uses the map and scan to plan and follow paths.

### Diagram (real robot)

```
 keyboard ── /cmd_vel_key ─┐
 joystick ── /cmd_vel_joy ─┼─► twist_mux ─► /diff_cont/cmd_vel_unstamped
 Nav2 ────── /cmd_vel ─────┘   (joy 100 > keyboard 90 > Nav2 10)
                                         │
                                         ▼
                     diff_cont (diff_drive_controller): v, ω  →  wheel rad/s
                                         │
                                         ▼
                ros2_control_node + DiffBotHardware plugin (diffbot_hardware)
                    │  "W<left>,<right>\n" every 20 ms                 ▲  "E<left>,<right>\n" at 50 Hz
                    ▼              USB serial 115200                    │
                  ESP32 firmware ──► BTS7960 ──► motors      encoders ──┘

 encoder ticks ─► DiffBotHardware ─► joint_broad ─► /joint_states ─► robot_state_publisher ─► /tf (wheels)
                                  └► diff_cont ───► /diff_cont/odom  +  TF odom → base_link

 YDLIDAR X2 ─► ydlidar_ros2_driver_node ─► /scan (frame laser_frame, fixed to chassis in the URDF)
 /scan + /tf ─► slam_toolbox (map → odom, /map)   or   map_server + AMCL
            ─► Nav2 ─► /cmd_vel ─► twist_mux (above)
```

### Safety stops built in

- If ROS stops sending commands (crash, cable pulled), the **ESP32 stops the motors after 0.5 s**.
- If nobody publishes a speed, **diff_cont sends zero after 0.1 s**.
- So releasing the keyboard key, or pressing Ctrl+C on the launch, stops the robot.

### Simulation

The simulation uses the same topics and controllers. Only two things are swapped out:

- the URDF is built with `sim_mode:=true`, so ros2_control uses `gazebo_ros2_control/GazeboSystem` instead of
  `DiffBotHardware` (no ESP32, no serial);
- `/scan` comes from a simulated lidar in `lidar.xacro` instead of the YDLIDAR driver.

Everything after that (`twist_mux`, SLAM, Nav2) is identical. The only thing you change is `use_sim_time:=true`.

## A5. Launch files

All launch files are in `articubot_one/launch/`. Details of each are in [articubot_one/README.md](articubot_one/README.md).

| Launch file | Starts | Use it for |
|---|---|---|
| `launch_robot.launch.py` | robot_state_publisher, twist_mux, ros2_control_node (DiffBotHardware), spawners for `joint_broad` and `diff_cont`, **YDLIDAR driver**, optional joystick | Real robot. One command brings up everything except SLAM/Nav2. |
| `launch_sim.launch.py` | robot_state_publisher, twist_mux, Gazebo + world, spawn_entity, the same two controllers, optional joystick | Simulation |
| `rsp.launch.py` | robot_state_publisher only (URDF from xacro) | Included by the two above. Alone: view the model in RViz. |
| `joystick.launch.py` | joy_node + teleop_twist_joy → `/cmd_vel_joy` | Included with `use_joystick:=true`, or run alone |
| `online_async_launch.py` | slam_toolbox (async) with `config/mapper_params_online_async.yaml` | Mapping, or localization on a saved slam_toolbox map |
| `localization_launch.py` | map_server + AMCL with `config/nav2_params.yaml` | Localization on a saved `.pgm/.yaml` map |
| `navigation_launch.py` | Nav2 (controller, planner, behaviors, bt_navigator, smoother, velocity_smoother, waypoint_follower) → `/cmd_vel` | Autonomous navigation |

`launch_robot.launch.py` arguments (write them after the launch file name as `name:=value`):

| Argument | Default | Meaning |
|---|---|---|
| `serial_port` | `/dev/ttyUSB0` | ESP32 port (use `/dev/esp32` after [step 10](#10-serial-ports-which-one-is-the-esp32-and-which-one-is-the-lidar)) |
| `use_lidar` | `true` | start the YDLIDAR driver |
| `lidar_port` | `/dev/ttyUSB1` | YDLIDAR port (use `/dev/lidar` after step 10) |
| `lidar_params_file` | `ydlidar_ros2_driver/params/X2.yaml` | lidar model settings |
| `use_joystick` | `false` | start joy + teleop_twist_joy |

`launch_sim.launch.py` arguments: `world` (default `worlds/turtlebot.world`), `use_joystick` (default `false`).

---

# Part B – Set up the PC

## 1. Install Ubuntu 22.04 and ROS 2 Humble

ROS 2 Humble needs **Ubuntu 22.04** exactly. Newer Ubuntu versions will not work with these instructions.

1. Install Ubuntu 22.04 Desktop (64-bit) on the laptop. Dual-boot is fine; a virtual machine works for simulation
   but makes USB devices (ESP32, lidar) awkward.
2. Install ROS 2 Humble Desktop (this is the official procedure from docs.ros.org, shortened):

   ```bash
   sudo apt update && sudo apt install -y software-properties-common curl
   sudo add-apt-repository universe -y
   sudo curl -sSL https://raw.githubusercontent.com/ros/rosdistro/master/ros.key \
     -o /usr/share/keyrings/ros-archive-keyring.gpg
   echo "deb [arch=$(dpkg --print-architecture) signed-by=/usr/share/keyrings/ros-archive-keyring.gpg] \
   http://packages.ros.org/ros2/ubuntu $(. /etc/os-release && echo $UBUNTU_CODENAME) main" \
     | sudo tee /etc/apt/sources.list.d/ros2.list > /dev/null
   sudo apt update && sudo apt upgrade -y
   sudo apt install -y ros-humble-desktop
   ```

   If anything here fails, follow the full guide at
   <https://docs.ros.org/en/humble/Installation/Ubuntu-Install-Debs.html>.

3. Make every new terminal load ROS automatically:

   ```bash
   echo "source /opt/ros/humble/setup.bash" >> ~/.bashrc
   source ~/.bashrc
   ros2 --help        # should print the list of ros2 commands
   ```

## 2. Put the code in place

The workspace must be at **`~/autonomous_bot`**, with this folder as `~/autonomous_bot/src`. Several commands in this
guide, and one config file, use that path.

```bash
mkdir -p ~/autonomous_bot
cd ~/autonomous_bot
git clone https://github.com/roganjo/Autonomous_bot.git src
ls ~/autonomous_bot/src
# README.md  YDLidar-SDK  articubot_one  diffbot_hardware  ydlidar_ros2_driver
```

`map_file_name` in `articubot_one/config/mapper_params_online_async.yaml` is built from the `AUTONOMOUS_BOT_WS`
variable, which you add to `~/.bashrc` in [step 5](#5-build-the-workspace). If you put the workspace somewhere else,
point that variable at it.

## 3. Install the dependencies

This step installs everything the project needs on the PC. The tables say what each package is for, so you know
why it is there. The commands to run are after the tables.

### 3.1 Everything this project uses

**Ubuntu system packages (apt)**

| Package | Why it is needed |
|---|---|
| `build-essential`, `cmake`, `pkg-config`, `git` | compilers and build tools for the C++ code |
| `python3-dev` | Python headers, used when building the lidar SDK |
| `swig` | optional: Python bindings of the lidar SDK (not used by ROS) |
| `libboost-system-dev` | Boost.Asio, used by the hardware plugin's serial port code |
| `python3-colcon-common-extensions` | `colcon`, the ROS 2 build tool |
| `python3-rosdep` | `rosdep`, checks and installs ROS dependencies from `package.xml` files |
| `libfuse2` | needed to start the Arduino IDE AppImage (step 8) |

**ROS 2 Humble packages (apt, all named `ros-humble-...`)**

| apt package | Provides | Used by |
|---|---|---|
| `ros-humble-desktop` | ROS 2 core, RViz, demo tools (installed in step 1) | everything |
| `ros-humble-xacro` | turns the `.xacro` robot description into URDF | `rsp.launch.py` |
| `ros-humble-robot-state-publisher` | publishes the robot's TF frames from the URDF | `rsp.launch.py` |
| `ros-humble-ros2-control` | `controller_manager`, `hardware_interface`, `ros2_control_node`, the `ros2 control` command | `launch_robot.launch.py`, `diffbot_hardware` (build) |
| `ros-humble-ros2-controllers` | `diff_drive_controller`, `joint_state_broadcaster` | `my_controllers.yaml` |
| `ros-humble-twist-mux` | picks keyboard / joystick / Nav2 speed commands | robot and sim launch |
| `ros-humble-teleop-twist-keyboard` | drive with the keyboard | steps 6 and 11 |
| `ros-humble-joy`, `ros-humble-teleop-twist-joy` | drive with a game controller | `joystick.launch.py` |
| `ros-humble-slam-toolbox` | mapping and localization | `online_async_launch.py` |
| `ros-humble-navigation2`, `ros-humble-nav2-bringup` | Nav2: planner, controller, AMCL, map_server, map_saver | `navigation_launch.py`, `localization_launch.py`, step 12 |
| `ros-humble-gazebo-ros-pkgs` | Gazebo Classic 11 and its ROS bridge (`gazebo_ros`) | `launch_sim.launch.py` |
| `ros-humble-gazebo-ros2-control` | ros2_control inside Gazebo | `ros2_control.xacro` with `sim_mode:=true` |
| `ros-humble-turtlebot3-gazebo` | wall meshes used by the default world `turtlebot.world` | `launch_sim.launch.py` |
| `ros-humble-tf2-tools` | `view_frames`, to draw the TF tree | troubleshooting |

**Built from this workspace (no install needed, steps 4 and 5)**

| Package | What it is |
|---|---|
| `YDLidar-SDK` | lidar library, built with CMake and installed to `/usr/local` (step 4) |
| `ydlidar_ros2_driver` | lidar ROS driver, built by colcon (step 5) |
| `diffbot_hardware` | ESP32 hardware plugin, built by colcon (step 5) |
| `articubot_one` | the robot package, built by colcon (step 5) |

**For the ESP32 (installed in step 8)**

| Item | Version |
|---|---|
| Arduino IDE | 2.x |
| Board package "esp32 by Espressif Systems" | **3.3.12** (must be 3.x) |
| Arduino libraries | none |

### 3.2 Install commands

**Everything in one command.** Copy this whole line into a terminal (ROS 2 Humble from step 1 must already be
installed, and this folder must be at `~/autonomous_bot/src`). It asks for your password once and takes 10–30 minutes
depending on your internet connection:

```bash
sudo apt update && sudo apt install -y build-essential cmake pkg-config git python3-dev swig libboost-system-dev python3-colcon-common-extensions python3-rosdep libfuse2 ros-humble-xacro ros-humble-robot-state-publisher ros-humble-ros2-control ros-humble-ros2-controllers ros-humble-twist-mux ros-humble-teleop-twist-keyboard ros-humble-joy ros-humble-teleop-twist-joy ros-humble-slam-toolbox ros-humble-navigation2 ros-humble-nav2-bringup ros-humble-gazebo-ros-pkgs ros-humble-gazebo-ros2-control ros-humble-turtlebot3-gazebo ros-humble-tf2-tools && source /opt/ros/humble/setup.bash && (sudo rosdep init 2>/dev/null || true) && rosdep update && rosdep install --from-paths ~/autonomous_bot/src --ignore-src -r -y && (grep -q turtlebot3_gazebo/models ~/.bashrc || echo 'export GAZEBO_MODEL_PATH=$GAZEBO_MODEL_PATH:/opt/ros/humble/share/turtlebot3_gazebo/models' >> ~/.bashrc) && sudo usermod -aG dialout $USER && sudo apt remove -y brltty
```

It does exactly what the five blocks below do. When it finishes, **log out and log back in**, then run the check in 3.3.
If it stops with an error, run the blocks below one at a time to see which part failed.

**The same, step by step.** Run these in a terminal, one block at a time. They need your password for `sudo` and an internet connection.
The download is about 1–2 GB.

```bash
# 1. System tools
sudo apt update
sudo apt install -y build-essential cmake pkg-config git python3-dev swig \
  libboost-system-dev python3-colcon-common-extensions python3-rosdep libfuse2

# 2. ROS 2 packages used by this project
sudo apt install -y \
  ros-humble-xacro ros-humble-robot-state-publisher \
  ros-humble-ros2-control ros-humble-ros2-controllers \
  ros-humble-twist-mux ros-humble-teleop-twist-keyboard \
  ros-humble-joy ros-humble-teleop-twist-joy \
  ros-humble-slam-toolbox ros-humble-navigation2 ros-humble-nav2-bringup \
  ros-humble-gazebo-ros-pkgs ros-humble-gazebo-ros2-control ros-humble-turtlebot3-gazebo \
  ros-humble-tf2-tools

# 3. rosdep: double-checks that nothing listed in the package.xml files is missing
source /opt/ros/humble/setup.bash
sudo rosdep init 2>/dev/null || true     # "already initialized" is fine
rosdep update
cd ~/autonomous_bot
rosdep install --from-paths src --ignore-src -r -y

# 4. Let Gazebo find the turtlebot3 world meshes (every new terminal)
echo 'export GAZEBO_MODEL_PATH=$GAZEBO_MODEL_PATH:/opt/ros/humble/share/turtlebot3_gazebo/models' >> ~/.bashrc

# 5. Serial port access for the ESP32 and the lidar
sudo usermod -aG dialout $USER
sudo apt remove -y brltty                # it grabs CH340 USB-serial chips on Ubuntu 22.04
```

**Log out and log back in** after the last block, so your user is really in the `dialout` group.

### 3.3 Check the installation

```bash
source /opt/ros/humble/setup.bash
for p in xacro robot_state_publisher controller_manager hardware_interface diff_drive_controller \
         joint_state_broadcaster twist_mux teleop_twist_keyboard joy teleop_twist_joy slam_toolbox \
         nav2_bringup nav2_map_server gazebo_ros gazebo_ros2_control turtlebot3_gazebo tf2_tools; do
  ros2 pkg prefix $p >/dev/null 2>&1 && echo "OK       $p" || echo "MISSING  $p"
done
which gzserver colcon rosdep          # all three must print a path
groups | grep -q dialout && echo "dialout OK" || echo "log out and in again"
```

Every line must say `OK`. If one says `MISSING`, install it with `sudo apt install ros-humble-<name>`, writing `_` as `-`
(for example `nav2_map_server` → `ros-humble-nav2-map-server`).

## 4. Build and install the YDLidar SDK

The lidar driver needs this library. It is not a ROS package, so it is built with plain CMake and installed
system-wide to **`/usr/local`** (the driver does not search anywhere else).

```bash
cd ~/autonomous_bot/src/YDLidar-SDK
rm -rf build && mkdir build && cd build
cmake ..
make -j$(nproc)
sudo make install
sudo ldconfig
```

Check that it worked:

```bash
ls /usr/local/lib/libydlidar_sdk.a /usr/local/lib/cmake/ydlidar_sdk/
```

Both must exist. `YDLidar-SDK/` contains an empty `COLCON_IGNORE` file, so `colcon build` skips it and the driver
uses the copy in `/usr/local`. **Don't delete that file.**

Optional test without ROS, once the lidar is plugged in: from `YDLidar-SDK/build` run `./tri_test`, then choose
the lidar's port, baudrate `115200`, one-way communication `yes`, frequency `10`.

## 5. Build the workspace

```bash
cd ~/autonomous_bot
source /opt/ros/humble/setup.bash
colcon build --symlink-install
source install/setup.bash
```

The build takes a few minutes the first time. It must end with `Summary: 3 packages finished` and no failed
packages. Some `stderr output` warnings from the vendor lidar driver are normal.

Make every new terminal load the workspace and know where it is:

```bash
echo 'export AUTONOMOUS_BOT_WS=$HOME/autonomous_bot' >> ~/.bashrc
echo 'source $AUTONOMOUS_BOT_WS/install/setup.bash' >> ~/.bashrc
source ~/.bashrc
echo $AUTONOMOUS_BOT_WS     # should print /home/<your-user>/autonomous_bot
```

`AUTONOMOUS_BOT_WS` is the workspace folder. `config/mapper_params_online_async.yaml` uses it
(`$(env AUTONOMOUS_BOT_WS)`) for the saved map path, so nobody has to type their username into that file. If it is
not set, `online_async_launch.py` stops with an error about `AUTONOMOUS_BOT_WS`.

**Keep your robot separate from other ROS computers on the same network** (important in a lab or on a shared
Wi-Fi). By default ROS 2 and Gazebo talk to every other ROS 2 / Gazebo computer on the network. If someone else is
running a simulation nearby, their nodes, their `/clock` and their Gazebo mix with yours. Symptoms seen on this
project: `robot_state_publisher` printing *"Moved backwards in time"*, the controllers not loading, and Gazebo
crashing with `Resource temporarily unavailable (src/zmtp_engine.cpp:446)`.

Everything for this robot runs on one computer, so limit ROS and Gazebo to this computer:

```bash
cat >> ~/.bashrc <<'EOF'
export ROS_LOCALHOST_ONLY=1        # ROS 2 only talks to nodes on this computer
export GAZEBO_IP=127.0.0.1         # Gazebo only talks to this computer
export IGN_IP=127.0.0.1
export IGN_PARTITION=autonomous_bot_$USER
EOF
source ~/.bashrc
ros2 daemon stop                   # restart the ROS discovery helper with the new setting
```

Check: with nothing of yours running, `ros2 node list` must print nothing. If you later want to watch the robot
from a second computer (e.g. RViz on a desktop), remove `ROS_LOCALHOST_ONLY` on both and instead give both the same
unused domain number, e.g. `export ROS_DOMAIN_ID=42` (any number 1–101 nobody else on the network uses).

Good to know:

- `--symlink-install` links config, launch and map files instead of copying them. After editing a `.yaml`,
  `.py` launch or `.xacro` file you only need to restart the launch, **not** rebuild.
- After changing C++ code (`diffbot_hardware/src/`) you must rebuild:
  `colcon build --symlink-install --packages-select diffbot_hardware`.
- If a build gets into a strange state: `rm -rf ~/autonomous_bot/build ~/autonomous_bot/install` and build again.

---

# Part C – Try it in simulation

## 6. Drive the robot in Gazebo

No hardware needed. Open two terminals.

```bash
# Terminal 1 – Gazebo with the robot in the default world
ros2 launch articubot_one launch_sim.launch.py
```

The first start can take a minute while Gazebo loads. You should see a room with hexagonal walls and the robot in it.

```bash
# Terminal 2 – keyboard driving
ros2 run teleop_twist_keyboard teleop_twist_keyboard --ros-args -r cmd_vel:=/cmd_vel_key
```

Keep terminal 2 selected and use the keys printed on screen: `i` forward, `,` backward, `j`/`l` turn left/right,
`k` stop, `q`/`z` faster/slower. The `-r cmd_vel:=/cmd_vel_key` part matters: it sends the commands to the keyboard
input of twist_mux. Without it the robot will not move.

Check from a third terminal:

```bash
ros2 control list_controllers      # joint_broad and diff_cont: active
ros2 topic hz /scan                # simulated lidar, ~10 Hz
rviz2 -d ~/autonomous_bot/src/articubot_one/config/drive_bot.rviz   # robot + scan in RViz
```

Other worlds: `ros2 launch articubot_one launch_sim.launch.py world:=$HOME/autonomous_bot/src/articubot_one/worlds/obstacles.world`
(cones and barrels) or `.../empty.world`.

Once this works you can also try [mapping](#12-make-a-map) and [navigation](#13-navigate-on-the-map) in simulation:
use `use_sim_time:=true` everywhere.

---

# Part D – The real robot

## 7. Build and wire the hardware

This section is about going from the simulated robot to the real one: how the robot should be laid out, what plugs
into what, and what changes in the software. Pin-level details are in
[diffbot_hardware/README.md](diffbot_hardware/README.md#wiring).

### 7.1 Physical layout

```
                  FRONT (+x, direction of travel)
        ┌──────────────────────────────────┐
        │            ( LIDAR )             │  ← on top, clear 360° view, cable pointing backwards
        │                                  │
  ┌───┐ │   ESP32      laptop / mini-PC    │ ┌───┐
  │ L │═╪═  BTS7960 L  (on the deck)       ╪═│ R │   ← drive wheels on one axle; base_link is
  │   │ │   BTS7960 R                      │ │   │     the midpoint between them
  └───┘ │                                  │ └───┘
        │   battery (low, near the axle)   │
        │                ○ caster          │
        └──────────────────────────────────┘
                  BACK
```

Rules that matter for mapping and navigation:

- **Front is +x.** The lidar, the face and "forward" on the keyboard (`i`) must all point the same way.
- **Lidar on top, nothing in its scan plane.** The X2 scans a single flat plane about **0.26 m above the floor**
  (0.212 m above the chassis). The laptop screen, a luggage handle, cables or a mast in that plane show up as a
  permanent obstacle right next to the robot, and Nav2 refuses to move. Keep the plane clear, or see
  `obstacle_min_range` in [step 14](#14-tune-slam_toolbox-and-nav2-for-your-robot).
- **The lidar only sees at its own height.** Objects lower than ~0.26 m (shoes, bags on the floor, chair legs
  between rails) are invisible to it. Test in a space where obstacles reach that height.
- **Lidar position must match the model.** If you mount it somewhere other than 0.25 m forward from the back edge
  of the chassis and 0.212 m up, change `laser_joint` in `articubot_one/description/lidar.xacro`.
  If it is mounted rotated, change the yaw there too.
- **Heavy parts low and near the axle** (battery, luggage) so the robot doesn't tip when it brakes, and the caster
  doesn't carry most of the weight.
- **Measure the real robot**: wheel diameter, distance between wheel centres, the outline of the robot including
  luggage. You need these numbers in [section 15](#15-values-that-must-agree) and step 14.

### 7.2 How everything connects

```
                    ┌──────────────── Laptop / mini-PC (Ubuntu 22.04 + ROS 2) ────────────────┐
                    │  USB port A                                       USB port B           │
                    └─────┬─────────────────────────────────────────────────┬────────────────┘
                          │ USB data cable (power + serial, 115200)         │ USB data cable
                          ▼                                                 ▼
                    ┌───────────┐                                 ┌──────────────────┐   ribbon/
                    │  ESP32    │                                 │ YDLIDAR USB      │──  connector ──► YDLIDAR X2
                    │  DevKit   │                                 │ adapter board    │   (power + data)
                    └─┬───┬───┬─┘                                 └──────────────────┘
          GPIO 25,26,27│   │GPIO 32,33,14                → /dev/lidar
                       ▼   ▼
              ┌─────────┐ ┌─────────┐        → the ESP32 is /dev/esp32
              │BTS7960 L│ │BTS7960 R│
              └──┬───▲──┘ └──┬───▲──┘
          M+/M−  │   │ B+/B− │   │ B+/B−
                 ▼   │       ▼   │
           left motor│  right motor
                 │   └───────┴───┴──── Motor battery (+ through a switch/fuse)
                 │                              │ battery −
   encoder A/B ──┘ → GPIO 34/35                 └──── common GND ──── ESP32 GND
   right encoder A/B → GPIO 36/39
```

| Connection | From | To | Notes |
|---|---|---|---|
| USB | PC | ESP32 | powers the ESP32 and carries `W`/`E` messages |
| USB | PC | YDLIDAR adapter board | powers the lidar and carries scan data; use a port that gives enough current (not an unpowered hub) |
| PWM + enable | ESP32 GPIO 25/26/27 | left BTS7960 RPWM/LPWM/(R_EN+L_EN) | |
| PWM + enable | ESP32 GPIO 32/33/14 | right BTS7960 RPWM/LPWM/(R_EN+L_EN) | |
| Logic supply | ESP32 3V3 | both BTS7960 VCC | |
| Motor power | battery + / − | both BTS7960 B+ / B− | through a switch or fuse, thick wires |
| Motor output | BTS7960 M+ / M− | motor | swapping M+/M− reverses the motor (or use `*_MOTOR_INVERT`) |
| Encoders | encoder A/B | ESP32 GPIO 34/35 (left), 36/39 (right) | pull-ups to 3.3 V if open-collector |
| Ground | battery −, BTS7960 GND, encoder GND | ESP32 GND | **must** be common |

### 7.3 Power-up order

1. Robot on a box, wheels off the ground, battery switch **off**.
2. Plug both USB cables into the PC. Check `ls -l /dev/esp32 /dev/lidar` (after step 10).
3. Battery switch **on**.
4. Start `launch_robot.launch.py`. Only then drive.
5. To stop in an emergency: release the keys / Ctrl+C the launch (motors stop within 0.5 s), or switch the battery off.

Power down in reverse: stop ROS, battery off, then unplug USB.

### 7.4 What changes from simulation to the real robot

| | Simulation | Real robot | Where |
|---|---|---|---|
| Robot launch | `launch_sim.launch.py` | `launch_robot.launch.py serial_port:=/dev/esp32 lidar_port:=/dev/lidar` | command line |
| `use_sim_time` | `true` | `false` | every SLAM / Nav2 / localization launch command; the `True` in the YAML files is overwritten by the launch argument, so you don't edit the files |
| Wheel hardware | Gazebo | ESP32 + `DiffBotHardware` | chosen automatically by the launch file (`sim_mode`) |
| Lidar | simulated, 12 m range | YDLIDAR X2, 8 m range, ~10 Hz | `/scan` either way |
| Map | `turtlebot_map` (fits the Gazebo world only) | your own map from step 12 | `mapper_params_online_async.yaml` |
| Dimensions | ideal | measured | sections 14 and 15 |
| Speeds | anything works | start slow, raise later | step 14 |

The simulation is perfect: no wheel slip, exact dimensions, no lidar noise. On the real robot, first get keyboard
driving and odometry right (steps 9 and 11), then mapping, then navigation. Don't skip ahead.

## 8. Flash the ESP32 with the Arduino IDE

The firmware is a single Arduino sketch: `diffbot_hardware/firmware/diffbot_firmware/diffbot_firmware.ino`.
What it does and what its settings mean: [diffbot_hardware/README.md](diffbot_hardware/README.md#esp32-firmware).

### What you need (exact versions)

| Item | Version | Notes |
|---|---|---|
| Board | **ESP32 DevKit** (classic ESP32, ESP32-WROOM-32) | Not ESP32-S2/S3/C3: the pin numbers in the sketch are for the classic ESP32. |
| Arduino IDE | 2.x (tested with 2.3.x) | 1.8.x also works, menus are the same. |
| Board package | **esp32 by Espressif Systems 3.3.12** | Must be 3.x. Core 2.x does **not** compile this sketch (it uses the 3.x LEDC API). |
| Libraries | none | The sketch only uses the ESP32 core. |
| USB cable | data cable, not a charge-only cable | If no port appears, try another cable first. |

### Step 1 – Install the Arduino IDE (Ubuntu 22.04)

1. Download the **Linux AppImage** from <https://www.arduino.cc/en/software>.
2. Make it runnable and start it:

   ```bash
   sudo apt install -y libfuse2          # AppImages need it on 22.04
   chmod +x ~/Downloads/arduino-ide_*_Linux_64bit.AppImage
   ~/Downloads/arduino-ide_*_Linux_64bit.AppImage
   ```

3. Give your user access to serial ports, and remove `brltty` (on Ubuntu 22.04 it grabs CH340 USB-serial
   chips, so the port disappears right after you plug the board in):

   ```bash
   sudo usermod -aG dialout $USER
   sudo apt remove -y brltty
   ```

   **Log out and log in again** (or reboot) so the `dialout` group takes effect.

### Step 2 – Add the ESP32 board package

1. **File → Preferences…**
2. In **Additional boards manager URLs** paste:

   ```
   https://espressif.github.io/arduino-esp32/package_esp32_index.json
   ```

   Click **OK**.
3. **Tools → Board → Boards Manager…** (or the board icon in the left sidebar).
4. Search **`esp32`**. Pick **"esp32 by Espressif Systems"**.
   Do **not** pick "Arduino ESP32 Boards" (by Arduino), that one is for the Arduino Nano ESP32.
5. In the version drop-down choose **3.3.12**, click **Install**. It downloads ~300 MB of tools; wait until it says *INSTALLED*.

### Step 3 – Open the sketch

**File → Open…** and select

```
~/autonomous_bot/src/diffbot_hardware/firmware/diffbot_firmware/diffbot_firmware.ino
```

Keep the folder and file name as they are: the Arduino IDE requires the `.ino` to sit in a folder with the same name.

The settings at the top of the sketch (`MAX_RAD_S`, `TICKS_PER_REV`, the `*_INVERT` flags, `USE_PID`) match
`articubot_one`. **For the first flash leave them as they are.** You will adjust them in [step 9](#9-commission-the-drive-base).

### Step 4 – Select board and port

Plug the ESP32 in by USB, then in the **Tools** menu set:

| Tools menu item | Value |
|---|---|
| Board | **esp32 → ESP32 Dev Module** |
| Port | `/dev/ttyUSB0` (or `/dev/ttyACM0`), the one that appears when you plug the board in |
| Upload Speed | 921600 (use 115200 if the upload fails) |
| CPU Frequency | 240 MHz (WiFi/BT) |
| Flash Size | 4MB (32Mb) |
| Partition Scheme | Default 4MB with spiffs |

Leave everything else at the default. If no port is listed, see the troubleshooting table below.

### Step 5 – Compile and upload

1. Click **Verify** (✓). It should finish with something like
   `Sketch uses ... bytes (about 20%) of program storage space`.
2. Click **Upload** (→). When the log shows `Connecting....`, if it does not continue by itself,
   **hold the BOOT button** on the ESP32 until the percentage starts, then release it.
3. It ends with `Hard resetting via RTS pin...`. The firmware is now running.

### Step 6 – Check that it runs

1. **Tools → Serial Monitor**, set the baud rate (bottom right) to **115200**.
2. Press the **EN/RST** button on the ESP32. You should see:

   ```
   # DiffBot firmware v1.3.0
   # Ready
   E0,0
   E0,0
   ...
   ```

   Turning a wheel by hand changes the `E` numbers.
3. **Close the Serial Monitor** before starting ROS. Only one program can open the port at a time, and ROS will fail
   with `Failed to open serial port` if the monitor is still open.

### Troubleshooting the upload

| Problem | Fix |
|---|---|
| No port in **Tools → Port** | Try a data USB cable; `sudo apt remove brltty`; check `ls /dev/ttyUSB* /dev/ttyACM*` while plugging in. |
| `Permission denied: '/dev/ttyUSB0'` | `sudo usermod -aG dialout $USER`, then log out and in. |
| `Failed to connect to ESP32: No serial data received` / stuck at `Connecting....` | Hold **BOOT** while uploading; lower Upload Speed to 115200. |
| `Port is busy` / `Resource busy` | Close the Serial Monitor, stop the ROS launch, `kill` any `cat /dev/ttyUSB0`. |
| `'ledcAttachChannel' was not declared` or other LEDC errors | Wrong board package version: install **esp32 3.3.12** (2.x does not work). |
| Garbled text in the Serial Monitor | Baud rate is not 115200. |

### Alternative: command line (arduino-cli)

Same versions, no GUI:

```bash
U=https://espressif.github.io/arduino-esp32/package_esp32_index.json
arduino-cli core update-index --additional-urls $U
arduino-cli core install esp32:esp32@3.3.12 --additional-urls $U
cd ~/autonomous_bot/src/diffbot_hardware/firmware
arduino-cli compile --fqbn esp32:esp32:esp32 diffbot_firmware
arduino-cli upload  --fqbn esp32:esp32:esp32 -p /dev/ttyUSB0 diffbot_firmware
```

## 9. Commission the drive base

Before ROS drives the robot, check by hand that each motor turns the right way and each encoder counts the right way.
Wrong directions are the most common reason a new robot spins in circles or its odometry goes backwards.

**Put the robot on a box so the wheels are off the ground.** Then follow
[Commissioning in diffbot_hardware/README.md](diffbot_hardware/README.md#commissioning-first-power-up-wheels-off-the-ground).
It takes about 15 minutes and covers:

1. talking to the ESP32 from a terminal,
2. motor direction (`L_MOTOR_INVERT` / `R_MOTOR_INVERT`),
3. encoder direction (`L_ENC_INVERT` / `R_ENC_INVERT`),
4. measuring `ticks_per_rev`,
5. measuring the top speed `MAX_RAD_S`.

Every change to these settings means editing the `.ino` and uploading again (step 8, "Compile and upload"). For
`ticks_per_rev` and `max_speed_rad_s`, also edit `articubot_one/description/ros2_control.xacro`.

## 10. Serial ports: which one is the ESP32 and which one is the lidar

### Why this matters

The robot has **two** USB-serial devices plugged into the PC:

| Device | What it talks to | Launch argument | Default | Baud |
|---|---|---|---|---|
| ESP32 (motors + encoders) | `DiffBotHardware` plugin | `serial_port` | `/dev/ttyUSB0` | 115200 |
| YDLIDAR X2 (via its USB adapter board) | `ydlidar_ros2_driver_node` | `lidar_port` | `/dev/ttyUSB1` | 115200 |

Linux names them `/dev/ttyUSB0`, `/dev/ttyUSB1`, … **in the order they are detected**. That order changes with
plug-in order, which USB socket is used, and reboots. If the two get swapped, the plugin sends motor commands to
the lidar and the lidar driver opens the ESP32: nothing moves and `/scan` is empty, often without a clear error.

So: find out which device is which once, then give each a **fixed name** that never changes.

### Step 1 – Identify each device

Plug in **only the ESP32**, then run:

```bash
ls /dev/ttyUSB* /dev/ttyACM*
udevadm info -q property -n /dev/ttyUSB0 | grep -E 'ID_VENDOR_ID|ID_MODEL_ID|ID_SERIAL_SHORT|ID_PATH='
```

Write down the four values. Now plug in **only the lidar** (unplug the ESP32) and do the same.
Example output:

```
ESP32:  ID_VENDOR_ID=10c4  ID_MODEL_ID=ea60  ID_SERIAL_SHORT=0001  ID_PATH=pci-0000:00:14.0-usb-0:1:1.0
Lidar:  ID_VENDOR_ID=10c4  ID_MODEL_ID=ea60  ID_SERIAL_SHORT=0001  ID_PATH=pci-0000:00:14.0-usb-0:2:1.0
```

What the values mean:

| Value | Meaning | Typical |
|---|---|---|
| `ID_VENDOR_ID:ID_MODEL_ID` | the USB-serial chip | `10c4:ea60` = CP2102 (YDLIDAR adapter, many ESP32 DevKits); `1a86:7523` = CH340 (other ESP32 DevKits) |
| `ID_SERIAL_SHORT` | chip serial number | CP2102 often all `0001`, so it may **not** be unique |
| `ID_PATH` | the physical USB socket | different for each socket, always unique |

`dmesg -w` in a second terminal also shows which `ttyUSBx` appears when you plug something in.

### Step 2 – Pick how to name them

Three options, from simplest to most robust:

**Option A – `/dev/serial/by-id/` (works if the two chips are different or have different serials)**

```bash
ls -l /dev/serial/by-id/
# usb-Silicon_Labs_CP2102_USB_to_UART_Bridge_Controller_0001-if00-port0 -> ../../ttyUSB1
# usb-1a86_USB_Serial-if00-port0                                         -> ../../ttyUSB0
```

The name is built from the chip and its serial number, so it follows the device to any USB socket.
**If both devices are CP2102 with serial `0001`, they get the same by-id name and only one link exists.**
Use option B or C then.

**Option B – `/dev/serial/by-path/` (always unique, tied to the USB socket)**

```bash
ls -l /dev/serial/by-path/
# pci-0000:00:14.0-usb-0:1:1.0-port0 -> ../../ttyUSB0     ← ESP32 socket
# pci-0000:00:14.0-usb-0:2:1.0-port0 -> ../../ttyUSB1     ← lidar socket
```

Works for identical chips, but each device must **always go into the same USB socket** (label the sockets).
A USB hub counts as part of the path.

**Option C – udev rule with your own names `/dev/esp32` and `/dev/lidar` (recommended)**

Create the rule file (`sudo nano /etc/udev/rules.d/99-autonomous-bot.rules`) with **one** of these two variants,
using the values you wrote down in step 1.

If the two devices differ by chip (vendor/product) or serial number:

```
# ESP32 (here a CH340 DevKit)
SUBSYSTEM=="tty", ATTRS{idVendor}=="1a86", ATTRS{idProduct}=="7523", SYMLINK+="esp32", MODE="0666", ENV{ID_MM_DEVICE_IGNORE}="1"
# YDLIDAR X2 adapter (CP2102)
SUBSYSTEM=="tty", ATTRS{idVendor}=="10c4", ATTRS{idProduct}=="ea60", SYMLINK+="lidar", MODE="0666", ENV{ID_MM_DEVICE_IGNORE}="1"
```

(Add `ATTRS{serial}=="<ID_SERIAL_SHORT>"` to a line if both have the same chip but different serials.)

If both are identical CP2102 `0001` chips, match the USB socket instead (`ID_PATH` from step 1):

```
SUBSYSTEM=="tty", ENV{ID_PATH}=="pci-0000:00:14.0-usb-0:1:1.0", SYMLINK+="esp32", MODE="0666", ENV{ID_MM_DEVICE_IGNORE}="1"
SUBSYSTEM=="tty", ENV{ID_PATH}=="pci-0000:00:14.0-usb-0:2:1.0", SYMLINK+="lidar", MODE="0666", ENV{ID_MM_DEVICE_IGNORE}="1"
```

The file name must start with a number above 60 (here `99-`) so `ID_PATH` is already set when the rule runs.
`ID_MM_DEVICE_IGNORE` stops ModemManager from probing the ports (it sends modem commands to new serial
devices for a few seconds after plug-in, which can confuse the ESP32 or delay the lidar).

Load the rule and re-plug both devices:

```bash
sudo udevadm control --reload-rules
sudo udevadm trigger
ls -l /dev/esp32 /dev/lidar
# /dev/esp32 -> ttyUSB0
# /dev/lidar -> ttyUSB1
```

Both links must exist and point to **different** `ttyUSB` devices. Unplug one device and check that only its
link disappears.

### Step 3 – Check each port before starting ROS

```bash
# ESP32: should print "# DiffBot firmware v1.3.0", "# Ready", then E0,0 lines (Ctrl+C to stop)
stty -F /dev/esp32 115200 raw -echo && cat /dev/esp32
```

For the lidar, the motor spinning up when the driver starts is the easiest check ([step 11](#11-drive-the-real-robot)),
or use `./tri_test` from `YDLidar-SDK/build` with port `/dev/lidar`.

### Step 4 – Pass the names to the launch file

```bash
ros2 launch articubot_one launch_robot.launch.py serial_port:=/dev/esp32 lidar_port:=/dev/lidar
```

(or the `/dev/serial/by-id/...` / `/dev/serial/by-path/...` paths if you used option A or B).
`lidar_port` overrides the `port:` value in `ydlidar_ros2_driver/params/X2.yaml`, so that file does not need editing.

Do **not** run `ydlidar_ros2_driver/startup/initenv.sh`. It creates `/dev/ydlidar` for **every** CP2102
(`10c4:ea60`), so if the ESP32 also uses a CP2102, `/dev/ydlidar` may point at the ESP32. The rule above replaces it.

### Port problems

| Symptom | Cause | Fix |
|---|---|---|
| `Failed to open serial port '/dev/esp32'` | link missing or wrong | `ls -l /dev/esp32`; check the rule values against `udevadm info` |
| Lidar driver keeps restarting, ESP32 works | lidar port wrong, or both links point to the same device | `ls -l /dev/esp32 /dev/lidar` must show two different ttyUSB |
| Wheels don't move, `/scan` empty | ports swapped | swap `serial_port`/`lidar_port`, or fix the rule |
| Port vanishes right after plug-in | `brltty` grabbed it (CH340) | `sudo apt remove brltty` |
| `Permission denied` | not in `dialout` | `sudo usermod -aG dialout $USER`, log out and in |
| `Device or resource busy` | Arduino Serial Monitor, `cat` or another launch has it open | close it |

## 11. Drive the real robot

Start with the wheels **off the ground** the first time, then put the robot on the floor.

```bash
# Terminal 1 – robot + controllers + lidar
ros2 launch articubot_one launch_robot.launch.py serial_port:=/dev/esp32 lidar_port:=/dev/lidar
```

Wait about 5 seconds. The log should show `Serial port opened successfully`, `Hardware activated`, and the two
spawners reporting that `joint_broad` and `diff_cont` are configured and activated. The lidar starts spinning.

```bash
# Terminal 2 – checks
ros2 control list_controllers        # joint_broad and diff_cont: active
ros2 topic hz /scan                  # ~10 Hz
ros2 topic echo /diff_cont/odom --once

# Terminal 3 – drive with the keyboard (same keys as in simulation)
ros2 run teleop_twist_keyboard teleop_twist_keyboard --ros-args -r cmd_vel:=/cmd_vel_key

# Terminal 4 – see the robot and the laser scan
rviz2 -d ~/autonomous_bot/src/articubot_one/config/drive_bot.rviz
```

What correct looks like:

- `i` drives the robot forward, and in RViz the robot model moves forward too.
- `j` turns it left (counter-clockwise seen from above).
- In RViz the laser points line up with the real walls around the robot.

If something is wrong, see [section 16](#16-troubleshooting).

**Joystick instead of keyboard:** add `use_joystick:=true` to the launch command. Hold button 6 (enable) and move
the left stick; hold button 7 for turbo. Button numbers depend on the controller; check them with
`ros2 run joy joy_node` and `ros2 topic echo /joy`, and change them in `articubot_one/config/joystick.yaml`.

---

# Part E – Maps and autonomous navigation

These steps work the same in simulation and on the real robot. Only two things change:

| | Real robot | Simulation |
|---|---|---|
| Robot launch | `launch_robot.launch.py` | `launch_sim.launch.py` |
| `use_sim_time` | `false` | `true` |

`online_async_launch.py` defaults to `use_sim_time:=true`, the other launch files default to `false`, so always
write it out.

## 12. Make a map

slam_toolbox builds the map while you drive the robot around.

**1. Switch slam_toolbox to mapping mode.** Open `articubot_one/config/mapper_params_online_async.yaml` and change:

```yaml
    mode: mapping                # was: localization
    # map_file_name: $(env AUTONOMOUS_BOT_WS)/src/articubot_one/map/turtlebot_map   ← comment out for a new map
    # map_start_at_dock: true                                                  ← comment out too
```

If `map_file_name` stays active, slam_toolbox loads the old map and adds to it instead of starting fresh.
No rebuild is needed (symlink install).

**2. Start everything** (four terminals, real robot shown):

```bash
ros2 launch articubot_one launch_robot.launch.py serial_port:=/dev/esp32 lidar_port:=/dev/lidar
ros2 launch articubot_one online_async_launch.py use_sim_time:=false
rviz2 -d ~/autonomous_bot/src/articubot_one/config/map.rviz
ros2 run teleop_twist_keyboard teleop_twist_keyboard --ros-args -r cmd_vel:=/cmd_vel_key
```

**3. Drive slowly** through the whole area. In RViz you see the map grow: grey is unknown, white is free, black is
walls. Go slowly, especially when turning, and come back to where you started so slam_toolbox can close the loop.

**4. Save the map** in both formats, while everything is still running:

```bash
# for slam_toolbox localization (option A in step 13): creates my_map.posegraph + my_map.data
ros2 service call /slam_toolbox/serialize_map slam_toolbox/srv/SerializePoseGraph "{filename: '$HOME/autonomous_bot/src/articubot_one/map/my_map'}"
# for AMCL (option B in step 13): creates my_map.pgm + my_map.yaml
ros2 run nav2_map_server map_saver_cli -f $HOME/autonomous_bot/src/articubot_one/map/my_map
ls ~/autonomous_bot/src/articubot_one/map     # my_map.data  my_map.pgm  my_map.posegraph  my_map.yaml  ...
```

You can also save from RViz with the **SlamToolboxPlugin** panel (Panels → Add New Panel).

The map that ships with the project (`turtlebot_map`) is of the Gazebo `turtlebot.world`. It is useless on the real
robot; make your own.

## 13. Navigate on the map

You need two things running on top of the robot launch: **localization** (where am I on the map?) and **Nav2**
(how do I get to the goal?). Pick one of the two localization options.

**Option A – slam_toolbox localization (uses the `.posegraph` map).** In
`config/mapper_params_online_async.yaml` set it back to:

```yaml
    mode: localization
    map_file_name: $(env AUTONOMOUS_BOT_WS)/src/articubot_one/map/my_map   # without extension
    map_start_at_dock: true
```

```bash
ros2 launch articubot_one launch_robot.launch.py serial_port:=/dev/esp32 lidar_port:=/dev/lidar
ros2 launch articubot_one online_async_launch.py use_sim_time:=false
ros2 launch articubot_one navigation_launch.py use_sim_time:=false
rviz2 -d ~/autonomous_bot/src/articubot_one/config/map.rviz
```

Start the robot from the same place where you started mapping (that is what `map_start_at_dock` means).

**Option B – AMCL (uses the `.pgm/.yaml` map).**

```bash
ros2 launch articubot_one launch_robot.launch.py serial_port:=/dev/esp32 lidar_port:=/dev/lidar
ros2 launch articubot_one localization_launch.py use_sim_time:=false map:=$HOME/autonomous_bot/src/articubot_one/map/my_map.yaml
ros2 launch articubot_one navigation_launch.py use_sim_time:=false
rviz2 -d ~/autonomous_bot/src/articubot_one/config/map.rviz
```

With AMCL, click **2D Pose Estimate** in the RViz toolbar, and click-and-drag on the map where the
robot really is and which way it faces. The laser scan should then line up with the map walls.

`map.rviz` shows the map, the robot and the laser scan, with Fixed Frame `map`. To also see the planned path:
**Add → By topic → `/plan` → Path**. To see the obstacle margins: **Add → By topic →
`/global_costmap/costmap` → Map**, and set its Color Scheme to `costmap`.

**Send a goal (both options):** in the RViz toolbar click **2D Goal Pose**, then click-and-drag on the map where
the robot should go (click) and which way it should face (drag). Nav2 plans a path and drives there.

- To take over at any time, use the keyboard teleop: it has higher priority than Nav2 in twist_mux.
- Nav2 limits: 0.26 m/s, 1.0 rad/s, robot radius 0.26 m (`config/nav2_params.yaml`).

In simulation, the shipped `turtlebot_map` already matches the world, so option A works right away:
`launch_sim.launch.py`, then the same three commands with `use_sim_time:=true`.

## 14. Tune slam_toolbox and Nav2 for your robot

The parameter files ship with values for this robot's model: 0.054 m wheels, 0.30 m wheel separation, a
0.33 × 0.24 m chassis, YDLIDAR X2. On a real robot you must check them against **your** measurements, and you will
probably want to lower speeds and accelerations at first.

Both files are in `articubot_one/config/`. No rebuild needed after editing; restart the launch that uses the file.

### 14.1 How to work out the robot's size from base_link

Nav2 needs to know how far the robot sticks out from `base_link` (the midpoint between the two drive wheels).
With this chassis, `base_link` is **not** in the middle of the robot:

```
   x (forward)
   ▲
   │   +0.104 ┌──────────┐            chassis: 0.33 m long, back edge 0.226 m behind the axle
   │          │          │                     0.24 m wide (±0.12 m)
   │   0      │    ●     │  ← base_link (wheel axle)
   │          │          │            farthest corner: (−0.226, ±0.12)
   │          │          │            distance = √(0.226² + 0.12²) = 0.256 m  → robot_radius 0.26
   │   −0.226 └──────────┘
   └───────────────────────► y
            +0.12      −0.12
```

If luggage or anything else sticks out past the chassis, measure to that point instead.

### 14.2 slam_toolbox: `mapper_params_online_async.yaml`

**Must be right for the robot:**

| Parameter | Current value | What it means | When to change |
|---|---|---|---|
| `mode` | `localization` | `mapping` = build a new map, `localization` = use a saved one | switch for steps 12 / 13 |
| `map_file_name` | `$(env AUTONOMOUS_BOT_WS)/src/articubot_one/map/turtlebot_map` | saved map to load, no extension | your map name; comment out when mapping fresh |
| `map_start_at_dock` | `true` | robot starts where the map started | or use `map_start_pose: [x, y, yaw]` instead |
| `base_frame` | `base_footprint` | robot frame used by SLAM | keep (it equals `base_link` here) |
| `odom_frame`, `map_frame` | `odom`, `map` | frame names | keep |
| `scan_topic` | `/scan` | laser topic | keep |
| `max_laser_range` | `8.0` | ignore laser points farther than this (m) | X2 maximum is 8 m; lower to 5–6 if far walls look noisy |
| `min_laser_range` | `0.1` | ignore laser points closer than this (m) | raise (e.g. 0.3) if the lidar sees parts of the robot or luggage |

**Tune for map quality:**

| Parameter | Current value | What it means | Tip |
|---|---|---|---|
| `resolution` | `0.05` | map cell size in m (5 cm) | 0.05 is good for rooms; larger = smaller files, less detail |
| `minimum_travel_distance` | `0.05` | process a new scan after moving this far (m) | raise to 0.1–0.2 if the PC is slow |
| `minimum_travel_heading` | `0.05` | … or after turning this much (rad) | |
| `map_update_interval` | `5.0` | how often `/map` is republished (s) | lower for faster updates in RViz, uses more CPU |
| `do_loop_closing` | `true` | correct the map when you return to a known place | keep on |
| `transform_publish_period` | `0.02` | how often `map → odom` is published (s) | keep |

### 14.3 Nav2: `nav2_params.yaml`

**Must match the robot (wrong values make Nav2 crash into things or refuse to move):**

| Parameter | Where in the file | Current value | What it means / how to set it |
|---|---|---|---|
| `robot_radius` | `local_costmap` **and** `global_costmap` | `0.26` | farthest point of the robot from `base_link` (14.1). Change both. |
| or `footprint` (instead of `robot_radius`) | both costmaps | – | exact outline, better for a long robot like this: `footprint: "[[0.104, 0.12], [0.104, -0.12], [-0.226, -0.12], [-0.226, 0.12]]"` (remove `robot_radius` if you use it) |
| `odom_topic` | `bt_navigator`, `velocity_smoother` | `/diff_cont/odom` | must be the diff_cont odometry; keep |
| `robot_base_frame` | `bt_navigator`, both costmaps, `behavior_server` | `base_link` | keep |
| `base_frame_id` | `amcl` | `base_footprint` | keep |
| `robot_model_type` | `amcl` | `DifferentialMotionModel` | two-wheeled robot; keep |
| `scan_topic` / `topic` | `amcl`, costmap `scan` sources | `scan` / `/scan` | keep |
| `laser_max_range`, `laser_min_range` | `amcl` | `8.0`, `0.1` | match the lidar (X2: 8 m) |
| `obstacle_max_range` | both costmaps, `scan` source | `2.5` | laser points farther than this are not marked as obstacles |
| `raytrace_max_range` | both costmaps | `3.0` | free space is cleared up to this distance; keep it above `obstacle_max_range` |
| `obstacle_min_range` | both costmaps, `scan` source | local `0.31`, global `0.1` | laser points closer than this are ignored. Set **both** to just more than the distance from the lidar to the farthest part of the robot or luggage it can see, otherwise the robot sees itself as an obstacle |

**Speeds and accelerations (start low, raise once everything works):**

The robot's top speed is `MAX_RAD_S × wheel radius` = 10 × 0.054 = **0.54 m/s**. Keep Nav2 well below that so the
controller has room to correct. These numbers must be the same in `FollowPath` (under `controller_server`) and in
`velocity_smoother`, or one of them silently limits the other.

| Parameter | Where | Current value | Suggested first value on the real robot |
|---|---|---|---|
| `max_vel_x`, `max_speed_xy` | `controller_server` → `FollowPath` | `0.26` m/s | 0.2 |
| `max_vel_theta` | `FollowPath` | `1.0` rad/s | 0.8 |
| `acc_lim_x` / `decel_lim_x` | `FollowPath` | `2.5` / `-2.5` m/s² | 0.5 / -0.5 (a loaded luggage robot can't accelerate like a small sim robot) |
| `acc_lim_theta` / `decel_lim_theta` | `FollowPath` | `3.2` / `-3.2` rad/s² | 1.5 / -1.5 |
| `max_velocity` | `velocity_smoother` | `[0.26, 0.0, 1.0]` | `[0.2, 0.0, 0.8]` (x, y, rotation) |
| `min_velocity` | `velocity_smoother` | `[-0.26, 0.0, -1.0]` | `[-0.2, 0.0, -0.8]` |
| `max_accel` / `max_decel` | `velocity_smoother` | `[2.5, 0.0, 3.2]` / `[-2.5, 0.0, -3.2]` | `[0.5, 0.0, 1.5]` / `[-0.5, 0.0, -1.5]` |
| `min_vel_x` | `FollowPath` | `0.0` | keep 0.0: the robot does not reverse while following a path (backing up is left to Nav2's recovery behaviour) |

**Behaviour near obstacles and at the goal:**

| Parameter | Where | Current value | What it does / tip |
|---|---|---|---|
| `inflation_radius` | both costmaps, `inflation_layer` | `0.55` | how far from walls the costmap stays "expensive" (m). Must be larger than `robot_radius`. Lower (e.g. 0.4) if the robot won't go through doorways |
| `cost_scaling_factor` | both costmaps | `3.0` | how quickly that cost drops off; higher = robot drives closer to walls |
| `width`, `height`, `resolution` | `local_costmap` | `3`, `3`, `0.05` | size of the moving obstacle window around the robot (m) |
| `xy_goal_tolerance` | `general_goal_checker` and `FollowPath` | `0.25` | how close (m) counts as "arrived" |
| `yaw_goal_tolerance` | `general_goal_checker` | `0.25` | how well (rad, ~14°) it must face the goal direction |
| `required_movement_radius`, `movement_time_allowance` | `progress_checker` | `0.5`, `10.0` | goal is aborted if the robot moves less than 0.5 m in 10 s; raise the time if you lower speeds a lot |
| `controller_frequency` | `controller_server` | `20.0` | how often Nav2 sends speed commands (Hz); keep |

**Localization with AMCL (only if you use `localization_launch.py`):**

| Parameter | Current value | What it does / tip |
|---|---|---|
| `alpha1` … `alpha5` | `0.2` | how much AMCL distrusts the wheel odometry. Raise (0.3–0.5) if the wheels slip or the floor is smooth |
| `update_min_d`, `update_min_a` | `0.25` m, `0.2` rad | AMCL updates after moving or turning this much |
| `transform_tolerance` | `1.0` | allowed delay of transforms (s) |

### 14.4 A safe tuning order

1. Put the measured `robot_radius` (or `footprint`) and `obstacle_min_range` in.
2. Lower the speeds and accelerations to the suggested first values.
3. Navigate to goals 1–2 m away in an open area. Watch RViz: the costmap around the robot should be clean, with no
   obstacle on top of the robot itself.
4. Try a doorway. If the planner refuses, lower `inflation_radius` a little.
5. Raise speeds step by step (e.g. +0.05 m/s) once it is reliable.

---

# Reference

## 15. Values that must agree

If you change one of these, change it everywhere in its row. Paths are relative to `src/`.

| Value | Where |
|---|---|
| wheel radius 0.054 m | `articubot_one/description/robot_core.xacro` (`wheel_radius`), `articubot_one/description/ros2_control.xacro`, `articubot_one/config/my_controllers.yaml` |
| wheel separation 0.30 m | `robot_core.xacro` (2 × `wheel_offset_y`), `my_controllers.yaml` (`wheel_separation`) |
| 2520 ticks/rev | `ros2_control.xacro` (`ticks_per_rev`), firmware `TICKS_PER_REV` |
| 10 rad/s max wheel speed | `ros2_control.xacro` (`max_speed_rad_s`), firmware `MAX_RAD_S` |
| 115200 baud | `ros2_control.xacro` (`baud_rate`), firmware `Serial.begin` |
| `AUTONOMOUS_BOT_WS` in `~/.bashrc` (the workspace path) | `mapper_params_online_async.yaml` (`map_file_name`) |

## 16. Troubleshooting

Hardware-specific problems (motors, encoders, ESP32) have their own table in
[diffbot_hardware/README.md](diffbot_hardware/README.md#troubleshooting). Serial port problems:
[section 10](#port-problems). Arduino upload problems: [section 8](#troubleshooting-the-upload).

| Symptom | Likely cause | Fix |
|---|---|---|
| `robot_state_publisher`: "Moved backwards in time", other people's nodes in `ros2 node list` | another ROS computer on the network | `ROS_LOCALHOST_ONLY=1` or your own `ROS_DOMAIN_ID` ([step 5](#5-build-the-workspace)) |
| Gazebo dies with `Resource temporarily unavailable (src/zmtp_engine.cpp:446)` | another Gazebo on the network | `GAZEBO_IP`, `IGN_IP`, `IGN_PARTITION` ([step 5](#5-build-the-workspace)) |
| Gazebo: `parser error Couldn't parse parameter override rule: '--param robot_description:=...` | a comment in a `.xacro` file contains `'`, `: ` (colon + space), ` #` or a non-ASCII character | remove it from the comment; see [articubot_one/README.md](articubot_one/README.md#2-the-robot-model-urdf) |
| `Package 'articubot_one' not found` | workspace not sourced in this terminal | `source ~/autonomous_bot/install/setup.bash` |
| `colcon build` fails in `ydlidar_ros2_driver` with `ydlidar_sdk` not found | SDK not installed | do [step 4](#4-build-and-install-the-ydlidar-sdk) |
| `colcon build` fails with `hardware_interface` / `controller_manager` not found | dependencies missing | `rosdep install --from-paths src --ignore-src -r -y` |
| Robot doesn't move from the keyboard | teleop not remapped | run teleop with `--ros-args -r cmd_vel:=/cmd_vel_key`, keep its terminal selected |
| Robot doesn't move, `list_controllers` shows nothing | ros2_control failed to start | look in terminal 1 for `Failed to open serial port`; see section 10 |
| Robot turns when it should go straight | one motor direction wrong | [step 9](#9-commission-the-drive-base) |
| Robot in RViz moves the opposite way to the real one | encoder direction wrong | step 9 |
| RViz: "No transform from ... to map/odom" | wrong Fixed Frame, or localization not running | Fixed Frame `odom` for driving, `map` with SLAM/AMCL |
| SLAM/Nav2 does nothing, warnings about time or old messages | `use_sim_time` wrong | `false` on the real robot, `true` in Gazebo |
| Scan in RViz is rotated or mirrored compared to the walls | lidar mounted differently from the model | change the yaw in `laser_joint` in `articubot_one/description/lidar.xacro`, or `reversion`/`inverted` in `ydlidar_ros2_driver/params/X2.yaml` |
| Gazebo world has no walls | turtlebot3 models missing | install `ros-humble-turtlebot3-gazebo` and set `GAZEBO_MODEL_PATH` ([step 3](#3-install-the-dependencies)) |
| slam_toolbox starts with an old map | `map_file_name` still set | comment it out for a new map ([step 12](#12-make-a-map)) |
| Nav2 goal is rejected or robot stops near walls | goal inside an obstacle's safety margin | pick a goal further from walls, or lower `inflation_radius` ([step 14](#14-tune-slam_toolbox-and-nav2-for-your-robot)) |
| Costmap shows an obstacle on the robot itself, Nav2 won't move | lidar sees the robot body, laptop or luggage | clear the lidar's scan plane, or raise `obstacle_min_range` (step 14) |
| Robot overshoots or wobbles when following a path | accelerations too high for the real robot | lower `acc_lim_*` and `velocity_smoother` limits (step 14) |
| Goal aborted with "failed to make progress" | robot too slow for the progress checker | raise `movement_time_allowance` (step 14) |

Useful commands for finding out what is going on:

```bash
ros2 node list                                  # what is running
ros2 topic list                                 # which topics exist
ros2 topic echo /cmd_vel_key                    # do my key presses arrive?
ros2 topic echo /diff_cont/cmd_vel_unstamped    # does twist_mux forward them?
ros2 control list_hardware_interfaces           # is the hardware active?
ros2 run tf2_tools view_frames                  # writes frames.pdf with the TF tree
```

## 17. Verification status

What has been checked so far (13 Oct 2026). Re-check the "not yet done" items when you build the project.

**Simulation, tested end to end on 13 Oct 2026** (wheels-only version, firmware v1.3.0, ROS 2 Humble, Gazebo 11):

- `colcon build` of all three packages: OK, no warnings in `diffbot_hardware`.
- `launch_sim.launch.py`: robot spawned, `joint_broad` and `diff_cont` active, simulated `/scan` at 9.8 Hz.
- Driving: 0.2 m/s on `/cmd_vel_key` → `/diff_cont/odom` x increased.
- Localization: `online_async_launch.py use_sim_time:=true` (slam_toolbox, `mode: localization`, `turtlebot_map`):
  map loaded, `map → base_link` transform available.
- Navigation: `navigation_launch.py use_sim_time:=true`, goals sent to `/navigate_to_pose`:
  (2.0, −0.23) → **SUCCEEDED**, stopped 0.23 m away; (1.0, 1.77) facing +90° → **SUCCEEDED** in 12 s,
  stopped 0.24 m away, heading 1.59 rad. Both within the 0.25 m / 0.25 rad goal tolerance.
- Problems found and fixed during this test: comments in the xacro files contained `'`, `: ` and box-drawing
  characters, which made `gazebo_ros2_control` fail to load (no controllers). Other ROS 2 / Gazebo computers on the
  network disturbed the simulation until it was isolated (step 5).

**Real-robot pipeline, checked before the servo was removed:**

- **YDLidar-SDK** clean build + install: OK.
- `launch_robot.launch.py` run end to end against a fake ESP32 on a virtual serial port:
  hardware activated, controllers active; `/cmd_vel_key` 0.2 m/s → `W3.7037,3.7037` (0.2/0.054);
  0.5 rad/s turn → `W-1.3889,1.3889`; encoder feedback → `/diff_cont/odom` and `/joint_states`.
- **Firmware**: logic tested on the PC with Arduino stubs (parsing, clamping, ramp, watchdog, BTS7960 pin
  outputs, encoder line format, closed-loop option). Compiled with arduino-cli 1.5.1 for `esp32:esp32:esp32`,
  core 3.3.12, `--warnings all`: OK.

**Not yet done:**

- Firmware v1.3.0 compile and the fake-ESP32 pipeline test after the servo removal.
- Mapping (step 12) in simulation: only localization on the existing map was tested.
- On the real robot: motor/encoder direction, PWM tuning, lidar mounting angle, mapping and navigation.
  Step 9 covers the first two.
