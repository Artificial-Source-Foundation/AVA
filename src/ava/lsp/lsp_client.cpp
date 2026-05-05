#include "ava/lsp/lsp_client.h"

#include <fcntl.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <string_view>
#include <thread>
#include <utility>

#include "ava/core/json.h"
#include "ava/lsp/lsp_client_support.h"

namespace ava::lsp {
namespace {

using detail::canceled_error;
using detail::close_fd;
using detail::close_nonstandard_fds;
using detail::errno_error;
using detail::exit_detail;
using detail::file_uri;
using detail::is_canceled;
using detail::json_string;
using detail::kTrustedExecPath;
using detail::lsp_error;
using detail::make_pipe;
using detail::parse_diagnostics_response;
using detail::waitpid_retry;

}  // namespace

SubprocessLspClient::SubprocessLspClient(ServerConfig config) : config_(std::move(config))
{
}

SubprocessLspClient::~SubprocessLspClient()
{
  terminate_child();
  close_fds();
}

ava::core::Result<std::shared_ptr<SubprocessLspClient>> SubprocessLspClient::start(ServerConfig config,
                                                                                   CancelCallback cancel_requested)
{
  if (config.argv.empty() || config.argv.front().empty()) {
    auto error =
        ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "LSP server command argv must not be empty");
    return std::unexpected(std::move(error));
  }
  if (config.request_timeout < std::chrono::milliseconds(100) || config.request_timeout > std::chrono::seconds(30)) {
    auto error = ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "LSP request timeout is out of bounds");
    error.with_context("min_ms", "100");
    error.with_context("max_ms", "30000");
    return std::unexpected(std::move(error));
  }
  if (is_canceled(cancel_requested)) {
    return std::unexpected(canceled_error("LSP startup canceled", config));
  }

  auto client = std::make_shared<SubprocessLspClient>(std::move(config));
  if (auto launched = client->launch(); !launched) return std::unexpected(std::move(launched.error()));
  if (auto initialized = client->initialize(cancel_requested); !initialized) {
    return std::unexpected(std::move(initialized.error()));
  }
  return client;
}

ava::core::VoidResult SubprocessLspClient::launch()
{
  auto stdin_pipe = make_pipe(config_);
  if (!stdin_pipe) return std::unexpected(std::move(stdin_pipe.error()));
  auto stdout_pipe = make_pipe(config_);
  if (!stdout_pipe) {
    close((*stdin_pipe)[0]);
    close((*stdin_pipe)[1]);
    return std::unexpected(std::move(stdout_pipe.error()));
  }

  pid_t const pid = fork();
  if (pid < 0) {
    auto const saved_errno = errno;
    close((*stdin_pipe)[0]);
    close((*stdin_pipe)[1]);
    close((*stdout_pipe)[0]);
    close((*stdout_pipe)[1]);
    errno = saved_errno;
    return std::unexpected(errno_error("failed to fork LSP server", config_));
  }

  if (pid == 0) {
    setpgid(0, 0);
    close((*stdin_pipe)[1]);
    close((*stdout_pipe)[0]);
    if (dup2((*stdin_pipe)[0], STDIN_FILENO) < 0) _exit(127);
    if (dup2((*stdout_pipe)[1], STDOUT_FILENO) < 0) _exit(127);
    int const dev_null = open("/dev/null", O_WRONLY);
    if (dev_null >= 0) {
      dup2(dev_null, STDERR_FILENO);
      close(dev_null);
    }
    close((*stdin_pipe)[0]);
    close((*stdout_pipe)[1]);
    if (chdir(config_.workspace_root.string().c_str()) != 0) _exit(127);
    if (setenv("PATH", kTrustedExecPath, 1) != 0) _exit(127);

    std::vector<char*> argv;
    argv.reserve(config_.argv.size() + 1);
    for (auto& arg : config_.argv) argv.push_back(arg.data());
    argv.push_back(nullptr);
    close_nonstandard_fds();
    execvp(argv[0], argv.data());
    _exit(127);
  }

  pid_ = pid;
  can_signal_group_ = setpgid(pid, pid) == 0 || errno == EACCES;
  close((*stdin_pipe)[0]);
  close((*stdout_pipe)[1]);
  stdin_fd_ = (*stdin_pipe)[1];
  stdout_fd_ = (*stdout_pipe)[0];

  int const stdout_flags = fcntl(stdout_fd_, F_GETFL, 0);
  int const stdin_flags = fcntl(stdin_fd_, F_GETFL, 0);
  if (stdout_flags < 0 || stdin_flags < 0 || fcntl(stdout_fd_, F_SETFL, stdout_flags | O_NONBLOCK) < 0 ||
      fcntl(stdin_fd_, F_SETFL, stdin_flags | O_NONBLOCK) < 0) {
    auto error = errno_error("failed to configure LSP stdio pipes", config_);
    terminate_child();
    close_fds();
    return std::unexpected(std::move(error));
  }
  return {};
}

