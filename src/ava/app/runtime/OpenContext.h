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
#include <vector>

namespace ava::core {
class AnchorSet;
} // namespace ava::core

namespace ava::diagnostics {
class RuntimeDiagnostics;
} // namespace ava::diagnostics

namespace ava::app {
class SessionTitleCoordinator;
class SubagentDeliveryManager;
} // namespace ava::app

namespace ava::app::runtime {

// Collect stable policy, filesystem authority, and application services used
// to open runtime sessions.
//
// Long-lived frontends may reuse one context across lifecycle requests. paths
// defaults to the process-wide XDG layout.
struct OpenContext
{
  std::filesystem::path workspace_dir;
  std::filesystem::path current_dir;
  ava::agent::Mode mode = ava::agent::Mode::Build;
  ava::agent::ToolVisibilityOptions tool_visibility;
  ava::config::XdgPaths paths = ava::config::xdg_paths();
  bool offline = false;

  // Additional writable directories beyond workspace_dir (e.g., spill_dir,
  // session storage). These are opened as anchor descriptors at startup and
  // made available to tools via ToolContext::anchor_set.
  std::vector<std::filesystem::path> additional_writable_dirs = {};
  // Long-lived adapters may supply one immutable descriptor authority shared
  // by every session and application-scoped background service.
  std::shared_ptr<ava::core::AnchorSet> anchor_set = nullptr;
  PromptOverrides prompt_overrides;
  // Strict long-lived adapters may pin the startup default so later config edits cannot drift immutable connection capabilities from new sessions.
  std::optional<ava::config::ModelInfo> default_model_override = std::nullopt;
  // When true, the override is the complete immutable model snapshot for both new and resumed sessions.
  bool pin_model_override = false;
  // Strict adapters opt out of CLI prefix resolution and may bound persistence reads without changing legacy CLI/RPC behavior.
  bool exact_session_id = false;
  std::optional<ava::session::SessionReadLimits> session_read_limits = std::nullopt;
  // Long-lived frontends supply one shared coordinator across every runtime
  // session they open. Null creates the initial application coordinator.
  std::shared_ptr<ava::agent::SubagentCoordinator> subagent_coordinator = nullptr;
  // Application-scoped automatic parent-summary delivery. When supplied it
  // also owns the coordinator used by every navigated runtime session.
  std::shared_ptr<ava::app::SubagentDeliveryManager> subagent_delivery_manager = nullptr;
  // Application-scoped asynchronous root-session title generation.
  std::shared_ptr<ava::app::SessionTitleCoordinator> session_title_coordinator = nullptr;
  // Application-lifetime private diagnostics owner shared by visible,
  // retained, and protocol-managed runtime sessions.
  std::shared_ptr<ava::diagnostics::RuntimeDiagnostics> diagnostics = nullptr;

  AVA_DEBUG_PRINT_MEMBERS_ON
};

}  // namespace ava::app::runtime
