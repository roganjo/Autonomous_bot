#include "diffbot_hardware/diffbot_hardware.hpp"

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <stdexcept>

// pluginlib macro to register the plugin
#include <pluginlib/class_list_macros.hpp>
PLUGINLIB_EXPORT_CLASS(
  diffbot_hardware::DiffBotHardware,
  hardware_interface::SystemInterface)

namespace diffbot_hardware
{

using hardware_interface::CallbackReturn;
using hardware_interface::return_type;
using hardware_interface::HW_IF_POSITION;
using hardware_interface::HW_IF_VELOCITY;

// ── on_init ──────────────────────────────────────────────────────────────────
// Called once when the plugin is loaded.  Parse all parameters from the URDF
// <hardware> block and validate the joint configuration.
CallbackReturn DiffBotHardware::on_init(
  const hardware_interface::HardwareInfo & info)
{
  if (SystemInterface::on_init(info) != CallbackReturn::SUCCESS) {
    return CallbackReturn::ERROR;
  }

  // ── Read hardware parameters ─────────────────────────────────────────────
  auto get_param = [&](const std::string & key, const std::string & def) -> std::string {
    auto it = info_.hardware_parameters.find(key);
    return (it != info_.hardware_parameters.end()) ? it->second : def;
  };

  port_name_        = get_param("serial_port",      "/dev/ttyACM0");
  baud_rate_        = std::stoi(get_param("baud_rate",        "115200"));
  wheel_radius_     = std::stod(get_param("wheel_radius",     "0.033"));
  ticks_per_rev_    = std::stod(get_param("ticks_per_rev",    "1440"));
  max_rad_s_        = std::stod(get_param("max_speed_rad_s",  "10.0"));
  read_timeout_ms_  = std::stoi(get_param("read_timeout_ms",  "5"));
  vel_filter_alpha_ = std::stod(get_param("vel_filter_alpha", "0.3"));

  RCLCPP_INFO(logger_,
    "Init: port=%s baud=%d wheel_r=%.4f ticks/rev=%.0f max=%.1f rad/s alpha=%.2f",
    port_name_.c_str(), baud_rate_, wheel_radius_,
    ticks_per_rev_, max_rad_s_, vel_filter_alpha_);

  // ── Validate joints ──────────────────────────────────────────────────────
  // We expect exactly 2 joints in this order: left_wheel, right_wheel
  if (info_.joints.size() != 2) {
    RCLCPP_FATAL(logger_,
      "Expected 2 joints (left_wheel, right_wheel), got %zu",
      info_.joints.size());
    return CallbackReturn::ERROR;
  }

  // Validate wheel joints have velocity command + position & velocity state
  for (size_t i = 0; i < 2; ++i) {
    const auto & j = info_.joints[i];

    if (j.command_interfaces.size() != 1 ||
        j.command_interfaces[0].name != HW_IF_VELOCITY) {
      RCLCPP_FATAL(logger_,
        "Joint '%s' must have exactly one 'velocity' command interface.",
        j.name.c_str());
      return CallbackReturn::ERROR;
    }
    if (j.state_interfaces.size() != 2) {
      RCLCPP_FATAL(logger_,
        "Joint '%s' must have 'position' and 'velocity' state interfaces.",
        j.name.c_str());
      return CallbackReturn::ERROR;
    }
  }

  // Initialise state and command arrays to 0
  std::fill(std::begin(hw_position_), std::end(hw_position_), 0.0);
  std::fill(std::begin(hw_velocity_), std::end(hw_velocity_), 0.0);
  std::fill(std::begin(hw_commands_), std::end(hw_commands_), 0.0);

  return CallbackReturn::SUCCESS;
}

// ── on_configure ─────────────────────────────────────────────────────────────
// Open the serial port.  This runs on the 'configure' lifecycle transition so
// the port can be re-opened after a shutdown without restarting the node.
CallbackReturn DiffBotHardware::on_configure(
  const rclcpp_lifecycle::State & /*previous_state*/)
{
  RCLCPP_INFO(logger_, "Configuring — opening %s at %d baud …",
    port_name_.c_str(), baud_rate_);

  serial_ = std::make_unique<SerialPort>();

  if (!serial_->open(port_name_, baud_rate_)) {
    RCLCPP_ERROR(logger_,
      "Failed to open serial port '%s'. "
      "Check the port path and udev permissions (add user to 'dialout' group).",
      port_name_.c_str());
    return CallbackReturn::FAILURE;
  }

  // Flush stale data from the ESP32 that may have accumulated before we started
  serial_->flush();

  RCLCPP_INFO(logger_, "Serial port opened successfully.");
  return CallbackReturn::SUCCESS;
}

// ── on_activate ──────────────────────────────────────────────────────────────
// Send an initial zero command so the ESP32 watchdog does not fire immediately.
CallbackReturn DiffBotHardware::on_activate(
  const rclcpp_lifecycle::State & /*previous_state*/)
{
  RCLCPP_INFO(logger_, "Activating hardware interface …");

  // Zero velocity for both wheels
  if (!serial_->write("W0.0000,0.0000\n")) {
    RCLCPP_ERROR(logger_, "Could not write initial zero command.");
    return CallbackReturn::FAILURE;
  }

  hw_commands_[0]  = 0.0;
  hw_commands_[1]  = 0.0;
  hw_velocity_[0]  = 0.0;
  hw_velocity_[1]  = 0.0;
  enc_initialised_ = false;         // re-sync encoder baseline on first read

  RCLCPP_INFO(logger_, "Hardware activated.");
  return CallbackReturn::SUCCESS;
}

// ── on_deactivate ────────────────────────────────────────────────────────────
// Stop the motors before the controller shuts down.
CallbackReturn DiffBotHardware::on_deactivate(
  const rclcpp_lifecycle::State & /*previous_state*/)
{
  RCLCPP_INFO(logger_, "Deactivating — stopping motors …");
  if (serial_ && serial_->is_open()) {
    serial_->write("W0.0000,0.0000\n");
  }
  return CallbackReturn::SUCCESS;
}

// ── on_cleanup ───────────────────────────────────────────────────────────────
CallbackReturn DiffBotHardware::on_cleanup(
  const rclcpp_lifecycle::State & /*previous_state*/)
{
  if (serial_) {
    serial_->close();
    serial_.reset();
  }
  return CallbackReturn::SUCCESS;
}

// ── on_error ─────────────────────────────────────────────────────────────────
CallbackReturn DiffBotHardware::on_error(
  const rclcpp_lifecycle::State & /*previous_state*/)
{
  RCLCPP_ERROR(logger_, "Hardware interface entered error state — stopping motors.");
  if (serial_ && serial_->is_open()) {
    serial_->write("W0.0000,0.0000\n");
  }
  return CallbackReturn::SUCCESS;
}

// ── export_state_interfaces ───────────────────────────────────────────────────
// Bind pointers to our state doubles so ros2_control can read them.
std::vector<hardware_interface::StateInterface>
DiffBotHardware::export_state_interfaces()
{
  std::vector<hardware_interface::StateInterface> si;

  // Left wheel
  si.emplace_back(info_.joints[0].name, HW_IF_POSITION, &hw_position_[0]);
  si.emplace_back(info_.joints[0].name, HW_IF_VELOCITY, &hw_velocity_[0]);

  // Right wheel
  si.emplace_back(info_.joints[1].name, HW_IF_POSITION, &hw_position_[1]);
  si.emplace_back(info_.joints[1].name, HW_IF_VELOCITY, &hw_velocity_[1]);

  return si;
}

// ── export_command_interfaces ─────────────────────────────────────────────────
// Bind pointers to our command doubles so controllers can write into them.
std::vector<hardware_interface::CommandInterface>
DiffBotHardware::export_command_interfaces()
{
  std::vector<hardware_interface::CommandInterface> ci;

  // Wheel velocity commands (rad/s)
  ci.emplace_back(info_.joints[0].name, HW_IF_VELOCITY, &hw_commands_[0]);
  ci.emplace_back(info_.joints[1].name, HW_IF_VELOCITY, &hw_commands_[1]);

  return ci;
}

// ── read ──────────────────────────────────────────────────────────────────────
// Called at the START of every control loop tick.
// Parse the latest encoder line from the ESP32 and update state interfaces.
hardware_interface::return_type DiffBotHardware::read(
  const rclcpp::Time & /*time*/, const rclcpp::Duration & period)
{
  if (!serial_ || !serial_->is_open()) {
    RCLCPP_ERROR_THROTTLE(logger_, *rclcpp::Clock::make_shared(), 2000,
      "Serial port not open during read()");
    return return_type::ERROR;
  }

  // Drain everything the ESP32 has sent and keep only the newest encoder
  // line, so a backlog never builds up. If nothing is buffered, wait at most
  // read_timeout_ms_ for one line. A missed packet keeps the previous state.
  std::string line;
  int32_t enc_l = 0, enc_r = 0;
  bool got_encoder = false;
  while (serial_->try_readline(line)) {
    int32_t l = 0, r = 0;
    if (parse_encoder_line(line, l, r)) {
      enc_l = l;
      enc_r = r;
      got_encoder = true;
    } else {
      // Not an encoder message — e.g. a "# ..." debug print from the ESP32
      RCLCPP_DEBUG(logger_, "Non-encoder line: '%s'", line.c_str());
    }
  }
  if (!got_encoder && serial_->readline(line, read_timeout_ms_)) {
    got_encoder = parse_encoder_line(line, enc_l, enc_r);
  }
  if (!got_encoder) {
    return return_type::OK;  // stale state is fine for one tick
  }

  const auto now = std::chrono::steady_clock::now();

  // On the first valid read, baseline the encoder counters without computing
  // a spurious velocity spike from an arbitrary initial tick value.
  if (!enc_initialised_) {
    enc_left_prev_  = enc_l;
    enc_right_prev_ = enc_r;
    enc_prev_time_  = now;
    enc_initialised_ = true;
    return return_type::OK;
  }

  // Time between the two encoder samples, so a skipped line does not double
  // the measured velocity. Falls back to the control period.
  double dt = std::chrono::duration<double>(now - enc_prev_time_).count();
  if (dt <= 0.0) dt = period.seconds();
  if (dt <= 0.0) dt = 0.02;

  const double rad_per_tick = (2.0 * M_PI) / ticks_per_rev_;

  int32_t delta_l = enc_l - enc_left_prev_;
  int32_t delta_r = enc_r - enc_right_prev_;

  // Accumulate positions (rad)
  hw_position_[0] += static_cast<double>(delta_l) * rad_per_tick;
  hw_position_[1] += static_cast<double>(delta_r) * rad_per_tick;

  // Velocity with a first-order low-pass filter:
  // v_filtered = α × v_raw + (1-α) × v_prev
  double raw_vel_l = static_cast<double>(delta_l) * rad_per_tick / dt;
  double raw_vel_r = static_cast<double>(delta_r) * rad_per_tick / dt;
  hw_velocity_[0] = vel_filter_alpha_ * raw_vel_l + (1.0 - vel_filter_alpha_) * hw_velocity_[0];
  hw_velocity_[1] = vel_filter_alpha_ * raw_vel_r + (1.0 - vel_filter_alpha_) * hw_velocity_[1];

  enc_left_prev_  = enc_l;
  enc_right_prev_ = enc_r;
  enc_prev_time_  = now;

  return return_type::OK;
}

// ── write ──────────────────────────────────────────────────────────────────────
// Called at the END of every control loop tick.
// "W<l>,<r>" is sent on every tick; it is both the velocity command and the
// ESP32 watchdog feed.
hardware_interface::return_type DiffBotHardware::write(
  const rclcpp::Time & /*time*/, const rclcpp::Duration & /*period*/)
{

  if (!serial_ || !serial_->is_open()) {
    RCLCPP_ERROR_THROTTLE(logger_, *rclcpp::Clock::make_shared(), 2000,
      "Serial port not open during write()");
    return return_type::ERROR;
  }

  // Clamp velocity commands to configured max (NaN -> 0)
  double l_cmd = std::isfinite(hw_commands_[0]) ? clamp(hw_commands_[0], -max_rad_s_, max_rad_s_) : 0.0;
  double r_cmd = std::isfinite(hw_commands_[1]) ? clamp(hw_commands_[1], -max_rad_s_, max_rad_s_) : 0.0;

  // ── Wheel velocity command (always sent — feeds ESP32 watchdog) ──────────
  char wheel_msg[48];
  std::snprintf(wheel_msg, sizeof(wheel_msg), "W%.4f,%.4f\n", l_cmd, r_cmd);
  if (!serial_->write(wheel_msg)) {
    RCLCPP_ERROR_THROTTLE(logger_, *rclcpp::Clock::make_shared(), 1000,
      "Failed to write wheel command");
    return return_type::ERROR;
  }

  return return_type::OK;
}

// ── Helpers ───────────────────────────────────────────────────────────────────

bool DiffBotHardware::parse_encoder_line(
  const std::string & line, int32_t & left, int32_t & right)
{
  if (line.empty() || line[0] != 'E') return false;
  return (std::sscanf(line.c_str() + 1, "%d,%d", &left, &right) == 2);
}

}  // namespace diffbot_hardware