ava::core::VoidResult SubprocessLspClient::initialize(CancelCallback cancel_requested)
{
  auto const root_uri = file_uri(config_.workspace_root);
  std::string const params = "{\"processId\":null,\"rootUri\":" + json_string(root_uri) + ",\"capabilities\":{}}";
  auto response = request_response("initialize", params, cancel_requested);
  if (!response) return std::unexpected(std::move(response.error()));
  return send_notification("initialized", "{}", cancel_requested);
}

ava::core::Result<std::vector<Diagnostic>> SubprocessLspClient::diagnostics(std::filesystem::path const& path,
                                                                            CancelCallback cancel_requested)
{
  if (is_canceled(cancel_requested)) {
    auto error = canceled_error("LSP diagnostics canceled", config_);
    error.with_context("path", path.string());
    return std::unexpected(std::move(error));
  }
  auto const uri = file_uri(path);
  std::string const params = "{\"textDocument\":{\"uri\":" + json_string(uri) + "}}";
  auto response = request_response("textDocument/diagnostic", params, cancel_requested);
  if (!response) return std::unexpected(std::move(response.error()));
  return parse_diagnostics_response(*response, config_, path);
}

ava::core::VoidResult SubprocessLspClient::send_notification(std::string_view method, std::string_view params_json,
                                                             CancelCallback cancel_requested)
{
  std::string const body =
      "{\"jsonrpc\":\"2.0\",\"method\":" + json_string(method) + ",\"params\":" + std::string(params_json) + "}";
  return write_message(body, cancel_requested);
}

ava::core::Result<std::string> SubprocessLspClient::request_response(std::string_view method,
                                                                     std::string_view params_json,
                                                                     CancelCallback cancel_requested)
{
  int const id = next_id_++;
  auto const deadline = std::chrono::steady_clock::now() + config_.request_timeout;
  std::string const body = "{\"jsonrpc\":\"2.0\",\"id\":" + std::to_string(id) + ",\"method\":" + json_string(method) +
                           ",\"params\":" + std::string(params_json) + "}";
  if (auto written = write_message(body, cancel_requested); !written) {
    return std::unexpected(std::move(written.error()));
  }

  while (true) {
    auto message = read_message(deadline, cancel_requested);
    if (!message) return std::unexpected(std::move(message.error()));
    auto const response_id = ava::core::json::integer_field(*message, "id");
    if (!response_id) {
      if (ava::core::json::string_field(*message, "method")) continue;
      auto error = lsp_error(ava::core::ErrorCategory::Tool, "LSP response is malformed", config_);
      error.with_context("method", std::string(method));
      return std::unexpected(std::move(error));
    }
    if (*response_id != id) continue;
    if (ava::core::json::object_field(*message, "error")) {
      auto error = lsp_error(ava::core::ErrorCategory::Tool, "LSP request returned an error", config_);
      error.with_context("method", std::string(method));
      return std::unexpected(std::move(error));
    }
    return *message;
  }
}

ava::core::VoidResult SubprocessLspClient::check_child_running()
{
  if (pid_ < 0) {
    return std::unexpected(lsp_error(ava::core::ErrorCategory::Io, "LSP server is not running", config_));
  }
  int status = 0;
  pid_t const waited = waitpid_retry(pid_, &status, WNOHANG);
  if (waited == 0) return {};
  if (waited == pid_) {
    pid_ = -1;
    auto error = lsp_error(ava::core::ErrorCategory::Io, "LSP server exited", config_);
    error.with_context("status", exit_detail(status));
    return std::unexpected(std::move(error));
  }
  if (errno == ECHILD) {
    pid_ = -1;
    return std::unexpected(lsp_error(ava::core::ErrorCategory::Io, "LSP server is already reaped", config_));
  }
  return std::unexpected(errno_error("failed to wait for LSP server", config_));
}

void SubprocessLspClient::close_fds() noexcept
{
  close_fd(stdin_fd_);
  close_fd(stdout_fd_);
}

void SubprocessLspClient::terminate_child() noexcept
{
  if (pid_ < 0) return;
  pid_t const target = can_signal_group_ ? -pid_ : pid_;
  kill(target, SIGTERM);
  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  int status = 0;
  pid_t const terminated = waitpid_retry(pid_, &status, WNOHANG);
  if (terminated == 0) {
    kill(target, SIGKILL);
    waitpid_retry(pid_, &status, 0);
  }
  pid_ = -1;
}

}  // namespace ava::lsp
