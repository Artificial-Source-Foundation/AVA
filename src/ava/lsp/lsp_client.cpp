#include "sys.h"
#include "ava/lsp/bounded_file_reader.h"
#include "ava/lsp/lsp_client.h"
#include "ava/core/json.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <optional>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/wait.h>
#ifdef __linux__
#include <sys/syscall.h>
#endif
#include <unistd.h>

namespace ava::lsp {
namespace {

constexpr char kTrustedExecPath[] = "/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin";
constexpr std::size_t kMaxLspHeaderBytes = 64 * 1024;
constexpr std::size_t kMaxLspMessageBytes = 4 * 1024 * 1024;
constexpr std::uintmax_t kMaxDidOpenBytes = 512 * 1024;
constexpr auto kTerminationGrace = std::chrono::milliseconds(50);
constexpr auto kTerminationPollInterval = std::chrono::milliseconds(5);

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
  if (pipe(fds.data()) != 0)
    return std::unexpected(errno_error("failed to create LSP process pipe", config));

  for (auto& fd : fds)
  {
    int const original = fd;
    int const moved = fcntl(original, F_DUPFD_CLOEXEC, STDERR_FILENO + 1);
    int const saved_errno = errno;
    close(original);
    fd = -1;
    if (moved < 0)
    {
      for (int const remaining : fds)
      {
        if (remaining >= 0)
          close(remaining);
      }
      errno = saved_errno;
      return std::unexpected(errno_error("failed to move LSP process pipe above standard fds", config));
    }
    fd = moved;
  }
  return fds;
}

void close_nonstandard_fds()
{
#if defined(__linux__) && defined(SYS_close_range)
  if (syscall(SYS_close_range, static_cast<unsigned int>(STDERR_FILENO + 1), ~0U, 0U) == 0)
    return;
#endif
  long const open_max = sysconf(_SC_OPEN_MAX);
  int const max_fd = open_max > 0 ? static_cast<int>(open_max) : 1024;
  for (int fd = STDERR_FILENO + 1; fd < max_fd; ++fd) close(fd);
}

bool allowlisted_lsp_environment_name(std::string_view name)
{
  constexpr std::array<std::string_view, 15> names{
      "HOME",           "USER",          "LOGNAME",        "TMPDIR", "TMP",       "TEMP", "LANG", "LANGUAGE", "LC_ALL", "XDG_CONFIG_HOME",
      "XDG_CACHE_HOME", "XDG_DATA_HOME", "XDG_STATE_HOME", "TERM",   "COLORTERM",
  };
  return std::ranges::find(names, name) != names.end() || (name.starts_with("LC_") && name.size() > 3);
}

std::vector<std::string> lsp_environment()
{
  // LSP servers are arbitrary local programs. Retain only terminal, locale,
  // temporary-directory, XDG, and identity compatibility variables. AVA has no
  // configured/tested toolchain home requirement, so no compiler/runtime home
  // is inherited. In particular, provider/cloud/token variables and AVA_*
  // variables are never forwarded.
  std::vector<std::string> values;
  std::vector<std::string_view> names;
  for (char** inherited = ::environ; inherited != nullptr && *inherited != nullptr; ++inherited)
  {
    std::string_view const variable(*inherited);
    auto const separator = variable.find('=');
    if (separator == std::string_view::npos || separator == 0)
      continue;
    auto const name = variable.substr(0, separator);
    if (!allowlisted_lsp_environment_name(name) || std::ranges::find(names, name) != names.end())
      continue;
    names.push_back(name);
    values.emplace_back(variable);
  }
  values.emplace_back(std::string("PATH=") + kTrustedExecPath);
  return values;
}

std::vector<std::string> trusted_executable_candidates(std::string_view command)
{
  if (command.find('/') != std::string_view::npos)
    return {std::string(command)};

  std::vector<std::string> candidates;
  std::string_view remaining(kTrustedExecPath);
  while (!remaining.empty())
  {
    auto const separator = remaining.find(':');
    auto const directory = remaining.substr(0, separator);
    if (!directory.empty())
      candidates.emplace_back(std::string(directory) + "/" + std::string(command));
    if (separator == std::string_view::npos)
      break;
    remaining.remove_prefix(separator + 1);
  }
  return candidates;
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

int hex_value(char ch)
{
  if (ch >= '0' && ch <= '9') return ch - '0';
  if (ch >= 'a' && ch <= 'f') return 10 + (ch - 'a');
  if (ch >= 'A' && ch <= 'F') return 10 + (ch - 'A');
  return -1;
}

ava::core::Result<std::filesystem::path> path_from_file_uri(std::string_view uri, ServerConfig const& config)
{
  constexpr std::string_view prefix = "file://";
  if (!uri.starts_with(prefix)) {
    return std::unexpected(lsp_error(ava::core::ErrorCategory::Tool, "LSP location URI is not a file URI", config));
  }
  std::string decoded;
  auto const path = uri.substr(prefix.size());
  decoded.reserve(path.size());
  for (std::size_t index = 0; index < path.size(); ++index) {
    if (path[index] == '%') {
      if (index + 2 >= path.size()) {
        return std::unexpected(lsp_error(ava::core::ErrorCategory::Tool, "LSP location URI has invalid escaping", config));
      }
      int const hi = hex_value(path[index + 1]);
      int const lo = hex_value(path[index + 2]);
      if (hi < 0 || lo < 0) {
        return std::unexpected(lsp_error(ava::core::ErrorCategory::Tool, "LSP location URI has invalid escaping", config));
      }
      char const byte = static_cast<char>((hi << 4) | lo);
      if (byte == '/' || byte == '\0') {
        return std::unexpected(lsp_error(ava::core::ErrorCategory::Tool, "LSP location URI escapes a path separator", config));
      }
      decoded.push_back(byte);
      index += 2;
    } else {
      decoded.push_back(path[index]);
    }
  }
  auto const candidate = std::filesystem::path(decoded).lexically_normal();
  auto const workspace = std::filesystem::absolute(config.workspace_root).lexically_normal().generic_string();
  auto const value = std::filesystem::absolute(candidate).lexically_normal().generic_string();
  if (value != workspace && !value.starts_with(workspace + "/")) {
    return std::unexpected(lsp_error(ava::core::ErrorCategory::PermissionDenied, "LSP location is outside the workspace", config));
  }
  return std::filesystem::path(value);
}

std::string json_string(std::string_view value)
{
  return "\"" + ava::core::json::escape(value) + "\"";
}

ava::core::Result<std::string> read_text_document(std::filesystem::path const& path, ServerConfig const& config, std::chrono::steady_clock::time_point deadline,
                                                  CancelCallback const& cancel_requested)
{
  auto content = read_bounded_lsp_file(BoundedFileReadOptions{
      .path = path,
      .workspace_root = config.workspace_root,
      .max_bytes = kMaxDidOpenBytes,
      .scope = BoundedFileReadScope::Workspace,
      .deadline = deadline,
      .cancel_requested = cancel_requested,
  });
  if (!content)
  {
    auto error = std::move(content.error());
    error.with_context("command", command_label(config.argv));
    error.with_context("workspace", config.workspace_root.string());
    return std::unexpected(std::move(error));
  }
  if (!*content)
  {
    auto error = lsp_error(ava::core::ErrorCategory::NotFound, "LSP document was not found", config);
    error.with_context("path", path.string());
    return std::unexpected(std::move(error));
  }
  return std::move(**content);
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

std::string exit_detail(siginfo_t const& info)
{
  switch (info.si_code)
  {
    case CLD_EXITED:
      return "exit " + std::to_string(info.si_status);
    case CLD_KILLED:
      return "signal " + std::to_string(info.si_status);
    case CLD_DUMPED:
      return "signal " + std::to_string(info.si_status) + " (core dumped)";
    default:
      return "unexpected child status code " + std::to_string(info.si_code) + " value " + std::to_string(info.si_status);
  }
}

int waitid_retry(idtype_t id_type, id_t id, siginfo_t* info, int options)
{
  while (true)
  {
    auto const result = waitid(id_type, id, info, options);
    if (result < 0 && errno == EINTR)
      continue;
    return result;
  }
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

ava::core::Result<Range> parse_range(std::string_view object, ServerConfig const& config)
{
  auto range = ava::core::json::object_field(object, "range");
  auto start = range ? ava::core::json::object_field(*range, "start") : std::optional<std::string>{};
  auto end = range ? ava::core::json::object_field(*range, "end") : std::optional<std::string>{};
  if (!start || !end) {
    return std::unexpected(lsp_error(ava::core::ErrorCategory::Tool, "LSP range is malformed", config));
  }
  auto const start_line = ava::core::json::integer_field(*start, "line");
  auto const start_character = ava::core::json::integer_field(*start, "character");
  auto const end_line = ava::core::json::integer_field(*end, "line");
  auto const end_character = ava::core::json::integer_field(*end, "character");
  if (!start_line || !start_character || !end_line || !end_character || *start_line < 0 || *start_character < 0 ||
      *end_line < 0 || *end_character < 0) {
    return std::unexpected(lsp_error(ava::core::ErrorCategory::Tool, "LSP range is malformed", config));
  }
  return Range{.start_line = static_cast<int>(*start_line),
               .start_column = static_cast<int>(*start_character),
               .end_line = static_cast<int>(*end_line),
               .end_column = static_cast<int>(*end_character)};
}

ava::core::Result<Location> parse_location(std::string_view object, ServerConfig const& config)
{
  auto uri = ava::core::json::string_field(object, "uri");
  if (!uri) return std::unexpected(lsp_error(ava::core::ErrorCategory::Tool, "LSP location is missing URI", config));
  auto path = path_from_file_uri(*uri, config);
  if (!path) return std::unexpected(std::move(path.error()));
  auto range = parse_range(object, config);
  if (!range) return std::unexpected(std::move(range.error()));
  return Location{.path = std::move(*path), .range = *range};
}

ava::core::VoidResult parse_document_symbol_object(std::string_view object, ServerConfig const& config,
                                                   std::filesystem::path const& path, std::string const& container,
                                                   std::vector<Symbol>& symbols)
{
  auto name = ava::core::json::string_field(object, "name");
  auto kind = ava::core::json::integer_field(object, "kind");
  auto range = parse_range(object, config);
  if (!name || !kind || !range) {
    return std::unexpected(lsp_error(ava::core::ErrorCategory::Tool, "LSP document symbol item is malformed", config));
  }
  symbols.push_back(Symbol{.name = std::move(*name),
                           .kind = static_cast<int>(*kind),
                           .path = path,
                           .range = *range,
                           .container = container});
  auto const child_container = symbols.back().name;
  for (auto const& child : ava::core::json::objects_in_array_field(object, "children")) {
    if (auto parsed = parse_document_symbol_object(child, config, path, child_container, symbols); !parsed) {
      return parsed;
    }
  }
  return {};
}

ava::core::Result<std::vector<Symbol>> parse_document_symbols_response(std::string_view response,
                                                                       ServerConfig const& config,
                                                                       std::filesystem::path const& path)
{
  auto result = ava::core::json::field_value_start(response, "result");
  if (result && *result < response.size() && response[*result] == 'n') return std::vector<Symbol>{};
  if (!result || *result >= response.size() || response[*result] != '[') {
    return std::unexpected(lsp_error(ava::core::ErrorCategory::Tool, "LSP document symbols response is missing result", config));
  }
  std::vector<Symbol> symbols;
  for (auto const& object : ava::core::json::objects_in_array_field(response, "result")) {
    if (ava::core::json::object_field(object, "location")) {
      auto name = ava::core::json::string_field(object, "name");
      auto kind = ava::core::json::integer_field(object, "kind");
      auto location = ava::core::json::object_field(object, "location");
      if (!name || !kind || !location) {
        return std::unexpected(lsp_error(ava::core::ErrorCategory::Tool, "LSP symbol item is malformed", config));
      }
      auto parsed = parse_location(*location, config);
      if (!parsed) return std::unexpected(std::move(parsed.error()));
      symbols.push_back(Symbol{.name = std::move(*name),
                               .kind = static_cast<int>(*kind),
                               .path = parsed->path,
                               .range = parsed->range,
                               .container = ava::core::json::string_field(object, "containerName").value_or(std::string{})});
      continue;
    }
    if (auto parsed = parse_document_symbol_object(object, config, path, {}, symbols); !parsed) {
      return std::unexpected(std::move(parsed.error()));
    }
  }
  return symbols;
}

ava::core::Result<std::vector<Symbol>> parse_workspace_symbols_response(std::string_view response,
                                                                        ServerConfig const& config)
{
  auto result = ava::core::json::field_value_start(response, "result");
  if (result && *result < response.size() && response[*result] == 'n') return std::vector<Symbol>{};
  if (!result || *result >= response.size() || response[*result] != '[') {
    return std::unexpected(lsp_error(ava::core::ErrorCategory::Tool, "LSP workspace symbols response is missing result", config));
  }
  std::vector<Symbol> symbols;
  for (auto const& object : ava::core::json::objects_in_array_field(response, "result")) {
    auto name = ava::core::json::string_field(object, "name");
    auto kind = ava::core::json::integer_field(object, "kind");
    auto location = ava::core::json::object_field(object, "location");
    if (!name || !kind || !location) {
      return std::unexpected(lsp_error(ava::core::ErrorCategory::Tool, "LSP workspace symbol item is malformed", config));
    }
    auto parsed = parse_location(*location, config);
    if (!parsed) return std::unexpected(std::move(parsed.error()));
    symbols.push_back(Symbol{.name = std::move(*name),
                             .kind = static_cast<int>(*kind),
                             .path = parsed->path,
                             .range = parsed->range,
                             .container = ava::core::json::string_field(object, "containerName").value_or(std::string{})});
  }
  return symbols;
}

ava::core::Result<std::vector<Location>> parse_definition_response(std::string_view response, ServerConfig const& config)
{
  auto result_start = ava::core::json::field_value_start(response, "result");
  if (!result_start) {
    return std::unexpected(lsp_error(ava::core::ErrorCategory::Tool, "LSP definition response is missing result", config));
  }
  std::vector<Location> locations;
  if (*result_start < response.size() && response[*result_start] == '[') {
    for (auto const& object : ava::core::json::objects_in_array_field(response, "result")) {
      auto location = parse_location(object, config);
      if (!location) return std::unexpected(std::move(location.error()));
      locations.push_back(std::move(*location));
    }
    return locations;
  }
  if (*result_start < response.size() && response[*result_start] == '{') {
    auto result = ava::core::json::object_field(response, "result");
    if (!result) return std::unexpected(lsp_error(ava::core::ErrorCategory::Tool, "LSP definition result is malformed", config));
    auto location = parse_location(*result, config);
    if (!location) return std::unexpected(std::move(location.error()));
    locations.push_back(std::move(*location));
    return locations;
  }
  return locations;
}

}  // namespace

ava::core::Result<std::vector<Symbol>> DiagnosticsProvider::document_symbols(std::filesystem::path const&, CancelCallback)
{
  return std::unexpected(ava::core::Error(ava::core::ErrorCategory::Tool, "LSP document symbols provider is unavailable"));
}

ava::core::Result<std::vector<Symbol>> DiagnosticsProvider::workspace_symbols(std::string_view, CancelCallback)
{
  return std::unexpected(ava::core::Error(ava::core::ErrorCategory::Tool, "LSP workspace symbols provider is unavailable"));
}

ava::core::Result<std::vector<Location>> DiagnosticsProvider::definitions(std::filesystem::path const&, int, int,
                                                                           CancelCallback)
{
  return std::unexpected(ava::core::Error(ava::core::ErrorCategory::Tool, "LSP definitions provider is unavailable"));
}

ava::core::Result<std::vector<Location>> DiagnosticsProvider::references(std::filesystem::path const&, int, int,
                                                                          CancelCallback)
{
  return std::unexpected(ava::core::Error(ava::core::ErrorCategory::Tool, "LSP references provider is unavailable"));
}

void DiagnosticsProvider::set_permission_request_ids(std::shared_ptr<std::vector<std::string>>)
{
}

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
  if (config.startup_timeout < std::chrono::milliseconds(100) || config.startup_timeout > std::chrono::seconds(30)) {
    auto error = ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "LSP startup timeout is out of bounds");
    error.with_context("field", "startup_timeout");
    error.with_context("min_ms", "100");
    error.with_context("max_ms", "30000");
    return std::unexpected(std::move(error));
  }
  if (config.request_timeout < std::chrono::milliseconds(100) || config.request_timeout > std::chrono::seconds(30)) {
    auto error = ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "LSP request timeout is out of bounds");
    error.with_context("field", "request_timeout");
    error.with_context("min_ms", "100");
    error.with_context("max_ms", "30000");
    return std::unexpected(std::move(error));
  }
  if (is_canceled(cancel_requested)) {
    return std::unexpected(canceled_error("LSP startup canceled", config));
  }

