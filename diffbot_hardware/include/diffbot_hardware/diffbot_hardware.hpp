#pragma once

#include "hardware_interface/system_interface.hpp"
#include "hardware_interface/handle.hpp"
#include "hardware_interface/hardware_info.hpp"
#include "hardware_interface/types/hardware_interface_return_values.hpp"
#include "hardware_interface/types/hardware_interface_type_values.hpp"
#include "rclcpp/rclcpp.hpp"
#include "rclcpp_lifecycle/state.hpp"
#include "diffbot_hardware/serial_port.hpp"

#include <memory>
#include <string>
#include <vector>
#include <chrono>
#include <cmath>

namespace diffbot_hardware
{

/**
 * DiffBotHardware
 *
 * A ros2_control SystemInterface for a two-wheel differential drive robot.
 * Hardware:
 *   - Two DC geared motors driven by BTS7960 H-bridge drivers
 *   - Two quadrature encoders read via hardware interrupts on an ESP32
 *
 * Serial protocol (newline-terminated ASCII, 115200 or 921600 baud):
 *   ROS2 → ESP32:
 *     "W<left_rad_s>,<right_rad_s>\n"   – wheel velocity commands
 *     "H\n"                             – heartbeat (resets ESP32 watchdog, unused here)
 *
 *   ESP32 → ROS2:
 *     "E<left_ticks>,<right_ticks>\n"   – cumulative encoder counts
 *
 * Joint ordering in info_.joints[] (must match URDF ros2_control block):
 *   [0] left_wheel_joint   – velocity command, position+velocity state
 *   [1] right_wheel_joint  – velocity command, position+velocity state
 */
class DiffBotHardware : public hardware_interface::SystemInterface
{
public:
  RCLCPP_SHARED_PTR_DEFINITIONS(DiffBotHardware)

  // ── Lifecycle ───────────────────────────────────────────────────────────
  hardware_interface::CallbackReturn on_init(
    const hardware_interface::HardwareInfo & info) override;

  hardware_interface::CallbackReturn on_configure(
    const rclcpp_lifecycle::State & previous_state) override;

  hardware_interface::CallbackReturn on_activate(
    const rclcpp_lifecycle::State & previous_state) override;

  hardware_interface::CallbackReturn on_deactivate(
    const rclcpp_lifecycle::State & previous_state) override;

  hardware_interface::CallbackReturn on_cleanup(
    const rclcpp_lifecycle::State & previous_state) override;

  hardware_interface::CallbackReturn on_error(
    const rclcpp_lifecycle::State & previous_state) override;

  // ── Interface export ────────────────────────────────────────────────────
  std::vector<hardware_interface::StateInterface>   export_state_interfaces()   override;
  std::vector<hardware_interface::CommandInterface> export_command_interfaces() override;

  // ── Control loop ────────────────────────────────────────────────────────
  hardware_interface::return_type read(
    const rclcpp::Time & time, const rclcpp::Duration & period) override;

  hardware_interface::return_type write(
    const rclcpp::Time & time, const rclcpp::Duration & period) override;

private:
  // ── Hardware state (rad, rad/s) ─────────────────────────────────────────
  // Index: 0 = left wheel, 1 = right wheel
  double hw_position_[2]{0.0, 0.0};
  double hw_velocity_[2]{0.0, 0.0};

  // ── Command interfaces (rad/s) ──────────────────────────────────────────
  double hw_commands_[2]{0.0, 0.0};

  // ── Encoder tracking ────────────────────────────────────────────────────
  int32_t enc_left_prev_  = 0;
  int32_t enc_right_prev_ = 0;
  bool    enc_initialised_ = false;
  std::chrono::steady_clock::time_point enc_prev_time_;

  // ── Parameters (read from URDF <hardware> block) ────────────────────────
  std::string port_name_    = "/dev/ttyUSB0";
  int         baud_rate_    = 115200;
  double      wheel_radius_ = 0.033;        // metres (logged only; diff_cont does the kinematics)
  double      ticks_per_rev_= 1440.0;       // after gearbox + 4× decode
  double      max_rad_s_    = 10.0;         // safety clamp
  int         read_timeout_ms_ = 5;

  // ── Serial port ─────────────────────────────────────────────────────────
  std::unique_ptr<SerialPort> serial_;

  // ── Watchdog ────────────────────────────────────────────────────────────
  // write() sends "W<l>,<r>" on every tick, which feeds the ESP32 watchdog
  // (500 ms in the firmware). If ROS stops, the ESP32 stops the motors.

  // ── Velocity low-pass filter (FIX #11) ──────────────────────────────────
  // hw_velocity_ is updated with:  v_filtered = α × v_raw + (1-α) × v_prev
  // α = 1.0  → no filtering (instantaneous)
  // α = 0.1  → heavy smoothing (good for noisy encoders)
  // Configurable via URDF param "vel_filter_alpha" (default 0.3)
  double vel_filter_alpha_ = 0.3;

  // ── Helpers ─────────────────────────────────────────────────────────────
  rclcpp::Logger logger_{rclcpp::get_logger("DiffBotHardware")};

  bool parse_encoder_line(const std::string & line,
                          int32_t & left, int32_t & right);
  double clamp(double v, double lo, double hi) const
  {
    return v < lo ? lo : (v > hi ? hi : v);
  }
};

}  // namespace diffbot_hardware
