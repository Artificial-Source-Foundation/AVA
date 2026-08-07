#pragma once

#include "ava/app/runtime.h"
#include "ava/tui/composer.h"
#include "ava/core/result.h"

#include <optional>
#include <string>
#include <string_view>

namespace ava::app {

[[nodiscard]] std::optional<std::string> reasoning_status_for_session(runtime::session_ts const& unlocked_session);

// Returns no view when policy resolution exposes no configurable non-off level.
// Canonical level values remain hidden selector authority; rows use concise labels.
[[nodiscard]] std::optional<tui::SelectListView> reasoning_selector_view(ava::config::ModelInfo const& model,
                                                                         std::optional<runtime::ReasoningSelection> const& current,
                                                                         std::string footer_hint = {});
[[nodiscard]] std::optional<tui::SelectListView> reasoning_selector_view(runtime::session_ts const& unlocked_session, std::string footer_hint = {});
[[nodiscard]] std::string reasoning_level_label(std::string_view level);

[[nodiscard]] ava::core::Result<runtime::ReasoningSelection> reasoning_selection_for_level(ava::config::ModelInfo const& model, std::string level);

[[nodiscard]] ava::core::Result<std::string> cycle_runtime_reasoning(runtime::session_ts& unlocked_session);

}  // namespace ava::app