  auto client = std::make_shared<SubprocessLspClient>(std::move(config));
  if (auto launched = client->launch(); !launched) return std::unexpected(std::move(launched.error()));
  if (is_canceled(cancel_requested)) {
    client->terminate_child();
    return std::unexpected(canceled_error("LSP startup canceled", client->config_));
  }
  if (auto initialized = client->initialize(cancel_requested); !initialized) {
    return std::unexpected(std::move(initialized.error()));
  }
  return client;
}

ava::core::VoidResult SubprocessLspClient::launch()
{
  auto stdin_pipe = make_pipe(config_);
  if (!stdin_pipe)
    return std::unexpected(std::move(stdin_pipe.error()));
  auto stdout_pipe = make_pipe(config_);
  if (!stdout_pipe)
  {
    close((*stdin_pipe)[0]);
    close((*stdin_pipe)[1]);
    return std::unexpected(std::move(stdout_pipe.error()));
  }
  auto gate_pipe = make_pipe(config_);
  if (!gate_pipe)
  {
    close((*stdin_pipe)[0]);
    close((*stdin_pipe)[1]);
    close((*stdout_pipe)[0]);
    close((*stdout_pipe)[1]);
    return std::unexpected(std::move(gate_pipe.error()));
  }

  int const stdout_flags = fcntl((*stdout_pipe)[0], F_GETFL, 0);
  int const stdin_flags = fcntl((*stdin_pipe)[1], F_GETFL, 0);
  if (stdout_flags < 0 || stdin_flags < 0 || fcntl((*stdout_pipe)[0], F_SETFL, stdout_flags | O_NONBLOCK) < 0 ||
      fcntl((*stdin_pipe)[1], F_SETFL, stdin_flags | O_NONBLOCK) < 0)
  {
    auto error = errno_error("failed to configure LSP stdio pipes", config_);
    close((*stdin_pipe)[0]);
    close((*stdin_pipe)[1]);
    close((*stdout_pipe)[0]);
    close((*stdout_pipe)[1]);
    close((*gate_pipe)[0]);
    close((*gate_pipe)[1]);
    return std::unexpected(std::move(error));
  }

  // Prepare all storage used by exec before fork. The child only performs
  // async-signal-safe descriptor/process operations before execve.
  auto const child_cwd_path = config_.process_cwd.empty() ? config_.workspace_root : config_.process_cwd;
  auto const child_cwd = child_cwd_path.string();
  auto executable_candidates = trusted_executable_candidates(config_.argv.front());
  auto environment_strings = lsp_environment();
  std::vector<char*> environment;
  environment.reserve(environment_strings.size() + 1);
  for (auto& value : environment_strings) environment.push_back(value.data());
  // Set PWD to the logical cwd so that child processes preserve symlinked
  // path components instead of the physical path that getcwd() would return.
  environment_strings.emplace_back(std::string("PWD=") + child_cwd);
  // Rebuild environment pointers to include PWD.
  environment.clear();
  environment.reserve(environment_strings.size() + 1);
  for (auto& value : environment_strings) environment.push_back(value.data());
  environment.push_back(nullptr);
  std::vector<char*> argv;
  argv.reserve(config_.argv.size() + 1);
  for (auto& argument : config_.argv) argv.push_back(argument.data());
  argv.push_back(nullptr);

  pid_t const parent_pgid = getpgrp();
  pid_t const pid = fork();
  if (pid < 0)
  {
    auto const saved_errno = errno;
    close((*stdin_pipe)[0]);
    close((*stdin_pipe)[1]);
    close((*stdout_pipe)[0]);
    close((*stdout_pipe)[1]);
    close((*gate_pipe)[0]);
    close((*gate_pipe)[1]);
    errno = saved_errno;
    return std::unexpected(errno_error("failed to fork LSP server", config_));
  }

  if (pid == 0)
  {
    close((*stdin_pipe)[1]);
    close((*stdout_pipe)[0]);
    close((*gate_pipe)[1]);
    if (setpgid(0, 0) != 0)
      _exit(127);
    char release = '\0';
    if (read_retry((*gate_pipe)[0], &release, 1) != 1)
      _exit(127);
    close((*gate_pipe)[0]);

    if (dup2((*stdin_pipe)[0], STDIN_FILENO) < 0)
      _exit(127);
    if (dup2((*stdout_pipe)[1], STDOUT_FILENO) < 0)
      _exit(127);
    int const dev_null = open("/dev/null", O_WRONLY | O_CLOEXEC);
    if (dev_null < 0 || dup2(dev_null, STDERR_FILENO) < 0)
      _exit(127);
    if (dev_null != STDERR_FILENO)
      close(dev_null);
    close((*stdin_pipe)[0]);
    close((*stdout_pipe)[1]);
    if (chdir(child_cwd.c_str()) != 0)
      _exit(127);
    close_nonstandard_fds();
    for (auto const& executable : executable_candidates) execve(executable.c_str(), argv.data(), environment.data());
    _exit(127);
  }

  close((*stdin_pipe)[0]);
  close((*stdout_pipe)[1]);
  close((*gate_pipe)[0]);
  stdin_fd_ = (*stdin_pipe)[1];
  stdout_fd_ = (*stdout_pipe)[0];

  auto abort_before_exec = [&](std::string message, int saved_errno) -> ava::core::VoidResult {
    close((*gate_pipe)[1]);
    kill(pid, SIGKILL);
    int status = 0;
    waitpid_retry(pid, &status, 0);
    close_fds();
    pid_ = -1;
    owned_pgid_ = -1;
    errno = saved_errno;
    return std::unexpected(errno_error(std::move(message), config_));
  };

  bool group_set = false;
  for (int attempt = 0; attempt < 20; ++attempt)
  {
    if (setpgid(pid, pid) == 0)
    {
      group_set = true;
      break;
    }
    if (errno != EINTR)
      break;
  }
  int const setpgid_errno = errno;
  pid_t const child_pgid = getpgid(pid);
  int const getpgid_errno = errno;
  if (!group_set || pid <= 1 || parent_pgid <= 0 || child_pgid != pid || child_pgid == parent_pgid)
  {
    return abort_before_exec("failed to establish verified LSP process group", group_set ? getpgid_errno : setpgid_errno);
  }

  pid_ = pid;
  owned_pgid_ = child_pgid;
  ScopedSignalIgnore const ignore_sigpipe(SIGPIPE);
  char const release = '1';
  if (write_retry((*gate_pipe)[1], &release, 1) != 1)
  {
    int const saved_errno = errno;
    return abort_before_exec("failed to release LSP server for exec", saved_errno);
  }
  close((*gate_pipe)[1]);
  return {};
}

