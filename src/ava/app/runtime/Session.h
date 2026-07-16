#pragma once

#include "BasePromptMetadata.h"
#include "ContextSourceMetadata.h"
#include "FreshnessSourceMetadata.h"
#include "PromptOverrides.h"
#include "ReasoningSelection.h"
#include "ava/debug/print_members_on.h"
#include "ava/app/project_trust.h"
#include "ava/app/session_run_controller.h"
#include "ava/agent/agent_loop.h"
#include "ava/mcp/config.h"
#include "ava/config/model_config.h"
#include "ava/config/xdg_paths.h"
#include "ava/session/session_store.h"
#include "ava/core/error.h"

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace ava::app::runtime {

// Hold the mutable application state associated with an open runtime session.
//
// The store may be persistent or ephemeral according to sessionless. Shared background jobs remain valid when the aggregate is moved or replaced.
struct Session
{
  ava::session::SessionStore store;
  // Persistent runtime owners hold a cross-process lease for the complete session lifetime.
  ava::session::SessionLease lease;
  ava::agent::Mode mode = ava::agent::Mode::Build;
  ava::config::ModelInfo model;
  BasePromptMetadata base_prompt;
  ava::config::XdgPaths paths;
  std::filesystem::path workspace_dir;
  std::filesystem::path current_dir;
  ProjectTrustState project_trust;
  PromptOverrides prompt_overrides;
  ava::agent::ToolVisibilityOptions tool_visibility;
  std::vector<ContextSourceMetadata> context_sources;
  std::vector<FreshnessSourceMetadata> freshness_sources;
  std::string system_prompt;
  std::optional<ReasoningSelection> reasoning = std::nullopt;
  std::optional<std::vector<std::string>> scoped_model_cycle = std::nullopt;
  bool created = false;
  bool sessionless = false;
  // Declare before workers so reverse destruction stops background work before destroying store routes.
  std::unique_ptr<SessionRunController> run_controller = std::make_unique<SessionRunController>();
  std::shared_ptr<ava::agent::BackgroundJobRegistry> background_jobs = std::make_shared<ava::agent::BackgroundJobRegistry>();
  // Null uses normal global/project discovery; non-null is immutable session-local MCP composition.
  std::shared_ptr<ava::mcp::McpConfig const> mcp_config = nullptr;
  bool offline = false;

  // Append through the session owner so writes remain serialized with active runs.
  [[nodiscard]] ava::core::VoidResult append_owned(ava::session::SessionEntry entry)
  {
    if (!run_controller)
      return std::unexpected(ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "runtime session controller is unavailable"));
    return run_controller->append(store, std::move(entry));
  }

  // Return the stable append route owned by this session, or an empty route when the controller is unavailable.
  [[nodiscard]] ava::agent::SessionAppendSink owner_append_route()
  {
    return run_controller ? run_controller->owner_append_route(store) : ava::agent::SessionAppendSink{};
  }

  AVA_DEBUG_PRINT_MEMBERS_ON
};

}  // namespace ava::app::runtime
