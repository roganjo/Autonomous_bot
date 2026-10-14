/**
 * diffbot_firmware.ino  — v1.3.0
 *
 * ESP32 (classic, e.g. "ESP32 Dev Module") firmware for the autonomous bot:
 * two DC motors on BTS7960 drivers and two quadrature encoders.
 * Talks to the ROS 2 plugin diffbot_hardware/DiffBotHardware over USB serial.
 *
 * Build: Arduino core "esp32 by Espressif" 3.3.x, no extra libraries
 *        (tested with core 3.3.12).
 *
 * Serial protocol (115200 baud, newline-terminated ASCII):
 *   Receive:  "W<left_rad_s>,<right_rad_s>\n"  → wheel velocity command (feeds watchdog)
 *             "H\n"                            → heartbeat (feeds watchdog)
 *   Transmit: "E<left_ticks>,<right_ticks>\n"  → cumulative encoder counts, 50 Hz
 *             "# <text>\n"                     → info lines, ignored by ROS
 *
 * Changes from v1.2.0:
 *   - Servo removed (no S command, no ESP32Servo library). Drive motors only.
 *
 * Changes from v1.1.0:
 *   - Motor PWM on fixed LEDC channels 2–5, servo attached first and limited
 *     to ESP32Servo's timer-0 channels (0, 1, 8, 9). Before, ledcAttach() gave
 *     the motors channels 0–3 and ESP32Servo then asked for channel 0, which
 *     the core treats as "already set up": the servo pin ended up on the left
 *     motor's 20 kHz channel.
 *   - Acceleration ramp is time based (RAMP_PWM_PER_MS). The old per-loop
 *     ramp ran every few microseconds, so it was effectively instant.
 *   - Only W and H feed the watchdog. S no longer keeps the motors alive.
 *   - Strict command parsing (strtof + full-line check); overlong lines are
 *     dropped up to the next '\n' instead of being parsed from the middle.
 *   - Direction flags for each motor and encoder (mirrored right side).
 *   - Optional closed-loop wheel speed control (USE_PID), off by default.
 *   - Encoder counters protected with a portMUX spinlock.
 *
 * BTS7960 truth table:
 *   RPWM > 0, LPWM = 0  →  forward
 *   RPWM = 0, LPWM > 0  →  reverse
 *   RPWM = 0, LPWM = 0  →  coast
 */

#include <Arduino.h>
#include <math.h>
#include <stdlib.h>

// ── Firmware version ─────────────────────────────────────────────────────────
#define FW_VERSION "1.3.0"

// ── Tuning constants — EDIT THESE ───────────────────────────────────────────
// Wheel speed in rad/s that maps to 100 % duty cycle (open-loop feed-forward).
// Must match max_speed_rad_s in articubot_one/description/ros2_control.xacro.
static constexpr float MAX_RAD_S = 10.0f;

// Encoder ticks per wheel revolution after gearbox and 4× decoding.
// Must match ticks_per_rev in ros2_control.xacro. Only used when USE_PID is on.
static constexpr float TICKS_PER_REV = 2520.0f;

// Watchdog: stop motors if no W or H command arrives for this many ms.
// ROS sends W every control tick (50 Hz).
static constexpr uint32_t WATCHDOG_MS = 500UL;

// Encoder feedback rate
static constexpr uint32_t FEEDBACK_HZ = 50UL;
static constexpr uint32_t FEEDBACK_MS = 1000UL / FEEDBACK_HZ;

// Acceleration ramp: maximum PWM duty change per millisecond (0–255 scale).
// 2 → 0 to full speed in ~130 ms. 255 = no ramp.
static constexpr int RAMP_PWM_PER_MS = 2;

// Direction fixes. Set so that a positive W command drives that wheel
// forward and the encoder then counts up (see README, "Commissioning").
static constexpr bool L_MOTOR_INVERT = false;
static constexpr bool R_MOTOR_INVERT = false;
static constexpr bool L_ENC_INVERT   = false;
static constexpr bool R_ENC_INVERT   = false;

// Optional closed-loop speed control. Turn on only after the direction flags
// above are correct, otherwise the loop runs away to full speed.
// duty = feed-forward(target) + KP·error + KI·∫error, error in rad/s.
static constexpr bool  USE_PID       = false;
static constexpr float PID_KP        = 8.0f;    // duty per rad/s
static constexpr float PID_KI        = 40.0f;   // duty per rad
static constexpr float PID_I_LIMIT   = 80.0f;   // max integral contribution (duty)
static constexpr uint32_t PID_PERIOD_MS = 20UL;

