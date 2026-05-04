#pragma once

#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "ava/agent/question.h"
#include "ava/app/events.h"
#include "ava/core/result.h"
#include "ava/core/version.h"
#include "ava/permissions/permission.h"
#include "ava/tui/composer.h"
#include "ava/tui/keybindings.h"

namespace ava::tui {

struct TuiSubmitResult {
  bool quit = false;
  std::vector<std::string> output;
  std::vector<ToolTimelineItem> tool_timeline;
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
  std::function<TuiSubmitResult(const std::string&, const ava::permissions::PermissionResolver&,
                                const ava::agent::QuestionResolver&, ava::app::RuntimeEventSink, std::function<bool()>)>
      on_submit;
  std::function<ava::core::Result<std::string>()> on_toggle_mode;
  std::function<ava::core::Result<std::string>()> on_cycle_reasoning;
};

[[nodiscard]] int run_interactive_composer(TuiRuntimeOptions options);
[[nodiscard]] ava::core::Result<ava::agent::QuestionAnswer> question_answer_from_prompt_view(
    const QuestionPromptView& prompt);

}  // namespace ava::tui
