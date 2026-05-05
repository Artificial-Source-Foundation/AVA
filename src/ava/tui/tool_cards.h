#pragma once

#include "ava/tui/composer.h"

#include <cstddef>
#include <string>
#include <vector>

namespace ava::tui::detail {

[[nodiscard]] bool tool_card_details_visible(ToolTimelineItem const& item, bool global_details_visible);
[[nodiscard]] std::vector<std::string> render_tool_card(ToolTimelineItem const& item, std::size_t width,
                                                        bool global_details_visible);

}  // namespace ava::tui::detail
