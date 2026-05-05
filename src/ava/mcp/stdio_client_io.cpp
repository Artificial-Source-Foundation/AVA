#include <poll.h>
#include <signal.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <utility>

#include "ava/mcp/process_support.h"
#include "ava/mcp/protocol.h"
#include "ava/mcp/stdio_client.h"
#include "ava/mcp/stdio_support.h"

namespace ava::mcp {
namespace {

constexpr int kMaxDrainReadsPerPoll = 16;

using detail::canceled_error;
using detail::close_fd;
using detail::errno_error;
using detail::exit_detail;
using detail::is_canceled;
using detail::protocol_error;
using detail::read_retry;
using detail::remaining_ms;
using detail::ScopedSignalIgnore;
using detail::with_mcp_server_context;
using detail::write_retry;

}  // namespace

ava::core::VoidResult McpStdioClient::write_message(std::string_view message,
                                                    std::chrono::steady_clock::time_point deadline,
                                                    std::chrono::milliseconds timeout, std::string_view timeout_message,
                                                    CancelCallback cancel_requested)
{
  if (stdin_fd_ < 0) return std::unexpected(protocol_error("MCP stdin is closed", server_));
  std::string const frame = "Content-Length: " + std::to_string(message.size()) + "\r\n\r\n" + std::string(message);
  std::size_t offset = 0;
  ScopedSignalIgnore const ignore_sigpipe(SIGPIPE);
  while (offset < frame.size()) {
    if (is_canceled(cancel_requested)) {
      terminate_child();
      return std::unexpected(canceled_error("MCP request canceled", server_));
    }
    auto const bytes = write_retry(stdin_fd_, frame.data() + offset, frame.size() - offset);
    if (bytes > 0) {
      offset += static_cast<std::size_t>(bytes);
      continue;
    }
    if (bytes == 0) return std::unexpected(errno_error("failed to write MCP request", server_));
    if (errno == EAGAIN || errno == EWOULDBLOCK) {
      if (auto writable = wait_for_writable(deadline, timeout, timeout_message, cancel_requested); !writable) {
        return std::unexpected(std::move(writable.error()));
      }
      continue;
    }
    return std::unexpected(errno_error("failed to write MCP request", server_));
  }
  return {};
}

ava::core::Result<std::optional<std::string>> McpStdioClient::try_extract_message()
{
  auto const header_end = mcp_header_end_offset(stdout_buffer_);
  if (!header_end) {
    if (stdout_buffer_.size() > kMcpMaxHeaderBytes) {
      auto error = protocol_error("MCP message header exceeds size cap", server_);
      error.with_context("max_bytes", std::to_string(kMcpMaxHeaderBytes));
      terminate_child();
      return std::unexpected(std::move(error));
    }
    return std::optional<std::string>{};
  }
  auto const header_size = *header_end;
  if (header_size > kMcpMaxHeaderBytes) {
    auto error = protocol_error("MCP message header exceeds size cap", server_);
    error.with_context("max_bytes", std::to_string(kMcpMaxHeaderBytes));
    terminate_child();
    return std::unexpected(std::move(error));
  }
  auto content_length =
      parse_mcp_content_length(std::string_view(stdout_buffer_).substr(0, header_size), options_.max_message_bytes);
  if (!content_length) {
    auto error = with_mcp_server_context(std::move(content_length.error()), server_);
    terminate_child();
    return std::unexpected(std::move(error));
  }
  if (stdout_buffer_.size() < header_size + *content_length) return std::optional<std::string>{};
  auto message = stdout_buffer_.substr(header_size, *content_length);
  stdout_buffer_.erase(0, header_size + *content_length);
  return std::optional<std::string>{std::move(message)};
}

ava::core::Result<std::string> McpStdioClient::read_message(std::chrono::steady_clock::time_point deadline,
                                                            std::chrono::milliseconds timeout,
                                                            std::string_view timeout_message,
                                                            std::string_view closed_message,
                                                            CancelCallback cancel_requested)
{
  while (true) {
    if (is_canceled(cancel_requested)) {
      terminate_child();
      return std::unexpected(canceled_error("MCP request canceled", server_));
    }
    auto extracted = try_extract_message();
    if (!extracted) return std::unexpected(std::move(extracted.error()));
    if (*extracted) return std::move(**extracted);

    if (stdout_fd_ < 0) {
      auto const reap_deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(10);
      do {
        if (auto reaped = reap_child(); !reaped) return std::unexpected(std::move(reaped.error()));
        if (child_exited_) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
      } while (std::chrono::steady_clock::now() < reap_deadline);
      auto error = protocol_error(
          stdout_buffer_.empty() ? std::string(closed_message) : "MCP protocol message ended before full frame",
          server_);
      if (child_exited_) error.with_context("status", exit_detail(child_status_));
      if (!stderr_tail_.empty()) error.with_context("stderr_tail", stderr_tail_);
      return std::unexpected(std::move(error));
    }
    if (auto reaped = reap_child(); !reaped) return std::unexpected(std::move(reaped.error()));
    if (std::chrono::steady_clock::now() >= deadline) {
      auto error = protocol_error(std::string(timeout_message), server_);
      error.with_context("timeout_ms", std::to_string(timeout.count()));
      if (!stderr_tail_.empty()) error.with_context("stderr_tail", stderr_tail_);
      terminate_child();
      return std::unexpected(std::move(error));
    }

    std::array<pollfd, 2> fds{pollfd{.fd = stdout_fd_, .events = POLLIN, .revents = 0},
                              pollfd{.fd = stderr_fd_, .events = POLLIN, .revents = 0}};
    int const poll_timeout = static_cast<int>(std::min<std::size_t>(remaining_ms(deadline), 100));
    int const polled = poll(fds.data(), fds.size(), poll_timeout);
    if (polled < 0) {
      if (errno == EINTR) continue;
      return std::unexpected(errno_error("failed to poll MCP process pipes", server_));
    }
    if (polled == 0) continue;
    if (fds[1].revents != 0) {
      if (auto drained = drain_stderr(); !drained) return std::unexpected(std::move(drained.error()));
    }
    if (fds[0].revents != 0) {
      if (auto drained = drain_stdout(); !drained) return std::unexpected(std::move(drained.error()));
    }
  }
}

ava::core::VoidResult McpStdioClient::wait_for_writable(std::chrono::steady_clock::time_point deadline,
                                                        std::chrono::milliseconds timeout,
                                                        std::string_view timeout_message,
                                                        CancelCallback cancel_requested)
{
  while (true) {
    if (is_canceled(cancel_requested)) {
      terminate_child();
      return std::unexpected(canceled_error("MCP request canceled", server_));
    }
    if (auto reaped = reap_child(); !reaped) return std::unexpected(std::move(reaped.error()));
    if (std::chrono::steady_clock::now() >= deadline) {
      auto error = protocol_error(std::string(timeout_message), server_);
      error.with_context("timeout_ms", std::to_string(timeout.count()));
      terminate_child();
      return std::unexpected(std::move(error));
    }
    std::array<pollfd, 2> fds{pollfd{.fd = stdin_fd_, .events = POLLOUT, .revents = 0},
                              pollfd{.fd = stderr_fd_, .events = POLLIN, .revents = 0}};
    int const poll_timeout = static_cast<int>(std::min<std::size_t>(remaining_ms(deadline), 100));
    int const polled = poll(fds.data(), fds.size(), poll_timeout);
    if (polled < 0) {
      if (errno == EINTR) continue;
      return std::unexpected(errno_error("failed to poll MCP request pipe", server_));
    }
    if (fds[1].revents != 0) {
      if (auto drained = drain_stderr(); !drained) return std::unexpected(std::move(drained.error()));
    }
    if ((fds[0].revents & POLLOUT) != 0) return {};
    if ((fds[0].revents & (POLLERR | POLLHUP | POLLNVAL)) != 0) {
      auto error = protocol_error("MCP request pipe closed", server_);
      if (child_exited_) error.with_context("status", exit_detail(child_status_));
      if (!stderr_tail_.empty()) error.with_context("stderr_tail", stderr_tail_);
      return std::unexpected(std::move(error));
    }
  }
}

ava::core::VoidResult McpStdioClient::drain_stdout()
{
  if (stdout_fd_ < 0) return {};
  std::array<char, 4096> buffer{};
  int reads = 0;
  while (true) {
    auto const bytes = read_retry(stdout_fd_, buffer.data(), buffer.size());
    if (bytes > 0) {
      stdout_buffer_.append(buffer.data(), static_cast<std::size_t>(bytes));
      if (++reads >= kMaxDrainReadsPerPoll) return {};
      continue;
    }
    if (bytes == 0) {
      close_fd(stdout_fd_);
      return {};
    }
    if (errno == EAGAIN || errno == EWOULDBLOCK) return {};
    return std::unexpected(errno_error("failed to read MCP stdout", server_));
  }
}

ava::core::VoidResult McpStdioClient::drain_stderr()
{
  if (stderr_fd_ < 0) return {};
  std::array<char, 4096> buffer{};
  int reads = 0;
  while (true) {
    auto const bytes = read_retry(stderr_fd_, buffer.data(), buffer.size());
    if (bytes > 0) {
      append_stderr(std::string_view(buffer.data(), static_cast<std::size_t>(bytes)));
      if (++reads >= kMaxDrainReadsPerPoll) return {};
      continue;
    }
    if (bytes == 0) {
      close_fd(stderr_fd_);
      return {};
    }
    if (errno == EAGAIN || errno == EWOULDBLOCK) return {};
    return std::unexpected(errno_error("failed to read MCP stderr", server_));
  }
}

}  // namespace ava::mcp
