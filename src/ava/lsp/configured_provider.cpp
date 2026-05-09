#include "ava/lsp/configured_provider.h"

#include "ava/core/ids.h"
#include "ava/core/json.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <mutex>
#include <optional>
#include <string_view>
#include <unordered_map>
#include <utility>

namespace ava::lsp {
namespace {

constexpr std::uintmax_t kMaxLspConfigBytes = 64 * 1024;
constexpr std::size_t kMaxServers = 8;
constexpr std::size_t kMaxArgv = 32;
constexpr std::size_t kMaxArgBytes = 4096;
constexpr std::size_t kMaxIdBytes = 64;
constexpr long long kMinTimeoutMs = 100;
constexpr long long kMaxTimeoutMs = 30000;

struct ConfiguredServer {
  std::string id;
  std::vector<std::string> argv;
  std::vector<std::string> file_extensions;
  std::string language_id = "plaintext";
  std::chrono::milliseconds request_timeout{3000};
};

std::string argv_permission_command(std::vector<std::string> const& argv)
{
  std::string command = "[";
  for (std::size_t index = 0; index < argv.size(); ++index) {
    if (index > 0) command += ',';
    command += '"';
    command += ava::core::json::escape(argv[index]);
    command += '"';
  }
  command += ']';
  return command;
}

ava::core::Error config_error(ava::core::ErrorCategory category, std::string message, std::filesystem::path const& path)
{
  auto error = ava::core::Error(category, std::move(message));
  error.with_context("path", path.string());
  return error;
}

bool has_control_byte(std::string_view value)
{
  return std::ranges::any_of(value, [](unsigned char ch) { return ch < 0x20 || ch == 0x7f; });
}

bool valid_server_id(std::string_view value)
{
  if (value.empty() || value.size() > kMaxIdBytes || has_control_byte(value)) return false;
  return std::ranges::all_of(value, [](unsigned char ch) {
    return std::isalnum(ch) != 0 || ch == '_' || ch == '-' || ch == '.';
  });
}

bool valid_extension(std::string_view value)
{
  if (value.empty() || value.size() > 32 || value.front() != '.' || has_control_byte(value)) return false;
  return std::ranges::all_of(value.substr(1), [](unsigned char ch) {
    return std::isalnum(ch) != 0 || ch == '_' || ch == '-' || ch == '+';
  });
}

bool valid_language_id(std::string_view value)
{
  if (value.empty() || value.size() > 64 || has_control_byte(value)) return false;
  return std::ranges::all_of(value, [](unsigned char ch) {
    return std::isalnum(ch) != 0 || ch == '_' || ch == '-' || ch == '+' || ch == '#';
  });
}

std::optional<char> field_first_char(std::string_view object, std::string_view key)
{
  auto const start = ava::core::json::field_value_start(object, key);
  if (!start || *start >= object.size()) return std::nullopt;
  return object[*start];
}

bool field_is_present(std::string_view object, std::string_view key)
{
  return ava::core::json::field_value_start(object, key).has_value();
}

std::optional<std::vector<char>> array_value_start_chars(std::string_view array)
{
  if (array.size() < 2 || array.front() != '[' || array.back() != ']') return std::nullopt;
  std::vector<char> starts;
  bool in_string = false;
  bool escaped = false;
  int object_depth = 0;
  int array_depth = 0;
  bool expecting_value = true;
  for (std::size_t index = 1; index + 1 < array.size(); ++index) {
    auto const ch = array[index];
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
    if (object_depth == 0 && array_depth == 0 && std::isspace(static_cast<unsigned char>(ch))) continue;
    if (object_depth == 0 && array_depth == 0 && ch == ',') {
      expecting_value = true;
      continue;
    }
    if (expecting_value && object_depth == 0 && array_depth == 0) {
      starts.push_back(ch);
      expecting_value = false;
    }
    if (ch == '"') {
      in_string = true;
    } else if (ch == '{') {
      ++object_depth;
    } else if (ch == '}') {
      if (object_depth > 0) --object_depth;
    } else if (ch == '[') {
      ++array_depth;
    } else if (ch == ']') {
      if (array_depth > 0) --array_depth;
    }
  }
  return starts;
}

std::optional<std::size_t> json_string_end(std::string_view text, std::size_t start)
{
  if (start >= text.size() || text[start] != '"') return std::nullopt;
  bool escaped = false;
  for (std::size_t index = start + 1; index < text.size(); ++index) {
    auto const ch = text[index];
    if (escaped) {
      escaped = false;
      continue;
    }
    if (ch == '\\') {
      escaped = true;
      continue;
    }
    if (ch == '"') return index + 1;
  }
  return std::nullopt;
}

std::optional<std::size_t> json_balanced_end(std::string_view text, std::size_t start, char open, char close)
{
  if (start >= text.size() || text[start] != open) return std::nullopt;
  std::size_t depth = 0;
  for (std::size_t index = start; index < text.size(); ++index) {
    auto const ch = text[index];
    if (ch == '"') {
      auto const end = json_string_end(text, index);
      if (!end) return std::nullopt;
      index = *end - 1;
      continue;
    }
    if (ch == open) ++depth;
    if (ch == close) {
      if (depth == 0) return std::nullopt;
      --depth;
      if (depth == 0) return index + 1;
    }
  }
  return std::nullopt;
}

ava::core::Result<long long> strict_integer_field(std::string_view object, std::string_view key, long long fallback)
{
  auto const start = ava::core::json::field_value_start(object, key);
  if (!start) return fallback;
  auto index = *start;
  if (index >= object.size() || !std::isdigit(static_cast<unsigned char>(object[index]))) {
    return std::unexpected(ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "integer field must be a JSON integer"));
  }
  long long value = 0;
  while (index < object.size() && std::isdigit(static_cast<unsigned char>(object[index]))) {
    auto const digit = static_cast<long long>(object[index] - '0');
    if (value > (std::numeric_limits<long long>::max() - digit) / 10) {
      return std::unexpected(ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "integer field is too large"));
    }
    value = value * 10 + digit;
    ++index;
  }
  while (index < object.size() && std::isspace(static_cast<unsigned char>(object[index]))) ++index;
  if (index >= object.size() || (object[index] != ',' && object[index] != '}')) {
    return std::unexpected(ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "integer field must be a JSON integer"));
  }
  return value;
}

