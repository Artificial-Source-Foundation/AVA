#pragma once

#include "BasePromptMetadata.h"
#include "ContextSourceMetadata.h"
#include "FreshnessSourceMetadata.h"
#include "PromptOverrides.h"
#include "ReasoningSelection.h"
#include "ava/debug/print_members_on.h"
#include "ava/app/project_trust.h"
#include "ava/app/session_run_controller.h"
#include "ava/agent/agent_loop.h"
#include "ava/mcp/config.h"
#include "ava/config/model_config.h"
#include "ava/config/xdg_paths.h"
#include "ava/session/session_store.h"
#include "ava/core/error.h"

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace ava::diagnostics {
class RuntimeDiagnostics;
}

namespace ava::app {
class SubagentDeliveryManager;
}

namespace ava::app::runtime {

// Hold the mutable application state associated with an open runtime session.
//
// The store may be persistent or ephemeral according to sessionless. Shared background jobs remain valid when the aggregate is moved or replaced.
struct Session
{
  ava::session::SessionStore store;
  // Persistent runtime owners hold a cross-process lease for the complete session lifetime.
  ava::session::SessionLease lease;
  ava::agent::Mode mode = ava::agent::Mode::Build;
  ava::config::ModelInfo model;
  BasePromptMetadata base_prompt;
  ava::config::XdgPaths paths;
  // Resolved once at open time and reused by every runtime history reader.
  ava::session::SessionReadLimits session_read_limits = ava::session::legacy_unbounded_session_read_limits();
  std::filesystem::path workspace_dir;
  std::filesystem::path current_dir;
  ProjectTrustState project_trust;
  PromptOverrides prompt_overrides;
  ava::agent::ToolVisibilityOptions tool_visibility;
  std::vector<ContextSourceMetadata> context_sources;
  std::vector<FreshnessSourceMetadata> freshness_sources;
  std::string system_prompt;
  std::optional<ReasoningSelection> reasoning = std::nullopt;
  std::optional<std::vector<std::string>> scoped_model_cycle = std::nullopt;
  bool created = false;
  bool sessionless = false;
  std::shared_ptr<SessionRunController> run_controller = nullptr;
  // Detached retained capsules bind reads to the exact original leased inode
  // without reacquiring the pathname. Ordinary visible sessions leave this
  // empty and derive an authority from their owned lease.
  std::optional<ava::session::SessionReadAuthority> bound_read_authority = std::nullopt;
  // Shared application process state. Direct coordinator injection remains a
  // compatibility seam; production sessions use the delivery manager.
  std::shared_ptr<ava::agent::SubagentCoordinator> subagent_coordinator = nullptr;
  std::shared_ptr<ava::app::SubagentDeliveryManager> subagent_delivery_manager = nullptr;
  std::shared_ptr<ava::diagnostics::RuntimeDiagnostics> diagnostics = nullptr;
  // Null uses normal global/project discovery; non-null is immutable session-local MCP composition.
  std::shared_ptr<ava::mcp::McpConfig const> mcp_config = nullptr;
  bool offline = false;

  // Bind a lifetime-safe history snapshot route to this session's exact lease
  // (or to its shared in-memory state in sessionless mode).
  [[nodiscard]] ava::core::Result<ava::session::SessionReadAuthority> read_authority() const
  {
    if (bound_read_authority)
      return *bound_read_authority;
    return sessionless ? ava::session::SessionReadAuthority::create_ephemeral(store, session_read_limits)
                       : ava::session::SessionReadAuthority::create_persistent(store, lease, session_read_limits);
  }

  // Append through the session owner so writes remain serialized with active runs.
  [[nodiscard]] ava::core::VoidResult append_owned(ava::session::SessionEntry entry)
  {
    if (!run_controller)
      return std::unexpected(ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "runtime session controller is unavailable"));
    return run_controller->append(std::move(entry));
  }

  // Return the stable append route owned by this session, or an empty route when the controller is unavailable.
  [[nodiscard]] ava::agent::SessionAppendSink owner_append_route()
  {
    return run_controller ? run_controller->owner_append_route() : ava::agent::SessionAppendSink{};
  }

  [[nodiscard]] ava::agent::SessionAppendBatchSink owner_append_batch_route()
  {
    return run_controller ? run_controller->owner_append_batch_route() : ava::agent::SessionAppendBatchSink{};
  }

  AVA_DEBUG_PRINT_MEMBERS_ON
};

}  // namespace ava::app::runtime
