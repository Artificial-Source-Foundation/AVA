#pragma once

#include "ava/debug/print_members_on.h"
#include "ava/tui/composer.h"

#include <string>
#include <string_view>

namespace ava::tui::detail {

// Fail-closed TUI presentation for the task and job tools.
// Parses only allowlisted JSON fields from arguments_json/result_json.
// Never parses XML and never falls back to raw JSON/summaries.
enum class TaskJobToolKind
{
  None = 0,
  Task,
  Job,
};

struct TaskJobCardPresentation
{
  TaskJobToolKind kind = TaskJobToolKind::None;
  // Header tool name (humanized subagent type for task when safely known).
  std::string display_name = {};
  // Single-line primary summary after the status marker and name.
  std::string primary = {};
  // Expanded-only body (bounded sanitized plain task_result). Empty for jobs.
  // Render may show this under effective Expanded presentation; copy/search only
  // include it when the item is explicitly expanded (details_visible == true).
  std::string expanded_detail = {};
  // Copy/search corpus built only from the non-result presentation allowlist.
  std::string searchable_text = {};

  AVA_DEBUG_PRINT_MEMBERS_OPT_OUT
};

[[nodiscard]] TaskJobToolKind task_job_tool_kind(ToolTimelineItem const& item) noexcept;
[[nodiscard]] bool is_task_or_job_tool(ToolTimelineItem const& item) noexcept;
[[nodiscard]] bool task_job_explicitly_expanded(ToolTimelineItem const& item) noexcept;
[[nodiscard]] TaskJobCardPresentation task_job_card_presentation(ToolTimelineItem const& item);
[[nodiscard]] bool task_job_card_matches_query(ToolTimelineItem const& item, TaskJobCardPresentation const& presentation, std::string_view query);
[[nodiscard]] std::string task_job_card_copy_text(ToolTimelineItem const& item, TaskJobCardPresentation const& presentation);

}  // namespace ava::tui::detail
