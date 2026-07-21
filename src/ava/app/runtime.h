#pragma once

#include "runtime/OpenOptions.h"
#include "runtime/PromptState.h"
#include "runtime/RunOptions.h"
#include "ava/app/session_run_controller.h"
#include "ava/app/runtime/ReasoningSelection.h"
#include "ava/agent/agent_loop.h"
#include "ava/config/model_config.h"
#include "ava/config/xdg_paths.h"
#include "ava/session/compaction.h"
#include "ava/session/session_metadata.h"
#include "ava/session/session_store.h"
#include "ava/provider/provider.h"
#include "ava/core/result.h"

#include <cstddef>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ava::app {

namespace runtime {
struct Session;
} // namespace runtime

using CompactionSummaryGenerator =
    std::function<ava::core::Result<std::string>(std::vector<ava::session::SessionEntry> const& entries, ava::session::CompactionConfig const& config,
                                                 std::string_view instructions, std::size_t estimated_tokens)>;

struct PreparedCompactionContext
{
  std::vector<ava::session::SessionEntry> active_entries;
  std::string recent_context;
  std::size_t estimated_tokens = 0;
  std::size_t retained_tokens = 0;
  bool recent_context_omitted = false;

  AVA_DEBUG_PRINT_MEMBERS_ON
};

[[nodiscard]] ava::core::Result<runtime::Session> open_runtime_session(runtime::OpenOptions const& options);

// Consume an already-owned persistent store and its lease without reopening by path.
// Inputs remain intact on failure so callers can roll back by stable identity.
[[nodiscard]] ava::core::Result<runtime::Session> open_owned_runtime_session(runtime::OpenOptions const& options, ava::session::SessionStore& store,
                                                                             ava::session::SessionLease& lease, bool created);

[[nodiscard]] ava::core::Result<runtime::PromptState> select_runtime_prompt_state(runtime::Session const& session, ava::agent::Mode mode);

[[nodiscard]] ava::core::VoidResult apply_runtime_prompt_state(runtime::Session& session, runtime::PromptState prompt_state);

// Publishes callback-free mutable runtime configuration into an existing
// retained parent capsule without changing its safe policy snapshot.
[[nodiscard]] ava::core::VoidResult refresh_runtime_parent_configuration(runtime::Session const& session);

// Append session metadata through the runtime owner's serialized route.
[[nodiscard]] ava::core::Result<ava::session::SessionMetadataView> append_runtime_session_metadata(runtime::Session& session,
                                                                                                   ava::session::SessionMetadataUpdate update);

// Append a mode change through the runtime owner's serialized route.
[[nodiscard]] ava::core::VoidResult append_runtime_mode_change(runtime::Session& session, ava::agent::Mode mode);

[[nodiscard]] ava::core::Result<ava::config::ModelInfo> resolve_runtime_model(ava::config::XdgPaths const& paths, std::string_view provider_id,
                                                                              std::string_view model_id);

[[nodiscard]] ava::core::Result<bool> switch_runtime_model(runtime::Session& session, ava::config::ModelInfo model);

[[nodiscard]] ava::core::Result<bool> set_runtime_reasoning(runtime::Session& session, std::optional<runtime::ReasoningSelection> selection);

// Stop background work and replace an attached session without memberwise move assignment.
[[nodiscard]] ava::core::VoidResult replace_runtime_session(runtime::Session& destination, runtime::Session&& replacement);

[[nodiscard]] ava::core::Result<ava::agent::AgentLoopResult> run_prompt(runtime::Session& session, std::string const& user_message,
                                                                        ava::provider::Provider const& provider, ava::provider::Transport& transport,
                                                                        runtime::RunOptions const& options);

// Run a prompt using an admission guard already acquired from this session's controller.
[[nodiscard]] ava::core::Result<ava::agent::AgentLoopResult> run_admitted_prompt(runtime::Session& session, std::string const& user_message,
                                                                                 ava::provider::Provider const& provider, ava::provider::Transport& transport,
                                                                                 runtime::RunOptions const& options, ActiveRunGuard guard);

[[nodiscard]] ava::core::Error offline_provider_error(std::string_view action);

[[nodiscard]] bool same_session_snapshot(std::vector<ava::session::SessionEntry> const& expected, std::vector<ava::session::SessionEntry> const& actual);

[[nodiscard]] ava::core::Error stale_compaction_snapshot_error(std::string_view trigger, std::size_t snapshot_entries, std::size_t current_entries);

[[nodiscard]] ava::core::Result<ava::session::CompactionConfig> resolve_compaction_config(runtime::Session const& session,
                                                                                          ava::session::CompactionConfig config);

[[nodiscard]] ava::core::Result<PreparedCompactionContext> prepare_compaction_context(std::vector<ava::session::SessionEntry> const& entries,
                                                                                      ava::session::CompactionConfig const& config,
                                                                                      std::vector<std::string> const& replayed_user_messages = {});

[[nodiscard]] ava::core::Result<std::string> build_compaction_summary_prompt(std::vector<ava::session::SessionEntry> const& entries,
                                                                             ava::session::CompactionConfig const& config, std::string_view instructions,
                                                                             std::size_t estimated_tokens);

[[nodiscard]] ava::core::Result<std::string> generate_compaction_summary(runtime::Session const& session,
                                                                         std::vector<ava::session::SessionEntry> const& entries,
                                                                         ava::session::CompactionConfig const& config, std::string_view instructions,
                                                                         std::size_t estimated_tokens, ava::provider::Provider const& provider,
                                                                         ava::provider::Transport& transport, runtime::RunOptions const& options);

[[nodiscard]] std::string to_string(runtime::FreshnessSourceKind kind);

}  // namespace ava::app
