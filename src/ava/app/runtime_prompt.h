#pragma once

#include <filesystem>

#include "ava/app/runtime.h"
#include "ava/core/result.h"

namespace ava::app::runtime {

[[nodiscard]] ava::core::Result<RuntimePromptState> load_runtime_prompt_state(
    const ava::config::XdgPaths& paths, const ava::config::ModelInfo& model, ava::agent::Mode mode,
    const std::filesystem::path& workspace_dir, const std::filesystem::path& current_dir);

}  // namespace ava::app::runtime
