#pragma once

#include <filesystem>
#include <functional>
#include <string>
#include <string_view>

#include "ava/agent/mode.h"
#include "ava/core/result.h"

namespace ava::permissions {

enum class PermissionAction {
  Allow,
  Ask,
  Deny,
};

enum class Operation {
  ReadFile,
  SearchFiles,
  EditFile,
  RunCommand,
  NetworkFetch,
  LspQuery,
  PluginExecute,
  PluginToolCall,
  PluginCommandRun,
  PluginEventObserve,
  McpServerLaunch,
  McpServerConnect,
  McpToolCall,
};

struct PermissionRequest {
  Operation operation;
  ava::agent::Mode mode;
  std::filesystem::path workspace_dir;
  std::filesystem::path target_path;
  std::string command;
};

struct PermissionDecision {
  PermissionAction action;
  std::string reason;
};

enum class PermissionResolution {
  Allow,
  Deny,
};

struct PermissionPrompt {
  Operation operation;
  ava::agent::Mode mode;
  std::filesystem::path workspace_dir;
  std::filesystem::path target_path;
  std::string command;
  std::string tool_name;
  std::string reason;
};

using PermissionResolver = std::function<ava::core::Result<PermissionResolution>(const PermissionPrompt&)>;

[[nodiscard]] PermissionDecision decide(const PermissionRequest& request);
[[nodiscard]] PermissionDecision classify_command(std::string_view command);
[[nodiscard]] std::string to_string(PermissionAction action);
[[nodiscard]] std::string to_string(PermissionResolution resolution);
[[nodiscard]] std::string to_string(Operation operation);

}  // namespace ava::permissions
