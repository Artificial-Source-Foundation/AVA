#pragma once

#include "BasePromptMetadata.h"
#include "ContextSourceMetadata.h"
#include "FreshnessSourceMetadata.h"
#include "OpenContext.h"
#include "PromptOverrides.h"
#include "ReasoningSelection.h"
#include "SessionLifecycleRequest.h"
#include "ava/debug/print_members_on.h"
#include "ava/app/command_registry.h"
#include "ava/app/project_trust.h"
#include "ava/app/runtime/session_ts.h"
#include "ava/app/session_run_controller.h"
#include "ava/agent/agent_loop.h"
#include "ava/mcp/config.h"
#include "ava/config/model_config.h"
#include "ava/config/xdg_paths.h"
#include "ava/session/session_store.h"
#include "ava/permissions/permission_rules.h"
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
} // namespace ava::diagnostics

namespace ava::provider {
class ProviderCatalog;
} // namespace ava::provider

namespace ava::app {
class SessionTitleCoordinator;
class SubagentDeliveryManager;
} // namespace ava::app

namespace ava::app::runtime {

// Invocation inputs.
//
// Mirrors of OpenContext describing how this session was opened. None of
// these are restored from the store on resume; each is sourced from the
// OpenContext of the current process invocation.
//
struct InvocationInputs
{
  std::filesystem::path workspace_dir;
  std::filesystem::path current_dir;
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

// Resolved prompt state.
//
// The agent mode and assembled prompt material currently in effect for the
// session: base-prompt metadata, contributing context and freshness sources,
// the ordinary system prompt, and its ambient-extension-free runtime variant.
// This whole bundle is recomputed
// whenever the mode or model changes (see apply_prompt_state and
// switch_model) and is otherwise the session's live resolved state.
//
// It is field-compatible with the transient PromptState returned by
// load_runtime_prompt_state, which apply_prompt_state moves into this
// member as a single object rather than copying fields one at a time.
struct ResolvedPromptState
{
  ava::agent::Mode mode = ava::agent::Mode::Build;
  BasePromptMetadata base_prompt;
  std::vector<ContextSourceMetadata> context_sources;
  std::vector<FreshnessSourceMetadata> freshness_sources;
  std::string system_prompt;
  std::string ambient_extension_free_system_prompt;

  AVA_DEBUG_PRINT_MEMBERS_ON
};

// Model selection.
//
// The active model and its derived/user-selected companions: the resolved
// ModelInfo, the latest reasoning selection, and the optional scoped model
// rotation. `model` and `reasoning` are re-derived on resume (model from the
// persisted entries, reasoning from the last assistant turn); all three are
// runtime-mutable: model/reasoning change on /models, scoped_model_cycle on
// /models scope commands.
struct ModelSelection
{
  ava::config::ModelInfo model;
  std::optional<ReasoningSelection> reasoning = std::nullopt;
  std::optional<std::vector<std::string>> scoped_model_cycle = std::nullopt;

  AVA_DEBUG_PRINT_MEMBERS_ON
};

// Trust state.
//
// The project trust decision loaded for this session. Mutated when the user
// adjusts trust (/trust commands); otherwise stable for the session lifetime.
struct TrustState
{
  ProjectTrustState project_trust;

  AVA_DEBUG_PRINT_MEMBERS_ON
};

// Session resources.
//
// Process-held live infrastructure bound to this process: the cross-process
// lease, pre-opened anchor descriptors, the run controller and append target,
// the bound read authority for detached capsules, and the shared
// application-scoped services (subagent coordinator/delivery, title
// coordinator, diagnostics, MCP config). These cannot round-trip through
// disk; the three app-scoped services are rebound by Session::replace_with
// and mcp_config is set once at ACP session setup.
struct SessionResources
{
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
  // Exact application-scoped provider catalog pinned for this session's lifetime.
  std::shared_ptr<ava::provider::ProviderCatalog const> provider_catalog = nullptr;

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
  ResolvedPromptState resolved_prompt_state_;   // Do NOT use this variable name outside of this file!
  ModelSelection model_selection_;              // Do NOT use this variable name outside of this file!
  TrustState trust_state_;                      // Do NOT use this variable name outside of this file!
  SessionResources resources_;                  // Do NOT use this variable name outside of this file!

