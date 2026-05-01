#pragma once

#include <filesystem>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "ava/agent/agent_loop.h"
#include "ava/app/events.h"
#include "ava/config/model_config.h"
#include "ava/config/prompt_config.h"
#include "ava/config/xdg_paths.h"
#include "ava/context/context_loader.h"
#include "ava/permissions/permission.h"
#include "ava/provider/provider.h"
#include "ava/session/session_store.h"

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
  bool openai_oauth = false;
  std::string openai_account_id;
  bool stream = true;
  RuntimeEventSink event_sink = nullptr;
  ava::permissions::PermissionResolver permission_resolver = nullptr;
  ava::agent::QuestionResolver question_resolver = nullptr;
  std::function<bool()> cancel_requested = nullptr;
  std::function<ava::core::Result<std::vector<std::string>>()> take_steering_messages = nullptr;
  std::mutex* session_mutex = nullptr;
};

[[nodiscard]] ava::core::Result<RuntimeSession> open_runtime_session(const RuntimeOpenOptions& options);

[[nodiscard]] ava::core::Result<RuntimePromptState> select_runtime_prompt_state(const RuntimeSession& session,
                                                                                ava::agent::Mode mode);

void apply_runtime_prompt_state(RuntimeSession& session, RuntimePromptState prompt_state);

[[nodiscard]] ava::core::Result<ava::agent::AgentLoopResult> run_prompt(RuntimeSession& session,
                                                                        const std::string& user_message,
                                                                        const ava::provider::Provider& provider,
                                                                        ava::provider::Transport& transport,
                                                                        const RuntimeRunOptions& options);

}  // namespace ava::app
