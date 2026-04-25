#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "ava/orchestration/interactive.hpp"

namespace ava::tui {

struct DockDetailProjection {
  std::vector<std::string> lines;
  bool complete{true};
};

[[nodiscard]] std::string truncate_for_dock(std::string text, std::size_t max_size = 300);
[[nodiscard]] std::string preview_line(std::string label, const std::string& value, std::size_t max_size = 300);
[[nodiscard]] DockDetailProjection approval_detail_projection(
    const ava::orchestration::ApprovalRequestPayload& payload
);
[[nodiscard]] std::vector<std::string> question_detail_lines(
    const ava::orchestration::QuestionRequestPayload& payload
);
[[nodiscard]] std::vector<std::string> plan_detail_lines(const ava::orchestration::PlanRequestPayload& payload);

}  // namespace ava::tui