// LEDC PWM for the motors. 20 kHz is above the audible range.
// Fixed channels 2–5.
static constexpr int PWM_FREQ       = 20000;  // Hz
static constexpr int PWM_RESOLUTION = 8;      // bits (0–255)
static constexpr int PWM_MAX        = 255;
static constexpr uint8_t CH_L_RPWM  = 2;
static constexpr uint8_t CH_L_LPWM  = 3;
static constexpr uint8_t CH_R_RPWM  = 4;
static constexpr uint8_t CH_R_LPWM  = 5;

// ── Pin map ──────────────────────────────────────────────────────────────────
static constexpr int L_RPWM  = 25;
static constexpr int L_LPWM  = 26;
static constexpr int L_EN    = 27;   // wire to both R_EN and L_EN of the left BTS7960

static constexpr int R_RPWM  = 32;
static constexpr int R_LPWM  = 33;
static constexpr int R_EN    = 14;   // wire to both R_EN and L_EN of the right BTS7960

// GPIO 34–39 are input only and have NO internal pull-ups: open-collector
// encoders need external pull-ups (e.g. 4.7 kΩ to 3.3 V).
static constexpr int ENC_L_A = 34;
static constexpr int ENC_L_B = 35;
static constexpr int ENC_R_A = 36;
static constexpr int ENC_R_B = 39;

// ── Globals ──────────────────────────────────────────────────────────────────
static volatile int32_t enc_left_ticks  = 0;
static volatile int32_t enc_right_ticks = 0;
static portMUX_TYPE     g_enc_mux = portMUX_INITIALIZER_UNLOCKED;

static uint32_t g_last_cmd_ms  = 0;
static uint32_t g_last_tx_ms   = 0;
static uint32_t g_last_ramp_ms = 0;
static uint32_t g_last_pid_ms  = 0;

// Commanded wheel speeds (rad/s), set by W, cleared by the watchdog
static float g_target_left  = 0.0f;
static float g_target_right = 0.0f;

// PWM duty the ramp is moving toward, and the duty currently applied
// (signed, -255..255, positive = forward)
static int g_pwm_left_target  = 0;
static int g_pwm_right_target = 0;
static int g_pwm_left_cur     = 0;
static int g_pwm_right_cur    = 0;

// Closed-loop state
static float   g_integral_left  = 0.0f;
static float   g_integral_right = 0.0f;
static int32_t g_pid_prev_left  = 0;
static int32_t g_pid_prev_right = 0;

// Non-blocking serial line buffer
static char    g_serial_buf[64];
static uint8_t g_serial_pos = 0;
static bool    g_serial_overflow = false;

// ── ISRs — encoders (full 4× quadrature decode on both channels) ─────────────
// A CHANGE: forward if A == B.  B CHANGE: forward if A != B.
void IRAM_ATTR isr_enc_left_a()
{
  const bool fwd = digitalRead(ENC_L_A) == digitalRead(ENC_L_B);
  portENTER_CRITICAL_ISR(&g_enc_mux);
  enc_left_ticks += fwd ? 1 : -1;
  portEXIT_CRITICAL_ISR(&g_enc_mux);
}

void IRAM_ATTR isr_enc_left_b()
{
  const bool fwd = digitalRead(ENC_L_A) != digitalRead(ENC_L_B);
  portENTER_CRITICAL_ISR(&g_enc_mux);
  enc_left_ticks += fwd ? 1 : -1;
  portEXIT_CRITICAL_ISR(&g_enc_mux);
}

void IRAM_ATTR isr_enc_right_a()
{
  const bool fwd = digitalRead(ENC_R_A) == digitalRead(ENC_R_B);
  portENTER_CRITICAL_ISR(&g_enc_mux);
  enc_right_ticks += fwd ? 1 : -1;
  portEXIT_CRITICAL_ISR(&g_enc_mux);
}

void IRAM_ATTR isr_enc_right_b()
{
  const bool fwd = digitalRead(ENC_R_A) != digitalRead(ENC_R_B);
  portENTER_CRITICAL_ISR(&g_enc_mux);
  enc_right_ticks += fwd ? 1 : -1;
  portEXIT_CRITICAL_ISR(&g_enc_mux);
}

/** Read both encoder counters atomically, with direction flags applied. */
static void read_encoders(int32_t & left, int32_t & right)
{
  portENTER_CRITICAL(&g_enc_mux);
  left  = enc_left_ticks;
  right = enc_right_ticks;
  portEXIT_CRITICAL(&g_enc_mux);
  if (L_ENC_INVERT) left  = -left;
  if (R_ENC_INVERT) right = -right;
}

