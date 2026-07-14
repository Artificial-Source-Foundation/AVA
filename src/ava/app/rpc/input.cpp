#include "sys.h"
#include "ava/app/rpc/input.h"
#include "ava/app/rpc/protocol.h"

#include <array>
#include <cerrno>
#include <utility>
#include <fcntl.h>
#include <poll.h>
#include <unistd.h>

namespace ava::app::rpc {
namespace {

ava::core::Error input_io_error(std::string message)
{
  return ava::core::Error(ava::core::ErrorCategory::Io, std::move(message));
}

bool configure_wake_fd(int fd)
{
  auto const descriptor_flags = fcntl(fd, F_GETFD);
  if (descriptor_flags == -1 || fcntl(fd, F_SETFD, descriptor_flags | FD_CLOEXEC) == -1)
    return false;
  auto const status_flags = fcntl(fd, F_GETFL);
  return status_flags != -1 && fcntl(fd, F_SETFL, status_flags | O_NONBLOCK) != -1;
}

class PosixRpcLineReader final : public RpcLineReader
{
 public:
  PosixRpcLineReader(int input_fd, int wake_read_fd, int wake_write_fd) : input_fd_(input_fd), wake_read_fd_(wake_read_fd), wake_write_fd_(wake_write_fd) { }

  ~PosixRpcLineReader() override
  {
    close(wake_read_fd_);
    close(wake_write_fd_);
  }

  [[nodiscard]] ava::core::Result<bool> read_line(std::string& line) override
  {
    line.clear();
    bool oversized = false;
    while (true)
    {
      if (canceled_.load(std::memory_order_acquire))
        return false;

      if (buffer_offset_ == buffer_size_)
      {
        pollfd descriptors[2] = {
            pollfd{.fd = input_fd_, .events = POLLIN, .revents = 0},
            pollfd{.fd = wake_read_fd_, .events = POLLIN, .revents = 0},
        };
        int ready = 0;
        do
        {
          ready = poll(descriptors, 2, -1);
        } while (ready == -1 && errno == EINTR);
        if (ready == -1)
          return std::unexpected(input_io_error("failed to poll RPC stdin"));
        if (canceled_.load(std::memory_order_acquire) || (descriptors[1].revents & (POLLIN | POLLHUP | POLLERR | POLLNVAL)) != 0)
          return false;
        if ((descriptors[0].revents & POLLNVAL) != 0)
          return std::unexpected(input_io_error("failed to read RPC stdin"));
        if ((descriptors[0].revents & (POLLIN | POLLHUP | POLLERR)) == 0)
          continue;

        ssize_t count = 0;
        do
        {
          count = read(input_fd_, buffer_.data(), buffer_.size());
        } while (count == -1 && errno == EINTR);
        if (count == -1)
          return std::unexpected(input_io_error("failed to read RPC stdin"));
        if (count == 0)
        {
          if (oversized)
            return std::unexpected(invalid_rpc("RPC request line is too large"));
          return !line.empty();
        }
        buffer_offset_ = 0;
        buffer_size_ = static_cast<std::size_t>(count);
      }

      char const ch = buffer_[buffer_offset_++];
      if (ch == '\n')
        break;
      if (line.size() >= kMaxRpcLineBytes)
      {
        oversized = true;
        continue;
      }
      if (!oversized)
        line.push_back(ch);
    }
    if (oversized)
      return std::unexpected(invalid_rpc("RPC request line is too large"));
    return true;
  }

  void cancel() noexcept override
  {
    if (canceled_.exchange(true, std::memory_order_acq_rel))
      return;
    char const wake = 1;
    ssize_t written = 0;
    do
    {
      written = write(wake_write_fd_, &wake, 1);
    } while (written == -1 && errno == EINTR);
  }

 private:
  int input_fd_ = -1;
  int wake_read_fd_ = -1;
  int wake_write_fd_ = -1;
  std::array<char, 8192> buffer_{};
  std::size_t buffer_offset_ = 0;
  std::size_t buffer_size_ = 0;
  std::atomic_bool canceled_ = false;
};

}  // namespace

StreamRpcLineReader::StreamRpcLineReader(std::istream& input, std::function<void()> wake) : input_(input), wake_(std::move(wake))
{
}

ava::core::Result<bool> StreamRpcLineReader::read_line(std::string& line)
{
  if (canceled_.load(std::memory_order_acquire))
    return false;
  return read_rpc_line_bounded(input_, line);
}

void StreamRpcLineReader::cancel() noexcept
{
  if (canceled_.exchange(true, std::memory_order_acq_rel))
    return;
  if (wake_)
  {
    try
    {
      wake_();
    }
    catch (...)
    {
    }
  }
}

ava::core::Result<std::unique_ptr<RpcLineReader>> make_posix_rpc_line_reader(int input_fd)
{
  int wake_fds[2] = {-1, -1};
  if (pipe(wake_fds) == -1)
    return std::unexpected(input_io_error("failed to create RPC stdin wake pipe"));
  if (!configure_wake_fd(wake_fds[0]) || !configure_wake_fd(wake_fds[1]))
  {
    close(wake_fds[0]);
    close(wake_fds[1]);
    return std::unexpected(input_io_error("failed to configure RPC stdin wake pipe"));
  }
  return std::unique_ptr<RpcLineReader>(new PosixRpcLineReader(input_fd, wake_fds[0], wake_fds[1]));
}

}  // namespace ava::app::rpc
