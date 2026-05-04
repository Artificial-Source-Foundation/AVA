#include "ava/mcp/stdio_client.h"

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

#include "ava/core/json.h"
#include "ava/core/version.h"

namespace ava::mcp {
namespace {

constexpr char kTrustedExecPath[] = "/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin";
constexpr int kMaxMcpJsonDepth = 128;
constexpr int kMaxDrainReadsPerPoll = 16;
constexpr std::size_t kMaxMcpHeaderBytes = 16 * 1024;

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

ava::core::Error mcp_error(ava::core::ErrorCategory category, std::string message, const McpServerConfig& server) {
  auto error = ava::core::Error(category, std::move(message));
  error.with_context("mcp_server", server.id);
  if (!server.source_path.empty()) error.with_context("config", server.source_path.string());
  return error;
}

ava::core::Error errno_error(std::string message, const McpServerConfig& server) {
  auto error = mcp_error(ava::core::ErrorCategory::Io, std::move(message), server);
  error.with_context("cause", std::strerror(errno));
  return error;
}

ava::core::Error protocol_error(std::string message, const McpServerConfig& server) {
  return mcp_error(ava::core::ErrorCategory::Tool, std::move(message), server);
}

bool is_canceled(const CancelCallback& cancel_requested) { return cancel_requested && cancel_requested(); }

ava::core::Error canceled_error(std::string message, const McpServerConfig& server) {
  auto error = mcp_error(ava::core::ErrorCategory::Unknown, std::move(message), server);
  error.with_context("canceled", "true");
  return error;
}

ava::core::Result<std::array<int, 2>> make_pipe(const McpServerConfig& server) {
  std::array<int, 2> fds{-1, -1};
  if (pipe(fds.data()) != 0) return std::unexpected(errno_error("failed to create MCP process pipe", server));
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
      return std::unexpected(errno_error("failed to move MCP pipe above standard fds", server));
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

std::optional<std::size_t> field_value_start_any_depth(std::string_view object, std::string_view key) {
  const std::string needle = "\"" + ava::core::json::escape(key) + "\"";
  bool in_string = false;
  bool escaped = false;
  for (std::size_t index = 0; index < object.size(); ++index) {
    const char ch = object[index];
    if (in_string) {
      if (escaped) {
        escaped = false;
        continue;
      }
      if (ch == '\\') {
        escaped = true;
        continue;
      }
      if (ch == '"') in_string = false;
      continue;
    }
    if (ch != '"') continue;
    if (object.substr(index, needle.size()) == needle) {
      auto colon = index + needle.size();
      while (colon < object.size() && std::isspace(static_cast<unsigned char>(object[colon])) != 0) ++colon;
      if (colon < object.size() && object[colon] == ':') {
        ++colon;
        while (colon < object.size() && std::isspace(static_cast<unsigned char>(object[colon])) != 0) ++colon;
        return colon;
      }
    }
    in_string = true;
  }
  return std::nullopt;
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

std::string exit_detail(int status) {
  if (WIFEXITED(status)) return "exit " + std::to_string(WEXITSTATUS(status));
  if (WIFSIGNALED(status)) return "signal " + std::to_string(WTERMSIG(status));
  return "unknown status " + std::to_string(status);
}

std::vector<std::string> mcp_argv(const McpServerConfig& server) {
  std::vector<std::string> argv;
  argv.reserve(server.args.size() + 1);
  argv.push_back(server.command);
  argv.insert(argv.end(), server.args.begin(), server.args.end());
  return argv;
}

std::filesystem::path child_working_dir(const McpStdioClientOptions& options) {
  if (!options.workspace_dir.empty()) return options.workspace_dir;
  return std::filesystem::current_path();
}

std::string trim_ascii(std::string text) {
  auto first = text.begin();
  while (first != text.end() && std::isspace(static_cast<unsigned char>(*first)) != 0) ++first;
  auto last = text.end();
  while (last != first && std::isspace(static_cast<unsigned char>(*(last - 1))) != 0) --last;
  return std::string(first, last);
}

std::string lowercase_ascii(std::string text) {
  for (auto& ch : text) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
  return text;
}

ava::core::Result<std::size_t> parse_content_length(std::string_view headers, const McpServerConfig& server,
                                                    std::size_t max_message_bytes) {
  std::optional<std::size_t> content_length;
  std::size_t line_start = 0;
  while (line_start <= headers.size()) {
    const auto line_end = headers.find('\n', line_start);
    auto line = headers.substr(
        line_start, line_end == std::string_view::npos ? headers.size() - line_start : line_end - line_start);
    if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
    const auto colon = line.find(':');
    if (colon != std::string_view::npos) {
      auto key = lowercase_ascii(trim_ascii(std::string(line.substr(0, colon))));
      if (key == "content-length") {
        auto value = trim_ascii(std::string(line.substr(colon + 1)));
        if (value.empty() ||
            !std::ranges::all_of(value, [](char ch) { return std::isdigit(static_cast<unsigned char>(ch)) != 0; })) {
          return std::unexpected(protocol_error("MCP Content-Length header is invalid", server));
        }
        try {
          content_length = static_cast<std::size_t>(std::stoull(value));
        } catch (...) {
          return std::unexpected(protocol_error("MCP Content-Length header is out of range", server));
        }
      }
    }
    if (line_end == std::string_view::npos) break;
    line_start = line_end + 1;
  }
  if (!content_length) return std::unexpected(protocol_error("MCP message is missing Content-Length", server));
  if (*content_length > max_message_bytes) {
    auto error = protocol_error("MCP message exceeds size cap", server);
    error.with_context("max_bytes", std::to_string(max_message_bytes));
    return std::unexpected(std::move(error));
  }
  return *content_length;
}

std::optional<std::size_t> header_end_offset(std::string_view buffer) {
  if (const auto crlf = buffer.find("\r\n\r\n"); crlf != std::string_view::npos) return crlf + 4;
  if (const auto lf = buffer.find("\n\n"); lf != std::string_view::npos) return lf + 2;
  return std::nullopt;
}

std::optional<std::string> response_id(std::string_view message) {
  auto id = ava::core::json::string_field(message, "id");
  if (id) return id;
  const auto numeric_start = field_value_start_any_depth(message, "id");
  if (!numeric_start) return std::nullopt;
  std::size_t end = *numeric_start;
  while (end < message.size() && std::isdigit(static_cast<unsigned char>(message[end])) != 0) ++end;
  if (end == *numeric_start) return std::nullopt;
  return std::string(message.substr(*numeric_start, end - *numeric_start));
}

std::optional<std::string> error_message_from_response(std::string_view error_json) {
  return ava::core::json::string_field(error_json, "message");
}

bool is_valid_mcp_tool_name(std::string_view name) {
  if (name.empty() || name.size() > 128) return false;
  for (const char ch : name) {
    const auto byte = static_cast<unsigned char>(ch);
    if (byte < 0x20 || byte == 0x7F) return false;
  }
  return true;
}

std::string text_content_from_result(std::string_view result_json) {
  std::string content;
  for (const auto& item : ava::core::json::objects_in_array_field(result_json, "content")) {
    const auto type = ava::core::json::string_field(item, "type");
    const auto text = ava::core::json::string_field(item, "text");
    if (type && *type == "text" && text) {
      if (!content.empty()) content += '\n';
      content += *text;
    }
  }
  if (!content.empty()) return content;
  if (const auto structured = ava::core::json::object_field(result_json, "structuredContent")) return *structured;
  return {};
}

}  // namespace

McpStdioClient::McpStdioClient(McpServerConfig server, McpStdioClientOptions options)
    : server_(std::move(server)), options_(std::move(options)) {}

McpStdioClient::~McpStdioClient() {
  terminate_child();
  close_fds();
}

ava::core::Result<std::unique_ptr<McpStdioClient>> McpStdioClient::start(McpServerConfig server,
                                                                         McpStdioClientOptions options,
                                                                         CancelCallback cancel_requested) {
  if (server.command.empty()) {
    return std::unexpected(
        mcp_error(ava::core::ErrorCategory::InvalidArgument, "MCP server command must not be empty", server));
  }
  if (options.workspace_dir.empty()) options.workspace_dir = std::filesystem::current_path();
  if (options.startup_timeout < std::chrono::milliseconds(50) || options.startup_timeout > std::chrono::seconds(30)) {
    auto error = mcp_error(ava::core::ErrorCategory::InvalidArgument, "MCP startup timeout is out of bounds", server);
    error.with_context("min_ms", "50");
    error.with_context("max_ms", "30000");
    return std::unexpected(std::move(error));
  }
  if (options.request_timeout < std::chrono::milliseconds(50) || options.request_timeout > std::chrono::seconds(30)) {
    auto error = mcp_error(ava::core::ErrorCategory::InvalidArgument, "MCP request timeout is out of bounds", server);
    error.with_context("min_ms", "50");
    error.with_context("max_ms", "30000");
    return std::unexpected(std::move(error));
  }
  if (options.max_message_bytes == 0 || options.max_stderr_bytes == 0) {
    return std::unexpected(
        mcp_error(ava::core::ErrorCategory::InvalidArgument, "MCP client byte limits must be non-zero", server));
  }
  if (is_canceled(cancel_requested)) {
    return std::unexpected(canceled_error("MCP startup canceled", server));
  }

  auto client = std::make_unique<McpStdioClient>(std::move(server), std::move(options));
  if (auto launched = client->launch(); !launched) return std::unexpected(std::move(launched.error()));
  if (auto initialized = client->initialize(cancel_requested); !initialized) {
    return std::unexpected(std::move(initialized.error()));
  }
  return client;
}

const McpServerConfig& McpStdioClient::server() const noexcept { return server_; }

const McpInitialization& McpStdioClient::initialization() const noexcept { return initialization_; }

const std::string& McpStdioClient::stderr_tail() const noexcept { return stderr_tail_; }

bool McpStdioClient::stderr_truncated() const noexcept { return stderr_truncated_; }

ava::core::VoidResult McpStdioClient::launch() {
  auto stdin_pipe = make_pipe(server_);
  if (!stdin_pipe) return std::unexpected(std::move(stdin_pipe.error()));
  UniqueFd stdin_read((*stdin_pipe)[0]);
  UniqueFd stdin_write((*stdin_pipe)[1]);

  auto stdout_pipe = make_pipe(server_);
  if (!stdout_pipe) return std::unexpected(std::move(stdout_pipe.error()));
  UniqueFd stdout_read((*stdout_pipe)[0]);
  UniqueFd stdout_write((*stdout_pipe)[1]);

  auto stderr_pipe = make_pipe(server_);
  if (!stderr_pipe) return std::unexpected(std::move(stderr_pipe.error()));
  UniqueFd stderr_read((*stderr_pipe)[0]);
  UniqueFd stderr_write((*stderr_pipe)[1]);

  auto argv_strings = mcp_argv(server_);
  std::vector<char*> argv;
  argv.reserve(argv_strings.size() + 1);
  for (auto& arg : argv_strings) argv.push_back(arg.data());
  argv.push_back(nullptr);

  const auto cwd = child_working_dir(options_).string();
  const pid_t pid = fork();
  if (pid < 0) return std::unexpected(errno_error("failed to fork MCP server process", server_));

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

ava::core::VoidResult McpStdioClient::initialize(CancelCallback cancel_requested) {
  const std::string params =
      "{\"protocolVersion\":\"2024-11-05\",\"capabilities\":{},\"clientInfo\":{\"name\":\"ava\","
      "\"version\":\"" +
      std::string(ava::core::version::kFullVersion) + "\"}}";
  auto response = request("initialize", params, options_.startup_timeout, "timed out waiting for MCP initialization",
                          cancel_requested);
  if (!response) return std::unexpected(std::move(response.error()));
  const auto server_info = ava::core::json::object_field(response->result_json, "serverInfo");
  const auto capabilities = ava::core::json::object_field(response->result_json, "capabilities").value_or("{}");
  if (!server_info || !ava::core::json::is_valid_object(capabilities)) {
    auto error = protocol_error("MCP initialize response is malformed", server_);
    error.with_context("response", response->raw_json.substr(0, 512));
    return std::unexpected(std::move(error));
  }
  initialization_ =
      McpInitialization{.server_name = ava::core::json::string_field(*server_info, "name").value_or(server_.id),
                        .server_version = ava::core::json::string_field(*server_info, "version").value_or(""),
                        .capabilities_json = capabilities,
                        .raw_json = response->raw_json};

  const std::string notification = "{\"jsonrpc\":\"2.0\",\"method\":\"notifications/initialized\"}";
  const auto deadline = std::chrono::steady_clock::now() + options_.request_timeout;
  return write_message(notification, deadline, options_.request_timeout,
                       "timed out writing MCP initialized notification", cancel_requested);
}

ava::core::Result<std::vector<McpToolDescription>> McpStdioClient::list_tools(CancelCallback cancel_requested) {
  if (is_canceled(cancel_requested)) {
    return std::unexpected(canceled_error("MCP tools/list canceled", server_));
  }
  auto response =
      request("tools/list", "{}", options_.request_timeout, "timed out waiting for MCP tools/list", cancel_requested);
  if (!response) return std::unexpected(std::move(response.error()));
  const auto tools_start = ava::core::json::field_value_start(response->result_json, "tools");
  if (tools_start && (*tools_start >= response->result_json.size() || response->result_json[*tools_start] != '[')) {
    auto error = protocol_error("MCP tools/list result has invalid tools field", server_);
    error.with_context("response", response->raw_json.substr(0, 512));
    return std::unexpected(std::move(error));
  }

  std::vector<McpToolDescription> tools;
  for (const auto& tool_json : ava::core::json::objects_in_array_field(response->result_json, "tools")) {
    auto name = ava::core::json::string_field(tool_json, "name");
    if (!name || !is_valid_mcp_tool_name(*name)) {
      auto error = protocol_error("MCP tool has invalid name", server_);
      error.with_context("response", tool_json.substr(0, 512));
      return std::unexpected(std::move(error));
    }
    auto input_schema = ava::core::json::object_field(tool_json, "inputSchema").value_or("{\"type\":\"object\"}");
    if (!ava::core::json::is_valid_object(input_schema)) {
      auto error = protocol_error("MCP tool inputSchema is invalid", server_);
      error.with_context("tool", *name);
      return std::unexpected(std::move(error));
    }
    tools.push_back(
        McpToolDescription{.name = std::move(*name),
                           .description = ava::core::json::string_field(tool_json, "description").value_or(""),
                           .input_schema_json = std::move(input_schema)});
  }
  return tools;
}

ava::core::Result<McpToolCallResult> McpStdioClient::call_tool(std::string_view tool_name,
                                                               std::string_view arguments_json,
                                                               CancelCallback cancel_requested) {
  if (!is_valid_mcp_tool_name(tool_name)) {
    return std::unexpected(mcp_error(ava::core::ErrorCategory::InvalidArgument, "MCP tool name is invalid", server_));
  }
  if (!ava::core::json::is_valid_object(arguments_json)) {
    auto error =
        mcp_error(ava::core::ErrorCategory::InvalidArgument, "MCP tool arguments must be a JSON object", server_);
    error.with_context("tool", std::string(tool_name));
    return std::unexpected(std::move(error));
  }
  if (is_canceled(cancel_requested)) {
    auto error = canceled_error("MCP tools/call canceled", server_);
    error.with_context("tool", std::string(tool_name));
    return std::unexpected(std::move(error));
  }
  const std::string params =
      "{\"name\":" + json_string(tool_name) + ",\"arguments\":" + std::string(arguments_json) + "}";
  auto response =
      request("tools/call", params, options_.request_timeout, "timed out waiting for MCP tools/call", cancel_requested);
  if (!response) return std::unexpected(std::move(response.error()));
  return McpToolCallResult{.is_error = bool_field(response->result_json, "isError").value_or(false),
                           .content = text_content_from_result(response->result_json),
                           .raw_json = response->raw_json};
}

ava::core::Result<McpStdioClient::JsonRpcResponse> McpStdioClient::request(std::string_view method,
                                                                           std::string_view params_json,
                                                                           std::chrono::milliseconds timeout,
                                                                           std::string_view timeout_message,
                                                                           CancelCallback cancel_requested) {
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  const auto request_id = "ava_mcp_" + std::to_string(next_request_id_++);
  const std::string request_json = "{\"jsonrpc\":\"2.0\",\"id\":" + json_string(request_id) +
                                   ",\"method\":" + json_string(method) + ",\"params\":" + std::string(params_json) +
                                   "}";
  if (auto written = write_message(request_json, deadline, timeout, "timed out writing MCP request", cancel_requested);
      !written) {
    return std::unexpected(std::move(written.error()));
  }

  while (true) {
    auto message =
        read_message(deadline, timeout, timeout_message, "MCP server closed stdout before response", cancel_requested);
    if (!message) return std::unexpected(std::move(message.error()));
    if (!json_depth_within_limit(*message, kMaxMcpJsonDepth) || !ava::core::json::is_valid_object(*message)) {
      auto error = protocol_error("MCP response is not a valid JSON object", server_);
      error.with_context("response", message->substr(0, 512));
      return std::unexpected(std::move(error));
    }
    const auto id = response_id(*message);
    if (!id) continue;
    if (*id != request_id) {
      auto error = protocol_error("MCP response id did not match request", server_);
      error.with_context("expected", request_id);
      error.with_context("actual", *id);
      return std::unexpected(std::move(error));
    }
    if (auto error_json = ava::core::json::object_field(*message, "error")) {
      auto error = protocol_error(error_message_from_response(*error_json).value_or("MCP request failed"), server_);
      error.with_context("method", std::string(method));
      error.with_context("mcp_error", error_json->substr(0, 512));
      return std::unexpected(std::move(error));
    }
    auto result_json = ava::core::json::object_field(*message, "result");
    if (!result_json) {
      auto error = protocol_error("MCP response is missing result object", server_);
      error.with_context("method", std::string(method));
      error.with_context("response", message->substr(0, 512));
      return std::unexpected(std::move(error));
    }
    return JsonRpcResponse{.result_json = std::move(*result_json), .raw_json = std::move(*message)};
  }
}

ava::core::VoidResult McpStdioClient::write_message(std::string_view message,
                                                    std::chrono::steady_clock::time_point deadline,
                                                    std::chrono::milliseconds timeout, std::string_view timeout_message,
                                                    CancelCallback cancel_requested) {
  if (stdin_fd_ < 0) return std::unexpected(protocol_error("MCP stdin is closed", server_));
  const std::string frame = "Content-Length: " + std::to_string(message.size()) + "\r\n\r\n" + std::string(message);
  std::size_t offset = 0;
  const ScopedSignalIgnore ignore_sigpipe(SIGPIPE);
  while (offset < frame.size()) {
    if (is_canceled(cancel_requested)) {
      terminate_child();
      return std::unexpected(canceled_error("MCP request canceled", server_));
    }
    const auto bytes = write_retry(stdin_fd_, frame.data() + offset, frame.size() - offset);
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

ava::core::Result<std::optional<std::string>> McpStdioClient::try_extract_message() {
  const auto header_end = header_end_offset(stdout_buffer_);
  if (!header_end) {
    if (stdout_buffer_.size() > kMaxMcpHeaderBytes) {
      auto error = protocol_error("MCP message header exceeds size cap", server_);
      error.with_context("max_bytes", std::to_string(kMaxMcpHeaderBytes));
      terminate_child();
      return std::unexpected(std::move(error));
    }
    return std::optional<std::string>{};
  }
  const auto header_size = *header_end;
  if (header_size > kMaxMcpHeaderBytes) {
    auto error = protocol_error("MCP message header exceeds size cap", server_);
    error.with_context("max_bytes", std::to_string(kMaxMcpHeaderBytes));
    terminate_child();
    return std::unexpected(std::move(error));
  }
  auto content_length = parse_content_length(std::string_view(stdout_buffer_).substr(0, header_size), server_,
                                             options_.max_message_bytes);
  if (!content_length) {
    terminate_child();
    return std::unexpected(std::move(content_length.error()));
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
                                                            CancelCallback cancel_requested) {
  while (true) {
    if (is_canceled(cancel_requested)) {
      terminate_child();
      return std::unexpected(canceled_error("MCP request canceled", server_));
    }
    auto extracted = try_extract_message();
    if (!extracted) return std::unexpected(std::move(extracted.error()));
    if (*extracted) return std::move(**extracted);

    if (stdout_fd_ < 0) {
      if (auto reaped = reap_child(); !reaped) return std::unexpected(std::move(reaped.error()));
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
    const int poll_timeout = static_cast<int>(std::min<std::size_t>(remaining_ms(deadline), 100));
    const int polled = poll(fds.data(), fds.size(), poll_timeout);
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
                                                        CancelCallback cancel_requested) {
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
    const int poll_timeout = static_cast<int>(std::min<std::size_t>(remaining_ms(deadline), 100));
    const int polled = poll(fds.data(), fds.size(), poll_timeout);
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

ava::core::VoidResult McpStdioClient::drain_stdout() {
  if (stdout_fd_ < 0) return {};
  std::array<char, 4096> buffer{};
  int reads = 0;
  while (true) {
    const auto bytes = read_retry(stdout_fd_, buffer.data(), buffer.size());
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

ava::core::VoidResult McpStdioClient::drain_stderr() {
  if (stderr_fd_ < 0) return {};
  std::array<char, 4096> buffer{};
  int reads = 0;
  while (true) {
    const auto bytes = read_retry(stderr_fd_, buffer.data(), buffer.size());
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

ava::core::VoidResult McpStdioClient::reap_child() {
  if (pid_ <= 0 || child_exited_) return {};
  int status = 0;
  const auto waited = waitpid_retry(static_cast<pid_t>(pid_), &status, WNOHANG);
  if (waited < 0) {
    if (errno == ECHILD) {
      pid_ = -1;
      child_exited_ = true;
      return {};
    }
    return std::unexpected(errno_error("failed to reap MCP server process", server_));
  }
  if (waited == 0) return {};
  child_status_ = status;
  child_exited_ = true;
  close_fd(stdin_fd_);
  return {};
}

ava::core::VoidResult McpStdioClient::set_pipe_nonblocking(int fd, std::string_view pipe_name) {
  const int flags = fcntl(fd, F_GETFL, 0);
  if (flags < 0) return std::unexpected(errno_error("failed to inspect MCP pipe flags", server_));
  if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) != 0) {
    auto error = errno_error("failed to set MCP pipe nonblocking", server_);
    error.with_context("pipe", std::string(pipe_name));
    return std::unexpected(std::move(error));
  }
  return {};
}

void McpStdioClient::append_stderr(std::string_view chunk) {
  if (chunk.size() >= options_.max_stderr_bytes) {
    stderr_tail_ = std::string(chunk.substr(chunk.size() - options_.max_stderr_bytes));
    stderr_truncated_ = true;
    return;
  }
  if (stderr_tail_.size() + chunk.size() > options_.max_stderr_bytes) {
    const auto drop = stderr_tail_.size() + chunk.size() - options_.max_stderr_bytes;
    stderr_tail_.erase(0, drop);
    stderr_truncated_ = true;
  }
  stderr_tail_.append(chunk);
}

void McpStdioClient::close_fds() noexcept {
  close_fd(stdin_fd_);
  close_fd(stdout_fd_);
  close_fd(stderr_fd_);
}

void McpStdioClient::terminate_child() noexcept {
  if (pid_ <= 0 || child_exited_) return;
  close_fd(stdin_fd_);
  if (can_signal_group_) {
    kill(-static_cast<pid_t>(pid_), SIGTERM);
  } else {
    kill(static_cast<pid_t>(pid_), SIGTERM);
  }
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(100);
  while (std::chrono::steady_clock::now() < deadline) {
    int status = 0;
    const auto waited = waitpid_retry(static_cast<pid_t>(pid_), &status, WNOHANG);
    if (waited == pid_) {
      child_status_ = status;
      child_exited_ = true;
      return;
    }
    if (waited < 0) return;
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  if (can_signal_group_) {
    kill(-static_cast<pid_t>(pid_), SIGKILL);
  } else {
    kill(static_cast<pid_t>(pid_), SIGKILL);
  }
  int status = 0;
  if (waitpid_retry(static_cast<pid_t>(pid_), &status, 0) == pid_) {
    child_status_ = status;
    child_exited_ = true;
  }
}

ava::core::VoidResult McpStdioClient::shutdown(std::chrono::milliseconds grace) {
  close_fd(stdin_fd_);
  const auto deadline = std::chrono::steady_clock::now() + grace;
  while (pid_ > 0 && !child_exited_ && std::chrono::steady_clock::now() < deadline) {
    if (auto drained = drain_stdout(); !drained) return std::unexpected(std::move(drained.error()));
    if (auto drained = drain_stderr(); !drained) return std::unexpected(std::move(drained.error()));
    if (auto reaped = reap_child(); !reaped) return std::unexpected(std::move(reaped.error()));
    if (!child_exited_) std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  if (pid_ > 0 && !child_exited_) terminate_child();
  close_fds();
  return {};
}

}  // namespace ava::mcp
