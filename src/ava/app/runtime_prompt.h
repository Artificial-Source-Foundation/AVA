#pragma once

#include <filesystem>

#include "ava/app/runtime.h"
#include "ava/core/result.h"

namespace ava::app::runtime {

[[nodiscard]] ava::core::Result<RuntimePromptState> load_runtime_prompt_state(
    ava::config::XdgPaths const& paths, ava::config::ModelInfo const& model, ava::agent::Mode mode,
    std::filesystem::path const& workspace_dir, std::filesystem::path const& current_dir);

}  // namespace ava::app::runtime
