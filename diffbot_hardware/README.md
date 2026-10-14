# diffbot_hardware – motors, encoders and the ESP32

This package is the link between ROS and the robot's electronics. It has two parts:

1. **ESP32 firmware** (`firmware/diffbot_firmware/diffbot_firmware.ino`). An Arduino sketch that runs on the ESP32.
   It drives the two motors through **BTS7960** motor drivers and counts the pulses of the two wheel **encoders**.
2. **ros2_control hardware plugin** `diffbot_hardware/DiffBotHardware` (C++, in `src/`). It runs on the PC inside ROS,
   sends wheel speeds to the ESP32 over the USB cable and reads the encoder counts back.

**New to the project?** Start with the [workspace README](../README.md). It tells you when to come here:
step 7 (wiring), step 8 (flashing) and step 9 (commissioning).

**This package is required on the real robot.** `articubot_one/description/ros2_control.xacro` loads the plugin
when `sim_mode:=false`. In simulation Gazebo replaces it, but the package must still be built because
`articubot_one` depends on it.

---

## Contents

1. [Background: the parts and what they do](#background-the-parts-and-what-they-do)
2. [What is in this folder](#what-is-in-this-folder)
3. [Wiring](#wiring)
4. [ESP32 firmware](#esp32-firmware)
5. [Commissioning (first power-up, wheels off the ground)](#commissioning-first-power-up-wheels-off-the-ground)
6. [How the ROS plugin works](#how-the-ros-plugin-works)
7. [Serial protocol](#serial-protocol)
8. [Standalone test bring-up (not the autonomous bot)](#standalone-test-bring-up-not-the-autonomous-bot)
9. [Troubleshooting](#troubleshooting)

---

## Background: the parts and what they do

| Part | What it does |
|---|---|
| **ESP32 DevKit** | A small microcontroller board. It does the time-critical work the PC can't do reliably: generating PWM for the motors and counting encoder pulses thousands of times a second. It is powered by and talks to the PC over one USB cable. |
| **BTS7960** | A motor driver ("H-bridge") that can handle up to 43 A. The ESP32's pins are far too weak to power a motor; the BTS7960 switches the battery current to the motor based on the ESP32's signals. One board per motor. |
| **PWM** | Pulse-width modulation: switching the motor on and off very fast (here 20 000 times a second). The percentage of "on" time, the **duty**, sets the speed. 0 = stopped, 255 = full speed. |
| **RPWM / LPWM** | The two inputs of a BTS7960. PWM on RPWM turns the motor one way, PWM on LPWM the other way. Only one is used at a time. |
| **R_EN / L_EN** | Enable pins of the BTS7960. They must be high (3.3 V) or the driver does nothing. This design ties both together to one ESP32 pin. |
| **Quadrature encoder** | A sensor on the motor shaft with two outputs, **A** and **B**. Each produces pulses as the shaft turns; which one comes first tells the direction. The ESP32 counts every edge of both signals ("4× decoding"). |
| **ticks per revolution** | How many counts the ESP32 sees for **one full turn of the wheel** (not the motor shaft): encoder pulses per motor turn × gearbox ratio × 4. This robot is set to 2520. You measure your own value during commissioning. |
| **Watchdog** | A safety timer in the firmware. If the PC stops sending commands for 0.5 s, the motors stop. |

## What is in this folder

```
diffbot_hardware/
├── firmware/diffbot_firmware/
│   └── diffbot_firmware.ino       ESP32 firmware (v1.3.0)                ← flash this onto the ESP32
├── src/diffbot_hardware.cpp       the ros2_control plugin                ← used by articubot_one
├── src/serial_port.cpp            serial port with read timeout          ← used
├── include/diffbot_hardware/      headers                                ← used
├── diffbot_hardware.xml           tells ROS the plugin exists            ← used
├── launch/diffbot_bringup.launch.py   ┐
├── urdf/diffbot.urdf.xacro            ├ standalone bench test on a smaller robot (r = 0.033 m)
└── config/controllers.yaml            ┘ NOT used by articubot_one, ignore for the autonomous bot
```

The autonomous bot's own settings for the plugin (wheel radius 0.054 m, 2520 ticks/rev, serial port) are in
`articubot_one/description/ros2_control.xacro`, not in this package.

## Wiring

### Signal wiring (ESP32 DevKit, classic ESP32)

| Signal | ESP32 GPIO | Goes to | Note |
|---|---|---|---|
| Left motor forward PWM | 25 | left BTS7960 **RPWM** | |
| Left motor reverse PWM | 26 | left BTS7960 **LPWM** | |
| Left driver enable | 27 | left BTS7960 **R_EN and L_EN** (both) | tie the two EN pins together |
| Right motor forward PWM | 32 | right BTS7960 **RPWM** | |
| Right motor reverse PWM | 33 | right BTS7960 **LPWM** | |
| Right driver enable | 14 | right BTS7960 **R_EN and L_EN** (both) | |
| Left encoder A / B | 34 / 35 | left motor encoder | input only, **no internal pull-up** |
| Right encoder A / B | 36 / 39 | right motor encoder | input only, **no internal pull-up** (36 is labelled VP, 39 is VN on many boards) |
| GND | GND | both BTS7960 GND, both encoder GND, battery − | **all grounds connected** |
| 3.3 V | 3V3 | encoder supply (if 3.3 V encoders), pull-up resistors | |

The BTS7960 boards also have `R_IS` / `L_IS` (current sense) pins. They are not used; leave them unconnected.

### Power wiring

```
 Motor battery + ──┬──► left BTS7960  B+        left BTS7960  M+ / M− ──► left motor
                   └──► right BTS7960 B+        right BTS7960 M+ / M− ──► right motor
 Motor battery − ──┬──► left BTS7960  B−
                   ├──► right BTS7960 B−
                   └──► ESP32 GND               ← common ground, required

 BTS7960 VCC (logic supply, 3.3–5 V) ◄── ESP32 3V3 (or 5V)
 ESP32 ◄── USB cable ◄── PC   (power and data)
```

Rules:

- **Common ground.** The ESP32 GND must be connected to the battery − and the BTS7960 GND. Without it the motors
  twitch randomly or don't move.
- **Never more than 3.3 V on an ESP32 pin.** If your encoders run on 5 V and output 5 V signals, use a voltage
  divider or level shifter on A and B.
- **Pull-ups for open-collector encoders.** GPIO 34–39 have no internal pull-ups. Many motor encoders (open-collector
  or "NPN" outputs) need a 4.7 kΩ resistor from each A/B pin to 3.3 V. If an encoder count never changes, this is
  usually why.
- Use wire thick enough for the motor current between battery, BTS7960 and motors.
- Put a switch or fuse in the battery line so you can cut motor power quickly.

### Mechanical

- Left and right are as seen from **behind** the robot, looking forward.
- If a motor or encoder ends up turning the "wrong" way, you **don't** need to rewire: commissioning (below) fixes it
  with a setting in the firmware.

## ESP32 firmware

How to install the Arduino IDE, the ESP32 board package and upload the sketch, step by step:
[workspace README, step 8](../README.md#8-flash-the-esp32-with-the-arduino-ide). Short version: Arduino core
**esp32 by Espressif Systems 3.3.12** (core 2.x does not work), board **ESP32 Dev Module**, no extra libraries.

### What the firmware does

- Reads commands from USB serial without blocking; malformed or overlong lines are dropped.
- Converts each wheel speed to a PWM duty: `duty = |rad/s| / MAX_RAD_S × 255`. The sign chooses RPWM (forward) or
  LPWM (reverse). With `USE_PID = true` it also corrects the duty using the measured encoder speed.
- Changes the duty gradually (`RAMP_PWM_PER_MS`, 2 per ms → 0 to full speed in about 130 ms) to avoid current spikes.
- Stops the motors if no `W` or `H` command arrives for 500 ms (watchdog).
- Counts encoder edges in interrupts and sends the totals as `E<left>,<right>` 50 times a second.
- Uses fixed LEDC (PWM) channels 2–5 at 20 kHz for the motors.
- Ignores any other line, including the old `S<deg>` servo command from earlier versions.

### Settings at the top of the `.ino`

These are the only lines you normally edit. After changing one, upload the sketch again.

| Constant | Default | Meaning | When to change |
|---|---|---|---|
| `MAX_RAD_S` | 10.0 | wheel speed (rad/s) at 100 % duty | commissioning step 5; must equal `max_speed_rad_s` in `ros2_control.xacro` |
| `TICKS_PER_REV` | 2520 | encoder counts per wheel turn | commissioning step 4; must equal `ticks_per_rev` in `ros2_control.xacro` |
| `WATCHDOG_MS` | 500 | stop if no command for this long (ms) | normally never |
| `RAMP_PWM_PER_MS` | 2 | acceleration ramp | raise for snappier, lower for gentler starts |
| `L_MOTOR_INVERT`, `R_MOTOR_INVERT` | false | reverse a motor's direction | commissioning step 2 |
| `L_ENC_INVERT`, `R_ENC_INVERT` | false | reverse an encoder's count direction | commissioning step 3 |
| `USE_PID`, `PID_KP`, `PID_KI` | false, 8, 40 | closed-loop speed control | optional, commissioning step 6 |

Pin numbers are defined further down (`L_RPWM`, `ENC_L_A`, …). Change them only if you wire differently.

### Other ways to build and flash

arduino-cli:

```bash
U=https://espressif.github.io/arduino-esp32/package_esp32_index.json
arduino-cli core update-index --additional-urls $U
arduino-cli core install esp32:esp32@3.3.12 --additional-urls $U
cd ~/autonomous_bot/src/diffbot_hardware/firmware
arduino-cli compile --fqbn esp32:esp32:esp32 diffbot_firmware
arduino-cli upload  --fqbn esp32:esp32:esp32 -p /dev/ttyUSB0 diffbot_firmware
```

PlatformIO (`platformio.ini`):

```ini
[env:esp32dev]
platform  = https://github.com/pioarduino/platform-espressif32/releases/download/stable/platform-espressif32.zip
board     = esp32dev
framework = arduino
monitor_speed = 115200
```

(The pioarduino platform provides Arduino core 3.x. If you use `platform = espressif32`, check that the
build log shows framework-arduinoespressif32 3.x; 2.x will not compile this firmware.)

## Commissioning (first power-up, wheels off the ground)

Do this once, after wiring and flashing and **before** driving with ROS. It makes sure that "forward" really means
forward and that the encoder counts match the wheels. Takes about 15 minutes.

**Safety:** put the robot on a box so both wheels spin freely. Keep a hand on the battery switch.

You will send commands to the ESP32 by typing in a terminal. Close the Arduino Serial Monitor and stop any ROS
launch first, because only one program can use the port.

1. **Talk to the ESP32 directly.** Use `/dev/esp32` if you already made the udev rule
   ([workspace README, step 10](../README.md#10-serial-ports-which-one-is-the-esp32-and-which-one-is-the-lidar)),
   otherwise the port you saw in the Arduino IDE.

   ```bash
   PORT=/dev/ttyUSB0
   stty -F $PORT 115200 raw -echo
   cat $PORT &                       # shows "# DiffBot firmware v1.3.0", "# Ready", then E0,0 ...
   ```

   The `E` lines keep scrolling in this terminal; that's expected. You can type the next commands in the same
   terminal.

2. **Motor direction.** Send a slow "both wheels forward" command for 2 s. It has to be repeated because the
   watchdog stops the motors after 0.5 s:

   ```bash
   for i in $(seq 20); do echo "W2,2" > $PORT; sleep 0.1; done
   ```

   Both wheels must turn so the robot would drive **forward**. If one turns backwards, set its
   `L_MOTOR_INVERT` or `R_MOTOR_INVERT` to `true` in the `.ino`, upload again, and repeat this step.
   If a wheel doesn't move at all: check the enable pin (27 / 14), the battery and the common ground.

3. **Encoder direction.** While step 2 runs, watch the `E<left>,<right>` numbers. Both must **increase**.
   If one decreases, set `L_ENC_INVERT` or `R_ENC_INVERT` to `true`, upload again, repeat.
   If one stays at the same number, check its wiring and pull-up resistors.

4. **ticks_per_rev.** Put a tape mark on one wheel. Note the `E` value for that wheel, turn the wheel **by hand**
   exactly 10 times forward, and note the value again. `ticks_per_rev = (end − start) / 10`.
   Example: 0 → 25 200 gives 2520. Put your number in `TICKS_PER_REV` in the `.ino` **and** in `ticks_per_rev` in
   `articubot_one/description/ros2_control.xacro`.

5. **MAX_RAD_S.** Send full speed for a few seconds and measure how fast the wheel really turns:

   ```bash
   for i in $(seq 30); do echo "W10,10" > $PORT; sleep 0.1; done
   ```

   Note two `E` values and the time between them (or count ticks over the 3 s). Then
   `rad/s = Δticks / Δt × 2π / ticks_per_rev`. Example: 12 000 ticks in 3 s with 2520 ticks/rev →
   12 000 / 3 × 6.283 / 2520 ≈ 9.97 rad/s. Set `MAX_RAD_S` in the `.ino` and `max_speed_rad_s` in
   `ros2_control.xacro` to that value. Use the slower of the two wheels.

6. **Optional closed loop.** Only after steps 2 and 3 are correct: set `USE_PID = true`, upload, and check that
   `W2,2` gives about 2 rad/s (from the `E` values) even with the robot on the floor. If the wheels run away to full
   speed, an encoder direction is wrong: set `USE_PID = false` and redo step 3.

7. **Hand over to ROS.** Stop `cat` with `kill %1`, then start ROS:

   ```bash
   ros2 launch articubot_one launch_robot.launch.py serial_port:=$PORT
   ```

   Drive forward with the keyboard teleop (workspace README, step 11). `ros2 topic echo /diff_cont/odom` must show
   `pose.position.x` increasing.

Opening the port can reset some ESP32 boards. The plugin ignores the boot messages and resyncs on the first `E` line.

## How the ROS plugin works

You don't need to change the plugin to complete the project. This section explains what it does in case you need to
debug.

```
diff_cont
        │ wheel velocity (rad/s)
        ▼
DiffBotHardware::write()  ── every control tick (50 Hz) ──►  "W<left>,<right>\n"
DiffBotHardware::read()   ◄─ ESP32 sends at 50 Hz ───────   "E<left_ticks>,<right_ticks>\n"
        │ position = ticks · 2π / ticks_per_rev, velocity = Δposition / Δt (low-pass filtered)
        ▼
joint_broad (/joint_states), diff_cont (/diff_cont/odom, TF odom → base_link)
```

- **Lifecycle.** ros2_control calls the plugin in stages: `on_init` reads the parameters and checks the two wheel
  joints; `on_configure` opens the serial port; `on_activate` sends a zero speed; `on_deactivate` / `on_error` stop
  the motors.
- **read()** takes everything the ESP32 has sent and uses the newest `E` line, so no backlog builds up. If nothing has
  arrived, it waits at most `read_timeout_ms`. Lines starting with `#` are ignored.
- **write()** sends `W` on every tick. That also keeps the ESP32 watchdog happy.

### Plugin parameters (`<hardware>` block in `articubot_one/description/ros2_control.xacro`)

| Parameter | Robot value | Meaning |
|---|---|---|
| `serial_port` | from launch arg `serial_port` (default `/dev/ttyUSB0`) | ESP32 device |
| `baud_rate` | `115200` | must match the firmware |
| `ticks_per_rev` | `2520` | encoder counts per wheel turn; must match firmware `TICKS_PER_REV` |
| `wheel_radius` | `0.054` | only printed in the log; `diff_cont` does the maths |
| `max_speed_rad_s` | `10.0` | wheel speed limit; must match firmware `MAX_RAD_S` |
| `read_timeout_ms` | `5` | longest wait for an encoder line per tick |
| `vel_filter_alpha` | `0.3` | smoothing of the measured wheel speed, 1.0 = none |

The joints must be listed in this order: `left_wheel_joint`, then `right_wheel_joint`, each with a velocity command
and position + velocity state. No other joints.

After changing the C++ code, rebuild: `colcon build --symlink-install --packages-select diffbot_hardware`.

## Serial protocol

Plain text, 115200 baud, one message per line (ending in `\n`). You can type these yourself as in commissioning.

| Direction | Message | Example | Meaning |
|---|---|---|---|
| PC → ESP32 | `W<l>,<r>` | `W3.7037,-1.2000` | wheel speeds in rad/s, positive = forward. Feeds the watchdog. |
| PC → ESP32 | `H` | `H` | heartbeat: feeds the watchdog without changing speed (not used by the plugin) |
| ESP32 → PC | `E<l>,<r>` | `E12345,-678` | total encoder counts since power-up, 50 times a second |
| ESP32 → PC | `# text` | `# Ready` | information, ignored by ROS |

Malformed lines (missing comma, letters, NaN, longer than 63 characters) are ignored.

Example: driving forward at 0.2 m/s with 0.054 m wheels needs 0.2 / 0.054 = 3.70 rad/s per wheel, so the plugin
sends `W3.7037,3.7037`.

## Standalone test bring-up (not the autonomous bot)

`launch/diffbot_bringup.launch.py` runs the plugin with its own URDF (`urdf/diffbot.urdf.xacro`, wheel radius
0.033 m, separation 0.16 m, 1440 ticks/rev) and controllers named `diff_drive_controller` and
`joint_state_broadcaster`. It is only for testing the plugin on its own.

```bash
ros2 launch diffbot_hardware diffbot_bringup.launch.py serial_port:=/dev/ttyUSB0
```

For the autonomous bot always use `ros2 launch articubot_one launch_robot.launch.py`.

## Troubleshooting

| Symptom | Likely cause | Fix |
|---|---|---|
| `Failed to open serial port` | wrong port, no permission, or port in use | [workspace README, step 10](../README.md#10-serial-ports-which-one-is-the-esp32-and-which-one-is-the-lidar); `groups` must list `dialout`; close the Serial Monitor |
| No `# Ready` / no `E` lines in `cat` | firmware not running, wrong baud | re-flash; check `stty ... 115200`; press EN/RST on the ESP32 |
| `E` lines appear but wheels don't move on `W2,2` | enable pins low, no battery, no common ground | check GPIO 27 / 14 to R_EN + L_EN, battery voltage, GND between ESP32 and battery − |
| Wheels move for half a second then stop | normal watchdog: nothing is repeating `W` | send `W` in a loop (commissioning step 2); under ROS check `ros2 control list_controllers` |
| Robot spins instead of driving straight | one motor direction wrong | `*_MOTOR_INVERT` (commissioning step 2) |
| Odometry goes backwards / turns the wrong way | encoder direction wrong | `*_ENC_INVERT` (step 3) |
| One encoder count never changes | missing pull-up, wrong pin, encoder not powered | 4.7 kΩ to 3.3 V on A and B; check GPIO 34/35 or 36/39 |
| Odometry distance wrong | `ticks_per_rev` or wheel radius wrong | step 4; measure the wheel diameter |
| Motors twitch or run at random | floating inputs or no common ground | connect all grounds; check RPWM/LPWM wiring |
| Wheels run away with `USE_PID` | encoder direction wrong | set `USE_PID = false`, redo step 3 |
| ESP32 resets when motors start | power dip | power the ESP32 only from USB, not from the motor supply; check the battery |
