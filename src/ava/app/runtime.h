#pragma once

#include "ava/app/RuntimeOpenOptions.h"
#include "ava/app/RuntimePromptState.h"
#include "ava/app/RuntimeRunOptions.h"
#include "ava/app/RuntimeSession.h"
#include "ava/agent/agent_loop.h"
#include "ava/config/model_config.h"
#include "ava/config/xdg_paths.h"
#include "ava/session/compaction.h"
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

using CompactionSummaryGenerator =
    std::function<ava::core::Result<std::string>(std::vector<ava::session::SessionEntry> const& entries, ava::session::CompactionConfig const& config,
                                                 std::string_view instructions, std::size_t estimated_tokens)>;

[[nodiscard]] ava::core::Result<RuntimeSession> open_runtime_session(RuntimeOpenOptions const& options);

[[nodiscard]] ava::core::Result<RuntimePromptState> select_runtime_prompt_state(RuntimeSession const& session, ava::agent::Mode mode);

void apply_runtime_prompt_state(RuntimeSession& session, RuntimePromptState prompt_state);

[[nodiscard]] ava::core::Result<ava::config::ModelInfo> resolve_runtime_model(ava::config::XdgPaths const& paths, std::string_view provider_id,
                                                                              std::string_view model_id);

[[nodiscard]] ava::core::Result<bool> switch_runtime_model(RuntimeSession& session, ava::config::ModelInfo model);

[[nodiscard]] ava::core::Result<bool> set_runtime_reasoning(RuntimeSession& session, std::optional<RuntimeReasoningSelection> selection);

[[nodiscard]] ava::core::Result<ava::agent::AgentLoopResult> run_prompt(RuntimeSession& session, std::string const& user_message,
                                                                        ava::provider::Provider const& provider, ava::provider::Transport& transport,
                                                                        RuntimeRunOptions const& options);

[[nodiscard]] bool same_session_snapshot(std::vector<ava::session::SessionEntry> const& expected, std::vector<ava::session::SessionEntry> const& actual);

[[nodiscard]] ava::core::Error stale_compaction_snapshot_error(std::string_view trigger, std::size_t snapshot_entries, std::size_t current_entries);

[[nodiscard]] std::string build_compaction_summary_prompt(std::vector<ava::session::SessionEntry> const& entries, ava::session::CompactionConfig const& config,
                                                          std::string_view instructions, std::size_t estimated_tokens);

[[nodiscard]] ava::core::Result<std::string> generate_compaction_summary(RuntimeSession const& session, std::vector<ava::session::SessionEntry> const& entries,
                                                                         ava::session::CompactionConfig const& config, std::string_view instructions,
                                                                         std::size_t estimated_tokens, ava::provider::Provider const& provider,
                                                                         ava::provider::Transport& transport, RuntimeRunOptions const& options);

[[nodiscard]] std::string to_string(RuntimeFreshnessSourceKind kind);

}  // namespace ava::app
