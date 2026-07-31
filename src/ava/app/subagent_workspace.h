#pragma once

#include "ava/debug/print_members_on.h"
#include "ava/agent/subagent_coordinator.h"
#include "ava/tui/composer.h"
#include "ava/core/result.h"

#include <memory>
#include <string_view>
#include <vector>

namespace ava::app {

// Builds the interactive, path-free /jobs selector. Exact job ids remain only
// in SelectListItemView::value as control authority and are never rendered.
[[nodiscard]] ava::tui::SelectListView subagent_selector_view(std::vector<ava::agent::SubagentCoordinatorJobSnapshot> const& snapshots);
[[nodiscard]] ava::core::Result<ava::tui::SelectListView> subagent_selector_view(std::shared_ptr<ava::agent::SubagentCoordinator> const& coordinator,
                                                                                 std::string_view parent_session_id);

}  // namespace ava::app
