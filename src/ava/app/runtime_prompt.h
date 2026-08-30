#pragma once

#include "ava/app/runtime.h"
#include "ava/agent/subagent_config.h"
#include "ava/core/result.h"

#include <filesystem>

namespace ava::app::runtime {

[[nodiscard]] ava::core::Result<PromptState> load_runtime_prompt_state(
    ava::config::XdgPaths const& paths, ava::config::ModelInfo const& model, ava::agent::Mode mode, std::filesystem::path const& workspace_dir,
    std::filesystem::path const& current_dir, bool include_project_resources, PromptOverrides const& prompt_overrides,
    std::optional<ava::agent::SubagentDefinition> const& selected_primary_agent = std::nullopt);

}  // namespace ava::app::runtime