  // ===========================================================================
  // Persistent session identity.
  // `store` is the session's on-disk identity (a durable content handle);
  // `created` records whether this Session was freshly created vs resumed.
  // Resolved model/reasoning live in `model_selection_` above and are
  // projected back out of the persisted entries on resume.
  // ===========================================================================
  ava::session::SessionStore store;
  bool created = false;                         // True when this is a freshly created session rather than a resumed one.

  AVA_DEBUG_PRINT_MEMBERS_ON
};

// Hold the mutable application state associated with an open runtime session.
//
// The store may be persistent or ephemeral according to sessionless. Shared background jobs remain valid when the aggregate is moved or replaced.
class Session : protected Session_aggregate_base
{
 public:
  using Session_aggregate_base::created;
  using Session_aggregate_base::store;

  // Move constructors.
  Session(Session&& session) = default;

 private:
  Session(Session_aggregate_base&& base) : Session_aggregate_base(std::move(base)) { }

  // Called from replace_with.
  Session& operator=(Session&& session) = default;

  static ava::core::Result<session_ts> construct(OpenContext const& context, SessionLifecycleRequest const& request, ava::session::SessionStore& store,
                                                 ava::session::SessionLease& lease, bool created, bool load_existing_entries, bool append_session_start,
                                                 bool append_initial_session_name, std::shared_ptr<ava::app::SubagentDeliveryManager> delivery_manager,
                                                 std::shared_ptr<ava::app::SessionTitleCoordinator> title_coordinator);

 public:
  // Open a runtime session using stable `context` and the one-shot lifecycle
  // `request`.
  //
  // An empty request creates a persistent session. Returns failure when selectors
  // conflict or the selected session cannot be created, recovered, or opened.
  static ava::core::Result<session_ts> open(OpenContext const& context, SessionLifecycleRequest const& request = {});

  // Consume an already-owned persistent store and its lease without reopening by path.
  // Inputs remain intact on failure so callers can roll back by stable identity.
  static ava::core::Result<session_ts> open_owned(OpenContext const& context, ava::session::SessionStore& store, ava::session::SessionLease& lease,
                                                  bool created);

  // Open a session using `request` at `workspace_root` and `current_dir`, overriding those locations in `context`.
  static ava::core::Result<session_ts> open_at(OpenContext context, std::filesystem::path const& workspace_root, std::filesystem::path const& current_dir,
                                               SessionLifecycleRequest request);

  // Create a persistent session at `workspace_root` and `current_dir`, overriding
  // those locations in `context`.
  static ava::core::Result<session_ts> create_at(OpenContext context, std::filesystem::path const& workspace_root, std::filesystem::path const& current_dir);

  // Create a persistent session inheriting active state from `current` and
  // frontend-only policy from `base_context`.
  ava::core::Result<session_ts> create_similar(OpenContext const& base_context) const;

  // Open a session using `request`, inheriting active state from `current` and
  // frontend-only policy from `base_context`.
  ava::core::Result<session_ts> open_similar(OpenContext const& base_context, SessionLifecycleRequest request) const;

  Session create_detached(ava::session::SessionLease lease, ava::session::SessionReadAuthority authority,
                          std::shared_ptr<ava::app::SubagentDeliveryManager> manager) const;

  ava::core::Result<session_ts> open_requested(OpenContext const& base_context, std::string_view requested_session_id) const
  {
    SessionLifecycleRequest request;
    request.requested_session_id = std::string(requested_session_id);
    return open_similar(base_context, std::move(request));
  }

