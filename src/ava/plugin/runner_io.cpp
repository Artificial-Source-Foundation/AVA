#include <poll.h>
#include <signal.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <string>
#include <string_view>
#include <thread>
#include <utility>

#include "ava/plugin/process_support.h"
#include "ava/plugin/runner.h"
#include "ava/plugin/runner_support.h"

namespace ava::plugin {
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
using detail::write_retry;

}  // namespace

ava::core::VoidResult PluginProcess::write_record(std::string_view record,
                                                  std::chrono::steady_clock::time_point deadline,
                                                  std::chrono::milliseconds timeout, std::string_view timeout_message,
                                                  CancelCallback cancel_requested)
{
  if (stdin_fd_ < 0) return std::unexpected(protocol_error("plugin stdin is closed", manifest_));
  std::string const frame = std::string(record) + '\n';
  std::size_t offset = 0;
  ScopedSignalIgnore const ignore_sigpipe(SIGPIPE);
  while (offset < frame.size()) {
    if (is_canceled(cancel_requested)) {
      terminate_child();
      return std::unexpected(canceled_error("plugin request canceled", manifest_));
    }
    auto const bytes = write_retry(stdin_fd_, frame.data() + offset, frame.size() - offset);
    if (bytes > 0) {
      offset += static_cast<std::size_t>(bytes);
      continue;
    }
    if (bytes == 0) return std::unexpected(errno_error("failed to write plugin request", manifest_));
    if (errno == EAGAIN || errno == EWOULDBLOCK) {
      if (auto writable = wait_for_writable(deadline, timeout, timeout_message, cancel_requested); !writable) {
        return std::unexpected(std::move(writable.error()));
      }
      continue;
    }
    return std::unexpected(errno_error("failed to write plugin request", manifest_));
  }
  return {};
}

