#include "diffbot_hardware/serial_port.hpp"

#include <algorithm>
#include <istream>
#include <stdexcept>
#include <sstream>

#if defined(__linux__)
#include <sys/ioctl.h>
#include <termios.h>
#endif

namespace diffbot_hardware
{

SerialPort::SerialPort()
: io_(),
  port_(io_),
  buf_(),
  deadline_(io_)
{
  deadline_.expires_at(boost::asio::steady_timer::time_point::max());
}

SerialPort::~SerialPort()
{
  close();
}

bool SerialPort::open(const std::string & port_name, int baud)
{
  try {
    port_.open(port_name);

    port_.set_option(boost::asio::serial_port_base::baud_rate(baud));
    port_.set_option(boost::asio::serial_port_base::character_size(8));
    port_.set_option(boost::asio::serial_port_base::parity(
      boost::asio::serial_port_base::parity::none));
    port_.set_option(boost::asio::serial_port_base::stop_bits(
      boost::asio::serial_port_base::stop_bits::one));
    port_.set_option(boost::asio::serial_port_base::flow_control(
      boost::asio::serial_port_base::flow_control::none));

    return true;
  } catch (const boost::system::system_error & e) {
    // Let the caller log the error
    (void)e;
    return false;
  }
}

void SerialPort::close()
{
  if (port_.is_open()) {
    boost::system::error_code ec;
    port_.cancel(ec);
    port_.close(ec);
  }
  io_.stop();
}

bool SerialPort::is_open() const
{
  return port_.is_open();
}

bool SerialPort::write(const std::string & msg)
{
  if (!port_.is_open()) return false;
  std::lock_guard<std::mutex> lock(write_mutex_);
  try {
    boost::asio::write(port_, boost::asio::buffer(msg));
    return true;
  } catch (...) {
    return false;
  }
}

bool SerialPort::extract_line(std::string & out)
{
  auto data = buf_.data();
  auto begin = boost::asio::buffers_begin(data);
  auto end = boost::asio::buffers_end(data);
  if (std::find(begin, end, '\n') == end) return false;

  std::istream is(&buf_);
  std::getline(is, out);
  // Strip trailing '\r' if present (ESP32 Serial.println adds \r\n)
  if (!out.empty() && out.back() == '\r') {
    out.pop_back();
  }
  return true;
}

bool SerialPort::try_readline(std::string & out)
{
  if (!port_.is_open()) return false;
#if defined(__linux__)
  int available = 0;
  if (::ioctl(port_.native_handle(), FIONREAD, &available) == 0 && available > 0) {
    boost::system::error_code ec;
    std::size_t n = port_.read_some(buf_.prepare(available), ec);
    if (!ec) buf_.commit(n);
  }
#endif
  return extract_line(out);
}

bool SerialPort::readline(std::string & out, int timeout_ms)
{
  if (!port_.is_open()) return false;

  // A line may already be buffered from an earlier read.
  if (extract_line(out)) return true;

  bool     got_line = false;
  bool     timed_out = false;
  boost::system::error_code ec;
  std::size_t bytes_read = 0;

  // Start async read
  boost::asio::async_read_until(
    port_, buf_, '\n',
    [&](const boost::system::error_code & e, std::size_t n) {
      ec = e;
      bytes_read = n;
      got_line = !e;
      deadline_.cancel();
    });

  // Start deadline timer
  deadline_.expires_after(std::chrono::milliseconds(timeout_ms));
  deadline_.async_wait(
    [&](const boost::system::error_code & e) {
      if (!e) {
        // Deadline fired — cancel the read
        timed_out = true;
        port_.cancel();
      }
    });

  // Run until both handlers have fired
  io_.restart();
  io_.run();

  (void)bytes_read;
  (void)timed_out;
  // Bytes read before a timeout stay in buf_ and are used on the next call.
  if (!got_line) return false;
  return extract_line(out);
}

void SerialPort::flush()
{
  if (!port_.is_open()) return;
  // Discard anything in the streambuf
  buf_.consume(buf_.size());
  // On Linux, also flush the kernel buffer
#if defined(__linux__)
  int fd = port_.native_handle();
  ::tcflush(fd, TCIFLUSH);
#endif
}

}  // namespace diffbot_hardware