ava::core::VoidResult SubprocessLspClient::initialize(CancelCallback cancel_requested)
{
  auto const deadline = std::chrono::steady_clock::now() + config_.startup_timeout;
  auto const root_uri = file_uri(config_.workspace_root);
  std::string const params = "{\"processId\":null,\"rootUri\":" + json_string(root_uri) + ",\"capabilities\":{}}";
  auto response = request_response("initialize", params, deadline, config_.startup_timeout, "startup", cancel_requested);
  if (!response) return std::unexpected(std::move(response.error()));
  return send_notification("initialized", "{}", deadline, config_.startup_timeout, "startup", cancel_requested);
}

ava::core::Result<std::vector<Diagnostic>> SubprocessLspClient::diagnostics(std::filesystem::path const& path,
                                                                             CancelCallback cancel_requested)
{
  if (is_canceled(cancel_requested)) {
    auto error = canceled_error("LSP diagnostics canceled", config_);
    error.with_context("path", path.string());
    return std::unexpected(std::move(error));
  }
  auto const deadline = std::chrono::steady_clock::now() + config_.request_timeout;
  auto const uri = file_uri(path);
  std::string const params = "{\"textDocument\":{\"uri\":" + json_string(uri) + "}}";
  auto response = request_response("textDocument/diagnostic", params, deadline, config_.request_timeout, "request", cancel_requested);
  if (!response) return std::unexpected(std::move(response.error()));
  return parse_diagnostics_response(*response, config_, path);
}