ava::core::Result<std::string> PluginProcess::read_record(std::chrono::steady_clock::time_point deadline,
                                                          std::chrono::milliseconds timeout,
                                                          std::string_view timeout_message,
                                                          std::string_view closed_message,
                                                          CancelCallback cancel_requested)
{
  while (true) {
    if (is_canceled(cancel_requested)) {
      terminate_child();
      return std::unexpected(canceled_error("plugin request canceled", manifest_));
    }
    if (auto record = stdout_buffer_.take_record()) {
      if (record->size() > options_.max_record_bytes) {
        auto error = protocol_error("plugin protocol record exceeds size cap", manifest_);
        error.with_context("max_bytes", std::to_string(options_.max_record_bytes));
        terminate_child();
        return std::unexpected(std::move(error));
      }
      return *record;
    }
    if (stdout_buffer_.exceeds_limit()) {
      auto error = protocol_error("plugin protocol record exceeds size cap", manifest_);
      error.with_context("max_bytes", std::to_string(options_.max_record_bytes));
      terminate_child();
      return std::unexpected(std::move(error));
    }
    if (stdout_fd_ < 0) {
      auto const reap_deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(10);
      do {
        if (auto reaped = reap_child(); !reaped) return std::unexpected(std::move(reaped.error()));
        if (child_exited_) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
      } while (std::chrono::steady_clock::now() < reap_deadline);
      auto error = protocol_error(
          stdout_buffer_.empty() ? std::string(closed_message) : "plugin protocol record ended without newline",
          manifest_);
      if (child_exited_) error.with_context("status", exit_detail(child_status_));
      if (!stderr_tail_.text().empty()) error.with_context("stderr_tail", stderr_tail_.text());
      return std::unexpected(std::move(error));
    }
    if (auto reaped = reap_child(); !reaped) return std::unexpected(std::move(reaped.error()));
    if (std::chrono::steady_clock::now() >= deadline) {
      auto error = protocol_error(std::string(timeout_message), manifest_);
      error.with_context("timeout_ms", std::to_string(timeout.count()));
      if (!stderr_tail_.text().empty()) error.with_context("stderr_tail", stderr_tail_.text());
      terminate_child();
      return std::unexpected(std::move(error));
    }

    std::array<pollfd, 2> fds{pollfd{.fd = stdout_fd_, .events = POLLIN, .revents = 0},
                              pollfd{.fd = stderr_fd_, .events = POLLIN, .revents = 0}};
    int const timeout = static_cast<int>(std::min<std::size_t>(remaining_ms(deadline), 100));
    int const polled = poll(fds.data(), fds.size(), timeout);
    if (polled < 0) {
      if (errno == EINTR) continue;
      return std::unexpected(errno_error("failed to poll plugin process pipes", manifest_));
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

ava::core::VoidResult PluginProcess::wait_for_writable(std::chrono::steady_clock::time_point deadline,
                                                       std::chrono::milliseconds timeout,
                                                       std::string_view timeout_message,
                                                       CancelCallback cancel_requested)
{
  while (true) {
    if (is_canceled(cancel_requested)) {
      terminate_child();
      return std::unexpected(canceled_error("plugin request canceled", manifest_));
    }
    if (auto reaped = reap_child(); !reaped) return std::unexpected(std::move(reaped.error()));
    if (std::chrono::steady_clock::now() >= deadline) {
      auto error = protocol_error(std::string(timeout_message), manifest_);
      error.with_context("timeout_ms", std::to_string(timeout.count()));
      terminate_child();
      return std::unexpected(std::move(error));
    }
    std::array<pollfd, 2> fds{pollfd{.fd = stdin_fd_, .events = POLLOUT, .revents = 0},
                              pollfd{.fd = stderr_fd_, .events = POLLIN, .revents = 0}};
    int const timeout = static_cast<int>(std::min<std::size_t>(remaining_ms(deadline), 100));
    int const polled = poll(fds.data(), fds.size(), timeout);
    if (polled < 0) {
      if (errno == EINTR) continue;
      return std::unexpected(errno_error("failed to poll plugin request pipe", manifest_));
    }
    if (fds[1].revents != 0) {
      if (auto drained = drain_stderr(); !drained) return std::unexpected(std::move(drained.error()));
    }
    if ((fds[0].revents & POLLOUT) != 0) return {};
    if ((fds[0].revents & (POLLERR | POLLHUP | POLLNVAL)) != 0) {
      auto error = protocol_error("plugin request pipe closed", manifest_);
      if (child_exited_) error.with_context("status", exit_detail(child_status_));
      if (!stderr_tail_.text().empty()) error.with_context("stderr_tail", stderr_tail_.text());
      return std::unexpected(std::move(error));
    }
  }
}

ava::core::VoidResult PluginProcess::drain_stdout()
{
  if (stdout_fd_ < 0) return {};
  std::array<char, 4096> buffer{};
  int reads = 0;
  while (true) {
    auto const bytes = read_retry(stdout_fd_, buffer.data(), buffer.size());
    if (bytes > 0) {
      stdout_buffer_.append(std::string_view(buffer.data(), static_cast<std::size_t>(bytes)));
      if (stdout_buffer_.exceeds_limit()) {
        auto error = protocol_error("plugin protocol record exceeds size cap", manifest_);
        error.with_context("max_bytes", std::to_string(options_.max_record_bytes));
        terminate_child();
        return std::unexpected(std::move(error));
      }
      ++reads;
      if (reads >= kMaxDrainReadsPerPoll) return {};
      continue;
    }
    if (bytes == 0) {
      close_fd(stdout_fd_);
      return {};
    }
    if (errno == EAGAIN || errno == EWOULDBLOCK) return {};
    return std::unexpected(errno_error("failed to read plugin stdout", manifest_));
  }
}

ava::core::VoidResult PluginProcess::drain_stderr()
{
  if (stderr_fd_ < 0) return {};
  std::array<char, 4096> buffer{};
  int reads = 0;
  while (true) {
    auto const bytes = read_retry(stderr_fd_, buffer.data(), buffer.size());
    if (bytes > 0) {
      stderr_tail_.append(std::string_view(buffer.data(), static_cast<std::size_t>(bytes)));
      ++reads;
      if (reads >= kMaxDrainReadsPerPoll) return {};
      continue;
    }
    if (bytes == 0) {
      close_fd(stderr_fd_);
      return {};
    }
    if (errno == EAGAIN || errno == EWOULDBLOCK) return {};
    return std::unexpected(errno_error("failed to read plugin stderr", manifest_));
  }
}

}  // namespace ava::plugin
