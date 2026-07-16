#pragma once

#include "PromptOverrides.h"
#include "ava/debug/print_members_on.h"
#include "ava/agent/mode.h"
#include "ava/agent/tool_visibility.h"
#include "ava/config/xdg_paths.h"

#include <filesystem>
#include <optional>
#include <string>

namespace ava::app::runtime {

// Collect the inputs that callers supply when opening a runtime session, such as workspace and
// current directories, optional session selection, agent mode, tool visibility and prompt overrides.
//
// paths defaults to the process-wide XDG layout so that callers that do not override it share the standard configuration locations.
struct OpenOptions
{
  std::filesystem::path workspace_dir;
  std::filesystem::path current_dir;
  std::optional<std::string> requested_session_id;
  std::optional<std::string> fork_session_id;
  std::optional<std::string> initial_session_name;
  bool continue_last_session = false;
  bool sessionless = false;
  ava::agent::Mode mode = ava::agent::Mode::Build;
  ava::agent::ToolVisibilityOptions tool_visibility;
  ava::config::XdgPaths paths = ava::config::xdg_paths();
  PromptOverrides prompt_overrides;

  AVA_DEBUG_PRINT_MEMBERS_ON
};

}  // namespace ava::app::runtime
