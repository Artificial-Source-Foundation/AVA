#pragma once

#include <optional>
#include <string>

#include "ava/app/runtime.h"
#include "ava/core/result.h"

namespace ava::app {

[[nodiscard]] std::optional<std::string> reasoning_status_for_session(RuntimeSession const& session);

[[nodiscard]] ava::core::Result<RuntimeReasoningSelection> reasoning_selection_for_level(
    ava::config::ModelInfo const& model, std::string level);

[[nodiscard]] ava::core::Result<std::string> cycle_runtime_reasoning(RuntimeSession& session);

}  // namespace ava::app