ava::core::Result<std::vector<Symbol>> SubprocessLspClient::document_symbols(std::filesystem::path const& path,
                                                                              CancelCallback cancel_requested)
{
  if (is_canceled(cancel_requested)) return std::unexpected(canceled_error("LSP document symbols canceled", config_));
  auto const deadline = std::chrono::steady_clock::now() + config_.request_timeout;
  auto const uri = file_uri(path);
  std::string const params = "{\"textDocument\":{\"uri\":" + json_string(uri) + "}}";
  auto response = request_response("textDocument/documentSymbol", params, deadline, config_.request_timeout, "request", cancel_requested);
  if (!response) return std::unexpected(std::move(response.error()));
  return parse_document_symbols_response(*response, config_, path);
}

ava::core::Result<std::vector<Symbol>> SubprocessLspClient::workspace_symbols(std::string_view query,
                                                                              CancelCallback cancel_requested)
{
  if (is_canceled(cancel_requested)) return std::unexpected(canceled_error("LSP workspace symbols canceled", config_));
  auto const deadline = std::chrono::steady_clock::now() + config_.request_timeout;
  std::string const params = "{\"query\":" + json_string(query) + "}";
  auto response = request_response("workspace/symbol", params, deadline, config_.request_timeout, "request", cancel_requested);
  if (!response) return std::unexpected(std::move(response.error()));
  return parse_workspace_symbols_response(*response, config_);
}

