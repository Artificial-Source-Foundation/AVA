#pragma once

#include "ava/core/result.h"

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace ava::mcp {

enum class McpServerScope {
  Global,
  Project,
};

struct McpServerConfig {
  std::string id;
  std::string name;
  std::string command;
  std::vector<std::string> args;
  bool enabled = true;
  McpServerScope scope = McpServerScope::Global;
  std::filesystem::path source_path;
};

struct McpConfig {
  std::vector<McpServerConfig> servers;
  std::filesystem::path global_config_file;
  std::filesystem::path project_config_file;
};

struct McpConfigLoadOptions {
  std::filesystem::path workspace_dir;
  std::filesystem::path global_config_file;
  std::filesystem::path project_config_file;
};

[[nodiscard]] std::string_view to_string(McpServerScope scope);
[[nodiscard]] bool is_valid_mcp_identifier(std::string_view id);
[[nodiscard]] McpConfigLoadOptions default_mcp_config_options(std::filesystem::path const& workspace_dir);
[[nodiscard]] ava::core::Result<McpConfig> parse_mcp_config(std::string_view json, std::filesystem::path config_path,
                                                            McpServerScope scope);
[[nodiscard]] ava::core::Result<McpConfig> load_mcp_config(McpConfigLoadOptions const& options);

}  // namespace ava::mcp
