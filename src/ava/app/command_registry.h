#pragma once

#include "ava/app/runtime.h"

#include "ava/mcp/stdio_client.h"

#include "ava/permissions/permission.h"

#include "ava/core/result.h"

#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ava::app {

enum class UnifiedCommandSource {
  Builtin,
  PromptProject,
  PromptGlobal,
  Skill,
  McpPrompt,
  PluginCommand,
};

enum class UnifiedCommandKind {
  Backend,
  PromptTemplate,
  SkillPrompt,
  McpPrompt,
  PluginCommand,
};

struct CommandRegistryEntry {
  std::string command = {};
  std::vector<std::string> aliases = {};
  std::string description = {};
  std::string hint = {};
  std::string category = {};
  bool enabled = true;
  std::string disabled_reason = {};
  UnifiedCommandSource source = UnifiedCommandSource::Builtin;
  UnifiedCommandKind kind = UnifiedCommandKind::Backend;
  std::string source_id = {};
  std::filesystem::path source_path = {};
  std::string source_scope = {};
  std::string template_text = {};
  std::string skill_name = {};
  std::string mcp_server_id = {};
  std::string mcp_prompt_name = {};
  std::vector<ava::mcp::McpPromptArgumentDescription> mcp_arguments = {};
  std::string plugin_id = {};
  std::string plugin_command_name = {};
};

struct CommandRegistryDiagnostic {
  std::string command = {};
  std::string source = {};
  std::string source_id = {};
  std::filesystem::path path = {};
  std::string message = {};
  std::string winner_source = {};
  std::string winner_source_id = {};
  std::filesystem::path winner_path = {};
};

struct CommandRegistry {
  std::vector<CommandRegistryEntry> entries;
  std::vector<CommandRegistryDiagnostic> diagnostics;
};

struct CommandRegistryOptions {
  bool include_builtins = true;
  bool include_prompt_commands = true;
  bool include_skills = true;
  bool include_plugin_commands = true;
  bool include_mcp_prompts = false;
  ava::permissions::PermissionResolver permission_resolver = nullptr;
  std::function<bool()> cancel_requested = nullptr;
};

[[nodiscard]] std::string to_string(UnifiedCommandSource source);
[[nodiscard]] std::string to_string(UnifiedCommandKind kind);
[[nodiscard]] CommandRegistry load_command_registry(RuntimeSession& session, CommandRegistryOptions options = {});
[[nodiscard]] CommandRegistryEntry const* find_command_registry_entry(CommandRegistry const& registry,
                                                                      std::string_view line) noexcept;
[[nodiscard]] bool command_registry_contains(RuntimeSession& session, std::string_view line);
[[nodiscard]] ava::core::Result<std::string> expand_prompt_command_template(std::string_view template_text,
                                                                            std::string_view argument_text);
[[nodiscard]] ava::core::Result<std::string> mcp_prompt_arguments_json(CommandRegistryEntry const& entry,
                                                                       std::string_view argument_text);

}  // namespace ava::app
