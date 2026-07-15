#pragma once

#include "ava/app/ContextSourceMetadata.h"
#include "ava/app/RuntimeBasePromptMetadata.h"
#include "ava/app/RuntimeFreshnessSourceMetadata.h"
#include "ava/app/RuntimePromptOverrides.h"
#include "ava/app/RuntimeReasoningSelection.h"
#include "ava/app/project_trust.h"
#include "ava/agent/agent_loop.h"
#include "ava/debug/print_members_on.h"
#include "ava/config/model_config.h"
#include "ava/config/xdg_paths.h"
#include "ava/session/session_store.h"

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace ava::app {

// Hold the mutable application state associated with an open runtime session.
//
// The store may be persistent or ephemeral according to sessionless. Shared background jobs remain valid when the aggregate is moved or replaced.
struct RuntimeSession
{
  ava::session::SessionStore store;
  ava::agent::Mode mode = ava::agent::Mode::Build;
  ava::config::ModelInfo model;
  RuntimeBasePromptMetadata base_prompt;
  ava::config::XdgPaths paths;
  std::filesystem::path workspace_dir;
  std::filesystem::path current_dir;
  ProjectTrustState project_trust;
  RuntimePromptOverrides prompt_overrides;
  ava::agent::ToolVisibilityOptions tool_visibility;
  std::vector<ContextSourceMetadata> context_sources;
  std::vector<RuntimeFreshnessSourceMetadata> freshness_sources;
  std::string system_prompt;
  std::optional<RuntimeReasoningSelection> reasoning = std::nullopt;
  std::optional<std::vector<std::string>> scoped_model_cycle = std::nullopt;
  bool created = false;
  bool sessionless = false;
  std::shared_ptr<ava::agent::BackgroundJobRegistry> background_jobs = nullptr;

  AVA_DEBUG_PRINT_MEMBERS_ON
};

}  // namespace ava::app
