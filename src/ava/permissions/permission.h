#pragma once

#include <filesystem>
#include <string>
#include <string_view>

#include "ava/agent/mode.h"

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

[[nodiscard]] PermissionDecision decide(const PermissionRequest& request);
[[nodiscard]] PermissionDecision classify_command(std::string_view command);
[[nodiscard]] std::string to_string(PermissionAction action);
[[nodiscard]] std::string to_string(Operation operation);

}  // namespace ava::permissions