ava::core::Result<std::vector<Location>> SubprocessLspClient::definitions(std::filesystem::path const& path, int line, int column,
                                                                          CancelCallback cancel_requested)
{
  if (is_canceled(cancel_requested))
    return std::unexpected(canceled_error("LSP definition canceled", config_));
  if (line < 0 || column < 0)
  {
    return std::unexpected(lsp_error(ava::core::ErrorCategory::InvalidArgument, "LSP definition position is invalid", config_));
  }
  auto const deadline = std::chrono::steady_clock::now() + config_.request_timeout;
  if (auto opened = send_did_open(path, deadline, cancel_requested); !opened)
    return std::unexpected(std::move(opened.error()));
  auto const uri = file_uri(path);
  std::string const params =
      "{\"textDocument\":{\"uri\":" + json_string(uri) + "},\"position\":{\"line\":" + std::to_string(line) + ",\"character\":" + std::to_string(column) + "}}";
  auto response = request_response("textDocument/definition", params, deadline, config_.request_timeout, "request", cancel_requested);
  if (!response)
    return std::unexpected(std::move(response.error()));
  return parse_definition_response(*response, config_);
}

ava::core::Result<std::vector<Location>> SubprocessLspClient::references(std::filesystem::path const& path, int line, int column,
                                                                         CancelCallback cancel_requested)
{
  if (is_canceled(cancel_requested))
    return std::unexpected(canceled_error("LSP references canceled", config_));
  if (line < 0 || column < 0)
  {
    return std::unexpected(lsp_error(ava::core::ErrorCategory::InvalidArgument, "LSP references position is invalid", config_));
  }
  auto const deadline = std::chrono::steady_clock::now() + config_.request_timeout;
  if (auto opened = send_did_open(path, deadline, cancel_requested); !opened)
    return std::unexpected(std::move(opened.error()));
  auto const uri = file_uri(path);
  std::string const params = "{\"textDocument\":{\"uri\":" + json_string(uri) + "},\"position\":{\"line\":" + std::to_string(line) +
                             ",\"character\":" + std::to_string(column) + "},\"context\":{\"includeDeclaration\":true}}";
  auto response = request_response("textDocument/references", params, deadline, config_.request_timeout, "request", cancel_requested);
  if (!response)
    return std::unexpected(std::move(response.error()));
  return parse_definition_response(*response, config_);
}

ava::core::VoidResult SubprocessLspClient::send_notification(std::string_view method, std::string_view params_json,
                                                              std::chrono::steady_clock::time_point deadline,
                                                              std::chrono::milliseconds timeout, std::string_view phase,
                                                              CancelCallback cancel_requested)
{
  std::string const body =
      "{\"jsonrpc\":\"2.0\",\"method\":" + json_string(method) + ",\"params\":" + std::string(params_json) + "}";
  return write_message(body, deadline, timeout, phase, method, cancel_requested);
}

bool SubprocessLspClient::is_alive()
{
  return check_child_running().has_value();
}

