#pragma once

#include "ava/app/runtime.h"
#include "ava/core/result.h"

#include <optional>
#include <string>

namespace ava::app {

[[nodiscard]] std::optional<std::string> reasoning_status_for_session(runtime::RuntimeSession const& session);

[[nodiscard]] ava::core::Result<runtime::RuntimeReasoningSelection> reasoning_selection_for_level(ava::config::ModelInfo const& model, std::string level);

[[nodiscard]] ava::core::Result<std::string> cycle_runtime_reasoning(runtime::RuntimeSession& session);

}  // namespace ava::app
