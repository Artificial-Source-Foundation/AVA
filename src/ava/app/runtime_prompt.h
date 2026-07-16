#pragma once

#include "ava/app/runtime.h"
#include "ava/core/result.h"

#include <filesystem>

namespace ava::app::runtime {

[[nodiscard]] ava::core::Result<runtime::PromptState> load_runtime_prompt_state(ava::config::XdgPaths const& paths, ava::config::ModelInfo const& model,
                                                                                ava::agent::Mode mode, std::filesystem::path const& workspace_dir,
                                                                                std::filesystem::path const& current_dir, bool include_project_resources,
                                                                                runtime::PromptOverrides const& prompt_overrides);

}  // namespace ava::app::runtime