ava::core::VoidResult SubprocessLspClient::send_did_open(std::filesystem::path const& path, std::chrono::steady_clock::time_point deadline,
                                                         CancelCallback cancel_requested)
{
  if (is_canceled(cancel_requested))
  {
    terminate_child();
    return std::unexpected(canceled_error("LSP didOpen canceled", config_));
  }
  auto content = read_text_document(path, config_, deadline, cancel_requested);
  if (!content)
  {
    if (is_canceled(cancel_requested))
    {
      terminate_child();
      return std::unexpected(canceled_error("LSP didOpen canceled", config_));
    }
    if (std::chrono::steady_clock::now() >= deadline)
    {
      auto error = lsp_error(ava::core::ErrorCategory::Tool, "timed out reading LSP document", config_);
      error.with_context("timeout_ms", std::to_string(config_.request_timeout.count()));
      error.with_context("phase", "request");
      error.with_context("method", "textDocument/didOpen");
      terminate_child();
      return std::unexpected(std::move(error));
    }
    return std::unexpected(std::move(content.error()));
  }
  if (is_canceled(cancel_requested))
  {
    terminate_child();
    return std::unexpected(canceled_error("LSP didOpen canceled", config_));
  }
  if (std::chrono::steady_clock::now() >= deadline)
  {
    auto error = lsp_error(ava::core::ErrorCategory::Tool, "timed out reading LSP document", config_);
    error.with_context("timeout_ms", std::to_string(config_.request_timeout.count()));
    error.with_context("phase", "request");
    error.with_context("method", "textDocument/didOpen");
    terminate_child();
    return std::unexpected(std::move(error));
  }
  auto const uri = file_uri(path);
  if (open_documents_.contains(uri))
    return {};
  std::string const params = "{\"textDocument\":{\"uri\":" + json_string(uri) + ",\"languageId\":" + json_string(config_.language_id) +
                             ",\"version\":1,\"text\":" + json_string(*content) + "}}";
  auto sent = send_notification("textDocument/didOpen", params, deadline, config_.request_timeout, "request", cancel_requested);
  if (!sent)
    return sent;
  open_documents_.insert(uri);
  return {};
}

ava::core::Result<std::string> SubprocessLspClient::request_response(std::string_view method, std::string_view params_json,
                                                                     std::chrono::steady_clock::time_point deadline, std::chrono::milliseconds timeout,
                                                                     std::string_view phase, CancelCallback cancel_requested)
{
  int const id = next_id_++;
  std::string const body =
      "{\"jsonrpc\":\"2.0\",\"id\":" + std::to_string(id) + ",\"method\":" + json_string(method) + ",\"params\":" + std::string(params_json) + "}";
  if (auto written = write_message(body, deadline, timeout, phase, method, cancel_requested); !written)
  {
    if (is_canceled(cancel_requested))
    {
      terminate_child();
      return std::unexpected(canceled_error("LSP request canceled", config_));
    }
    return std::unexpected(std::move(written.error()));
  }

  while (true)
  {
    auto message = read_message(deadline, timeout, phase, method, cancel_requested);
    if (!message)
    {
      if (is_canceled(cancel_requested))
      {
        terminate_child();
        return std::unexpected(canceled_error("LSP request canceled", config_));
      }
      return std::unexpected(std::move(message.error()));
    }
    if (is_canceled(cancel_requested))
    {
      terminate_child();
      return std::unexpected(canceled_error("LSP request canceled", config_));
    }
    auto const response_id = ava::core::json::integer_field(*message, "id");
    if (!response_id)
    {
      if (ava::core::json::string_field(*message, "method"))
        continue;
      auto error = lsp_error(ava::core::ErrorCategory::Tool, "LSP response is malformed", config_);
      error.with_context("method", std::string(method));
      return std::unexpected(std::move(error));
    }
    if (*response_id != id)
      continue;
    if (ava::core::json::object_field(*message, "error"))
    {
      auto error = lsp_error(ava::core::ErrorCategory::Tool, "LSP request returned an error", config_);
      error.with_context("method", std::string(method));
      return std::unexpected(std::move(error));
    }
    return *message;
  }
}

ava::core::VoidResult SubprocessLspClient::write_message(std::string_view body, std::chrono::steady_clock::time_point deadline,
                                                         std::chrono::milliseconds timeout, std::string_view phase, std::string_view method,
                                                         CancelCallback cancel_requested)
{
  if (is_canceled(cancel_requested))
  {
    terminate_child();
    return std::unexpected(canceled_error("LSP request canceled", config_));
  }
  if (auto running = check_child_running(); !running)
  {
    if (is_canceled(cancel_requested))
    {
      terminate_child();
      return std::unexpected(canceled_error("LSP request canceled", config_));
    }
    return std::unexpected(std::move(running.error()));
  }
  if (is_canceled(cancel_requested))
  {
    terminate_child();
    return std::unexpected(canceled_error("LSP request canceled", config_));
  }
  if (std::chrono::steady_clock::now() >= deadline)
  {
    auto error = lsp_error(ava::core::ErrorCategory::Tool, "timed out writing LSP request", config_);
    error.with_context("timeout_ms", std::to_string(timeout.count()));
    error.with_context("phase", std::string(phase));
    error.with_context("method", std::string(method));
    terminate_child();
    return std::unexpected(std::move(error));
  }
  std::string const frame = "Content-Length: " + std::to_string(body.size()) + "\r\n\r\n" + std::string(body);
  std::size_t offset = 0;
  ScopedSignalIgnore const ignore_sigpipe(SIGPIPE);
  while (offset < frame.size())
  {
    if (is_canceled(cancel_requested))
    {
      terminate_child();
      return std::unexpected(canceled_error("LSP request canceled", config_));
    }
    auto const bytes = write_retry(stdin_fd_, frame.data() + offset, frame.size() - offset);
    if (is_canceled(cancel_requested))
    {
      terminate_child();
      return std::unexpected(canceled_error("LSP request canceled", config_));
    }
    if (bytes < 0)
    {
      if (is_canceled(cancel_requested))
      {
        terminate_child();
        return std::unexpected(canceled_error("LSP request canceled", config_));
      }
      if (errno == EAGAIN || errno == EWOULDBLOCK)
      {
        if (auto writable = wait_for_writable(deadline, timeout, phase, method, cancel_requested); !writable)
        {
          if (is_canceled(cancel_requested))
          {
            terminate_child();
            return std::unexpected(canceled_error("LSP request canceled", config_));
          }
          return std::unexpected(std::move(writable.error()));
        }
        continue;
      }
      if (errno == EPIPE)
      {
        if (auto running = check_child_running(); !running)
          return std::unexpected(std::move(running.error()));
      }
      terminate_child();
      return std::unexpected(errno_error("failed to write LSP request", config_));
    }
    if (bytes == 0)
    {
      if (is_canceled(cancel_requested))
      {
        terminate_child();
        return std::unexpected(canceled_error("LSP request canceled", config_));
      }
      if (auto running = check_child_running(); !running)
        return std::unexpected(std::move(running.error()));
      terminate_child();
      return std::unexpected(lsp_error(ava::core::ErrorCategory::Io, "failed to write LSP request", config_));
    }
    offset += static_cast<std::size_t>(bytes);
  }
  return {};
}

