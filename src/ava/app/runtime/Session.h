#pragma once

#include "BasePromptMetadata.h"
#include "ContextSourceMetadata.h"
#include "FreshnessSourceMetadata.h"
#include "ReasoningSelection.h"
#include "RuntimeContinuity.h"
#include "ava/debug/print_members_on.h"
#include "ava/app/command_registry.h"
#include "ava/app/project_trust.h"
#include "ava/app/session_run_controller.h"
#include "ava/agent/agent_loop.h"
#include "ava/mcp/config.h"
#include "ava/config/model_config.h"
#include "ava/config/xdg_paths.h"
#include "ava/session/session_store.h"
#include "ava/core/AnchorSet.h"
#include "ava/core/error.h"

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace ava::app::runtime {

// Hold the mutable application state associated with an open runtime session.
//
// The store may be persistent or ephemeral according to sessionless. Shared background jobs remain valid when the aggregate is moved or replaced.
struct Session
{
  ava::session::SessionStore store;
  // Persistent runtime owners hold a cross-process lease for the complete session lifetime.
  ava::session::SessionLease lease;
  // The complete application/session continuity value transferred across
  // in-process replacement. It owns all configured policy and descriptor-safe
  // application services needed by later navigation.
  RuntimeContinuity continuity;
  ava::config::ModelInfo model;
  BasePromptMetadata base_prompt;
  // Resolved once at open time and reused by every runtime history reader.
  ava::session::SessionReadLimits session_read_limits = ava::session::legacy_unbounded_session_read_limits();
  ProjectTrustState project_trust;
  std::vector<ContextSourceMetadata> context_sources;
  std::vector<FreshnessSourceMetadata> freshness_sources;
  std::string system_prompt;
  std::optional<ReasoningSelection> reasoning = std::nullopt;
  std::optional<std::vector<std::string>> scoped_model_cycle = std::nullopt;
  bool created = false;
  bool sessionless = false;
  std::shared_ptr<SessionRunController> run_controller = nullptr;
  // Immutable append target bound into the controller-owned serialized routes.
  // Tests and retained runtime capsules may inspect/copy it, but mutations flow
  // through the controller so append failures remain latched.
  std::shared_ptr<ava::session::SessionAppendTarget> append_target = nullptr;
  // Detached retained capsules bind reads to the exact original leased inode
  // without reacquiring the pathname. Ordinary visible sessions leave this
  // empty and derive an authority from their owned lease.
  std::optional<ava::session::SessionReadAuthority> bound_read_authority = std::nullopt;
  // Null uses normal global/project discovery; non-null is immutable session-local MCP composition.
  std::shared_ptr<ava::mcp::McpConfig const> mcp_config = nullptr;

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
  [[nodiscard]] ava::agent::SessionAppendSink owner_append_route() const
  {
    return run_controller ? run_controller->owner_append_route() : ava::agent::SessionAppendSink{};
  }

 public:
  [[nodiscard]] CommandRegistry load_command_registry(CommandRegistryOptions options = {});
  [[nodiscard]] ava::agent::SessionAppendBatchSink owner_append_batch_route()
  {
    return run_controller ? run_controller->owner_append_batch_route() : ava::agent::SessionAppendBatchSink{};
  }

  AVA_DEBUG_PRINT_MEMBERS_ON
};

}  // namespace ava::app::runtime