std::optional<std::vector<std::string>> strict_string_array_field(std::string_view object, std::string_view key)
{
  auto const start = ava::core::json::field_value_start(object, key);
  if (!start) return std::vector<std::string>{};
  if (*start >= object.size() || object[*start] != '[') return std::nullopt;
  auto const end = json_balanced_end(object, *start, '[', ']');
  if (!end) return std::nullopt;
  auto const starts = array_value_start_chars(object.substr(*start, *end - *start));
  if (!starts || std::ranges::any_of(*starts, [](char ch) { return ch != '"'; })) return std::nullopt;
  auto values = ava::core::json::strings_in_array_field(object, key);
  if (!values.empty()) return values;
  auto const array = object.substr(*start, *end - *start);
  auto index = std::size_t{1};
  while (index + 1 < array.size() && std::isspace(static_cast<unsigned char>(array[index]))) ++index;
  if (index + 1 < array.size() && array[index] != ']') return std::nullopt;
  return std::vector<std::string>{};
}

std::optional<std::vector<std::string>> strict_object_array_field(std::string_view object, std::string_view key)
{
  auto const start = ava::core::json::field_value_start(object, key);
  if (!start) return std::vector<std::string>{};
  if (*start >= object.size() || object[*start] != '[') return std::nullopt;
  auto values = ava::core::json::objects_in_array_field(object, key);
  if (values.empty()) {
    auto const end = json_balanced_end(object, *start, '[', ']');
    if (!end) return std::nullopt;
    auto const array = object.substr(*start, *end - *start);
    auto index = std::size_t{1};
    while (index + 1 < array.size() && std::isspace(static_cast<unsigned char>(array[index]))) ++index;
    if (index + 1 < array.size() && array[index] != ']') return std::nullopt;
  } else {
    auto const end = json_balanced_end(object, *start, '[', ']');
    if (!end) return std::nullopt;
    auto const starts = array_value_start_chars(object.substr(*start, *end - *start));
    if (!starts || starts->size() != values.size() || std::ranges::any_of(*starts, [](char ch) { return ch != '{'; })) {
      return std::nullopt;
    }
  }
  return values;
}

