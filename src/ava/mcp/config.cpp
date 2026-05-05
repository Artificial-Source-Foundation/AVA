#include "ava/mcp/config.h"

#include <cctype>
#include <string_view>
#include <utility>

#include "ava/config/xdg_paths.h"
#include "ava/core/json.h"
#include "ava/mcp/config_support.h"

namespace ava::mcp {

std::string_view to_string(McpServerScope scope)
{
  switch (scope) {
    case McpServerScope::Global:
      return "global";
    case McpServerScope::Project:
      return "project";
  }
  return "unknown";
}

bool is_valid_mcp_identifier(std::string_view id)
{
  if (id.empty() || id.size() > 128) return false;
  bool last_was_separator = false;
  for (char const ch : id) {
    auto const byte = static_cast<unsigned char>(ch);
    bool const allowed = std::isalnum(byte) != 0 || ch == '.' || ch == '_' || ch == '-';
    if (!allowed) return false;
    if ((ch == '.' || ch == '_' || ch == '-') && last_was_separator) return false;
    last_was_separator = ch == '.' || ch == '_' || ch == '-';
  }
  return !last_was_separator;
}

McpConfigLoadOptions default_mcp_config_options(std::filesystem::path const& workspace_dir)
{
  auto paths = ava::config::xdg_paths();
  McpConfigLoadOptions options;
  options.workspace_dir = workspace_dir;
  options.global_config_file = paths.ava_config_dir / "mcp.json";
  if (!workspace_dir.empty()) options.project_config_file = workspace_dir / ".ava" / "mcp.json";
  return options;
}

ava::core::Result<McpConfig> parse_mcp_config(std::string_view json, std::filesystem::path config_path,
                                              McpServerScope scope)
{
  if (json.size() > detail::kMaxMcpConfigBytes) {
    return std::unexpected(detail::config_error("MCP config exceeds maximum size")
                               .with_context("max_bytes", std::to_string(detail::kMaxMcpConfigBytes)));
  }
  if (!ava::core::json::is_valid_object(json)) {
    return std::unexpected(detail::config_error("MCP config must be a valid JSON object"));
  }
  if (ava::core::json::field_value_start(json, "servers") && !detail::array_field(json, "servers")) {
    return std::unexpected(detail::config_error("MCP config servers field must be an array"));
  }

  McpConfig config;
  if (scope == McpServerScope::Global) {
    config.global_config_file = config_path;
  } else {
    config.project_config_file = config_path;
  }

  for (auto const& server_json : ava::core::json::objects_in_array_field(json, "servers")) {
    auto id = ava::core::json::string_field(server_json, "id");
    if (!id || !is_valid_mcp_identifier(*id)) {
      return std::unexpected(
          detail::config_error("MCP server requires a valid id").with_context("config", config_path.string()));
    }
    auto command = ava::core::json::string_field(server_json, "command");
    if (!command || command->empty() || command->size() > detail::kMaxMcpArgBytes ||
        detail::has_forbidden_byte(*command)) {
      return std::unexpected(detail::config_error("MCP server requires a safe non-empty command")
                                 .with_context("server", *id)
                                 .with_context("config", config_path.string()));
    }
    auto args = detail::string_array_field(server_json, "args");
    if (!args) return std::unexpected(std::move(args.error()));
    for (auto const& arg : *args) {
      if (arg.size() > detail::kMaxMcpArgBytes || detail::has_forbidden_byte(arg)) {
        return std::unexpected(detail::config_error("MCP server arg is unsafe")
                                   .with_context("server", *id)
                                   .with_context("config", config_path.string()));
      }
    }
    auto const enabled = detail::bool_field(server_json, "enabled").value_or(scope == McpServerScope::Global);
    auto const name = ava::core::json::string_field(server_json, "name").value_or(*id);
    if (name.empty() || name.size() > 256 || detail::has_forbidden_byte(name)) {
      return std::unexpected(detail::config_error("MCP server name is unsafe")
                                 .with_context("server", *id)
                                 .with_context("config", config_path.string()));
    }

    config.servers.push_back(McpServerConfig{.id = std::move(*id),
                                             .name = std::move(name),
                                             .command = std::move(*command),
                                             .args = std::move(*args),
                                             .enabled = enabled,
                                             .scope = scope,
                                             .source_path = config_path});
  }
  return config;
}

ava::core::Result<McpConfig> load_mcp_config(McpConfigLoadOptions const& options)
{
  McpConfig merged;
  merged.global_config_file = options.global_config_file;
  merged.project_config_file = options.project_config_file;

  auto global = detail::load_optional_mcp_config(options.global_config_file, McpServerScope::Global);
  if (!global) return std::unexpected(std::move(global.error()));
  if (auto appended = detail::append_mcp_config(merged, std::move(*global)); !appended) {
    return std::unexpected(std::move(appended.error()));
  }

  auto project = detail::load_optional_mcp_config(options.project_config_file, McpServerScope::Project);
  if (!project) return std::unexpected(std::move(project.error()));
  if (auto appended = detail::append_mcp_config(merged, std::move(*project)); !appended) {
    return std::unexpected(std::move(appended.error()));
  }

  return merged;
}

}  // namespace ava::mcp
