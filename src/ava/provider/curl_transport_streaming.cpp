#include <poll.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <array>
#include <cerrno>
#include <cstring>
#include <string_view>
#include <utility>

#include "ava/provider/curl_transport.h"
#include "ava/provider/curl_transport_process.h"
#include "ava/provider/curl_transport_protocol.h"

namespace ava::provider {
namespace {

using detail::append_bounded;
using detail::close_nonstandard_fds;
using detail::kill_and_wait;
using detail::kMaxCurlResponseBytes;
using detail::kMaxCurlStderrBytes;
using detail::kTrustedCurlExecPath;
using detail::make_curl_pipe;
using detail::read_retry;
using detail::TempBodyFile;
using detail::UniqueFd;
using detail::waitpid_retry;
using detail::write_curl_config;

}  // namespace

bool CurlCliTransport::supports_streaming() const noexcept
{
  return true;
}

ava::core::Result<HttpResponse> CurlCliTransport::send_streaming(HttpRequest const& request,
                                                                 BodyChunkSink on_body_chunk,
                                                                 CancelCallback cancel_requested)
{
  if (request.include_response_headers) {
    return std::unexpected(ava::core::Error(ava::core::ErrorCategory::InvalidArgument,
                                            "streaming curl transport does not support response headers"));
  }
  auto body_file = TempBodyFile::create(request.body);
  if (!body_file) return std::unexpected(body_file.error());
  auto const config = detail::build_curl_config(request, body_file->path());

  auto stdin_pipe = make_curl_pipe();
  if (!stdin_pipe) return std::unexpected(stdin_pipe.error());
  auto stdout_pipe = make_curl_pipe();
  if (!stdout_pipe) return std::unexpected(stdout_pipe.error());
  auto stderr_pipe = make_curl_pipe();
  if (!stderr_pipe) return std::unexpected(stderr_pipe.error());

  UniqueFd stdin_read((*stdin_pipe)[0]);
  UniqueFd stdin_write((*stdin_pipe)[1]);
  UniqueFd stdout_read((*stdout_pipe)[0]);
  UniqueFd stdout_write((*stdout_pipe)[1]);
  UniqueFd stderr_read((*stderr_pipe)[0]);
  UniqueFd stderr_write((*stderr_pipe)[1]);

  pid_t const pid = fork();
  if (pid < 0) {
    auto error = ava::core::Error(ava::core::ErrorCategory::Io, "failed to fork curl process");
    error.with_context("cause", std::strerror(errno));
    return std::unexpected(std::move(error));
  }

  if (pid == 0) {
    close(stdin_write.get());
    close(stdout_read.get());
    close(stderr_read.get());
    dup2(stdin_read.get(), STDIN_FILENO);
    dup2(stdout_write.get(), STDOUT_FILENO);
    dup2(stderr_write.get(), STDERR_FILENO);
    close(stdin_read.get());
    close(stdout_write.get());
    close(stderr_write.get());
    close_nonstandard_fds();
    if (setenv("PATH", kTrustedCurlExecPath, 1) != 0) _exit(127);
    execlp("curl", "curl", "-q", "--no-buffer", "--config", "-", "--write-out", "\nAVA_HTTP_STATUS:%{http_code}",
           static_cast<char*>(nullptr));
    _exit(127);
  }

  stdin_read.reset();
  stdout_write.reset();
  stderr_write.reset();

  if (auto wrote_config = write_curl_config(stdin_write.get(), pid, config); !wrote_config) {
    return std::unexpected(std::move(wrote_config.error()));
  }
  stdin_write.reset();

  std::string body;
  std::string pending_stdout;
  std::string stderr_output;
  std::array<char, 4096> buffer{};
  bool stdout_open = true;
  bool stderr_open = true;

  auto deliver_body = [&](std::string_view chunk) -> ava::core::VoidResult {
    if (chunk.empty()) return {};
    if (body.size() > kMaxCurlResponseBytes || chunk.size() > kMaxCurlResponseBytes - body.size()) {
      auto error = ava::core::Error(ava::core::ErrorCategory::Provider, "curl response exceeded byte limit");
      error.with_context("max_bytes", std::to_string(kMaxCurlResponseBytes));
      return std::unexpected(std::move(error));
    }
    body.append(chunk);
    if (on_body_chunk) {
      if (auto delivered = on_body_chunk(chunk); !delivered) return std::unexpected(std::move(delivered.error()));
    }
    return {};
  };

  while (stdout_open || stderr_open) {
    if (cancel_requested && cancel_requested()) {
      kill_and_wait(pid);
      return std::unexpected(ava::core::Error(ava::core::ErrorCategory::Unknown, "transport request canceled"));
    }

    std::array<pollfd, 2> poll_fds{pollfd{.fd = stdout_open ? stdout_read.get() : -1, .events = POLLIN, .revents = 0},
                                   pollfd{.fd = stderr_open ? stderr_read.get() : -1, .events = POLLIN, .revents = 0}};
    int const poll_result = poll(poll_fds.data(), poll_fds.size(), 100);
    if (poll_result < 0 && errno == EINTR) continue;
    if (poll_result < 0) {
      auto error = ava::core::Error(ava::core::ErrorCategory::Io, "failed to poll curl output");
      error.with_context("cause", std::strerror(errno));
      kill_and_wait(pid);
      return std::unexpected(std::move(error));
    }
    if (poll_result == 0) continue;

    if (stdout_open && (poll_fds[0].revents & (POLLIN | POLLHUP | POLLERR)) != 0) {
      auto const count = read_retry(stdout_read.get(), buffer.data(), buffer.size());
      if (count < 0) {
        auto error = ava::core::Error(ava::core::ErrorCategory::Io, "failed to read curl output");
        error.with_context("cause", std::strerror(errno));
        kill_and_wait(pid);
        return std::unexpected(std::move(error));
      }
      if (count == 0) {
        stdout_open = false;
        stdout_read.reset();
      } else {
        pending_stdout.append(buffer.data(), static_cast<std::size_t>(count));
        if (body.size() + pending_stdout.size() > kMaxCurlResponseBytes + detail::kCurlStatusTailReserve) {
          auto error = ava::core::Error(ava::core::ErrorCategory::Provider, "curl response exceeded byte limit");
          error.with_context("max_bytes", std::to_string(kMaxCurlResponseBytes));
          kill_and_wait(pid);
          return std::unexpected(std::move(error));
        }
        if (pending_stdout.size() > detail::kCurlStatusTailReserve) {
          auto const emit_size = pending_stdout.size() - detail::kCurlStatusTailReserve;
          if (auto delivered = deliver_body(std::string_view(pending_stdout).substr(0, emit_size)); !delivered) {
            kill_and_wait(pid);
            return std::unexpected(std::move(delivered.error()));
          }
          pending_stdout.erase(0, emit_size);
        }
      }
    }

    if (stderr_open && (poll_fds[1].revents & (POLLIN | POLLHUP | POLLERR)) != 0) {
      auto const count = read_retry(stderr_read.get(), buffer.data(), buffer.size());
      if (count < 0) {
        auto error = ava::core::Error(ava::core::ErrorCategory::Io, "failed to read curl stderr");
        error.with_context("cause", std::strerror(errno));
        kill_and_wait(pid);
        return std::unexpected(std::move(error));
      }
      if (count == 0) {
        stderr_open = false;
        stderr_read.reset();
      } else {
        append_bounded(stderr_output, buffer.data(), static_cast<std::size_t>(count), kMaxCurlStderrBytes);
      }
    }
  }

  int status = 0;
  if (waitpid_retry(pid, &status, 0) < 0) {
    auto error = ava::core::Error(ava::core::ErrorCategory::Io, "failed to wait for curl process");
    error.with_context("cause", std::strerror(errno));
    return std::unexpected(std::move(error));
  }
  if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
    auto error = ava::core::Error(ava::core::ErrorCategory::Provider, "curl transport failed");
    error.with_context("exit_code", WIFEXITED(status) ? std::to_string(WEXITSTATUS(status)) : "signaled");
    if (!stderr_output.empty()) error.with_context("stderr", stderr_output.substr(0, 512));
    if (!body.empty()) error.with_context("output", body.substr(0, 512));
    return std::unexpected(std::move(error));
  }

  auto final = detail::parse_curl_output(std::move(pending_stdout), false);
  if (!final) {
    if (!stderr_output.empty()) final.error().with_context("stderr", stderr_output.substr(0, 512));
    return std::unexpected(std::move(final.error()));
  }
  if (auto delivered = deliver_body(final->body); !delivered) return std::unexpected(std::move(delivered.error()));
  final->body = std::move(body);
  return final;
}

}  // namespace ava::provider