ava::core::Result<std::string> read_config_file(std::filesystem::path const& path)
{
  std::error_code exists_error;
  if (!std::filesystem::exists(path, exists_error)) return std::string{};
  if (exists_error) {
    auto error = config_error(ava::core::ErrorCategory::Io, "failed to inspect LSP config", path);
    error.with_context("cause", exists_error.message());
    return std::unexpected(std::move(error));
  }

  std::error_code status_error;
  auto const status = std::filesystem::symlink_status(path, status_error);
  if (status_error) {
    auto error = config_error(ava::core::ErrorCategory::Io, "failed to inspect LSP config", path);
    error.with_context("cause", status_error.message());
    return std::unexpected(std::move(error));
  }
  if (std::filesystem::is_symlink(status)) {
    return std::unexpected(config_error(ava::core::ErrorCategory::PermissionDenied,
                                        "LSP config must not be a symlink", path));
  }
  if (!std::filesystem::is_regular_file(status)) {
    return std::unexpected(config_error(ava::core::ErrorCategory::InvalidArgument,
                                        "LSP config must be a regular file", path));
  }

  std::error_code size_error;
  auto const size = std::filesystem::file_size(path, size_error);
  if (size_error) {
    auto error = config_error(ava::core::ErrorCategory::Io, "failed to stat LSP config", path);
    error.with_context("cause", size_error.message());
    return std::unexpected(std::move(error));
  }
  if (size > kMaxLspConfigBytes) {
    auto error = config_error(ava::core::ErrorCategory::InvalidArgument, "LSP config exceeds maximum size", path);
    error.with_context("max_bytes", std::to_string(kMaxLspConfigBytes));
    return std::unexpected(std::move(error));
  }

  std::ifstream file(path, std::ios::binary);
  if (!file) return std::unexpected(config_error(ava::core::ErrorCategory::Io, "failed to open LSP config", path));
  std::string content(static_cast<std::size_t>(size), '\0');
  if (!content.empty()) file.read(content.data(), static_cast<std::streamsize>(content.size()));
  if (file.bad() || file.gcount() != static_cast<std::streamsize>(content.size())) {
    return std::unexpected(config_error(ava::core::ErrorCategory::Io, "failed to read LSP config", path));
  }
  return content;
}

