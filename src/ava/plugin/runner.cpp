#include "ava/plugin/runner.h"

#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/wait.h>
#ifdef __linux__
#include <sys/syscall.h>
#endif
#include <unistd.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <optional>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include "ava/core/json.h"

namespace ava::plugin {
namespace {

constexpr char kTrustedExecPath[] = "/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin";
constexpr int kMaxPluginJsonDepth = 128;
constexpr int kMaxDrainReadsPerPoll = 16;

class UniqueFd {
 public:
  explicit UniqueFd(int fd = -1) : fd_(fd) {}
  UniqueFd(const UniqueFd&) = delete;
  UniqueFd& operator=(const UniqueFd&) = delete;
  UniqueFd(UniqueFd&& other) noexcept : fd_(other.release()) {}
  UniqueFd& operator=(UniqueFd&& other) noexcept {
    if (this != &other) reset(other.release());
    return *this;
  }
  ~UniqueFd() { reset(); }

  [[nodiscard]] int get() const noexcept { return fd_; }
  [[nodiscard]] int release() noexcept {
    const int fd = fd_;
    fd_ = -1;
    return fd;
  }
  void reset(int fd = -1) noexcept {
    if (fd_ >= 0) close(fd_);
    fd_ = fd;
  }

 private:
  int fd_ = -1;
};

class ScopedSignalIgnore {
 public:
  explicit ScopedSignalIgnore(int signal) : signal_(signal) {
    struct sigaction action {};
    action.sa_handler = SIG_IGN;
    sigemptyset(&action.sa_mask);
    if (sigaction(signal_, &action, &previous_) == 0) installed_ = true;
  }

  ScopedSignalIgnore(const ScopedSignalIgnore&) = delete;
  ScopedSignalIgnore& operator=(const ScopedSignalIgnore&) = delete;
  ScopedSignalIgnore(ScopedSignalIgnore&&) = delete;
  ScopedSignalIgnore& operator=(ScopedSignalIgnore&&) = delete;

  ~ScopedSignalIgnore() {
    if (installed_) sigaction(signal_, &previous_, nullptr);
  }

