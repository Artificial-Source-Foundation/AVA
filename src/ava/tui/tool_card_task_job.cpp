#include "sys.h"
#include "ava/tui/composer_internal.h"
#include "ava/tui/tool_card_task_job.h"
#include "ava/core/json.h"

#include <algorithm>
#include <cctype>
#include <optional>
#include <string>
#include <string_view>

namespace ava::tui::detail {
namespace {

constexpr std::size_t kMaxDescriptionDisplayBytes = 160;
constexpr std::size_t kMaxExpandedTaskResultBytes = 4 * 1024;
constexpr std::size_t kMaxExpandedTaskResultLines = 40;
constexpr std::size_t kMaxHumanLabelBytes = 64;
constexpr std::string_view kEllipsis = "…";

std::string lower_ascii(std::string_view text)
{
  std::string lowered;
  lowered.reserve(text.size());
  for (char const ch : text) lowered.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
  return lowered;
}

bool ascii_whitespace(char ch)
{
  return ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r' || ch == '\f' || ch == '\v';
}

bool is_simple_identifier(std::string_view text)
{
  if (text.empty() || text.size() > kMaxHumanLabelBytes)
    return false;
  auto const first = static_cast<unsigned char>(text.front());
  if ((first < 'A' || first > 'Z') && (first < 'a' || first > 'z'))
    return false;
  return std::ranges::all_of(text, [](char ch) {
    auto const byte = static_cast<unsigned char>(ch);
    return (byte >= 'A' && byte <= 'Z') || (byte >= 'a' && byte <= 'z') || (byte >= '0' && byte <= '9') || ch == '_' || ch == '-';
  });
}

std::string humanize_identifier(std::string_view text)
{
  if (!is_simple_identifier(text))
    return {};
  std::string human;
  human.reserve(text.size());
  bool capitalize = true;
  for (char const ch : text)
  {
    if (ch == '_' || ch == '-')
    {
      if (!human.empty() && human.back() != ' ')
        human.push_back(' ');
      capitalize = true;
      continue;
    }
    auto const byte = static_cast<unsigned char>(ch);
    if (capitalize && byte >= 'a' && byte <= 'z')
      human.push_back(static_cast<char>(byte - 'a' + 'A'));
    else
      human.push_back(ch);
    capitalize = false;
  }
  while (!human.empty() && human.back() == ' ') human.pop_back();
  return human;
}

// Cap text to max_bytes while keeping whole UTF-8 / compact terminal clusters.
std::string truncate_to_utf8_clusters(std::string_view text, std::size_t max_bytes)
{
  if (text.size() <= max_bytes)
    return std::string(text);
  if (max_bytes == 0)
    return {};

  auto const ellipsis_bytes = kEllipsis.size();
  auto const budget = max_bytes > ellipsis_bytes ? max_bytes - ellipsis_bytes : max_bytes;
  std::size_t index = 0;
  std::size_t kept = 0;
  while (index < text.size())
  {
    auto const step = terminal_text_cluster_bytes(text, index);
    if (step == 0)
      break;
    if (kept + step > budget)
      break;
    kept += step;
    index += step;
  }
  if (kept == 0 && max_bytes >= ellipsis_bytes)
    return std::string(kEllipsis);
  if (kept == 0)
    return {};
  std::string out(text.substr(0, kept));
  if (kept < text.size() && out.size() + ellipsis_bytes <= max_bytes)
    out += kEllipsis;
  return out;
}

std::string sanitize_plain_field(std::string_view text, std::size_t max_bytes)
{
  auto sanitized = sanitize_terminal_text(text);
  // Collapse internal newlines/tabs to spaces for single-line card fields.
  for (char& ch : sanitized)
  {
    if (ch == '\n' || ch == '\r' || ch == '\t')
      ch = ' ';
  }
  // Collapse repeated spaces without stripping legitimate markup-like text.
  std::string collapsed;
  collapsed.reserve(sanitized.size());
  bool previous_space = false;
  for (char const ch : sanitized)
  {
    if (ascii_whitespace(ch))
    {
      if (!previous_space && !collapsed.empty())
      {
        collapsed.push_back(' ');
        previous_space = true;
      }
      continue;
    }
    collapsed.push_back(ch);
    previous_space = false;
  }
  while (!collapsed.empty() && collapsed.back() == ' ') collapsed.pop_back();
  while (!collapsed.empty() && collapsed.front() == ' ') collapsed.erase(collapsed.begin());
  return truncate_to_utf8_clusters(collapsed, max_bytes);
}

std::string sanitize_expanded_body(std::string_view text)
{
  // Plain allowlisted task_result only: sanitize terminal controls, keep code-like text.
  auto sanitized = sanitize_terminal_text(text);
  sanitized = truncate_to_utf8_clusters(sanitized, kMaxExpandedTaskResultBytes);

  // Bound by logical lines after the byte/cluster cap.
  std::size_t lines = 1;
  std::size_t cut = sanitized.size();
  for (std::size_t index = 0; index < sanitized.size(); ++index)
  {
    if (sanitized[index] != '\n')
      continue;
    ++lines;
    if (lines > kMaxExpandedTaskResultLines)
    {
      cut = index;
      break;
    }
  }
  if (cut < sanitized.size())
  {
    sanitized.resize(cut);
    while (!sanitized.empty() && (sanitized.back() == '\n' || sanitized.back() == '\r')) sanitized.pop_back();
    if (!sanitized.empty())
      sanitized += "\n…";
    else
      sanitized = "…";
  }
  while (!sanitized.empty() && (sanitized.back() == '\n' || sanitized.back() == '\r')) sanitized.pop_back();
  return sanitized;
}

bool looks_like_json_object(std::string_view text)
{
  auto value = text;
  while (!value.empty() && ascii_whitespace(value.front())) value.remove_prefix(1);
  return !value.empty() && value.front() == '{' && ava::core::json::is_valid_object(value);
}

std::optional<std::string> allowlisted_string(std::string_view object, std::string_view key)
{
  if (!looks_like_json_object(object))
    return std::nullopt;
  auto value = ava::core::json::string_field(object, key);
  if (!value || value->empty())
    return std::nullopt;
  return value;
}

std::optional<long long> allowlisted_integer(std::string_view object, std::string_view key)
{
  if (!looks_like_json_object(object))
    return std::nullopt;
  return ava::core::json::integer_field(object, key);
}

bool allowlisted_bool(std::string_view object, std::string_view key)
{
  if (!looks_like_json_object(object))
    return false;
  auto const start = ava::core::json::field_value_start(object, key);
  if (!start)
    return false;
  auto value = object.substr(*start);
  while (!value.empty() && ascii_whitespace(value.front())) value.remove_prefix(1);
  return value.starts_with("true");
}

std::optional<std::string> object_field_if_object(std::string_view object, std::string_view key)
{
  if (!looks_like_json_object(object))
    return std::nullopt;
  auto nested = ava::core::json::object_field(object, key);
  if (!nested || !looks_like_json_object(*nested))
    return std::nullopt;
  return nested;
}

void append_part(std::string& text, std::string_view part)
{
  if (part.empty())
    return;
  if (!text.empty())
    text += " · ";
  text += part;
}

void append_search_token(std::string& text, std::string_view token)
{
  if (token.empty())
    return;
  if (!text.empty())
    text.push_back('\n');
  text += token;
}

std::optional<bool> task_background_flag(std::string_view arguments_json)
{
  if (!looks_like_json_object(arguments_json))
    return std::nullopt;
  if (auto mode = ava::core::json::string_field(arguments_json, "mode"))
  {
    if (*mode == "background")
      return true;
    if (*mode == "foreground")
      return false;
  }
  if (ava::core::json::field_value_start(arguments_json, "background"))
    return allowlisted_bool(arguments_json, "background");
  return std::nullopt;
}

std::string status_primary(ToolTimelineStatus status)
{
  switch (status)
  {
    case ToolTimelineStatus::Running:
      return "running";
    case ToolTimelineStatus::Success:
      return {};
    case ToolTimelineStatus::Canceled:
      return "canceled";
    case ToolTimelineStatus::Error:
      return "failed";
  }
  return {};
}

TaskJobCardPresentation present_task(ToolTimelineItem const& item)
{
  TaskJobCardPresentation presentation;
  presentation.kind = TaskJobToolKind::Task;
  presentation.display_name = "task";

  // Production task serializer allowlist only (no invented duration fields).
  auto description = allowlisted_string(item.arguments_json, "description");
  if (!description)
    description = allowlisted_string(item.result_json, "description");
  auto subagent_type = allowlisted_string(item.arguments_json, "subagent_type");
  if (!subagent_type)
    subagent_type = allowlisted_string(item.result_json, "subagent_type");
  auto const state = allowlisted_string(item.result_json, "state");
  auto const tool_calls = allowlisted_integer(item.result_json, "tool_calls");
  auto const task_result = allowlisted_string(item.result_json, "task_result");
  auto const background = task_background_flag(item.arguments_json);

  if (subagent_type)
  {
    if (auto human = humanize_identifier(*subagent_type); !human.empty())
      presentation.display_name = std::move(human);
  }

  auto const safe_description = description ? sanitize_plain_field(*description, kMaxDescriptionDisplayBytes) : std::string{};
  auto const state_lower = state ? lower_ascii(*state) : std::string{};
  bool const background_running = (background && *background) || state_lower == "running";

  std::string primary;
  if (item.status == ToolTimelineStatus::Running)
  {
    append_part(primary, safe_description);
    append_part(primary, background && *background ? "running in background" : "running");
  }
  else if (item.status == ToolTimelineStatus::Success && background_running)
  {
    // Background launch completed successfully while the child is still running.
    append_part(primary, safe_description);
    append_part(primary, "running in background");
  }
  else if (item.status == ToolTimelineStatus::Canceled || item.status == ToolTimelineStatus::Error)
  {
    append_part(primary, safe_description);
    append_part(primary, status_primary(item.status));
  }
  else
  {
    // Completed (or other success terminal states): type + description + tool count.
    append_part(primary, safe_description);
    if (tool_calls && *tool_calls > 0)
    {
      auto const count = *tool_calls;
      append_part(primary, std::to_string(count) + (count == 1 ? " tool" : " tools"));
    }
    if (primary.empty())
      append_part(primary, "completed");
  }
  presentation.primary = std::move(primary);

  if (task_result)
    presentation.expanded_detail = sanitize_expanded_body(*task_result);

  // Searchable corpus excludes task_result unless the caller adds it for explicit expansion.
  append_search_token(presentation.searchable_text, presentation.display_name);
  append_search_token(presentation.searchable_text, "task");
  append_search_token(presentation.searchable_text, safe_description);
  if (subagent_type)
  {
    append_search_token(presentation.searchable_text, sanitize_plain_field(*subagent_type, kMaxHumanLabelBytes));
    append_search_token(presentation.searchable_text, presentation.display_name);
  }
  if (background && *background)
    append_search_token(presentation.searchable_text, "background");
  if (auto mode = allowlisted_string(item.arguments_json, "mode"))
    append_search_token(presentation.searchable_text, sanitize_plain_field(*mode, kMaxHumanLabelBytes));
  if (state)
    append_search_token(presentation.searchable_text, sanitize_plain_field(*state, kMaxHumanLabelBytes));
  if (tool_calls && *tool_calls > 0)
    append_search_token(presentation.searchable_text, std::to_string(*tool_calls) + " tools");
  append_search_token(presentation.searchable_text, presentation.primary);
  return presentation;
}

std::string humanize_job_action(std::string_view action)
{
  auto const lowered = lower_ascii(action);
  if (lowered == "list" || lowered == "status" || lowered == "wait" || lowered == "result" || lowered == "cancel")
    return std::string(lowered);
  if (auto human = humanize_identifier(action); !human.empty())
    return lower_ascii(human);
  return {};
}

std::string humanize_job_state(std::string_view state)
{
  auto const lowered = lower_ascii(state);
  if (lowered == "starting" || lowered == "running" || lowered == "completed" || lowered == "failed" || lowered == "canceled" || lowered == "interrupted")
    return std::string(lowered);
  if (auto human = humanize_identifier(state); !human.empty())
    return lower_ascii(human);
  return {};
}

TaskJobCardPresentation present_job(ToolTimelineItem const& item)
{
  TaskJobCardPresentation presentation;
  presentation.kind = TaskJobToolKind::Job;
  presentation.display_name = "job";

  auto action = allowlisted_string(item.arguments_json, "action");
  auto const state = allowlisted_string(item.result_json, "state");
  auto const mode = allowlisted_string(item.result_json, "mode");
  auto const tool_calls = allowlisted_integer(item.result_json, "tool_calls");
  auto const total_jobs = allowlisted_integer(item.result_json, "total_jobs");
  auto const was_promoted = looks_like_json_object(item.result_json) && ava::core::json::field_value_start(item.result_json, "was_promoted")
                                ? std::optional<bool>{allowlisted_bool(item.result_json, "was_promoted")}
                                : std::nullopt;
  auto const result_object = object_field_if_object(item.result_json, "result");
  std::optional<std::string> result_summary;
  std::optional<std::string> result_message;
  std::optional<std::string> result_status;
  if (result_object)
  {
    result_summary = allowlisted_string(*result_object, "summary");
    result_message = allowlisted_string(*result_object, "message");
    result_status = allowlisted_string(*result_object, "status");
  }

  auto const safe_action = action ? humanize_job_action(*action) : std::string{};
  auto const safe_state = state ? humanize_job_state(*state) : std::string{};
  std::string mode_label;
  if (mode)
  {
    auto const lowered = lower_ascii(*mode);
    if (lowered == "foreground" || lowered == "background")
      mode_label = lowered;
  }

  std::string primary;
  if (item.status == ToolTimelineStatus::Running)
  {
    append_part(primary, safe_action);
    append_part(primary, "running");
  }
  else if (item.status == ToolTimelineStatus::Canceled)
  {
    append_part(primary, safe_action);
    append_part(primary, "canceled");
  }
  else if (item.status == ToolTimelineStatus::Error)
  {
    append_part(primary, safe_action);
    append_part(primary, "failed");
  }
  else
  {
    append_part(primary, safe_action);
    if (!safe_state.empty())
      append_part(primary, safe_state);
    else if (result_status)
    {
      if (auto human = humanize_job_state(*result_status); !human.empty())
        append_part(primary, human);
    }
    if (!mode_label.empty())
      append_part(primary, mode_label);
    if (was_promoted && *was_promoted)
      append_part(primary, "promoted");
    if (tool_calls && *tool_calls > 0)
    {
      auto const count = *tool_calls;
      append_part(primary, std::to_string(count) + (count == 1 ? " tool" : " tools"));
    }
    if (safe_action == "list" && total_jobs && *total_jobs >= 0)
    {
      auto const count = *total_jobs;
      append_part(primary, std::to_string(count) + (count == 1 ? " job" : " jobs"));
    }
    if (result_summary)
    {
      if (auto text = sanitize_plain_field(*result_summary, kMaxDescriptionDisplayBytes); !text.empty())
        append_part(primary, text);
    }
    else if (result_message)
    {
      if (auto text = sanitize_plain_field(*result_message, kMaxDescriptionDisplayBytes); !text.empty())
        append_part(primary, text);
    }
    if (primary.empty())
      append_part(primary, "ok");
  }
  presentation.primary = std::move(primary);

  append_search_token(presentation.searchable_text, "job");
  append_search_token(presentation.searchable_text, safe_action);
  append_search_token(presentation.searchable_text, safe_state);
  append_search_token(presentation.searchable_text, mode_label);
  if (was_promoted && *was_promoted)
    append_search_token(presentation.searchable_text, "promoted");
  if (tool_calls && *tool_calls > 0)
    append_search_token(presentation.searchable_text, std::to_string(*tool_calls) + " tools");
  if (safe_action == "list" && total_jobs && *total_jobs >= 0)
    append_search_token(presentation.searchable_text, std::to_string(*total_jobs) + " jobs");
  if (result_summary)
    append_search_token(presentation.searchable_text, sanitize_plain_field(*result_summary, kMaxDescriptionDisplayBytes));
  if (result_message)
    append_search_token(presentation.searchable_text, sanitize_plain_field(*result_message, kMaxDescriptionDisplayBytes));
  if (result_status)
    append_search_token(presentation.searchable_text, humanize_job_state(*result_status));
  append_search_token(presentation.searchable_text, presentation.primary);
  return presentation;
}

}  // namespace

TaskJobToolKind task_job_tool_kind(ToolTimelineItem const& item) noexcept
{
  // Exact reserved tool names only — do not accept case spoofs.
  if (item.name == "task")
    return TaskJobToolKind::Task;
  if (item.name == "job")
    return TaskJobToolKind::Job;
  return TaskJobToolKind::None;
}

bool is_task_or_job_tool(ToolTimelineItem const& item) noexcept
{
  return task_job_tool_kind(item) != TaskJobToolKind::None;
}

bool task_job_card_is_quiet_poll(ToolTimelineItem const& item)
{
  if (item.name != "job" || item.status == ToolTimelineStatus::Error || item.status == ToolTimelineStatus::Canceled)
    return false;

  auto const action = allowlisted_string(item.arguments_json, "action");
  if (!action || (*action != "list" && *action != "status" && *action != "wait"))
    return false;

  if (item.status == ToolTimelineStatus::Running)
    return true;
  if (item.status != ToolTimelineStatus::Success || !looks_like_json_object(item.result_json))
    return false;

  if (*action == "list")
  {
    auto const jobs = ava::core::json::strict_objects_in_array_field(item.result_json, "jobs");
    auto const total_jobs = allowlisted_integer(item.result_json, "total_jobs");
    return jobs && total_jobs && *total_jobs >= 0;
  }

  auto const state = allowlisted_string(item.result_json, "state");
  return state && (*state == "starting" || *state == "running");
}

bool task_job_explicitly_expanded(ToolTimelineItem const& item) noexcept
{
  return item.details_visible.has_value() && *item.details_visible;
}

TaskJobCardPresentation task_job_card_presentation(ToolTimelineItem const& item)
{
  switch (task_job_tool_kind(item))
  {
    case TaskJobToolKind::Task:
      return present_task(item);
    case TaskJobToolKind::Job:
      return present_job(item);
    case TaskJobToolKind::None:
      break;
  }
  return {};
}

bool task_job_card_matches_query(ToolTimelineItem const& item, TaskJobCardPresentation const& presentation, std::string_view query)
{
  if (presentation.kind == TaskJobToolKind::None)
    return false;
  if (query.empty())
    return true;
  auto haystack = presentation.searchable_text;
  // task_result participates in search only for explicitly expanded items.
  if (task_job_explicitly_expanded(item) && !presentation.expanded_detail.empty())
  {
    if (!haystack.empty())
      haystack.push_back('\n');
    haystack += presentation.expanded_detail;
  }
  auto const lowered_haystack = lower_ascii(haystack);
  auto const needle = lower_ascii(query);
  return !needle.empty() && lowered_haystack.find(needle) != std::string::npos;
}

std::string task_job_card_copy_text(ToolTimelineItem const& item, TaskJobCardPresentation const& presentation)
{
  if (presentation.kind == TaskJobToolKind::None || task_job_card_is_quiet_poll(item))
    return {};

  std::string output;
  auto append_block = [&](std::string_view label, std::string_view text) {
    if (text.empty())
      return;
    output += std::string(label);
    output += ": ";
    output += sanitize_terminal_text(text);
    output += '\n';
  };

  append_block("tool", presentation.display_name);
  append_block("status", ava::tui::to_string(item.status));

  if (presentation.kind == TaskJobToolKind::Task)
  {
    if (auto description = allowlisted_string(item.arguments_json, "description"))
      append_block("description", sanitize_plain_field(*description, kMaxDescriptionDisplayBytes));
    else if (auto description = allowlisted_string(item.result_json, "description"))
      append_block("description", sanitize_plain_field(*description, kMaxDescriptionDisplayBytes));
    if (auto type = allowlisted_string(item.arguments_json, "subagent_type"))
      append_block("type", sanitize_plain_field(*type, kMaxHumanLabelBytes));
    else if (auto type = allowlisted_string(item.result_json, "subagent_type"))
      append_block("type", sanitize_plain_field(*type, kMaxHumanLabelBytes));
    if (auto mode = allowlisted_string(item.arguments_json, "mode"))
      append_block("mode", sanitize_plain_field(*mode, kMaxHumanLabelBytes));
    else if (auto background = task_background_flag(item.arguments_json))
      append_block("mode", *background ? "background" : "foreground");
    if (auto state = allowlisted_string(item.result_json, "state"))
      append_block("state", sanitize_plain_field(*state, kMaxHumanLabelBytes));
    if (auto tool_calls = allowlisted_integer(item.result_json, "tool_calls"); tool_calls && *tool_calls > 0)
      append_block("tools", std::to_string(*tool_calls));
    // Explicit expansion only — default Rich/collapsed copy omits task_result.
    if (task_job_explicitly_expanded(item) && !presentation.expanded_detail.empty())
      append_block("task_result", presentation.expanded_detail);
  }
  else
  {
    if (auto action = allowlisted_string(item.arguments_json, "action"))
      append_block("action", humanize_job_action(*action));
    if (auto state = allowlisted_string(item.result_json, "state"))
      append_block("state", humanize_job_state(*state));
    if (auto mode = allowlisted_string(item.result_json, "mode"))
    {
      auto const lowered = lower_ascii(*mode);
      if (lowered == "foreground" || lowered == "background")
        append_block("mode", lowered);
    }
    if (looks_like_json_object(item.result_json) && ava::core::json::field_value_start(item.result_json, "was_promoted"))
      append_block("promoted", allowlisted_bool(item.result_json, "was_promoted") ? "true" : "false");
    if (auto tool_calls = allowlisted_integer(item.result_json, "tool_calls"); tool_calls && *tool_calls > 0)
      append_block("tools", std::to_string(*tool_calls));
    if (auto total_jobs = allowlisted_integer(item.result_json, "total_jobs"); total_jobs && *total_jobs >= 0)
      append_block("jobs", std::to_string(*total_jobs));
    if (auto result_object = object_field_if_object(item.result_json, "result"))
    {
      if (auto summary = allowlisted_string(*result_object, "summary"))
        append_block("summary", sanitize_plain_field(*summary, kMaxDescriptionDisplayBytes));
      if (auto message = allowlisted_string(*result_object, "message"))
        append_block("message", sanitize_plain_field(*message, kMaxDescriptionDisplayBytes));
    }
  }

  append_block("summary", presentation.primary);
  if (!output.empty() && output.back() == '\n')
    output.pop_back();
  return output;
}

}  // namespace ava::tui::detail
