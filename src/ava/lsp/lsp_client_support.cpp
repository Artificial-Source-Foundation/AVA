#include "ava/lsp/lsp_client_support.h"

#include <sys/wait.h>
#include <unistd.h>

#include <cctype>
#include <cerrno>
#include <cstring>
#include <optional>
#include <string>
#include <utility>

#include "ava/core/json.h"

namespace ava::lsp::detail {
namespace {

std::string command_label(std::vector<std::string> const& argv)
{
  std::string label;
  for (std::size_t index = 0; index < argv.size(); ++index) {
    if (index > 0) label += ' ';
    label += argv[index];
  }
  return label;
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

std::string diagnostic_code(std::string_view object)
{
  if (auto code = ava::core::json::string_field(object, "code")) return *code;
  if (auto code = ava::core::json::integer_field(object, "code")) return std::to_string(*code);
  return {};
}

}  // namespace

ScopedSignalIgnore::ScopedSignalIgnore(int signal_number) noexcept : signal_number_(signal_number)
{
  struct sigaction ignored {};
  ignored.sa_handler = SIG_IGN;
  sigemptyset(&ignored.sa_mask);
  active_ = sigaction(signal_number_, &ignored, &previous_) == 0;
}

ScopedSignalIgnore::~ScopedSignalIgnore()
{
  if (active_) sigaction(signal_number_, &previous_, nullptr);
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

}  // namespace ava::lsp::detail
