#include <poll.h>
#include <signal.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <string>
#include <string_view>
#include <utility>

#include "ava/lsp/lsp_client.h"
#include "ava/lsp/lsp_client_support.h"

namespace ava::lsp {
namespace {

using detail::canceled_error;
using detail::errno_error;
using detail::is_canceled;
using detail::kMaxLspHeaderBytes;
using detail::kMaxLspMessageBytes;
using detail::lsp_error;
using detail::parse_content_length;
using detail::read_retry;
using detail::remaining_ms;
using detail::ScopedSignalIgnore;
using detail::write_retry;

}  // namespace

ava::core::VoidResult SubprocessLspClient::write_message(std::string_view body, CancelCallback cancel_requested)
{
  if (auto running = check_child_running(); !running) return std::unexpected(std::move(running.error()));
  std::string const frame = "Content-Length: " + std::to_string(body.size()) + "\r\n\r\n" + std::string(body);
  std::size_t offset = 0;
  auto const deadline = std::chrono::steady_clock::now() + config_.request_timeout;
  ScopedSignalIgnore const ignore_sigpipe(SIGPIPE);
  while (offset < frame.size()) {
    if (is_canceled(cancel_requested)) {
      terminate_child();
      return std::unexpected(canceled_error("LSP request canceled", config_));
    }
    auto const bytes = write_retry(stdin_fd_, frame.data() + offset, frame.size() - offset);
    if (bytes < 0) {
      if (errno == EAGAIN || errno == EWOULDBLOCK) {
        if (auto writable = wait_for_writable(deadline, cancel_requested); !writable) {
          return std::unexpected(std::move(writable.error()));
        }
        continue;
      }
      return std::unexpected(errno_error("failed to write LSP request", config_));
    }
    if (bytes == 0) {
      return std::unexpected(lsp_error(ava::core::ErrorCategory::Io, "failed to write LSP request", config_));
    }
    offset += static_cast<std::size_t>(bytes);
  }
  return {};
}

ava::core::Result<std::string> SubprocessLspClient::read_message(std::chrono::steady_clock::time_point deadline,
                                                                 CancelCallback cancel_requested)
{
  while (true) {
    if (is_canceled(cancel_requested)) {
      terminate_child();
      return std::unexpected(canceled_error("LSP request canceled", config_));
    }
    auto const header_end = read_buffer_.find("\r\n\r\n");
    if (header_end != std::string::npos) {
      auto length = parse_content_length(std::string_view(read_buffer_).substr(0, header_end), config_);
      if (!length) return std::unexpected(std::move(length.error()));
      auto const body_start = header_end + 4;
      if (read_buffer_.size() >= body_start + *length) {
        auto body = read_buffer_.substr(body_start, *length);
        read_buffer_.erase(0, body_start + *length);
        return body;
      }
    } else if (read_buffer_.size() > kMaxLspHeaderBytes) {
      auto error = lsp_error(ava::core::ErrorCategory::Tool, "LSP response header exceeds size cap", config_);
      error.with_context("max_bytes", std::to_string(kMaxLspHeaderBytes));
      return std::unexpected(std::move(error));
    }
    if (read_buffer_.size() > kMaxLspMessageBytes + kMaxLspHeaderBytes) {
      auto error = lsp_error(ava::core::ErrorCategory::Tool, "LSP response exceeds message cap", config_);
      error.with_context("max_bytes", std::to_string(kMaxLspMessageBytes));
      return std::unexpected(std::move(error));
    }

    if (auto readable = wait_for_readable(deadline, cancel_requested); !readable) {
      return std::unexpected(std::move(readable.error()));
    }
    std::array<char, 4096> buffer{};
    auto const bytes = read_retry(stdout_fd_, buffer.data(), buffer.size());
    if (bytes > 0) {
      read_buffer_.append(buffer.data(), static_cast<std::size_t>(bytes));
      continue;
    }
    if (bytes == 0) {
      auto error = lsp_error(ava::core::ErrorCategory::Io, "LSP server closed stdout", config_);
      if (auto running = check_child_running(); !running) error.with_context("cause", running.error().format());
      return std::unexpected(std::move(error));
    }
    if (errno == EAGAIN || errno == EWOULDBLOCK) continue;
    return std::unexpected(errno_error("failed to read LSP response", config_));
  }
}

ava::core::VoidResult SubprocessLspClient::wait_for_readable(std::chrono::steady_clock::time_point deadline,
                                                             CancelCallback cancel_requested)
{
  if (is_canceled(cancel_requested)) {
    terminate_child();
    return std::unexpected(canceled_error("LSP request canceled", config_));
  }
  if (auto running = check_child_running(); !running) return std::unexpected(std::move(running.error()));
  auto const timeout_ms = remaining_ms(deadline);
  if (timeout_ms == 0) {
    auto error = lsp_error(ava::core::ErrorCategory::Tool, "timed out waiting for LSP response", config_);
    error.with_context("timeout_ms", std::to_string(config_.request_timeout.count()));
    terminate_child();
    return std::unexpected(std::move(error));
  }

  pollfd fd{.fd = stdout_fd_, .events = POLLIN, .revents = 0};
  int const timeout = static_cast<int>(std::min<std::size_t>(timeout_ms, 100));
  int const polled = poll(&fd, 1, timeout);
  if (polled < 0) {
    if (errno == EINTR) return {};
    return std::unexpected(errno_error("failed to poll LSP response", config_));
  }
  if (polled == 0) return check_child_running();
  if ((fd.revents & (POLLERR | POLLHUP | POLLNVAL)) != 0 && (fd.revents & POLLIN) == 0) {
    auto error = lsp_error(ava::core::ErrorCategory::Io, "LSP server pipe closed", config_);
    return std::unexpected(std::move(error));
  }
  return {};
}

ava::core::VoidResult SubprocessLspClient::wait_for_writable(std::chrono::steady_clock::time_point deadline,
                                                             CancelCallback cancel_requested)
{
  if (is_canceled(cancel_requested)) {
    terminate_child();
    return std::unexpected(canceled_error("LSP request canceled", config_));
  }
  if (auto running = check_child_running(); !running) return std::unexpected(std::move(running.error()));
  auto const timeout_ms = remaining_ms(deadline);
  if (timeout_ms == 0) {
    auto error = lsp_error(ava::core::ErrorCategory::Tool, "timed out writing LSP request", config_);
    error.with_context("timeout_ms", std::to_string(config_.request_timeout.count()));
    terminate_child();
    return std::unexpected(std::move(error));
  }

  pollfd fd{.fd = stdin_fd_, .events = POLLOUT, .revents = 0};
  int const timeout = static_cast<int>(std::min<std::size_t>(timeout_ms, 100));
  int const polled = poll(&fd, 1, timeout);
  if (polled < 0) {
    if (errno == EINTR) return {};
    return std::unexpected(errno_error("failed to poll LSP request pipe", config_));
  }
  if (polled == 0) return check_child_running();
  if ((fd.revents & (POLLERR | POLLHUP | POLLNVAL)) != 0 && (fd.revents & POLLOUT) == 0) {
    return std::unexpected(lsp_error(ava::core::ErrorCategory::Io, "LSP server request pipe closed", config_));
  }
  return {};
}

}  // namespace ava::lsp