ava::core::Result<std::string> SubprocessLspClient::read_message(std::chrono::steady_clock::time_point deadline, std::chrono::milliseconds timeout,
                                                                 std::string_view phase, std::string_view method, CancelCallback cancel_requested)
{
  while (true)
  {
    if (is_canceled(cancel_requested))
    {
      terminate_child();
      return std::unexpected(canceled_error("LSP request canceled", config_));
    }
    auto const header_end = read_buffer_.find("\r\n\r\n");
    if (header_end != std::string::npos)
    {
      auto length = parse_content_length(std::string_view(read_buffer_).substr(0, header_end), config_);
      if (!length)
      {
        if (is_canceled(cancel_requested))
        {
          terminate_child();
          return std::unexpected(canceled_error("LSP request canceled", config_));
        }
        return std::unexpected(std::move(length.error()));
      }
      auto const body_start = header_end + 4;
      if (read_buffer_.size() >= body_start + *length)
      {
        auto body = read_buffer_.substr(body_start, *length);
        read_buffer_.erase(0, body_start + *length);
        return body;
      }
    }
    else if (read_buffer_.size() > kMaxLspHeaderBytes)
    {
      if (is_canceled(cancel_requested))
      {
        terminate_child();
        return std::unexpected(canceled_error("LSP request canceled", config_));
      }
      auto error = lsp_error(ava::core::ErrorCategory::Tool, "LSP response header exceeds size cap", config_);
      error.with_context("max_bytes", std::to_string(kMaxLspHeaderBytes));
      return std::unexpected(std::move(error));
    }
    if (read_buffer_.size() > kMaxLspMessageBytes + kMaxLspHeaderBytes)
    {
      if (is_canceled(cancel_requested))
      {
        terminate_child();
        return std::unexpected(canceled_error("LSP request canceled", config_));
      }
      auto error = lsp_error(ava::core::ErrorCategory::Tool, "LSP response exceeds message cap", config_);
      error.with_context("max_bytes", std::to_string(kMaxLspMessageBytes));
      return std::unexpected(std::move(error));
    }

    if (auto readable = wait_for_readable(deadline, timeout, phase, method, cancel_requested); !readable)
    {
      if (is_canceled(cancel_requested))
      {
        terminate_child();
        return std::unexpected(canceled_error("LSP request canceled", config_));
      }
      return std::unexpected(std::move(readable.error()));
    }
    std::array<char, 4096> buffer{};
    auto const bytes = read_retry(stdout_fd_, buffer.data(), buffer.size());
    if (is_canceled(cancel_requested))
    {
      terminate_child();
      return std::unexpected(canceled_error("LSP request canceled", config_));
    }
    if (bytes > 0)
    {
      read_buffer_.append(buffer.data(), static_cast<std::size_t>(bytes));
      continue;
    }
    if (bytes == 0)
    {
      if (is_canceled(cancel_requested))
      {
        terminate_child();
        return std::unexpected(canceled_error("LSP request canceled", config_));
      }
      if (auto running = check_child_running(); !running)
        return std::unexpected(std::move(running.error()));
      auto error = lsp_error(ava::core::ErrorCategory::Io, "LSP server closed stdout", config_);
      terminate_child();
      return std::unexpected(std::move(error));
    }
    if (errno == EAGAIN || errno == EWOULDBLOCK)
      continue;
    if (is_canceled(cancel_requested))
    {
      terminate_child();
      return std::unexpected(canceled_error("LSP request canceled", config_));
    }
    terminate_child();
    return std::unexpected(errno_error("failed to read LSP response", config_));
  }
}

ava::core::VoidResult SubprocessLspClient::wait_for_readable(std::chrono::steady_clock::time_point deadline, std::chrono::milliseconds timeout,
                                                             std::string_view phase, std::string_view method, CancelCallback cancel_requested)
{
  if (is_canceled(cancel_requested))
  {
    terminate_child();
    return std::unexpected(canceled_error("LSP request canceled", config_));
  }
  if (auto running = check_child_running(); !running)
  {
    if (is_canceled(cancel_requested))
    {
      terminate_child();
      return std::unexpected(canceled_error("LSP request canceled", config_));
    }
    return std::unexpected(std::move(running.error()));
  }
  if (is_canceled(cancel_requested))
  {
    terminate_child();
    return std::unexpected(canceled_error("LSP request canceled", config_));
  }
  auto const timeout_ms = remaining_ms(deadline);
  if (timeout_ms == 0)
  {
    auto error = lsp_error(ava::core::ErrorCategory::Tool, "timed out waiting for LSP response", config_);
    error.with_context("timeout_ms", std::to_string(timeout.count()));
    error.with_context("phase", std::string(phase));
    error.with_context("method", std::string(method));
    terminate_child();
    return std::unexpected(std::move(error));
  }

  pollfd fd{.fd = stdout_fd_, .events = POLLIN, .revents = 0};
  int const poll_timeout = static_cast<int>(std::min<std::size_t>(timeout_ms, 100));
  int const polled = poll(&fd, 1, poll_timeout);
  int const poll_errno = errno;
  if (is_canceled(cancel_requested))
  {
    terminate_child();
    return std::unexpected(canceled_error("LSP request canceled", config_));
  }
  if (polled < 0)
  {
    if (poll_errno == EINTR)
      return {};
    errno = poll_errno;
    return std::unexpected(errno_error("failed to poll LSP response", config_));
  }
  if (polled == 0)
    return check_child_running();
  if ((fd.revents & (POLLERR | POLLHUP | POLLNVAL)) != 0 && (fd.revents & POLLIN) == 0)
  {
    if (auto running = check_child_running(); !running)
      return std::unexpected(std::move(running.error()));
    auto error = lsp_error(ava::core::ErrorCategory::Io, "LSP server pipe closed", config_);
    terminate_child();
    return std::unexpected(std::move(error));
  }
  return {};
}

