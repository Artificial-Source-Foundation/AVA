#pragma once

#include "ava/debug/print_members_on.h"
#include "ava/process/scope.h"
#include "ava/permissions/command_autonomy.h"

#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace ava::tools {
class ExactFileAccess;
class CommandExecutor;
class EditHistory;
}  // namespace ava::tools

namespace ava::agent {

// Agent-owned tool execution authority and capability inputs. Runtime and
// loop construction assemble this once; ToolContext mapping and child
// inheritance consume the nested fields without re-deriving policy.
struct ToolExecutionOptions
{
  bool require_descriptor_secure_workspace = false;
  bool announce_execution_after_permission = false;
  bool redact_permission_audit_arguments = false;
  bool require_explicit_file_permissions = false;
  // Runtime-owned AVA config/state/session directories that command sealing
  // must keep disjoint from the model command workspace.
  std::vector<std::filesystem::path> ava_authority_roots = {};
  std::shared_ptr<ava::tools::ExactFileAccess const> exact_file_access = nullptr;
  std::shared_ptr<ava::tools::CommandExecutor const> command_executor = nullptr;
  // The admitted run cancellation route copied into each model ToolContext.
  // Callers that leave it empty retain AgentLoopOptions compatibility.
  std::function<bool()> cancel_requested = nullptr;
  // Run authority copied into each model ToolContext. Child loops replace it
  // with a fresh application/session/run hierarchy before publication.
  std::optional<ava::process::ProcessScopeV1> process_scope = std::nullopt;
  std::shared_ptr<ava::tools::EditHistory> edit_history = nullptr;
  std::string edit_turn_id;
  std::shared_ptr<ava::permissions::CommandAutonomyState> command_autonomy = nullptr;
  ava::permissions::CommandPolicyReader command_policy_reader = nullptr;

  // Includes authority roots and capability adapters; never stream this
  // aggregate through generated debug output.
  AVA_DEBUG_PRINT_MEMBERS_OPT_OUT
};

}  // namespace ava::agent