  // Return the AVA-owned filesystem roots that command planning and model
  // ToolContexts are pre-authorized to access.
  //
  // This is the single bounded source for the `ava_authority_roots` field: AVA's
  // config, state, and sessions directories, the active auth file and its legacy
  // credential fallbacks, plus this session's store parent as a fallback for
  // custom/test path layouts. The list is short, stable, and deduplicated; keep
  // it in sync across direct command planning and ToolContext construction.
  std::vector<std::filesystem::path> ava_authority_roots_1() const;

  // Accessors.

  // Base class accessors.
  //
  // The non-const overload is used to replace the whole bundle on mode/model changes;
  // the const overload lets callers copy it out of a const Session without naming the member directly.

  InvocationInputs& invocation_inputs() { return invocation_inputs_; }
  InvocationInputs const& invocation_inputs() const { return invocation_inputs_; }

  ResolvedPromptState& resolve_prompt_state() { return resolved_prompt_state_; }
  ResolvedPromptState const& resolve_prompt_state() const { return resolved_prompt_state_; }

  ModelSelection& model_selection() { return model_selection_; }
  ModelSelection const& model_selection() const { return model_selection_; }

  TrustState& trust_state() { return trust_state_; }
  TrustState const& trust_state() const { return trust_state_; }

  SessionResources& resources() { return resources_; }
  SessionResources const& resources() const { return resources_; }

  // Invocation inputs.
  std::filesystem::path const& workspace_dir() const { return invocation_inputs_.workspace_dir; }
  std::filesystem::path const& current_dir() const noexcept { return invocation_inputs_.current_dir; }
  ava::agent::ToolVisibilityOptions const& tool_visibility() const { return invocation_inputs_.tool_visibility; }
  ava::config::XdgPaths const& paths() const { return invocation_inputs_.paths; }
  bool sessionless() const { return invocation_inputs_.sessionless; }

  // Resolved prompt state.
  ava::agent::Mode mode() const { return resolved_prompt_state_.mode; }
  BasePromptMetadata const& base_prompt() const { return resolved_prompt_state_.base_prompt; }
  std::vector<ContextSourceMetadata> const& context_sources() const { return resolved_prompt_state_.context_sources; }
  std::vector<FreshnessSourceMetadata> const& freshness_sources() const { return resolved_prompt_state_.freshness_sources; }
  std::string const& system_prompt() const { return resolved_prompt_state_.system_prompt; }
  std::string const& ambient_extension_free_system_prompt() const { return resolved_prompt_state_.ambient_extension_free_system_prompt; }
  bool is_offline() const { return invocation_inputs_.is_offline_; }
  std::vector<std::filesystem::path> const& additional_writable_dirs() const { return invocation_inputs_.additional_writable_dirs; }
  // Resolved once at open time and reused by every runtime history reader.
  ava::session::SessionReadLimits const& session_read_limits() const { return invocation_inputs_.session_read_limits; }
  PromptOverrides const& prompt_overrides() const { return invocation_inputs_.prompt_overrides; }

  // Model selection.
  ava::config::ModelInfo const& model() const { return model_selection_.model; }
  std::optional<ReasoningSelection> const& reasoning() const { return model_selection_.reasoning; }
  std::optional<std::vector<std::string>> const& scoped_model_cycle() const { return model_selection_.scoped_model_cycle; }

  // Trust state.
  ProjectTrustState const& project_trust() const { return trust_state_.project_trust; }

  // Session resources.
  ava::session::SessionLease const& lease() const { return resources_.lease; }
  std::shared_ptr<ava::core::AnchorSet> const& anchor_set() const { return resources_.anchor_set; }
  std::shared_ptr<SessionRunController> const& run_controller() const { return resources_.run_controller; }
  std::shared_ptr<ava::session::SessionAppendTarget> const& append_target() const { return resources_.append_target; }
  std::optional<ava::session::SessionReadAuthority> const& bound_read_authority() const { return resources_.bound_read_authority; }
  std::shared_ptr<ava::agent::SubagentCoordinator> const& subagent_coordinator() const { return resources_.subagent_coordinator; }
  std::shared_ptr<ava::app::SubagentDeliveryManager> const& subagent_delivery_manager() const { return resources_.subagent_delivery_manager; }
  std::shared_ptr<ava::app::SessionTitleCoordinator> const& session_title_coordinator() const { return resources_.session_title_coordinator; }
  std::shared_ptr<ava::diagnostics::RuntimeDiagnostics> const& diagnostics() const { return resources_.diagnostics; }
  std::shared_ptr<ava::mcp::McpConfig const> const& mcp_config() const { return resources_.mcp_config; }
  std::shared_ptr<ava::provider::ProviderCatalog const> const& provider_catalog() const { return resources_.provider_catalog; }

