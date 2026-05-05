#include "ava/lsp/lsp_client.h"

#include "ava/core/json.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <optional>
#include <string_view>
#include <system_error>
#include <thread>
#include <utility>

#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

namespace ava::lsp {
namespace {

constexpr char kTrustedExecPath[] = "/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin";
constexpr std::size_t kMaxLspHeaderBytes = 64 * 1024;
constexpr std::size_t kMaxLspMessageBytes = 4 * 1024 * 1024;

class ScopedSignalIgnore {
 public:
  explicit ScopedSignalIgnore(int signal_number) : signal_number_(signal_number)
  {
    struct sigaction ignored{};
    ignored.sa_handler = SIG_IGN;
    sigemptyset(&ignored.sa_mask);
    active_ = sigaction(signal_number_, &ignored, &previous_) == 0;
  }

  ScopedSignalIgnore(ScopedSignalIgnore const&) = delete;
  ScopedSignalIgnore& operator=(ScopedSignalIgnore const&) = delete;

  ~ScopedSignalIgnore()
  {
    if (active_) sigaction(signal_number_, &previous_, nullptr);
  }

 private:
  int signal_number_ = 0;
  bool active_ = false;
  struct sigaction previous_{};
};

std::string command_label(std::vector<std::string> const& argv)
{
  std::string label;
  for (std::size_t index = 0; index < argv.size(); ++index) {
    if (index > 0) label += ' ';
    label += argv[index];
  }
  return label;
}

ava::core::Error lsp_error(ava::core::ErrorCategory category, std::string message, ServerConfig const& config)
{
  auto error = ava::core::Error(category, std::move(message));
  error.with_context("command", command_label(config.argv));
  error.with_context("workspace", config.workspace_root.string());
  return error;
}

ava::core::Error errno_error(std::string message, ServerConfig const& config)
{
  auto error = lsp_error(ava::core::ErrorCategory::Io, std::move(message), config);
  error.with_context("cause", std::strerror(errno));
  return error;
}

bool is_canceled(CancelCallback const& cancel_requested)
{
  return cancel_requested && cancel_requested();
}

ava::core::Error canceled_error(std::string message, ServerConfig const& config)
{
  auto error = lsp_error(ava::core::ErrorCategory::Unknown, std::move(message), config);
  error.with_context("canceled", "true");
  return error;
}

pid_t waitpid_retry(pid_t pid, int* status, int options)
{
  while (true) {
    auto const waited = waitpid(pid, status, options);
    if (waited < 0 && errno == EINTR) continue;
    return waited;
  }
}

ssize_t read_retry(int fd, char* data, std::size_t size)
{
  while (true) {
    auto const bytes = read(fd, data, size);
    if (bytes < 0 && errno == EINTR) continue;
    return bytes;
  }
}

ssize_t write_retry(int fd, char const* data, std::size_t size)
{
  while (true) {
    auto const bytes = write(fd, data, size);
    if (bytes < 0 && errno == EINTR) continue;
    return bytes;
  }
}

void close_fd(int& fd) noexcept
{
  if (fd >= 0) {
    close(fd);
    fd = -1;
  }
}

ava::core::Result<std::array<int, 2>> make_pipe(ServerConfig const& config)
{
  std::array<int, 2> fds{-1, -1};
  if (pipe(fds.data()) != 0) return std::unexpected(errno_error("failed to create LSP process pipe", config));
  return fds;
}

void close_nonstandard_fds()
{
  long const open_max = sysconf(_SC_OPEN_MAX);
  int const max_fd = open_max > 0 ? static_cast<int>(open_max) : 1024;
  for (int fd = STDERR_FILENO + 1; fd < max_fd; ++fd) close(fd);
}

std::string percent_encoded_file_path(std::filesystem::path const& path)
{
  auto const value = std::filesystem::absolute(path).lexically_normal().generic_string();
  constexpr char hex[] = "0123456789ABCDEF";
  std::string encoded;
  encoded.reserve(value.size());
  for (unsigned char const byte : value) {
    bool const unreserved = (byte >= 'A' && byte <= 'Z') || (byte >= 'a' && byte <= 'z') ||
                            (byte >= '0' && byte <= '9') || byte == '-' || byte == '_' || byte == '.' || byte == '~';
    if (byte == '/') {
      encoded.push_back('/');
    } else if (unreserved) {
      encoded.push_back(static_cast<char>(byte));
    } else {
      encoded.push_back('%');
      encoded.push_back(hex[byte >> 4]);
      encoded.push_back(hex[byte & 0x0F]);
    }
  }
  return encoded;
}

std::string file_uri(std::filesystem::path const& path)
{
  return "file://" + percent_encoded_file_path(path);
}

std::string json_string(std::string_view value)
{
  return "\"" + ava::core::json::escape(value) + "\"";
}

std::size_t remaining_ms(std::chrono::steady_clock::time_point deadline)
{
  auto const now = std::chrono::steady_clock::now();
  if (now >= deadline) return 0;
  return static_cast<std::size_t>(std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now).count());
}

ava::core::Result<std::size_t> parse_content_length(std::string_view header, ServerConfig const& config)
{
  constexpr std::string_view key = "Content-Length:";
  auto const position = header.find(key);
  if (position == std::string_view::npos) {
    return std::unexpected(lsp_error(ava::core::ErrorCategory::Tool, "LSP response is missing Content-Length", config));
  }
  std::size_t index = position + key.size();
  while (index < header.size() && (header[index] == ' ' || header[index] == '\t')) ++index;
  std::size_t end = index;
  while (end < header.size() && std::isdigit(static_cast<unsigned char>(header[end])) != 0) ++end;
  if (end == index) {
    return std::unexpected(
        lsp_error(ava::core::ErrorCategory::Tool, "LSP response has invalid Content-Length", config));
  }
  try {
    auto const parsed = static_cast<std::size_t>(std::stoull(std::string(header.substr(index, end - index))));
    if (parsed > kMaxLspMessageBytes) {
      auto error = lsp_error(ava::core::ErrorCategory::Tool, "LSP response Content-Length exceeds message cap", config);
      error.with_context("max_bytes", std::to_string(kMaxLspMessageBytes));
      return std::unexpected(std::move(error));
    }
    return parsed;
  } catch (...) {
    return std::unexpected(
        lsp_error(ava::core::ErrorCategory::Tool, "LSP response Content-Length is too large", config));
  }
}

std::string exit_detail(int status)
{
  if (WIFEXITED(status)) return "exit " + std::to_string(WEXITSTATUS(status));
  if (WIFSIGNALED(status)) return "signal " + std::to_string(WTERMSIG(status));
  return "unknown status " + std::to_string(status);
}

std::string diagnostic_code(std::string_view object)
{
  if (auto code = ava::core::json::string_field(object, "code")) return *code;
  if (auto code = ava::core::json::integer_field(object, "code")) return std::to_string(*code);
  return {};
}

ava::core::Result<std::vector<Diagnostic>> parse_diagnostics_response(std::string_view response,
                                                                      ServerConfig const& config,
                                                                      std::filesystem::path const& path)
{
  auto const result = ava::core::json::object_field(response, "result");
  if (!result) {
    auto error = lsp_error(ava::core::ErrorCategory::Tool, "LSP diagnostics response is missing result", config);
    error.with_context("path", path.string());
    return std::unexpected(std::move(error));
  }
  if (!ava::core::json::field_value_start(*result, "items")) {
    auto error = lsp_error(ava::core::ErrorCategory::Tool, "LSP diagnostics response is missing items", config);
    error.with_context("path", path.string());
    return std::unexpected(std::move(error));
  }

  std::vector<Diagnostic> diagnostics;
  for (auto const& object : ava::core::json::objects_in_array_field(*result, "items")) {
    auto message = ava::core::json::string_field(object, "message");
    auto range = ava::core::json::object_field(object, "range");
    auto start = range ? ava::core::json::object_field(*range, "start") : std::optional<std::string>{};
    if (!message || !start) {
      auto error = lsp_error(ava::core::ErrorCategory::Tool, "LSP diagnostic item is malformed", config);
      error.with_context("path", path.string());
      return std::unexpected(std::move(error));
    }

    auto const severity = ava::core::json::integer_field(object, "severity").value_or(0);
    auto const line = ava::core::json::integer_field(*start, "line").value_or(0);
    auto const character = ava::core::json::integer_field(*start, "character").value_or(0);
    diagnostics.push_back(Diagnostic{.severity = static_cast<int>(severity),
                                     .message = std::move(*message),
                                     .line = static_cast<int>(line),
                                     .column = static_cast<int>(character),
                                     .code = diagnostic_code(object)});
  }
  return diagnostics;
}

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
