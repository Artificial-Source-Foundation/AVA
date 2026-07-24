#pragma once

#include "PromptOverrides.h"
#include "ava/agent/mode.h"
#include "ava/agent/subagent_coordinator.h"
#include "ava/agent/tool_visibility.h"
#include "ava/config/model_config.h"
#include "ava/config/xdg_paths.h"
#include "ava/session/session_store.h"

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>
#include "debug.h"

namespace ava::core {
class AnchorSet;
}

namespace ava::diagnostics {
class RuntimeDiagnostics;
}

namespace ava::app {
class SessionTitleCoordinator;
class SubagentDeliveryManager;
}  // namespace ava::app

namespace ava::app::runtime {

// Preserve application-lifetime inputs and descriptor-safe authorities across
// in-process session replacement. Session opening canonicalizes its resolved
// directory, anchor, and application-service values into this one value before
// transferring it to the new runtime session.
struct RuntimeContinuity
{
  std::filesystem::path workspace_dir;
  std::filesystem::path current_dir;
  // Additional writable directories beyond workspace_dir (e.g., spill_dir,
  // session storage). These are opened as anchor descriptors at startup and
  // made available to tools via their explicit ToolContext projection.
  std::vector<std::filesystem::path> additional_writable_dirs = {};
  // Long-lived adapters may supply one immutable descriptor authority shared
  // by every session and application-scoped background service.
  std::shared_ptr<ava::core::AnchorSet> anchor_set = nullptr;
  ava::agent::Mode mode = ava::agent::Mode::Build;
  ava::agent::ToolVisibilityOptions tool_visibility;
  // paths defaults to the process-wide XDG layout so callers that do not
  // override it share the standard configuration locations.
  ava::config::XdgPaths paths = ava::config::xdg_paths();
  PromptOverrides prompt_overrides;
  // Strict long-lived adapters may pin the startup default so later config
  // edits cannot drift immutable connection capabilities from new sessions.
  std::optional<ava::config::ModelInfo> default_model_override = std::nullopt;
  // When true, the override is the complete immutable model snapshot for both
  // new and resumed sessions.
  bool pin_model_override = false;
  bool offline = false;
  // Strict adapters retain this configured policy through runtime navigation;
  // Session stores its concrete effective value separately for all reads.
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
