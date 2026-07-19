#pragma once

#include "ava/command/command.h"
#include "ava/agent/mode.h"
#include "ava/core/result.h"

#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ava::permissions {

enum class PermissionAction
{
  Allow,
  Ask,
  Deny,
};

enum class PermissionRisk
{
  Low,
  Medium,
  High,
  Critical,
};

enum class CommandContainmentStatus
{
  NotRequired,
  Unavailable,
  // A containment plan has been prepared and is available, but has not yet
  // been applied in the child. This is the pre-execution label.
  Available,
  // The containment plan was successfully applied in the child before exec.
  // This is the post-execution label included in tool/process audit.
  Active,
  UnverifiedDelegatedExecutor,
};

// Copyable, dependency-free summary of containment availability for the
// permission decision. The tool layer prepares a full containment plan and
// extracts these fields so permissions can depend on containment without a
// header dependency on the containment subsystem.
struct CommandContainmentInfo
{
  bool available = false;
  std::string profile_id;
  bool network_allowed = false;

  AVA_DEBUG_PRINT_MEMBERS_ON
};

// Command planning is owned by ava_command. This value object deliberately
// copies only the approval/audit contract, so permissions can depend on the
// planner without creating a command-to-permissions cycle.
struct CommandPermissionMetadata
{
  ava::command::CommandLevel level = ava::command::CommandLevel::Critical;
  ava::command::CommandFamily family = ava::command::CommandFamily::UnknownWrapper;
  std::string fingerprint;
  ava::command::CommandExecutionDomain execution_domain = ava::command::CommandExecutionDomain::DirectArgv;
  std::filesystem::path resolved_executable;
  ava::command::ExecutableOrigin executable_origin = ava::command::ExecutableOrigin::System;
  std::filesystem::path cwd;
  bool executes_mutable_project_code = false;
  bool containment_available = false;
  CommandContainmentStatus containment_status = CommandContainmentStatus::Unavailable;
  std::string containment_profile_id;
  bool containment_network_allowed = false;
  ava::command::InteractiveScope backend_maximum_scope = ava::command::InteractiveScope::Once;
  // Stable recipe identities are minted only from a sealed direct-argv plan.
  // The global key deliberately omits workspace and synthetic environment
  // roots; the workspace key binds the canonical workspace instead.
  std::string recipe_payload_version;
  std::string global_recipe_key;
  std::string workspace_recipe_key;
  std::string recipe_display;
  std::vector<ava::command::InteractiveScope> effective_allowed_scopes;
  std::string environment_profile_id;
  std::string environment_digest;
  bool executor_identity_verified = true;

  AVA_DEBUG_PRINT_MEMBERS_ON
};

enum class Operation
{
  ReadFile,
  SearchFiles,
  EditFile,
  RunCommand,
  NetworkFetch,
  NetworkSearch,
  LspServerLaunch,
  LspQuery,
  SkillLoad,
  TaskRun,
  PluginExecute,
  PluginToolCall,
  PluginCommandRun,
  PluginEventObserve,
  McpServerLaunch,
  McpServerConnect,
  McpToolCall,
  McpResourceRead,
};

struct PermissionRequest
{
  Operation operation;
  ava::agent::Mode mode;
  std::filesystem::path workspace_dir;
  std::filesystem::path target_path;
  std::string command;
  std::optional<CommandPermissionMetadata> command_metadata = std::nullopt;

  AVA_DEBUG_PRINT_MEMBERS_ON
};

struct PermissionDecision
{
  PermissionAction action;
  std::string reason;
  PermissionRisk risk = PermissionRisk::Low;

  AVA_DEBUG_PRINT_MEMBERS_ON
};

enum class PermissionResolution
{
  Allow,
  Deny,
  AllowSessionGrant,
  Cancel,
};

struct PermissionResolutionDecision
{
  PermissionResolution resolution = PermissionResolution::Deny;
  std::string reason;
  std::string resolution_source;
  std::string rule_id;
  bool authoritative = false;

  PermissionResolutionDecision() = default;
  PermissionResolutionDecision(PermissionResolution resolution_in);
  PermissionResolutionDecision(PermissionResolution resolution_in, std::string reason_in);

  AVA_DEBUG_PRINT_MEMBERS_ON
};

struct PermissionPrompt
{
  std::string permission_request_id = {};
  std::string tool_call_id = {};
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
  std::optional<CommandPermissionMetadata> command_metadata = std::nullopt;

  AVA_DEBUG_PRINT_MEMBERS_ON
};

using PermissionResolver = std::function<ava::core::Result<PermissionResolutionDecision>(PermissionPrompt const&)>;

[[nodiscard]] CommandPermissionMetadata command_permission_metadata(ava::command::CommandPlan const& plan, bool unverified_delegated_executor = false);
[[nodiscard]] CommandPermissionMetadata command_permission_metadata(ava::command::CommandPlan const& plan, CommandContainmentInfo const& containment,
                                                                    bool unverified_delegated_executor = false);
[[nodiscard]] PermissionDecision decide(PermissionRequest const& request);
[[nodiscard]] PermissionDecision decide(CommandPermissionMetadata const& metadata);
[[nodiscard]] bool command_permission_allows_reusable_grant(CommandPermissionMetadata const& metadata) noexcept;
[[nodiscard]] bool command_prompt_allows_persistent_allow(PermissionPrompt const& prompt) noexcept;
[[nodiscard]] PermissionDecision classify_command(std::string_view command);
[[nodiscard]] bool is_repository_controlled_build_or_test_command(std::string_view command);
[[nodiscard]] bool operator==(PermissionResolutionDecision const& decision, PermissionResolution resolution);
[[nodiscard]] bool operator==(PermissionResolution resolution, PermissionResolutionDecision const& decision);
[[nodiscard]] std::optional<PermissionAction> parse_permission_action(std::string_view value);
[[nodiscard]] std::optional<Operation> parse_operation(std::string_view value);
[[nodiscard]] std::string to_string(PermissionAction action);
[[nodiscard]] std::string to_string(PermissionResolution resolution);
[[nodiscard]] std::string to_string(PermissionResolutionDecision const& decision);
[[nodiscard]] std::string to_string(PermissionRisk risk);
[[nodiscard]] std::string to_string(CommandContainmentStatus status);
[[nodiscard]] std::string to_string(Operation operation);

}  // namespace ava::permissions
