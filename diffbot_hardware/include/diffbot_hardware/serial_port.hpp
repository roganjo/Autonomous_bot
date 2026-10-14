#pragma once

#include <boost/asio.hpp>
#include <boost/asio/read_until.hpp>
#include <boost/asio/write.hpp>
#include <string>
#include <chrono>
#include <mutex>

namespace diffbot_hardware
{

/**
 * SerialPort
 *
 * A thread-safe, non-blocking serial port wrapper built on Boost.Asio.
 *
 * Design notes:
 *  - readline() uses a deadline so it never blocks the ros2_control loop
 *    longer than `timeout_ms`.
 *  - write() is synchronous and protected by a mutex; messages are small
 *    (<64 bytes) so the write completes before the deadline matters.
 *  - All methods are safe to call from a single thread (the ros2_control
 *    read/write loop).
 */
class SerialPort
{
public:
  SerialPort();
  ~SerialPort();

  /**
   * Open the serial device.
   * @param port   e.g. "/dev/ttyUSB0"
   * @param baud   e.g. 115200 or 921600
   * @return true on success
   */
  bool open(const std::string & port, int baud);

  /** Close the port (called automatically in destructor). */
  void close();

  bool is_open() const;

  /**
   * Write a message to the serial port.
   * The caller is responsible for appending the newline terminator.
   * @return true if all bytes were written
   */
  bool write(const std::string & msg);

  /**
   * Read one '\n'-terminated line with a deadline.
   * @param out         filled with the line content (newline stripped)
   * @param timeout_ms  maximum time to wait in milliseconds
   * @return true if a complete line was received before the deadline
   */
  bool readline(std::string & out, int timeout_ms = 5);

  /**
   * Return the next complete line that is already available, without waiting.
   * Pulls every byte currently in the OS receive buffer into the internal
   * buffer first.
   * @return true if a complete line was extracted
   */
  bool try_readline(std::string & out);

  /** Discard all bytes currently in the OS receive buffer. */
  void flush();

private:
  boost::asio::io_context       io_;
  boost::asio::serial_port      port_;
  boost::asio::streambuf        buf_;
  mutable std::mutex            write_mutex_;

  // Timer used to impose a deadline on async_read_until
  boost::asio::steady_timer     deadline_;

  /** Extract one '\n'-terminated line from buf_ if it holds one. */
  bool extract_line(std::string & out);
};

}  // namespace diffbot_hardware