 private:
  int signal_ = 0;
  struct sigaction previous_ {};
  bool installed_ = false;
};

ava::core::Error plugin_error(ava::core::ErrorCategory category, std::string message, const PluginManifest& manifest) {
  auto error = ava::core::Error(category, std::move(message));
  error.with_context("plugin", manifest.id);
  if (!manifest.path.empty()) error.with_context("manifest", manifest.path.string());
  return error;
}

ava::core::Error errno_error(std::string message, const PluginManifest& manifest) {
  auto error = plugin_error(ava::core::ErrorCategory::Io, std::move(message), manifest);
  error.with_context("cause", std::strerror(errno));
  return error;
}

ava::core::Error protocol_error(std::string message, const PluginManifest& manifest) {
  return plugin_error(ava::core::ErrorCategory::Tool, std::move(message), manifest);
}

ava::core::Result<std::array<int, 2>> make_pipe(const PluginManifest& manifest) {
  std::array<int, 2> fds{-1, -1};
  if (pipe(fds.data()) != 0) return std::unexpected(errno_error("failed to create plugin process pipe", manifest));
  for (auto& fd : fds) {
    if (fd > STDERR_FILENO) continue;
    const int moved = fcntl(fd, F_DUPFD_CLOEXEC, STDERR_FILENO + 1);
    const int move_errno = errno;
    close(fd);
    if (moved < 0) {
      for (const int pipe_fd : fds) {
        if (pipe_fd >= 0 && pipe_fd != fd) close(pipe_fd);
      }
      errno = move_errno;
      return std::unexpected(errno_error("failed to move plugin pipe above standard fds", manifest));
    }
    fd = moved;
  }
  return fds;
}

bool set_child_process_group(pid_t pid) {
  for (int attempt = 0; attempt < 20; ++attempt) {
    if (setpgid(pid, pid) == 0 || errno == EACCES) return true;
    if (errno != EINTR && errno != ESRCH) return false;
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  return false;
}

pid_t waitpid_retry(pid_t pid, int* status, int options) {
  while (true) {
    const auto waited = waitpid(pid, status, options);
    if (waited < 0 && errno == EINTR) continue;
    return waited;
  }
}

ssize_t read_retry(int fd, char* data, std::size_t size) {
  while (true) {
    const auto bytes = read(fd, data, size);
    if (bytes < 0 && errno == EINTR) continue;
    return bytes;
  }
}

ssize_t write_retry(int fd, const char* data, std::size_t size) {
  while (true) {
    const auto bytes = write(fd, data, size);
    if (bytes < 0 && errno == EINTR) continue;
    return bytes;
  }
}

std::size_t remaining_ms(std::chrono::steady_clock::time_point deadline) {
  const auto now = std::chrono::steady_clock::now();
  if (now >= deadline) return 0;
  return static_cast<std::size_t>(std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now).count());
}

void close_fd(int& fd) noexcept {
  if (fd >= 0) {
    close(fd);
    fd = -1;
  }
}

void close_nonstandard_fds() {
#if defined(__linux__) && defined(SYS_close_range)
  if (syscall(SYS_close_range, static_cast<unsigned int>(STDERR_FILENO + 1), ~0U, 0U) == 0) return;
#endif
  const long open_max = sysconf(_SC_OPEN_MAX);
  const int max_fd = open_max > 0 ? static_cast<int>(open_max) : 1024;
  for (int fd = STDERR_FILENO + 1; fd < max_fd; ++fd) close(fd);
}

std::string json_string(std::string_view value) { return "\"" + ava::core::json::escape(value) + "\""; }

std::optional<bool> bool_field(std::string_view object, std::string_view key) {
  const auto start = ava::core::json::field_value_start(object, key);
  if (!start) return std::nullopt;
  const auto valid_terminator = [](std::string_view value, std::size_t offset) {
    while (offset < value.size() && std::isspace(static_cast<unsigned char>(value[offset])) != 0) ++offset;
    return offset >= value.size() || value[offset] == ',' || value[offset] == '}';
  };
  if (object.substr(*start, 4) == "true" && valid_terminator(object, *start + 4)) return true;
  if (object.substr(*start, 5) == "false" && valid_terminator(object, *start + 5)) return false;
  return std::nullopt;
}

std::string exit_detail(int status) {
  if (WIFEXITED(status)) return "exit " + std::to_string(WEXITSTATUS(status));
  if (WIFSIGNALED(status)) return "signal " + std::to_string(WTERMSIG(status));
  return "unknown status " + std::to_string(status);
}

std::vector<std::string> plugin_argv(const PluginManifest& manifest) {
  std::vector<std::string> argv;
  argv.reserve(manifest.entrypoint.args.size() + 1);
  argv.push_back(manifest.entrypoint.command);
  argv.insert(argv.end(), manifest.entrypoint.args.begin(), manifest.entrypoint.args.end());
  return argv;
}

std::filesystem::path child_working_dir(const PluginManifest& manifest, const PluginRunnerOptions& options) {
  if (!manifest.directory.empty()) return manifest.directory;
  if (!options.workspace_dir.empty()) return options.workspace_dir;
  return std::filesystem::current_path();
}

bool json_depth_within_limit(std::string_view value, int max_depth) {
  bool in_string = false;
  bool escaped = false;
  int depth = 0;
  for (const char ch : value) {
    if (escaped) {
      escaped = false;
      continue;
    }
    if (ch == '\\' && in_string) {
      escaped = true;
      continue;
    }
    if (ch == '"') {
      in_string = !in_string;
      continue;
    }
    if (in_string) continue;
    if (ch == '{' || ch == '[') {
      ++depth;
      if (depth > max_depth) return false;
    } else if (ch == '}' || ch == ']') {
      --depth;
      if (depth < 0) return false;
    }
  }
  return true;
}

std::optional<PluginInitialization> parse_initialized_response(std::string_view record) {
  if (!json_depth_within_limit(record, kMaxPluginJsonDepth)) return std::nullopt;
  if (!ava::core::json::is_valid_object(record)) return std::nullopt;
  auto id = ava::core::json::string_field(record, "id");
  auto type = ava::core::json::string_field(record, "type");
  auto api_version = ava::core::json::string_field(record, "api_version");
  auto plugin_version = ava::core::json::string_field(record, "plugin_version");
  auto contributions = ava::core::json::object_field(record, "contributions");
  if (!id || *id != "ava_1" || !type || *type != "initialized" || !api_version || !plugin_version ||
      plugin_version->empty() || !contributions) {
    return std::nullopt;
  }
  if (*api_version != kPluginApiVersion) return std::nullopt;
  return PluginInitialization{.api_version = std::move(*api_version),
                              .plugin_version = std::move(*plugin_version),
                              .contributions_json = std::move(*contributions),
                              .raw_json = std::string(record)};
}

std::optional<PluginToolCallResult> parse_tool_result_response(std::string_view record, std::string_view request_id) {
  if (!json_depth_within_limit(record, kMaxPluginJsonDepth)) return std::nullopt;
  if (!ava::core::json::is_valid_object(record)) return std::nullopt;
  auto id = ava::core::json::string_field(record, "id");
  auto type = ava::core::json::string_field(record, "type");
  auto ok = bool_field(record, "ok");
  auto content = ava::core::json::string_field(record, "content");
  if (!id || *id != request_id || !type || *type != "tool.result" || !ok || !content) return std::nullopt;
  auto metadata = ava::core::json::object_field(record, "metadata");
  return PluginToolCallResult{.ok = *ok,
                              .content = std::move(*content),
                              .metadata_json = metadata.value_or(std::string{}),
                              .raw_json = std::string(record)};
}

std::optional<PluginCommandCallResult> parse_command_result_response(std::string_view record,
                                                                      std::string_view request_id) {
  if (!json_depth_within_limit(record, kMaxPluginJsonDepth)) return std::nullopt;
  if (!ava::core::json::is_valid_object(record)) return std::nullopt;
  auto id = ava::core::json::string_field(record, "id");
  auto type = ava::core::json::string_field(record, "type");
  auto ok = bool_field(record, "ok");
  auto content = ava::core::json::string_field(record, "content");
  if (!id || *id != request_id || !type || *type != "command.result" || !ok || !content) return std::nullopt;
  auto metadata = ava::core::json::object_field(record, "metadata");
  return PluginCommandCallResult{.ok = *ok,
                                 .content = std::move(*content),
                                 .metadata_json = metadata.value_or(std::string{}),
                                  .raw_json = std::string(record)};
}

std::optional<PluginEventObserveResult> parse_event_observed_response(std::string_view record,
                                                                      std::string_view request_id) {
  if (!json_depth_within_limit(record, kMaxPluginJsonDepth)) return std::nullopt;
  if (!ava::core::json::is_valid_object(record)) return std::nullopt;
  auto id = ava::core::json::string_field(record, "id");
  auto type = ava::core::json::string_field(record, "type");
  auto ok = bool_field(record, "ok");
  if (!id || *id != request_id || !type || *type != "event.observed" || !ok) return std::nullopt;
  auto content = ava::core::json::string_field(record, "content").value_or("");
  auto metadata = ava::core::json::object_field(record, "metadata");
  return PluginEventObserveResult{.ok = *ok,
                                  .content = std::move(content),
                                  .metadata_json = metadata.value_or(std::string{}),
                                  .raw_json = std::string(record)};
}

}  // namespace

PluginProcess::PluginProcess(PluginManifest manifest, PluginRunnerOptions options)
    : manifest_(std::move(manifest)), options_(std::move(options)) {}

PluginProcess::~PluginProcess() {
  terminate_child();
  close_fds();
}

ava::core::Result<std::unique_ptr<PluginProcess>> PluginProcess::start(PluginManifest manifest,
                                                                       PluginRunnerOptions options) {
  if (manifest.entrypoint.command.empty()) {
    return std::unexpected(plugin_error(ava::core::ErrorCategory::InvalidArgument,
                                        "plugin entrypoint command must not be empty", manifest));
  }
  if (options.workspace_dir.empty()) options.workspace_dir = std::filesystem::current_path();
  if (options.startup_timeout < std::chrono::milliseconds(50) || options.startup_timeout > std::chrono::seconds(30)) {
    auto error =
        plugin_error(ava::core::ErrorCategory::InvalidArgument, "plugin startup timeout is out of bounds", manifest);
    error.with_context("min_ms", "50");
    error.with_context("max_ms", "30000");
    return std::unexpected(std::move(error));
  }
  if (options.request_timeout < std::chrono::milliseconds(50) || options.request_timeout > std::chrono::seconds(30)) {
    auto error =
        plugin_error(ava::core::ErrorCategory::InvalidArgument, "plugin request timeout is out of bounds", manifest);
    error.with_context("min_ms", "50");
    error.with_context("max_ms", "30000");
    return std::unexpected(std::move(error));
  }
  if (options.max_record_bytes == 0 || options.max_stderr_bytes == 0) {
    return std::unexpected(plugin_error(ava::core::ErrorCategory::InvalidArgument,
                                        "plugin runner byte limits must be non-zero", manifest));
  }

  auto process = std::unique_ptr<PluginProcess>(new PluginProcess(std::move(manifest), std::move(options)));
  if (auto launched = process->launch(); !launched) return std::unexpected(std::move(launched.error()));
  if (auto initialized = process->initialize(); !initialized) return std::unexpected(std::move(initialized.error()));
  return process;
}

const PluginManifest& PluginProcess::manifest() const noexcept { return manifest_; }

const PluginInitialization& PluginProcess::initialization() const noexcept { return initialization_; }

const std::string& PluginProcess::stderr_tail() const noexcept { return stderr_tail_; }

bool PluginProcess::stderr_truncated() const noexcept { return stderr_truncated_; }

ava::core::VoidResult PluginProcess::launch() {
  auto stdin_pipe = make_pipe(manifest_);
  if (!stdin_pipe) return std::unexpected(std::move(stdin_pipe.error()));
  UniqueFd stdin_read((*stdin_pipe)[0]);
  UniqueFd stdin_write((*stdin_pipe)[1]);

  auto stdout_pipe = make_pipe(manifest_);
  if (!stdout_pipe) return std::unexpected(std::move(stdout_pipe.error()));
  UniqueFd stdout_read((*stdout_pipe)[0]);
  UniqueFd stdout_write((*stdout_pipe)[1]);

  auto stderr_pipe = make_pipe(manifest_);
  if (!stderr_pipe) return std::unexpected(std::move(stderr_pipe.error()));
  UniqueFd stderr_read((*stderr_pipe)[0]);
  UniqueFd stderr_write((*stderr_pipe)[1]);

  auto argv_strings = plugin_argv(manifest_);
  std::vector<char*> argv;
  argv.reserve(argv_strings.size() + 1);
  for (auto& arg : argv_strings) argv.push_back(arg.data());
  argv.push_back(nullptr);

  const auto cwd = child_working_dir(manifest_, options_).string();
  const pid_t pid = fork();
  if (pid < 0) return std::unexpected(errno_error("failed to fork plugin process", manifest_));

  if (pid == 0) {
    setpgid(0, 0);
    stdin_write.reset();
    stdout_read.reset();
    stderr_read.reset();
    if (dup2(stdin_read.get(), STDIN_FILENO) < 0) _exit(127);
    if (dup2(stdout_write.get(), STDOUT_FILENO) < 0) _exit(127);
    if (dup2(stderr_write.get(), STDERR_FILENO) < 0) _exit(127);
    stdin_read.reset();
    stdout_write.reset();
    stderr_write.reset();
    if (chdir(cwd.c_str()) != 0) _exit(127);
    if (setenv("PATH", kTrustedExecPath, 1) != 0) _exit(127);
    close_nonstandard_fds();
    execvp(argv[0], argv.data());
    _exit(127);
  }

  pid_ = static_cast<int>(pid);
  can_signal_group_ = set_child_process_group(pid);
  stdin_read.reset();
  stdout_write.reset();
  stderr_write.reset();
  stdin_fd_ = stdin_write.release();
  stdout_fd_ = stdout_read.release();
  stderr_fd_ = stderr_read.release();

  if (auto nonblocking = set_pipe_nonblocking(stdin_fd_, "stdin"); !nonblocking) {
    terminate_child();
    close_fds();
    return std::unexpected(std::move(nonblocking.error()));
  }
  if (auto nonblocking = set_pipe_nonblocking(stdout_fd_, "stdout"); !nonblocking) {
    terminate_child();
    close_fds();
    return std::unexpected(std::move(nonblocking.error()));
  }
  if (auto nonblocking = set_pipe_nonblocking(stderr_fd_, "stderr"); !nonblocking) {
    terminate_child();
    close_fds();
    return std::unexpected(std::move(nonblocking.error()));
  }
  return {};
}

ava::core::VoidResult PluginProcess::initialize() {
  const auto deadline = std::chrono::steady_clock::now() + options_.startup_timeout;
  const std::string request =
      "{\"id\":\"ava_1\",\"type\":\"initialize\",\"api_version\":" + json_string(kPluginApiVersion) +
      ",\"plugin_id\":" + json_string(manifest_.id) + ",\"workspace\":" + json_string(options_.workspace_dir.string()) +
      "}";
  if (auto written =
          write_record(request, deadline, options_.startup_timeout, "timed out writing plugin initialization");
      !written) {
    return std::unexpected(std::move(written.error()));
  }

  auto record = read_record(deadline, options_.startup_timeout, "timed out waiting for plugin initialization",
                            "plugin process closed stdout before initialization");
  if (!record) return std::unexpected(std::move(record.error()));
  auto initialized = parse_initialized_response(*record);
  if (!initialized) {
    auto error = protocol_error("plugin initialize response is malformed or unsupported", manifest_);
    error.with_context("response", record->substr(0, 512));
    return std::unexpected(std::move(error));
  }
  initialization_ = std::move(*initialized);
  return {};
}

ava::core::Result<PluginToolCallResult> PluginProcess::call_tool(std::string_view tool_name,
                                                                 std::string_view arguments_json,
                                                                 std::string_view call_id) {
  if (tool_name.empty()) {
    return std::unexpected(
        plugin_error(ava::core::ErrorCategory::InvalidArgument, "plugin tool name must not be empty", manifest_));
  }
  if (!ava::core::json::is_valid_object(arguments_json)) {
    auto error = plugin_error(ava::core::ErrorCategory::InvalidArgument, "plugin tool arguments must be a JSON object",
                              manifest_);
    error.with_context("tool", std::string(tool_name));
    return std::unexpected(std::move(error));
  }

  const auto deadline = std::chrono::steady_clock::now() + options_.request_timeout;
  const std::string request_id =
      call_id.empty() ? "ava_" + std::to_string(next_request_id_++) : "ava_tool_" + std::string(call_id);
  std::string request =
      "{\"id\":" + json_string(request_id) + ",\"type\":\"tool.call\",\"tool\":" + json_string(tool_name) +
      ",\"arguments\":" + std::string(arguments_json) + ",\"context\":{\"call_id\":" + json_string(call_id) +
      ",\"workspace\":" + json_string(options_.workspace_dir.string()) + "}}";
  if (auto written = write_record(request, deadline, options_.request_timeout, "timed out writing plugin tool request");
      !written) {
    return std::unexpected(std::move(written.error()));
  }

  auto record = read_record(deadline, options_.request_timeout, "timed out waiting for plugin tool result",
                            "plugin process closed stdout before plugin tool result");
  if (!record) return std::unexpected(std::move(record.error()));
  auto result = parse_tool_result_response(*record, request_id);
  if (!result) {
    auto error = protocol_error("plugin tool result is malformed", manifest_);
    error.with_context("tool", std::string(tool_name));
    error.with_context("response", record->substr(0, 512));
    return std::unexpected(std::move(error));
  }
  return *result;
}

ava::core::Result<PluginCommandCallResult> PluginProcess::call_command(std::string_view command_name,
                                                                        std::string_view arguments_json,
                                                                        std::string_view call_id) {
  if (command_name.empty()) {
    return std::unexpected(
        plugin_error(ava::core::ErrorCategory::InvalidArgument, "plugin command name must not be empty", manifest_));
  }
  if (!ava::core::json::is_valid_object(arguments_json)) {
    auto error = plugin_error(ava::core::ErrorCategory::InvalidArgument,
                              "plugin command arguments must be a JSON object", manifest_);
    error.with_context("command", std::string(command_name));
    return std::unexpected(std::move(error));
  }

  const auto deadline = std::chrono::steady_clock::now() + options_.request_timeout;
  const std::string request_id =
      call_id.empty() ? "ava_" + std::to_string(next_request_id_++) : "ava_command_" + std::string(call_id);
  std::string request = "{\"id\":" + json_string(request_id) + ",\"type\":\"command.call\",\"command\":" +
                        json_string(command_name) + ",\"arguments\":" + std::string(arguments_json) +
                        ",\"context\":{\"call_id\":" + json_string(call_id) + ",\"workspace\":" +
                        json_string(options_.workspace_dir.string()) + "}}";
  if (auto written =
          write_record(request, deadline, options_.request_timeout, "timed out writing plugin command request");
      !written) {
    return std::unexpected(std::move(written.error()));
  }

  auto record = read_record(deadline, options_.request_timeout, "timed out waiting for plugin command result",
                            "plugin process closed stdout before plugin command result");
  if (!record) return std::unexpected(std::move(record.error()));
  auto result = parse_command_result_response(*record, request_id);
  if (!result) {
    auto error = protocol_error("plugin command result is malformed", manifest_);
    error.with_context("command", std::string(command_name));
    error.with_context("response", record->substr(0, 512));
    return std::unexpected(std::move(error));
  }
  return *result;
}

ava::core::Result<PluginEventObserveResult> PluginProcess::observe_event(std::string_view event_name,
                                                                         std::string_view payload_json,
                                                                         std::string_view call_id) {
  if (event_name.empty()) {
    return std::unexpected(
        plugin_error(ava::core::ErrorCategory::InvalidArgument, "plugin event name must not be empty", manifest_));
  }
  if (!ava::core::json::is_valid_object(payload_json)) {
    auto error = plugin_error(ava::core::ErrorCategory::InvalidArgument,
                              "plugin event payload must be a JSON object", manifest_);
    error.with_context("event", std::string(event_name));
    return std::unexpected(std::move(error));
  }

  const auto deadline = std::chrono::steady_clock::now() + options_.request_timeout;
  const std::string request_id =
      call_id.empty() ? "ava_" + std::to_string(next_request_id_++) : "ava_event_" + std::string(call_id);
  std::string request = "{\"id\":" + json_string(request_id) + ",\"type\":\"event.observe\",\"event\":" +
                        json_string(event_name) + ",\"payload\":" + std::string(payload_json) +
                        ",\"context\":{\"call_id\":" + json_string(call_id) + ",\"workspace\":" +
                        json_string(options_.workspace_dir.string()) + "}}";
  if (auto written =
          write_record(request, deadline, options_.request_timeout, "timed out writing plugin event request");
      !written) {
    return std::unexpected(std::move(written.error()));
  }

  auto record = read_record(deadline, options_.request_timeout, "timed out waiting for plugin event response",
                            "plugin process closed stdout before plugin event response");
  if (!record) return std::unexpected(std::move(record.error()));
  auto result = parse_event_observed_response(*record, request_id);
  if (!result) {
    auto error = protocol_error("plugin event response is malformed", manifest_);
    error.with_context("event", std::string(event_name));
    error.with_context("response", record->substr(0, 512));
    return std::unexpected(std::move(error));
  }
  return *result;
}

ava::core::VoidResult PluginProcess::write_record(std::string_view record,
                                                  std::chrono::steady_clock::time_point deadline,
                                                  std::chrono::milliseconds timeout, std::string_view timeout_message) {
  if (stdin_fd_ < 0) return std::unexpected(protocol_error("plugin stdin is closed", manifest_));
  const std::string frame = std::string(record) + '\n';
  std::size_t offset = 0;
  const ScopedSignalIgnore ignore_sigpipe(SIGPIPE);
  while (offset < frame.size()) {
    const auto bytes = write_retry(stdin_fd_, frame.data() + offset, frame.size() - offset);
    if (bytes > 0) {
      offset += static_cast<std::size_t>(bytes);
      continue;
    }
    if (bytes == 0) return std::unexpected(errno_error("failed to write plugin request", manifest_));
    if (errno == EAGAIN || errno == EWOULDBLOCK) {
      if (auto writable = wait_for_writable(deadline, timeout, timeout_message); !writable) {
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
                                                          std::string_view closed_message) {
  while (true) {
    if (const auto newline = stdout_buffer_.find('\n'); newline != std::string::npos) {
      auto record = stdout_buffer_.substr(0, newline);
      stdout_buffer_.erase(0, newline + 1);
      if (!record.empty() && record.back() == '\r') record.pop_back();
      if (record.size() > options_.max_record_bytes) {
        auto error = protocol_error("plugin protocol record exceeds size cap", manifest_);
        error.with_context("max_bytes", std::to_string(options_.max_record_bytes));
        terminate_child();
        return std::unexpected(std::move(error));
      }
      return record;
    }
    if (stdout_buffer_.size() > options_.max_record_bytes) {
      auto error = protocol_error("plugin protocol record exceeds size cap", manifest_);
      error.with_context("max_bytes", std::to_string(options_.max_record_bytes));
      terminate_child();
      return std::unexpected(std::move(error));
    }
    if (stdout_fd_ < 0) {
      if (auto reaped = reap_child(); !reaped) return std::unexpected(std::move(reaped.error()));
      auto error = protocol_error(
          stdout_buffer_.empty() ? std::string(closed_message) : "plugin protocol record ended without newline",
          manifest_);
      if (child_exited_) error.with_context("status", exit_detail(child_status_));
      if (!stderr_tail_.empty()) error.with_context("stderr_tail", stderr_tail_);
      return std::unexpected(std::move(error));
    }
    if (auto reaped = reap_child(); !reaped) return std::unexpected(std::move(reaped.error()));
    if (std::chrono::steady_clock::now() >= deadline) {
      auto error = protocol_error(std::string(timeout_message), manifest_);
      error.with_context("timeout_ms", std::to_string(timeout.count()));
      if (!stderr_tail_.empty()) error.with_context("stderr_tail", stderr_tail_);
      terminate_child();
      return std::unexpected(std::move(error));
    }

    std::array<pollfd, 2> fds{pollfd{.fd = stdout_fd_, .events = POLLIN, .revents = 0},
                              pollfd{.fd = stderr_fd_, .events = POLLIN, .revents = 0}};
    const int timeout = static_cast<int>(std::min<std::size_t>(remaining_ms(deadline), 100));
    const int polled = poll(fds.data(), fds.size(), timeout);
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
                                                       std::string_view timeout_message) {
  while (true) {
    if (auto reaped = reap_child(); !reaped) return std::unexpected(std::move(reaped.error()));
    if (std::chrono::steady_clock::now() >= deadline) {
      auto error = protocol_error(std::string(timeout_message), manifest_);
      error.with_context("timeout_ms", std::to_string(timeout.count()));
      terminate_child();
      return std::unexpected(std::move(error));
    }
    std::array<pollfd, 2> fds{pollfd{.fd = stdin_fd_, .events = POLLOUT, .revents = 0},
                              pollfd{.fd = stderr_fd_, .events = POLLIN, .revents = 0}};
    const int timeout = static_cast<int>(std::min<std::size_t>(remaining_ms(deadline), 100));
    const int polled = poll(fds.data(), fds.size(), timeout);
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
      if (!stderr_tail_.empty()) error.with_context("stderr_tail", stderr_tail_);
      return std::unexpected(std::move(error));
    }
  }
}

ava::core::VoidResult PluginProcess::drain_stdout() {
  if (stdout_fd_ < 0) return {};
  std::array<char, 4096> buffer{};
  int reads = 0;
  while (true) {
    const auto bytes = read_retry(stdout_fd_, buffer.data(), buffer.size());
    if (bytes > 0) {
      stdout_buffer_.append(buffer.data(), static_cast<std::size_t>(bytes));
      const auto newline = stdout_buffer_.find('\n');
      if ((newline == std::string::npos && stdout_buffer_.size() > options_.max_record_bytes) ||
          (newline != std::string::npos && newline > options_.max_record_bytes)) {
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

ava::core::VoidResult PluginProcess::drain_stderr() {
  if (stderr_fd_ < 0) return {};
  std::array<char, 4096> buffer{};
  int reads = 0;
  while (true) {
    const auto bytes = read_retry(stderr_fd_, buffer.data(), buffer.size());
    if (bytes > 0) {
      append_stderr(std::string_view(buffer.data(), static_cast<std::size_t>(bytes)));
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

ava::core::VoidResult PluginProcess::reap_child() {
  if (pid_ < 0) return {};
  int status = 0;
  const pid_t waited = waitpid_retry(pid_, &status, WNOHANG);
  if (waited == 0) return {};
  if (waited == pid_) {
    child_status_ = status;
    child_exited_ = true;
    pid_ = -1;
    return {};
  }
  if (errno == ECHILD) {
    pid_ = -1;
    return {};
  }
  return std::unexpected(errno_error("failed to wait for plugin process", manifest_));
}

ava::core::VoidResult PluginProcess::set_pipe_nonblocking(int fd, std::string_view pipe_name) {
  const int flags = fcntl(fd, F_GETFL, 0);
  if (flags < 0 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) {
    auto error = errno_error("failed to configure plugin process pipe", manifest_);
    error.with_context("pipe", std::string(pipe_name));
    return std::unexpected(std::move(error));
  }
  return {};
}

void PluginProcess::append_stderr(std::string_view chunk) {
  if (chunk.empty()) return;
  const auto max_bytes = options_.max_stderr_bytes;
  if (chunk.size() >= max_bytes) {
    stderr_tail_.assign(chunk.substr(chunk.size() - max_bytes));
    stderr_truncated_ = true;
    return;
  }
  const auto next_size = stderr_tail_.size() + chunk.size();
  if (next_size > max_bytes) {
    stderr_tail_.erase(0, next_size - max_bytes);
    stderr_truncated_ = true;
  }
  stderr_tail_.append(chunk);
}

ava::core::VoidResult PluginProcess::shutdown(std::chrono::milliseconds grace) {
  close_fd(stdin_fd_);
  const auto deadline = std::chrono::steady_clock::now() + grace;
  while (pid_ >= 0 && std::chrono::steady_clock::now() < deadline) {
    if (auto reaped = reap_child(); !reaped) return std::unexpected(std::move(reaped.error()));
    if (pid_ < 0) break;
    drain_available_noexcept();
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  if (pid_ >= 0) terminate_child();
  drain_available_noexcept();
  close_fds();
  return {};
}

void PluginProcess::close_fds() noexcept {
  close_fd(stdin_fd_);
  close_fd(stdout_fd_);
  close_fd(stderr_fd_);
}

void PluginProcess::terminate_child() noexcept {
  if (pid_ < 0) return;
  const pid_t pid = static_cast<pid_t>(pid_);
  const pid_t target = can_signal_group_ ? -pid : pid;
  kill(target, SIGTERM);
  for (int attempt = 0; attempt < 5; ++attempt) {
    int status = 0;
    const pid_t waited = waitpid_retry(pid, &status, WNOHANG);
    if (waited == pid || waited < 0) {
      if (waited == pid) {
        child_status_ = status;
        child_exited_ = true;
      }
      pid_ = -1;
      return;
    }
    drain_available_noexcept();
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  kill(target, SIGKILL);
  for (int attempt = 0; attempt < 25; ++attempt) {
    int status = 0;
    const pid_t waited = waitpid_retry(pid, &status, WNOHANG);
    if (waited == pid || waited < 0) {
      if (waited == pid) {
        child_status_ = status;
        child_exited_ = true;
      }
      pid_ = -1;
      return;
    }
    drain_available_noexcept();
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  pid_ = -1;
}

void PluginProcess::drain_available_noexcept() noexcept {
  try {
    if (stdout_fd_ >= 0) {
      std::array<char, 4096> buffer{};
      for (int reads = 0; reads < kMaxDrainReadsPerPoll; ++reads) {
        const auto bytes = read_retry(stdout_fd_, buffer.data(), buffer.size());
        if (bytes > 0) {
          stdout_buffer_.append(buffer.data(), static_cast<std::size_t>(bytes));
          if (stdout_buffer_.size() > options_.max_record_bytes) {
            stdout_buffer_.erase(0, stdout_buffer_.size() - options_.max_record_bytes);
          }
          continue;
        }
        if (bytes == 0) close_fd(stdout_fd_);
        break;
      }
    }
    if (stderr_fd_ >= 0) {
      std::array<char, 4096> buffer{};
      for (int reads = 0; reads < kMaxDrainReadsPerPoll; ++reads) {
        const auto bytes = read_retry(stderr_fd_, buffer.data(), buffer.size());
        if (bytes > 0) {
          append_stderr(std::string_view(buffer.data(), static_cast<std::size_t>(bytes)));
          continue;
        }
        if (bytes == 0) close_fd(stderr_fd_);
        break;
      }
    }
  } catch (...) {
  }
}

}  // namespace ava::plugin
