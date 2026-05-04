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

enum class PermissionRisk {
  Low,
  Medium,
  High,
  Critical,
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
  PermissionRisk risk = PermissionRisk::Low;
};

enum class PermissionResolution {
  Allow,
  Deny,
  AllowSessionGrant,
};

struct PermissionPrompt {
  std::string permission_request_id = {};
  Operation operation;
  ava::agent::Mode mode;
  std::filesystem::path workspace_dir;
  std::filesystem::path target_path;
  std::string command;
  std::string tool_name;
  std::string reason;
  PermissionRisk risk = PermissionRisk::Low;
  std::string diff_preview = {};
  bool diff_truncated = false;
};

using PermissionResolver = std::function<ava::core::Result<PermissionResolution>(PermissionPrompt const&)>;

[[nodiscard]] PermissionDecision decide(PermissionRequest const& request);
[[nodiscard]] PermissionDecision classify_command(std::string_view command);
[[nodiscard]] std::string to_string(PermissionAction action);
[[nodiscard]] std::string to_string(PermissionResolution resolution);
[[nodiscard]] std::string to_string(PermissionRisk risk);
[[nodiscard]] std::string to_string(Operation operation);

}  // namespace ava::permissions
