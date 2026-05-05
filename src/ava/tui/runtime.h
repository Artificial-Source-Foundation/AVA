#pragma once

#include "ava/app/events.h"

#include "ava/agent/question.h"

#include "ava/tui/composer.h"
#include "ava/tui/keybindings.h"

#include "ava/permissions/permission.h"

#include "ava/core/result.h"
#include "ava/core/version.h"

#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ava::tui {

struct TuiSubmitResult {
  bool quit = false;
  std::vector<std::string> output;
  std::vector<ToolTimelineItem> tool_timeline;
};

struct TuiQueuedFollowUp {
  std::string request_id;
  std::string message;
};

struct TuiRestoredQueuedMessage {
  std::string message;
  bool steering = false;
};

struct TuiActiveRunQueues {
  std::string active_request_id;
  std::function<ava::core::VoidResult(std::string)> queue_steering;
  std::function<ava::core::VoidResult(std::string)> queue_follow_up;
  std::function<ava::core::Result<std::vector<std::string>>()> take_steering_messages;
  std::function<ava::core::VoidResult(std::string_view)> skip_active_steering;
  std::function<std::optional<TuiQueuedFollowUp>()> take_next_follow_up;
  std::function<ava::core::VoidResult(TuiQueuedFollowUp const&)> mark_follow_up_started;
  std::function<ava::core::Result<TuiRestoredQueuedMessage>()> restore_latest;
  std::function<ava::core::VoidResult(bool)> finish;
};

struct TuiSubmitContext {
  ava::permissions::PermissionResolver permission_resolver;
  ava::agent::QuestionResolver question_resolver;
  ava::app::RuntimeEventSink event_sink;
  std::function<bool()> cancel_requested;
  std::function<ava::core::Result<std::vector<std::string>>()> take_steering_messages;
  std::function<ava::core::VoidResult(std::string_view)> skip_active_steering;
  std::function<std::optional<TuiQueuedFollowUp>()> take_next_follow_up;
  std::function<ava::core::VoidResult(TuiQueuedFollowUp const&)> mark_follow_up_started;
};

struct TuiRuntimeOptions {
  std::string mode;
  std::string provider;
  std::string model;
  std::string session_id;
  std::string workspace;
  std::string git_branch;
  std::string app_version = std::string(ava::core::version::kDisplayVersion);
  std::optional<std::size_t> context_source_count = std::nullopt;
  std::string initial_status = "";
  std::vector<SlashCommandItem> slash_commands = {};
  TuiKeyBindings key_bindings = default_key_bindings();
  // Called on the TUI main thread at startup and after a submit worker completes; never from render/spinner loops.
  std::function<std::optional<std::string>()> token_status_provider;
  std::function<std::optional<std::string>()> reasoning_status_provider;
  std::function<TuiActiveRunQueues(ava::app::EventEnvelopeSink)> create_active_run_queues;
  std::function<TuiSubmitResult(std::string const&, TuiSubmitContext)> on_submit;
  std::function<ava::core::Result<std::string>()> on_toggle_mode;
  std::function<ava::core::Result<std::string>()> on_cycle_reasoning;
};

[[nodiscard]] int run_interactive_composer(TuiRuntimeOptions options);
[[nodiscard]] ava::core::Result<ava::agent::QuestionAnswer> question_answer_from_prompt_view(
    QuestionPromptView const& prompt);

}  // namespace ava::tui