// ── Motor output ─────────────────────────────────────────────────────────────
static inline void ledc_set(int pin, int duty)
{
  ledcWrite(pin, static_cast<uint32_t>(constrain(duty, 0, PWM_MAX)));
}

/** Apply a signed duty (-255..255, positive = forward) to one BTS7960. */
static void motor_apply(int rpwm_pin, int lpwm_pin, bool invert, int duty)
{
  if (invert) duty = -duty;
  if (duty >= 0) {
    ledc_set(lpwm_pin, 0);
    ledc_set(rpwm_pin, duty);
  } else {
    ledc_set(rpwm_pin, 0);
    ledc_set(lpwm_pin, -duty);
  }
}

/** Open-loop feed-forward: rad/s → signed duty. */
static int vel_to_pwm(float rad_s)
{
  int pwm = static_cast<int>(fabsf(rad_s) / MAX_RAD_S * PWM_MAX + 0.5f);
  pwm = constrain(pwm, 0, PWM_MAX);
  return (rad_s >= 0.0f) ? pwm : -pwm;
}

/** Move cur toward target by at most step. */
static int ramp_toward(int cur, int target, int step)
{
  if (cur < target) return min(cur + step, target);
  if (cur > target) return max(cur - step, target);
  return cur;
}

/** Immediately coast both motors — bypasses the ramp for safety stops. */
static void motors_stop()
{
  g_target_left = g_target_right = 0.0f;
  g_pwm_left_target = g_pwm_right_target = 0;
  g_pwm_left_cur = g_pwm_right_cur = 0;
  g_integral_left = g_integral_right = 0.0f;
  motor_apply(L_RPWM, L_LPWM, L_MOTOR_INVERT, 0);
  motor_apply(R_RPWM, R_LPWM, R_MOTOR_INVERT, 0);
}

// ── Closed-loop speed control (USE_PID) ──────────────────────────────────────
static int pid_step(float target, int32_t ticks, int32_t & prev_ticks,
                    float & integral, float dt)
{
  const float measured = static_cast<float>(ticks - prev_ticks) *
                         (2.0f * PI / TICKS_PER_REV) / dt;
  prev_ticks = ticks;

  if (target == 0.0f) {          // no hunting at standstill
    integral = 0.0f;
    return 0;
  }
  const float error = target - measured;
  integral = constrain(integral + error * dt,
                       -PID_I_LIMIT / PID_KI, PID_I_LIMIT / PID_KI);
  const float duty = vel_to_pwm(target) + PID_KP * error + PID_KI * integral;
  return constrain(static_cast<int>(lroundf(duty)), -PWM_MAX, PWM_MAX);
}

// ── Command parsing ──────────────────────────────────────────────────────────
/** Parse one float starting at s. Returns false on garbage or non-finite. */
static bool parse_float(const char * s, char ** end, float & out)
{
  out = strtof(s, end);
  return *end != s && isfinite(out);
}

static void process_command(const char * line, uint32_t now)
{
  char * end = nullptr;

  switch (line[0]) {

    case 'W': {
      // "W<left_rad_s>,<right_rad_s>"
      float lv = 0.0f, rv = 0.0f;
      if (!parse_float(line + 1, &end, lv) || *end != ',') break;
      if (!parse_float(end + 1, &end, rv) || *end != '\0') break;
      g_target_left  = constrain(lv, -MAX_RAD_S, MAX_RAD_S);
      g_target_right = constrain(rv, -MAX_RAD_S, MAX_RAD_S);
      g_last_cmd_ms  = now;
      break;
    }

    case 'H':
      // Heartbeat — reset watchdog only
      g_last_cmd_ms = now;
      break;

    default:
      // Unknown — ignore silently (avoids mixing debug output into data stream)
      break;
  }
}

// Reads all available bytes into a line buffer and dispatches complete lines.
// Never blocks.
static void poll_serial(uint32_t now)
{
  while (Serial.available() > 0) {
    const char c = static_cast<char>(Serial.read());

    if (c == '\r') continue;   // ignore CR in CRLF line endings

    if (c == '\n') {
      if (!g_serial_overflow && g_serial_pos > 0) {
        g_serial_buf[g_serial_pos] = '\0';
        process_command(g_serial_buf, now);
      }
      g_serial_pos = 0;
      g_serial_overflow = false;
      continue;
    }

    if (g_serial_pos < sizeof(g_serial_buf) - 1) {
      g_serial_buf[g_serial_pos++] = c;
    } else {
      g_serial_overflow = true;  // drop the rest of this line
    }
  }
}

