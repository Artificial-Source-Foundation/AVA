#pragma once

#include "BasePromptMetadata.h"
#include "ContextSourceMetadata.h"
#include "FreshnessSourceMetadata.h"
#include "PromptOverrides.h"
#include "ReasoningSelection.h"
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

namespace ava::diagnostics {
class RuntimeDiagnostics;
}

namespace ava::app {
class SessionTitleCoordinator;
class SubagentDeliveryManager;
}  // namespace ava::app

namespace ava::app::runtime {

// Invocation inputs.
//
// Mirrors of OpenOptions describing how this session was opened. None of
// these are restored from the store on resume; each is sourced from the
// OpenOptions of the current process invocation.
//
struct InvocationInputs
{
  std::filesystem::path workspace_dir;
  std::filesystem::path current_dir;
  ava::agent::Mode mode = ava::agent::Mode::Build;
  ava::agent::ToolVisibilityOptions tool_visibility = {};
  ava::config::XdgPaths paths;
  bool sessionless;
  bool is_offline_ = false;
  // Additional writable directories beyond workspace_dir (e.g., user-configured paths).
  // These are opened as anchor descriptors at startup and made available to tools via ToolContext::anchor_set.
  std::vector<std::filesystem::path> additional_writable_dirs = {};
  // Resolved once at open time and reused by every runtime history reader.
  ava::session::SessionReadLimits session_read_limits = ava::session::legacy_unbounded_session_read_limits();
  PromptOverrides prompt_overrides = {};

  AVA_DEBUG_PRINT_MEMBERS_ON
};

// Old aggregate for Session.
// This class holds members that are still initialized by a designated initializer list.
//
// Members are grouped into cohorts by where their values come from and how long
// they live. The grouping is documentation-only today, but it marks the seams
// along which this aggregate is expected to be decomposed into smaller types.
struct Session_aggregate_base
{
  InvocationInputs invocation_inputs_;          // Do NOT use this variable name outside of this file!

  // Accessor.
  InvocationInputs& invocation_inputs() { return invocation_inputs_; }

  // ===========================================================================
  // Persistent session identity.
  // The durable content handle plus attributes projected back out of it on
  // resume. `store` is the session's on-disk identity; `model` and `reasoning`
  // are re-derived from the persisted entries rather than stored as fields.
  // ===========================================================================
  ava::session::SessionStore store;
  ava::config::ModelInfo model;
  std::optional<ReasoningSelection> reasoning = std::nullopt;

  // ===========================================================================
  // Re-derived state.
  // Recomputed from config and workspace on every open, including resume.
  // None of these round-trip through the store as fields.
  // ===========================================================================
  BasePromptMetadata base_prompt;
  std::string system_prompt;
  std::vector<ContextSourceMetadata> context_sources;
  std::vector<FreshnessSourceMetadata> freshness_sources;
  ProjectTrustState project_trust;
  std::optional<std::vector<std::string>> scoped_model_cycle = std::nullopt;
  bool created = false;                        // True when this is a freshly created session rather than a resumed one.

  // ===========================================================================
  // Process-held live resources.
  // Opened or constructed at startup and owning descriptors, threads, or
  // mutexes. These cannot round-trip through disk and are not invocation
  // inputs; they are infrastructure bound to this process.
  // ===========================================================================
  // Persistent runtime owners hold a cross-process lease for the complete session lifetime.
  ava::session::SessionLease lease;
  // Pre-opened anchor descriptors for all writable directories. Opened once
  // at session creation and shared across all prompts and subagent loops.
  std::shared_ptr<ava::core::AnchorSet> anchor_set = nullptr;
  std::shared_ptr<SessionRunController> run_controller = nullptr;
  // Immutable append target bound into the controller-owned serialized routes.
  // Tests and retained runtime capsules may inspect/copy it, but mutations flow
  // through the controller so append failures remain latched.
  std::shared_ptr<ava::session::SessionAppendTarget> append_target = nullptr;
  // Detached retained capsules bind reads to the exact original leased inode
  // without reacquiring the pathname. Ordinary visible sessions leave this
  // empty and derive an authority from their owned lease.
  std::optional<ava::session::SessionReadAuthority> bound_read_authority = std::nullopt;
  // Shared application process state. Direct coordinator injection remains a
  // compatibility seam; production sessions use the delivery manager.
  std::shared_ptr<ava::agent::SubagentCoordinator> subagent_coordinator = nullptr;
  std::shared_ptr<ava::app::SubagentDeliveryManager> subagent_delivery_manager = nullptr;
  std::shared_ptr<ava::app::SessionTitleCoordinator> session_title_coordinator = nullptr;
  std::shared_ptr<ava::diagnostics::RuntimeDiagnostics> diagnostics = nullptr;
  // Null uses normal global/project discovery; non-null is immutable session-local MCP composition.
  std::shared_ptr<ava::mcp::McpConfig const> mcp_config = nullptr;

  AVA_DEBUG_PRINT_MEMBERS_ON
};

// Hold the mutable application state associated with an open runtime session.
//
// The store may be persistent or ephemeral according to sessionless. Shared background jobs remain valid when the aggregate is moved or replaced.
class Session : public Session_aggregate_base
{
 public:
  // Move constructors.
  Session(Session&& session) = default;
  Session(Session_aggregate_base&& base) : Session_aggregate_base(std::move(base)) { }

  // Called from replace_runtime_session.
  Session& operator=(Session&& session) = default;

  // Accessors.

  // Invocation inputs.
  std::filesystem::path const& workspace_dir() const { return invocation_inputs_.workspace_dir; }
  std::filesystem::path const& current_dir() const noexcept { return invocation_inputs_.current_dir; }
  ava::agent::Mode mode() const { return invocation_inputs_.mode; }
  ava::agent::ToolVisibilityOptions const& tool_visibility() const { return invocation_inputs_.tool_visibility; }
  ava::config::XdgPaths const& paths() const { return invocation_inputs_.paths; }
  bool sessionless() const { return invocation_inputs_.sessionless; }
  bool is_offline() const { return invocation_inputs_.is_offline_; }
  std::vector<std::filesystem::path> const& additional_writable_dirs() const { return invocation_inputs_.additional_writable_dirs; }
  // Resolved once at open time and reused by every runtime history reader.
  ava::session::SessionReadLimits const& session_read_limits() const { return invocation_inputs_.session_read_limits; }
  PromptOverrides const& prompt_overrides() const { return invocation_inputs_.prompt_overrides; }

  // Bind a lifetime-safe history snapshot route to this session's exact lease
  // (or to its shared in-memory state in sessionless mode).
  [[nodiscard]] ava::core::Result<ava::session::SessionReadAuthority> read_authority() const
  {
    if (bound_read_authority)
      return *bound_read_authority;
    return invocation_inputs_.sessionless ? ava::session::SessionReadAuthority::create_ephemeral(store, invocation_inputs_.session_read_limits)
                                          : ava::session::SessionReadAuthority::create_persistent(store, lease, invocation_inputs_.session_read_limits);
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

  AVA_DEBUG_PRINT_MEMBERS_ON_BASE(Session_aggregate_base)
};

}  // namespace ava::app::runtime
