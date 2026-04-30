#pragma once

#include <functional>
#include <string>
#include <vector>

#include "ava/core/result.h"
#include "ava/permissions/permission.h"
#include "ava/tui/composer.h"

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
  std::vector<SlashCommandItem> slash_commands = {};
  std::function<TuiSubmitResult(const std::string&, const ava::permissions::PermissionResolver&)> on_submit;
  std::function<ava::core::Result<std::string>()> on_toggle_mode;
};

[[nodiscard]] int run_interactive_composer(TuiRuntimeOptions options);

}  // namespace ava::tui