  // Bind a lifetime-safe history snapshot route to this session's exact lease
  // (or to its shared in-memory state in sessionless mode).
  [[nodiscard]] ava::core::Result<ava::session::SessionReadAuthority> read_authority_1() const
  {
    if (resources_.bound_read_authority)
      return *resources_.bound_read_authority;
    return invocation_inputs_.sessionless
               ? ava::session::SessionReadAuthority::create_ephemeral(store, invocation_inputs_.session_read_limits)
               : ava::session::SessionReadAuthority::create_persistent(store, resources_.lease, invocation_inputs_.session_read_limits);
  }

  // Append through the session owner so writes remain serialized with active runs.
  [[nodiscard]] ava::core::VoidResult append_owned(ava::session::SessionEntry entry)
  {
    if (!resources_.run_controller)
      return std::unexpected(ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "runtime session controller is unavailable"));
    return resources_.run_controller->append(std::move(entry));
  }

  // Return the stable append route owned by this session, or an empty route when the controller is unavailable.
  [[nodiscard]] ava::agent::SessionAppendSink owner_append_route_1() const
  {
    return resources_.run_controller ? resources_.run_controller->owner_append_route() : ava::agent::SessionAppendSink{};
  }

  [[nodiscard]] CommandRegistry load_command_registry(CommandRegistryOptions options = {});
  [[nodiscard]] ava::agent::SessionAppendBatchSink owner_append_batch_route_1()
  {
    return resources_.run_controller ? resources_.run_controller->owner_append_batch_route() : ava::agent::SessionAppendBatchSink{};
  }

  // Single app-owned source for the persistent permission-rule store bound to a runtime session.
  // Every command permission path (direct app commands, model tool calls, ACP hosts, RPC, print/line-shell)
  // resolves the same global and workspace rule files through this helper instead of duplicating path logic.
  [[nodiscard]] ava::permissions::PermissionRuleStore permission_rule_store() const
  {
    return {.global_rules_file = paths().ava_config_dir / "permission-rules.json",
            .workspace_rules_file = workspace_dir() / ".ava" / "permission-rules.json",
            .workspace_dir = workspace_dir(),
            .anchor_set = anchor_set()};
  }

  // Publish callback-free mutable runtime configuration into an existing retained parent capsule
  // without changing its safe policy snapshot.
  //
  // Returns failure when the parent delivery manager rejects the configuration update;
  // returns success when no delivery manager is attached or the update was applied.
  [[nodiscard]] ava::core::VoidResult refresh_parent_configuration() const;

  // Stop background work and replace this session's contents with `replacement`,
  // rebinding the application-scoped subagent coordinator/delivery, title
  // coordinator, and diagnostics services that cannot round-trip through disk.
  //
  // This is the visible-session detach boundary: when `replacement` targets a
  // different session the previously visible parent is released so another AVA
  // process may activate it. Always succeeds.
  [[nodiscard]] ava::core::VoidResult replace_with(Session&& replacement);

