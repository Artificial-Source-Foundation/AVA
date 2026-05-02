#pragma once

#include <functional>
#include <string>
#include <vector>

#include "ava/agent/question.h"
#include "ava/app/events.h"
#include "ava/core/result.h"
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
  std::string app_version = "0.32";
  std::string initial_status = "";
  std::vector<SlashCommandItem> slash_commands = {};
  TuiKeyBindings key_bindings = default_key_bindings();
  std::function<TuiSubmitResult(const std::string&, const ava::permissions::PermissionResolver&,
                                const ava::agent::QuestionResolver&, ava::app::RuntimeEventSink, std::function<bool()>)>
      on_submit;
  std::function<ava::core::Result<std::string>()> on_toggle_mode;
};

[[nodiscard]] int run_interactive_composer(TuiRuntimeOptions options);
[[nodiscard]] ava::core::Result<ava::agent::QuestionAnswer> question_answer_from_prompt_view(
    const QuestionPromptView& prompt);

}  // namespace ava::tui
