#pragma once

#include "runtime/OpenOptions.h"
#include "runtime/PromptState.h"
#include "runtime/RunOptions.h"
#include "runtime/Session.h"
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

[[nodiscard]] ava::core::Result<runtime::Session> open_runtime_session(runtime::OpenOptions const& options);

[[nodiscard]] ava::core::Result<runtime::PromptState> select_runtime_prompt_state(runtime::Session const& session, ava::agent::Mode mode);

void apply_runtime_prompt_state(runtime::Session& session, runtime::PromptState prompt_state);

[[nodiscard]] ava::core::Result<ava::config::ModelInfo> resolve_runtime_model(ava::config::XdgPaths const& paths, std::string_view provider_id,
                                                                              std::string_view model_id);

[[nodiscard]] ava::core::Result<bool> switch_runtime_model(runtime::Session& session, ava::config::ModelInfo model);

[[nodiscard]] ava::core::Result<bool> set_runtime_reasoning(runtime::Session& session, std::optional<runtime::ReasoningSelection> selection);

[[nodiscard]] ava::core::Result<ava::agent::AgentLoopResult> run_prompt(runtime::Session& session, std::string const& user_message,
                                                                        ava::provider::Provider const& provider, ava::provider::Transport& transport,
                                                                        runtime::RunOptions const& options);

[[nodiscard]] bool same_session_snapshot(std::vector<ava::session::SessionEntry> const& expected, std::vector<ava::session::SessionEntry> const& actual);

[[nodiscard]] ava::core::Error stale_compaction_snapshot_error(std::string_view trigger, std::size_t snapshot_entries, std::size_t current_entries);

[[nodiscard]] std::string build_compaction_summary_prompt(std::vector<ava::session::SessionEntry> const& entries, ava::session::CompactionConfig const& config,
                                                          std::string_view instructions, std::size_t estimated_tokens);

[[nodiscard]] ava::core::Result<std::string> generate_compaction_summary(runtime::Session const& session,
                                                                         std::vector<ava::session::SessionEntry> const& entries,
                                                                         ava::session::CompactionConfig const& config, std::string_view instructions,
                                                                         std::size_t estimated_tokens, ava::provider::Provider const& provider,
                                                                         ava::provider::Transport& transport, runtime::RunOptions const& options);

[[nodiscard]] std::string to_string(runtime::FreshnessSourceKind kind);

}  // namespace ava::app
