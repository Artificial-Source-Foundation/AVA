#pragma once

#include "ava/debug/print_members_on.h"

#include <filesystem>
#include <memory>
#include <vector>

namespace ava::tools {
class ExactFileAccess;
class CommandExecutor;
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

  // Includes authority roots and capability adapters; never stream this
  // aggregate through generated debug output.
  AVA_DEBUG_PRINT_MEMBERS_OPT_OUT
};

}  // namespace ava::agent