ava::core::Result<void> parse_config(std::filesystem::path const& path, std::string_view json,
                                     std::vector<ConfiguredServer>& servers)
{
  if (json.empty()) return {};
  if (!ava::core::json::is_valid_object(json)) {
    return std::unexpected(config_error(ava::core::ErrorCategory::InvalidArgument,
                                        "LSP config must be a JSON object", path));
  }
  auto const version = strict_integer_field(json, "version", 1);
  if (!version) {
    return std::unexpected(config_error(ava::core::ErrorCategory::InvalidArgument,
                                        "LSP config version must be an integer", path));
  }
  if (*version != 1) {
    return std::unexpected(config_error(ava::core::ErrorCategory::InvalidArgument,
                                        "unsupported LSP config version", path));
  }

  if (field_is_present(json, "servers") && field_first_char(json, "servers") != std::optional<char>{'['}) {
    return std::unexpected(config_error(ava::core::ErrorCategory::InvalidArgument,
                                        "LSP config servers must be an array", path));
  }
  auto const server_objects = strict_object_array_field(json, "servers");
  if (!server_objects) {
    return std::unexpected(config_error(ava::core::ErrorCategory::InvalidArgument,
                                        "LSP config servers must contain only objects", path));
  }
  if (server_objects->empty()) return {};
  if (servers.size() + server_objects->size() > kMaxServers) {
    auto error = config_error(ava::core::ErrorCategory::InvalidArgument, "LSP config defines too many servers", path);
    error.with_context("max_servers", std::to_string(kMaxServers));
    return std::unexpected(std::move(error));
  }

  for (auto const& server_json : *server_objects) {
    auto id = ava::core::json::string_field(server_json, "id");
    if (!id || !valid_server_id(*id)) {
      return std::unexpected(config_error(ava::core::ErrorCategory::InvalidArgument,
                                          "LSP server id is invalid", path));
    }
    auto argv = strict_string_array_field(server_json, "argv");
    if (!argv || argv->empty() || argv->size() > kMaxArgv) {
      return std::unexpected(config_error(ava::core::ErrorCategory::InvalidArgument,
                                          "LSP server argv is invalid", path));
    }
    for (auto const& arg : *argv) {
      if (arg.empty() || arg.size() > kMaxArgBytes || has_control_byte(arg)) {
        return std::unexpected(config_error(ava::core::ErrorCategory::InvalidArgument,
                                            "LSP server argv contains an invalid argument", path));
      }
    }

    std::vector<std::string> extensions;
    if (field_is_present(server_json, "file_extensions") && field_first_char(server_json, "file_extensions") != std::optional<char>{'['}) {
      return std::unexpected(config_error(ava::core::ErrorCategory::InvalidArgument,
                                          "LSP server file_extensions must be an array", path));
    }
    auto extensions_field = strict_string_array_field(server_json, "file_extensions");
    if (!extensions_field) {
      return std::unexpected(config_error(ava::core::ErrorCategory::InvalidArgument,
                                          "LSP server file_extensions must contain only strings", path));
    }
    for (auto extension : *extensions_field) {
      if (!valid_extension(extension)) {
        return std::unexpected(config_error(ava::core::ErrorCategory::InvalidArgument,
                                            "LSP server file extension is invalid", path));
      }
      extensions.push_back(std::move(extension));
    }

    std::string language_id = "plaintext";
    if (field_is_present(server_json, "language_id")) {
      if (field_first_char(server_json, "language_id") != std::optional<char>{'"'}) {
        return std::unexpected(config_error(ava::core::ErrorCategory::InvalidArgument,
                                            "LSP server language_id must be a string", path));
      }
      auto parsed_language_id = ava::core::json::string_field(server_json, "language_id");
      if (!parsed_language_id) {
        return std::unexpected(config_error(ava::core::ErrorCategory::InvalidArgument,
                                            "LSP server language_id must be a string", path));
      }
      language_id = std::move(*parsed_language_id);
    }
    if (!valid_language_id(language_id)) {
      return std::unexpected(config_error(ava::core::ErrorCategory::InvalidArgument,
                                          "LSP server language_id is invalid", path));
    }

    auto const timeout_value = strict_integer_field(server_json, "timeout_ms", 3000);
    if (!timeout_value) {
      return std::unexpected(config_error(ava::core::ErrorCategory::InvalidArgument,
                                          "LSP server timeout_ms must be an integer", path));
    }
    if (*timeout_value < kMinTimeoutMs || *timeout_value > kMaxTimeoutMs) {
      return std::unexpected(config_error(ava::core::ErrorCategory::InvalidArgument,
                                          "LSP server timeout is outside supported limits", path));
    }

    if (std::ranges::any_of(servers, [&id](ConfiguredServer const& existing) { return existing.id == *id; })) {
      return std::unexpected(config_error(ava::core::ErrorCategory::InvalidArgument,
                                          "LSP server id is duplicated", path));
    }

    servers.push_back(ConfiguredServer{.id = std::move(*id),
                                       .argv = std::move(*argv),
                                       .file_extensions = std::move(extensions),
                                       .language_id = std::move(language_id),
                                       .request_timeout = std::chrono::milliseconds(*timeout_value)});
  }
  return {};
}

