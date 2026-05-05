#include "ava/provider/curl_transport.h"

#include <poll.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <array>
#include <cerrno>
#include <cstring>

#include "ava/provider/curl_transport_process.h"
#include "ava/provider/curl_transport_protocol.h"

namespace ava::provider {
namespace {

using detail::close_nonstandard_fds;
using detail::kill_and_wait;
using detail::kMaxCurlResponseBytes;
using detail::kTrustedCurlExecPath;
using detail::make_curl_pipe;
using detail::read_retry;
using detail::TempBodyFile;
using detail::UniqueFd;
using detail::waitpid_retry;
using detail::write_curl_config;

}  // namespace

ava::core::Result<HttpResponse> CurlCliTransport::send(HttpRequest const& request)
{
  return send(request, nullptr);
}

ava::core::Result<HttpResponse> CurlCliTransport::send(HttpRequest const& request, CancelCallback cancel_requested)
{
  if (cancel_requested && cancel_requested()) {
    return std::unexpected(ava::core::Error(ava::core::ErrorCategory::Unknown, "transport request canceled"));
  }
  auto body_file = TempBodyFile::create(request.body);
  if (!body_file) return std::unexpected(body_file.error());
  auto const config = detail::build_curl_config(request, body_file->path());

  auto stdin_pipe = make_curl_pipe();
  if (!stdin_pipe) return std::unexpected(stdin_pipe.error());
  auto stdout_pipe = make_curl_pipe();
  if (!stdout_pipe) return std::unexpected(stdout_pipe.error());

  UniqueFd stdin_read((*stdin_pipe)[0]);
  UniqueFd stdin_write((*stdin_pipe)[1]);
  UniqueFd stdout_read((*stdout_pipe)[0]);
  UniqueFd stdout_write((*stdout_pipe)[1]);

  pid_t const pid = fork();
  if (pid < 0) {
    auto error = ava::core::Error(ava::core::ErrorCategory::Io, "failed to fork curl process");
    error.with_context("cause", std::strerror(errno));
    return std::unexpected(std::move(error));
  }

  if (pid == 0) {
    close(stdin_write.get());
    close(stdout_read.get());
    dup2(stdin_read.get(), STDIN_FILENO);
    dup2(stdout_write.get(), STDOUT_FILENO);
    dup2(stdout_write.get(), STDERR_FILENO);
    close(stdin_read.get());
    close(stdout_write.get());
    close_nonstandard_fds();
    if (setenv("PATH", kTrustedCurlExecPath, 1) != 0) _exit(127);
    execlp("curl", "curl", "-q", "--config", "-", "--write-out", "\nAVA_HTTP_STATUS:%{http_code}",
           static_cast<char*>(nullptr));
    _exit(127);
  }

  stdin_read.reset();
  stdout_write.reset();

  if (auto wrote_config = write_curl_config(stdin_write.get(), pid, config); !wrote_config) {
    return std::unexpected(std::move(wrote_config.error()));
  }
  stdin_write.reset();

  std::string output;
  std::array<char, 4096> buffer{};
  bool stdout_open = true;
  while (stdout_open) {
    if (cancel_requested && cancel_requested()) {
      kill_and_wait(pid);
      return std::unexpected(ava::core::Error(ava::core::ErrorCategory::Unknown, "transport request canceled"));
    }

    pollfd poll_fd{.fd = stdout_read.get(), .events = POLLIN, .revents = 0};
    int const poll_result = poll(&poll_fd, 1, 100);
    if (poll_result < 0 && errno == EINTR) continue;
    if (poll_result < 0) {
      auto error = ava::core::Error(ava::core::ErrorCategory::Io, "failed to poll curl output");
      error.with_context("cause", std::strerror(errno));
      kill_and_wait(pid);
      return std::unexpected(std::move(error));
    }
    if (poll_result == 0) continue;
    if ((poll_fd.revents & (POLLIN | POLLHUP | POLLERR)) == 0) continue;

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
      break;
    }
    output.append(buffer.data(), static_cast<std::size_t>(count));
    if (output.size() > kMaxCurlResponseBytes) {
      auto error = ava::core::Error(ava::core::ErrorCategory::Provider, "curl response exceeded byte limit");
      error.with_context("max_bytes", std::to_string(kMaxCurlResponseBytes));
      kill_and_wait(pid);
      return std::unexpected(std::move(error));
    }
  }

  if (cancel_requested && cancel_requested()) {
    kill_and_wait(pid);
    return std::unexpected(ava::core::Error(ava::core::ErrorCategory::Unknown, "transport request canceled"));
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
    if (!output.empty()) error.with_context("output", output.substr(0, 512));
    return std::unexpected(std::move(error));
  }
  return detail::parse_curl_output(std::move(output), request.include_response_headers);
}

}  // namespace ava::provider
