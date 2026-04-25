#include "interactive_detail_projection.hpp"

namespace ava::tui {

namespace {

[[nodiscard]] std::size_t utf8_safe_prefix_size(const std::string& text, std::size_t max_size) {
  if(max_size >= text.size()) {
    return text.size();
  }
  while(max_size > 0) {
    const auto byte = static_cast<unsigned char>(text[max_size]);
    if((byte & 0xC0U) != 0x80U) {
      break;
    }
    --max_size;
  }
  return max_size;
}

}  // namespace

std::string truncate_for_dock(std::string text, std::size_t max_size) {
  if(text.size() <= max_size) {
    return text;
  }
  text.resize(utf8_safe_prefix_size(text, max_size));
  text += "...";
  return text;
}

std::string preview_line(std::string label, const std::string& value, std::size_t max_size) {
  const auto truncated = value.size() > max_size;
  return label + (truncated ? "_preview(truncated): " : ": ") + truncate_for_dock(value, max_size);
}

DockDetailProjection approval_detail_projection(const ava::orchestration::ApprovalRequestPayload& payload) {
  const auto args = payload.call.arguments.dump();
  const auto tool_truncated = payload.call.name.size() > 120;
  const auto risk_truncated = payload.inspection.risk_level.size() > 80;
  const auto reason_truncated = payload.inspection.reason.size() > 300;
  const auto args_truncated = args.size() > 300;
  const auto truncated = tool_truncated || risk_truncated || reason_truncated || args_truncated;
  return DockDetailProjection{
      .lines = {
          preview_line("tool", payload.call.name, 120),
          preview_line("risk", payload.inspection.risk_level, 80),
          preview_line("reason", payload.inspection.reason),
          std::string(args_truncated ? "args_preview(truncated): " : "args: ") + truncate_for_dock(args),
          truncated ? "approval disabled: payload preview is truncated; reject and rerun with narrower request"
                    : "approval detail complete",
      },
      .complete = !truncated,
  };
}

std::vector<std::string> question_detail_lines(const ava::orchestration::QuestionRequestPayload& payload) {
  std::vector<std::string> lines{preview_line("question", payload.question)};
  if(!payload.options.empty()) {
    std::string options = "options:";
    for(const auto& option : payload.options) {
      options += " " + option;
    }
    lines.push_back(truncate_for_dock(std::move(options)));
  }
  return lines;
}

std::vector<std::string> plan_detail_lines(const ava::orchestration::PlanRequestPayload& payload) {
  return {"plan: " + truncate_for_dock(payload.plan.dump())};
}

}  // namespace ava::tui