bool path_matches(ConfiguredServer const& server, std::filesystem::path const& path)
{
  if (server.file_extensions.empty()) return true;
  auto const extension = path.extension().string();
  return std::ranges::find(server.file_extensions, extension) != server.file_extensions.end();
}

class ConfiguredLspProvider final : public DiagnosticsProvider {
 public:
  ConfiguredLspProvider(std::filesystem::path workspace_root, ava::agent::Mode mode,
                        ava::permissions::PermissionResolver permission_resolver, std::vector<ConfiguredServer> servers)
      : workspace_root_(std::move(workspace_root)),
        mode_(mode),
        permission_resolver_(std::move(permission_resolver)),
        servers_(std::move(servers))
  {
  }

  [[nodiscard]] ava::core::Result<std::vector<Diagnostic>> diagnostics(
      std::filesystem::path const& path, CancelCallback cancel_requested = nullptr) override
  {
    auto client = client_for_path(path, cancel_requested);
    if (!client) return std::unexpected(std::move(client.error()));
    return (*client)->diagnostics(path, std::move(cancel_requested));
  }

  [[nodiscard]] ava::core::Result<std::vector<Symbol>> document_symbols(
      std::filesystem::path const& path, CancelCallback cancel_requested = nullptr) override
  {
    auto client = client_for_path(path, cancel_requested);
    if (!client) return std::unexpected(std::move(client.error()));
    return (*client)->document_symbols(path, std::move(cancel_requested));
  }

  [[nodiscard]] ava::core::Result<std::vector<Symbol>> workspace_symbols(
      std::string_view query, CancelCallback cancel_requested = nullptr) override
  {
    std::vector<Symbol> symbols;
    for (auto const& server : servers_) {
      auto client = start_client(server, cancel_requested);
      if (!client) return std::unexpected(std::move(client.error()));
      auto server_symbols = (*client)->workspace_symbols(query, cancel_requested);
      if (!server_symbols) return std::unexpected(std::move(server_symbols.error()));
      symbols.insert(symbols.end(), server_symbols->begin(), server_symbols->end());
    }
    return symbols;
  }

  [[nodiscard]] ava::core::Result<std::vector<Location>> definitions(
      std::filesystem::path const& path, int line, int column, CancelCallback cancel_requested = nullptr) override
  {
    auto client = client_for_path(path, cancel_requested);
    if (!client) return std::unexpected(std::move(client.error()));
    return (*client)->definitions(path, line, column, std::move(cancel_requested));
  }

  [[nodiscard]] ava::core::Result<std::vector<Location>> references(
      std::filesystem::path const& path, int line, int column, CancelCallback cancel_requested = nullptr) override
  {
    auto client = client_for_path(path, cancel_requested);
    if (!client) return std::unexpected(std::move(client.error()));
    return (*client)->references(path, line, column, std::move(cancel_requested));
  }

  void set_permission_request_ids(std::shared_ptr<std::vector<std::string>> ids) override
  {
    permission_request_ids_ = std::move(ids);
  }

 private:
  [[nodiscard]] ava::core::Result<std::shared_ptr<SubprocessLspClient>> client_for_path(
      std::filesystem::path const& path, CancelCallback const& cancel_requested)
  {
    auto const it = std::ranges::find_if(servers_, [&path](ConfiguredServer const& server) {
      return path_matches(server, path);
    });
    if (it == servers_.end()) {
      auto error = ava::core::Error(ava::core::ErrorCategory::Tool, "no configured LSP server matches path");
      error.with_context("path", path.string());
      return std::unexpected(std::move(error));
    }
    return start_client(*it, cancel_requested);
  }