  // Recover torn-tail and incomplete-assistant-output suffix entries from the
  // session identified by `source_session_id` so a follow-up mutation
  // (fork/clone/branch summary) reads a clean source.
  //
  // When `source_session_id` names this session, recovery runs against the
  // current store and lease. Otherwise a separate SessionStore is opened and the
  // acquired lease is emplaced into `temporary_source_lease`, which the caller
  // must keep alive for the duration of the mutation. Returns failure when the
  // source store cannot be opened, the lease cannot be acquired, or a recovery
  // stage rejects. Leaves `temporary_source_lease` untouched when recovering this
  // session.
  [[nodiscard]] ava::core::VoidResult recover_source_for_mutation(std::string const& source_session_id,
                                                                  std::optional<ava::session::SessionLease>& temporary_source_lease);

  // Build replacement OpenContext from this session and `base_context`.
  //
  // Runtime context, filesystem policy, resolved read limits, and shared
  // application services are inherited from this session. Frontend policy in
  // `base_context`, including model pinning and exact-ID behavior, is retained.
  // An ephemeral session's AnchorSet is not inherited because its temporary
  // spill root cannot authorize a new persistent or ephemeral store.
  [[nodiscard]] OpenContext replacement_open_context(OpenContext const& base_context) const;

  // Append session metadata through the runtime owner's serialized route.
  [[nodiscard]] ava::core::Result<ava::session::SessionMetadataView> append_metadata_1(ava::session::SessionMetadataUpdate update);

  // Append a mode change entry through the runtime owner's serialized route.
  //
  // Records an EntryType::ModeChange entry carrying the given `mode` so the
  // transition survives session resume. Returns failure when the append route
  // rejects the entry (for example, a latched controller).
  [[nodiscard]] ava::core::VoidResult append_mode_change_1(ava::agent::Mode mode);

  // Apply a CLI-supplied initial reasoning level to a freshly opened session.
  //
  // Accepts the raw --thinking level token from SessionLifecycleRequest; an empty or
  // whitespace-only value is rejected. "off" clears the active selection;
  // any other token must resolve to a level the active model supports.
  // Returns failure when the level is empty, unsupported, or the underlying
  // reasoning update rejects the resolved selection.
  [[nodiscard]] ava::core::VoidResult apply_initial_reasoning_level(std::string_view requested_level);

  // Move a freshly assembled PromptState into the session's resolved state and
  // refresh bound parent capsules.
  //
  // `prompt_state` is consumed regardless of failure. Returns failure only when
  // refresh_parent_configuration rejects the configuration update.
  [[nodiscard]] ava::core::VoidResult apply_prompt_state(PromptState prompt_state);

  // Switch the active model to `model`, re-deriving the prompt state for the
  // current mode and clearing any active reasoning selection.
  //
  // Returns false (without writing) when `model` already matches the active
  // provider/model pair. Returns failure when prompt-state loading, the model
  // change append, or the parent configuration refresh rejects the switch.
  [[nodiscard]] ava::core::Result<bool> switch_model(ava::config::ModelInfo model);

  // Set the active reasoning selection, resolving it against the current model.
  //
  // A nullopt `selection` clears reasoning. Non-nullopt selections are trimmed
  // and resolved through the model's supported levels. Returns false (without
  // writing) when the resolved selection equals the active one. Returns failure
  // when the model rejects the level or the append/refresh route rejects the change.
  [[nodiscard]] ava::core::Result<bool> set_reasoning(std::optional<ReasoningSelection> selection);

  [[nodiscard]] ava::core::Result<ava::session::SessionMetadataView> load_metadata() const;

  [[nodiscard]] std::string state_result_json_1(bool cancel_requested) const;
  [[nodiscard]] ava::core::Result<std::string> messages_result_json() const;
  [[nodiscard]] ava::core::Result<std::string> list_sessions_result_json_1() const;
  [[nodiscard]] ava::core::Result<std::string> tree_result_json() const;
  [[nodiscard]] ava::core::Result<std::string> list_models_result_json_1() const;
  [[nodiscard]] ava::core::Result<std::string> stats_result_json() const;
  [[nodiscard]] ava::core::Result<std::string> validation_result_json() const;

  AVA_DEBUG_PRINT_MEMBERS_ON_BASE(Session_aggregate_base)
};

}  // namespace ava::app::runtime