ava::core::VoidResult SubprocessLspClient::wait_for_writable(std::chrono::steady_clock::time_point deadline, std::chrono::milliseconds timeout,
                                                             std::string_view phase, std::string_view method, CancelCallback cancel_requested)
{
  if (is_canceled(cancel_requested))
  {
    terminate_child();
    return std::unexpected(canceled_error("LSP request canceled", config_));
  }
  if (auto running = check_child_running(); !running)
  {
    if (is_canceled(cancel_requested))
    {
      terminate_child();
      return std::unexpected(canceled_error("LSP request canceled", config_));
    }
    return std::unexpected(std::move(running.error()));
  }
  if (is_canceled(cancel_requested))
  {
    terminate_child();
    return std::unexpected(canceled_error("LSP request canceled", config_));
  }
  auto const timeout_ms = remaining_ms(deadline);
  if (timeout_ms == 0)
  {
    auto error = lsp_error(ava::core::ErrorCategory::Tool, "timed out writing LSP request", config_);
    error.with_context("timeout_ms", std::to_string(timeout.count()));
    error.with_context("phase", std::string(phase));
    error.with_context("method", std::string(method));
    terminate_child();
    return std::unexpected(std::move(error));
  }

  pollfd fd{.fd = stdin_fd_, .events = POLLOUT, .revents = 0};
  int const poll_timeout = static_cast<int>(std::min<std::size_t>(timeout_ms, 100));
  int const polled = poll(&fd, 1, poll_timeout);
  int const poll_errno = errno;
  if (is_canceled(cancel_requested))
  {
    terminate_child();
    return std::unexpected(canceled_error("LSP request canceled", config_));
  }
  if (polled < 0)
  {
    if (poll_errno == EINTR)
      return {};
    errno = poll_errno;
    return std::unexpected(errno_error("failed to poll LSP request pipe", config_));
  }
  if (polled == 0)
    return check_child_running();
  if ((fd.revents & (POLLERR | POLLHUP | POLLNVAL)) != 0 && (fd.revents & POLLOUT) == 0)
  {
    if (auto running = check_child_running(); !running)
      return std::unexpected(std::move(running.error()));
    auto error = lsp_error(ava::core::ErrorCategory::Io, "LSP server request pipe closed", config_);
    terminate_child();
    return std::unexpected(std::move(error));
  }
  return {};
}

ava::core::VoidResult SubprocessLspClient::check_child_running()
{
  if (pid_ < 0)
  {
    return std::unexpected(lsp_error(ava::core::ErrorCategory::Io, "LSP server is not running", config_));
  }

  siginfo_t info{};
  if (waitid_retry(P_PID, static_cast<id_t>(pid_), &info, WEXITED | WNOHANG | WNOWAIT) != 0)
  {
    if (errno == ECHILD)
    {
      close_fd(stdin_fd_);
      pid_ = -1;
      owned_pgid_ = -1;
      return std::unexpected(lsp_error(ava::core::ErrorCategory::Io, "LSP server is already reaped", config_));
    }
    return std::unexpected(errno_error("failed to inspect LSP server", config_));
  }
  if (info.si_pid == 0)
    return {};

  auto error = lsp_error(ava::core::ErrorCategory::Io, "LSP server exited", config_);
  error.with_context("status", exit_detail(info));
  terminate_child();
  return std::unexpected(std::move(error));
}

void SubprocessLspClient::close_fds() noexcept
{
  close_fd(stdin_fd_);
  close_fd(stdout_fd_);
}

void SubprocessLspClient::terminate_child() noexcept
{
  pid_t const pid = pid_;
  pid_t const pgid = owned_pgid_;
  close_fd(stdin_fd_);

  bool const verified_group = pid > 1 && pgid > 1 && pgid == pid && getpgid(pid) == pgid;
  if (!verified_group)
  {
    if (pid > 1)
    {
      kill(pid, SIGKILL);
      int status = 0;
      waitpid_retry(pid, &status, 0);
    }
    pid_ = -1;
    owned_pgid_ = -1;
    return;
  }

  kill(-pgid, SIGTERM);
  bool group_still_verified = true;
  auto const grace_deadline = std::chrono::steady_clock::now() + kTerminationGrace;
  while (std::chrono::steady_clock::now() < grace_deadline)
  {
    siginfo_t info{};
    if (waitid_retry(P_PID, static_cast<id_t>(pid), &info, WEXITED | WNOHANG | WNOWAIT) != 0)
    {
      if (errno == ECHILD)
        group_still_verified = false;
      break;
    }
    if (info.si_pid != 0)
      break;
    std::this_thread::sleep_for(kTerminationPollInterval);
  }

  if (group_still_verified)
    kill(-pgid, SIGKILL);
  int status = 0;
  waitpid_retry(pid, &status, 0);
  pid_ = -1;
  owned_pgid_ = -1;
}

}  // namespace ava::lsp
