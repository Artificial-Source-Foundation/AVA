#pragma once

#include "ava/app/events.h"

#include "ava/agent/agent_loop.h"

#include "ava/config/model_config.h"
#include "ava/config/prompt_config.h"
#include "ava/config/xdg_paths.h"

#include "ava/session/compaction.h"
#include "ava/session/session_store.h"

#include "ava/permissions/permission.h"

#include "ava/provider/provider.h"

#include "ava/context/context_loader.h"

#include <filesystem>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ava::app {

struct ContextSourceMetadata {
  std::filesystem::path path;
  ava::context::ContextSourceType source_type = ava::context::ContextSourceType::Workspace;
  std::size_t byte_count = 0;
};

struct RuntimeOpenOptions {
  std::filesystem::path workspace_dir;
  std::filesystem::path current_dir;
  std::optional<std::string> requested_session_id;
  bool continue_last_session = false;
  ava::agent::Mode mode = ava::agent::Mode::Build;
  ava::config::XdgPaths paths = ava::config::xdg_paths();
};

struct RuntimeReasoningSelection {
  std::string level;
  std::optional<long long> budget_tokens = std::nullopt;
  std::string display;
};

struct RuntimeSession {
  ava::session::SessionStore store;
  ava::agent::Mode mode = ava::agent::Mode::Build;
  ava::config::ModelInfo model;
  ava::config::PromptSelection prompt;
  ava::config::XdgPaths paths;
  std::filesystem::path workspace_dir;
  std::filesystem::path current_dir;
  std::vector<ContextSourceMetadata> context_sources;
  std::string system_prompt;
  std::optional<RuntimeReasoningSelection> reasoning = std::nullopt;
  bool created = false;
};

struct RuntimePromptState {
  ava::agent::Mode mode = ava::agent::Mode::Build;
  ava::config::PromptSelection prompt;
  std::vector<ContextSourceMetadata> context_sources;
  std::string system_prompt;
};

struct RuntimeRunOptions {
  std::string access_token;
  std::string credential_type = "bearer";
  bool openai_oauth = false;
  std::string openai_account_id;
  bool stream = true;
  bool enable_transport_retries = false;
  RuntimeEventSink event_sink = nullptr;
  ava::permissions::PermissionResolver permission_resolver = nullptr;
  ava::agent::QuestionResolver question_resolver = nullptr;
  std::function<bool()> cancel_requested = nullptr;
  std::function<ava::core::Result<std::vector<std::string>>()> take_steering_messages = nullptr;
  std::mutex* session_mutex = nullptr;
};

using CompactionSummaryGenerator = std::function<ava::core::Result<std::string>(
    std::vector<ava::session::SessionEntry> const& entries, ava::session::CompactionConfig const& config,
    std::string_view instructions, std::size_t estimated_tokens)>;

[[nodiscard]] ava::core::Result<RuntimeSession> open_runtime_session(RuntimeOpenOptions const& options);

[[nodiscard]] ava::core::Result<RuntimePromptState> select_runtime_prompt_state(RuntimeSession const& session,
                                                                                ava::agent::Mode mode);

void apply_runtime_prompt_state(RuntimeSession& session, RuntimePromptState prompt_state);

[[nodiscard]] ava::core::Result<ava::config::ModelInfo> resolve_runtime_model(ava::config::XdgPaths const& paths,
                                                                              std::string_view provider_id,
                                                                              std::string_view model_id);

[[nodiscard]] ava::core::Result<bool> switch_runtime_model(RuntimeSession& session, ava::config::ModelInfo model);

[[nodiscard]] ava::core::Result<bool> set_runtime_reasoning(RuntimeSession& session,
                                                            std::optional<RuntimeReasoningSelection> selection);

[[nodiscard]] ava::core::Result<ava::agent::AgentLoopResult> run_prompt(RuntimeSession& session,
                                                                        std::string const& user_message,
                                                                        ava::provider::Provider const& provider,
                                                                        ava::provider::Transport& transport,
                                                                        RuntimeRunOptions const& options);

[[nodiscard]] bool same_session_snapshot(std::vector<ava::session::SessionEntry> const& expected,
                                         std::vector<ava::session::SessionEntry> const& actual);

[[nodiscard]] ava::core::Error stale_compaction_snapshot_error(std::string_view trigger, std::size_t snapshot_entries,
                                                               std::size_t current_entries);

[[nodiscard]] std::string build_compaction_summary_prompt(std::vector<ava::session::SessionEntry> const& entries,
                                                          ava::session::CompactionConfig const& config,
                                                          std::string_view instructions, std::size_t estimated_tokens);

[[nodiscard]] ava::core::Result<std::string> generate_compaction_summary(
    RuntimeSession const& session, std::vector<ava::session::SessionEntry> const& entries,
    ava::session::CompactionConfig const& config, std::string_view instructions, std::size_t estimated_tokens,
    ava::provider::Provider const& provider, ava::provider::Transport& transport, RuntimeRunOptions const& options);

}  // namespace ava::app
