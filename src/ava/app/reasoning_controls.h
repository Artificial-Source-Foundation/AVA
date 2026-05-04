#pragma once

#include <optional>
#include <string>

#include "ava/app/runtime.h"
#include "ava/core/result.h"

namespace ava::app {

[[nodiscard]] std::optional<std::string> reasoning_status_for_session(const RuntimeSession& session);

[[nodiscard]] ava::core::Result<RuntimeReasoningSelection> reasoning_selection_for_level(
    const ava::config::ModelInfo& model, std::string level);

[[nodiscard]] ava::core::Result<std::string> cycle_runtime_reasoning(RuntimeSession& session);

}  // namespace ava::app
