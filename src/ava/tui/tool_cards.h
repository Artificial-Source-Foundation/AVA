#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "ava/tui/composer.h"

namespace ava::tui::detail {

[[nodiscard]] bool tool_card_details_visible(const ToolTimelineItem& item, bool global_details_visible);
[[nodiscard]] std::vector<std::string> render_tool_card(const ToolTimelineItem& item, std::size_t width,
                                                        bool global_details_visible);

}  // namespace ava::tui::detail