// ── Setup ─────────────────────────────────────────────────────────────────────
void setup()
{
  Serial.begin(115200);

  const bool pwm_ok =
    ledcAttachChannel(L_RPWM, PWM_FREQ, PWM_RESOLUTION, CH_L_RPWM) &&
    ledcAttachChannel(L_LPWM, PWM_FREQ, PWM_RESOLUTION, CH_L_LPWM) &&
    ledcAttachChannel(R_RPWM, PWM_FREQ, PWM_RESOLUTION, CH_R_RPWM) &&
    ledcAttachChannel(R_LPWM, PWM_FREQ, PWM_RESOLUTION, CH_R_LPWM);

  // Enable pins (active-high)
  pinMode(L_EN, OUTPUT); digitalWrite(L_EN, HIGH);
  pinMode(R_EN, OUTPUT); digitalWrite(R_EN, HIGH);
  motors_stop();

  // ── Encoder pins ─────────────────────────────────────────────────────────
  pinMode(ENC_L_A, INPUT); pinMode(ENC_L_B, INPUT);
  pinMode(ENC_R_A, INPUT); pinMode(ENC_R_B, INPUT);

  attachInterrupt(digitalPinToInterrupt(ENC_L_A), isr_enc_left_a,  CHANGE);
  attachInterrupt(digitalPinToInterrupt(ENC_L_B), isr_enc_left_b,  CHANGE);
  attachInterrupt(digitalPinToInterrupt(ENC_R_A), isr_enc_right_a, CHANGE);
  attachInterrupt(digitalPinToInterrupt(ENC_R_B), isr_enc_right_b, CHANGE);

  const uint32_t now = millis();
  g_last_cmd_ms  = now;
  g_last_tx_ms   = now;
  g_last_ramp_ms = now;
  g_last_pid_ms  = now;

  Serial.printf("# DiffBot firmware v%s\n", FW_VERSION);
  if (!pwm_ok) {
    Serial.println("# ERROR: motor PWM setup failed");
  }
  Serial.println("# Ready");
}

// ── Main loop ─────────────────────────────────────────────────────────────────
void loop()
{
  const uint32_t now = millis();

  poll_serial(now);

  // ── Watchdog ──────────────────────────────────────────────────────────────
  if (now - g_last_cmd_ms > WATCHDOG_MS) {
    g_target_left  = 0.0f;
    g_target_right = 0.0f;
  }

  // ── Duty targets: open loop, or closed loop every PID_PERIOD_MS ──────────
  if (!USE_PID) {
    g_pwm_left_target  = vel_to_pwm(g_target_left);
    g_pwm_right_target = vel_to_pwm(g_target_right);
  } else if (now - g_last_pid_ms >= PID_PERIOD_MS) {
    const float dt = (now - g_last_pid_ms) / 1000.0f;
    g_last_pid_ms = now;
    int32_t el, er;
    read_encoders(el, er);
    g_pwm_left_target  = pid_step(g_target_left,  el, g_pid_prev_left,  g_integral_left,  dt);
    g_pwm_right_target = pid_step(g_target_right, er, g_pid_prev_right, g_integral_right, dt);
  }

  // ── Acceleration ramp, RAMP_PWM_PER_MS per elapsed millisecond ───────────
  const uint32_t elapsed = now - g_last_ramp_ms;
  if (elapsed > 0) {
    g_last_ramp_ms = now;
    const int step = static_cast<int>(min<uint32_t>(elapsed * RAMP_PWM_PER_MS, PWM_MAX * 2));
    g_pwm_left_cur  = ramp_toward(g_pwm_left_cur,  g_pwm_left_target,  step);
    g_pwm_right_cur = ramp_toward(g_pwm_right_cur, g_pwm_right_target, step);
    motor_apply(L_RPWM, L_LPWM, L_MOTOR_INVERT, g_pwm_left_cur);
    motor_apply(R_RPWM, R_LPWM, R_MOTOR_INVERT, g_pwm_right_cur);
  }

  // ── Encoder feedback at FEEDBACK_HZ ──────────────────────────────────────
  if (now - g_last_tx_ms >= FEEDBACK_MS) {
    g_last_tx_ms = now;
    int32_t el, er;
    read_encoders(el, er);
    Serial.printf("E%ld,%ld\n", static_cast<long>(el), static_cast<long>(er));
  }
}
