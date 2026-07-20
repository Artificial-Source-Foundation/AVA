#pragma once

#include "PromptOverrides.h"
#include "ava/debug/print_members_on.h"
#include "ava/agent/mode.h"
#include "ava/agent/subagent_coordinator.h"
#include "ava/agent/tool_visibility.h"
#include "ava/config/model_config.h"
#include "ava/config/xdg_paths.h"
#include "ava/session/session_metadata.h"

#include <filesystem>
#include <memory>
#include <optional>
#include <string>

namespace ava::diagnostics {
class RuntimeDiagnostics;
}

namespace ava::app {
class SubagentDeliveryManager;
}

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
  std::optional<std::string> initial_reasoning_level = std::nullopt;
  // Strict long-lived adapters may pin the startup default so later config edits cannot drift immutable connection capabilities from new sessions.
  std::optional<ava::config::ModelInfo> default_model_override = std::nullopt;
  // When true, the override is the complete immutable model snapshot for both new and resumed sessions.
  bool pin_model_override = false;
  bool offline = false;
  // Strict adapters opt out of CLI prefix resolution and may bound persistence reads without changing legacy CLI/RPC behavior.
  bool exact_session_id = false;
  std::optional<ava::session::SessionReadLimits> session_read_limits = std::nullopt;
  // Strict adapters may pin the persisted cwd while retaining lease acquisition inside the protocol-neutral runtime ownership boundary.
  std::optional<std::filesystem::path> expected_original_cwd = std::nullopt;
  // Long-lived frontends supply one shared coordinator across every runtime
  // session they open. Null creates the initial application coordinator.
  std::shared_ptr<ava::agent::SubagentCoordinator> subagent_coordinator = nullptr;
  // Application-scoped automatic parent-summary delivery. When supplied it
  // also owns the coordinator used by every navigated runtime session.
  std::shared_ptr<ava::app::SubagentDeliveryManager> subagent_delivery_manager = nullptr;
  // Application-lifetime private diagnostics owner shared by visible,
  // retained, and protocol-managed runtime sessions.
  std::shared_ptr<ava::diagnostics::RuntimeDiagnostics> diagnostics = nullptr;

  AVA_DEBUG_PRINT_MEMBERS_ON
};

}  // namespace ava::app::runtime
