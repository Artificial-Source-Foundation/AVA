#include "process_gate.h"

#include <array>
#include <cerrno>
#include <cstring>
#include <stdexcept>
#include <string>
#include <utility>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

namespace ava::test {

namespace {

constexpr int gate_count = 64;

std::string errno_message(std::string const& operation)
{
  return operation + ": " + std::strerror(errno);
}

std::uint64_t gate_bit(int gate)
{
  if (gate < 0 || gate >= gate_count)
    throw std::runtime_error("process gate must be in [0, 63]");
  return std::uint64_t{1} << gate;
}

}  // namespace

ProcessGateSet::ProcessGateSet(int control_fd) : control_fd_(control_fd)
{
  if (control_fd_ < 0)
    throw std::runtime_error("process gate requires a valid connected descriptor");
}

ProcessGateSet::ProcessGateSet(ProcessGateSet&& other) noexcept
    : control_fd_(std::exchange(other.control_fd_, -1)), opened_by_peer_(other.opened_by_peer_), opened_for_peer_(other.opened_for_peer_)
{
}

ProcessGateSet& ProcessGateSet::operator=(ProcessGateSet&& other) noexcept
{
  if (this != &other)
  {
    close();
    control_fd_ = std::exchange(other.control_fd_, -1);
    opened_by_peer_ = other.opened_by_peer_;
    opened_for_peer_ = other.opened_for_peer_;
  }
  return *this;
}

ProcessGateSet::~ProcessGateSet()
{
  close();
}

// Open one local gate by writing its complete one-byte protocol message.
void ProcessGateSet::open(int gate)
{
  auto const bit = gate_bit(gate);
  if (opened_for_peer_ & bit)
    return;
  for (;;)
  {
    auto const message = static_cast<std::uint8_t>(gate);
    auto const written = ::send(control_fd_, &message, sizeof(message), MSG_NOSIGNAL);
    if (written == static_cast<ssize_t>(sizeof(message)))
      break;
    if (written < 0 && errno == EINTR)
      continue;
    if (written < 0)
      throw std::runtime_error(errno_message("failed to open process gate " + std::to_string(gate)));
    throw std::runtime_error("failed to open process gate " + std::to_string(gate) + ": incomplete control message");
  }
  opened_for_peer_ |= bit;
}

// Drain and retain arbitrary peer opens until the requested gate is observed.
void ProcessGateSet::wait(int gate, std::chrono::milliseconds timeout)
{
  auto const requested_bit = gate_bit(gate);
  if (timeout < std::chrono::milliseconds::zero())
    throw std::runtime_error("process gate timeout must be nonnegative");
  auto const deadline = std::chrono::steady_clock::now() + timeout;
  std::array<std::uint8_t, gate_count> received{};
  while (!(opened_by_peer_ & requested_bit))
  {
    auto const now = std::chrono::steady_clock::now();
    auto const remaining = now < deadline ? std::chrono::ceil<std::chrono::milliseconds>(deadline - now) : std::chrono::milliseconds::zero();
    pollfd descriptor{.fd = control_fd_, .events = POLLIN, .revents = 0};
    int poll_result;
    do
      poll_result = ::poll(&descriptor, 1, static_cast<int>(remaining.count()));
    while (poll_result < 0 && errno == EINTR);
    if (poll_result < 0)
      throw std::runtime_error(errno_message("failed waiting for process gate " + std::to_string(gate)));
    if (poll_result == 0)
      throw std::runtime_error("timed out waiting for process gate " + std::to_string(gate));

    ssize_t count;
    do
      count = ::recv(control_fd_, received.data(), received.size(), 0);
    while (count < 0 && errno == EINTR);
    if (count < 0)
      throw std::runtime_error(errno_message("failed reading process gate " + std::to_string(gate)));
    if (count == 0)
      throw std::runtime_error("peer exited before opening process gate " + std::to_string(gate));
    for (int index = 0; index < count; ++index)
    {
      int const opened_gate = received[index];
      if (opened_gate < 0 || opened_gate >= gate_count)
        throw std::runtime_error("peer sent invalid process gate " + std::to_string(opened_gate));
      opened_by_peer_ |= std::uint64_t{1} << opened_gate;
    }
  }
}

void ProcessGateSet::close() noexcept
{
  if (control_fd_ >= 0)
    static_cast<void>(::close(control_fd_));
  control_fd_ = -1;
}

} // namespace ava::test