  [[nodiscard]] ava::core::Result<std::shared_ptr<SubprocessLspClient>> start_client(
      ConfiguredServer const& server, CancelCallback const& cancel_requested)
  {
    std::lock_guard<std::mutex> lock(clients_mutex_);
    if (auto cached = clients_.find(server.id); cached != clients_.end()) {
      if (cached->second && cached->second->is_alive()) return cached->second;
      clients_.erase(cached);
    }
    auto launch = ensure_launch_permission(server);
    if (!launch) return std::unexpected(std::move(launch.error()));
    auto client = SubprocessLspClient::start(ServerConfig{.argv = server.argv,
                                                          .workspace_root = workspace_root_,
                                                          .request_timeout = server.request_timeout,
                                                          .language_id = server.language_id},
                                             cancel_requested);
    if (!client) return std::unexpected(std::move(client.error()));
    clients_.emplace(server.id, *client);
    return *client;
  }

  [[nodiscard]] ava::core::VoidResult ensure_launch_permission(ConfiguredServer const& server) const
  {
    auto const decision = ava::permissions::decide(ava::permissions::PermissionRequest{
        .operation = ava::permissions::Operation::LspServerLaunch,
        .mode = mode_,
        .workspace_dir = workspace_root_,
        .target_path = workspace_root_,
        .command = argv_permission_command(server.argv),
    });
    if (decision.action == ava::permissions::PermissionAction::Allow) return {};
    if (decision.action == ava::permissions::PermissionAction::Deny) {
      auto error = ava::core::Error(ava::core::ErrorCategory::PermissionDenied, "LSP server launch denied by policy");
      error.with_context("server", server.id);
      return std::unexpected(std::move(error));
    }
    if (!permission_resolver_) {
      auto error = ava::core::Error(ava::core::ErrorCategory::PermissionDenied, "LSP server launch requires permission");
      error.with_context("server", server.id);
      return std::unexpected(std::move(error));
    }
    auto permission_request_id = ava::core::make_id("permreq");
    auto resolved = permission_resolver_(ava::permissions::PermissionPrompt{
        .permission_request_id = permission_request_id,
        .operation = ava::permissions::Operation::LspServerLaunch,
        .mode = mode_,
        .workspace_dir = workspace_root_,
        .target_path = workspace_root_,
        .command = argv_permission_command(server.argv),
        .tool_name = "lsp_server_launch",
        .reason = decision.reason,
        .risk = decision.risk,
    });
    if (permission_request_ids_ &&
        std::ranges::find(*permission_request_ids_, permission_request_id) == permission_request_ids_->end()) {
      permission_request_ids_->push_back(permission_request_id);
    }
    if (!resolved) return std::unexpected(std::move(resolved.error()));
    if (*resolved == ava::permissions::PermissionResolution::Allow ||
        *resolved == ava::permissions::PermissionResolution::AllowSessionGrant) {
      return {};
    }
    auto error = ava::core::Error(ava::core::ErrorCategory::PermissionDenied, "LSP server launch denied");
    error.with_context("server", server.id);
    return std::unexpected(std::move(error));
  }

  std::filesystem::path workspace_root_;
  ava::agent::Mode mode_ = ava::agent::Mode::Build;
  ava::permissions::PermissionResolver permission_resolver_ = nullptr;
  std::vector<ConfiguredServer> servers_;
  std::mutex clients_mutex_;
  std::unordered_map<std::string, std::shared_ptr<SubprocessLspClient>> clients_;
  std::shared_ptr<std::vector<std::string>> permission_request_ids_ = nullptr;
};

}  // namespace

ava::core::Result<std::shared_ptr<DiagnosticsProvider>> make_configured_lsp_provider(ConfiguredLspProviderFiles const& files)
{
  std::vector<ConfiguredServer> servers;
  for (auto const& config_file : {files.global_config_file, files.project_config_file}) {
    if (config_file.empty()) continue;
    auto content = read_config_file(config_file);
    if (!content) return std::unexpected(std::move(content.error()));
    auto parsed = parse_config(config_file, *content, servers);
    if (!parsed) return std::unexpected(std::move(parsed.error()));
  }
  if (servers.empty()) return std::shared_ptr<DiagnosticsProvider>{};
  return std::make_shared<ConfiguredLspProvider>(files.workspace_root.lexically_normal(), files.mode,
                                                files.permission_resolver, std::move(servers));
}

}  // namespace ava::lsp
