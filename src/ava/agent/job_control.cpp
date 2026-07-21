#include "sys.h"
#include "ava/agent/job_control.h"
#include "ava/core/json.h"

#include <algorithm>
#include <array>
#include <optional>
#include <string>
#include <string_view>

namespace ava::agent {
namespace {

std::string json_string(std::string_view value)
{
  return "\"" + ava::core::json::escape(value) + "\"";
}

void append_optional_string(std::string& json, std::string_view name, std::optional<std::string> const& value)
{
  json += ",\"" + std::string(name) + "\":";
  json += value ? json_string(*value) : "null";
}

std::string stable_terminal_message(SubagentJobSnapshot const& job)
{
  switch (job.execution)
  {
    case SubagentExecutionState::Completed:
      return "subagent job completed";
    case SubagentExecutionState::Failed:
      return "subagent job failed";
    case SubagentExecutionState::Canceled:
      return "subagent job was canceled";
    case SubagentExecutionState::Interrupted:
      return "subagent job was interrupted";
    case SubagentExecutionState::Starting:
    case SubagentExecutionState::Running:
      return "subagent job is running";
  }
  return "subagent job status is unavailable";
}

std::string_view stable_error_category(std::optional<std::string> const& category) noexcept
{
  constexpr std::array<std::string_view, 8> allowed = {"invalid_argument", "io", "not_found", "permission_denied", "provider", "session", "tool", "unknown"};
  if (category && std::ranges::find(allowed, *category) != allowed.end())
    return *category;
  return "unknown";
}

bool terminal(SubagentExecutionState state) noexcept
{
  return state == SubagentExecutionState::Completed || state == SubagentExecutionState::Failed || state == SubagentExecutionState::Canceled ||
         state == SubagentExecutionState::Interrupted;
}

}  // namespace

std::string safe_subagent_error_category(ava::core::Error const& error)
{
  return std::string(ava::core::to_string(error.category()));
}

std::string safe_subagent_error_message(ava::core::Error const&)
{
  return "subagent job failed";
}

std::string public_job_snapshot_json(SubagentCoordinatorJobSnapshot const& snapshot, PublicJobContent content)
{
  auto const& job = snapshot.job;
  std::string json = "{\"schema_version\":1";
  json += ",\"job_id\":" + json_string(job.identity.job_id);
  json += ",\"task_id\":" + json_string(job.identity.task_id);
  json += ",\"parent_session_id\":" + json_string(job.identity.parent_session_id);
  json += ",\"child_session_id\":" + json_string(job.identity.child_session_id);
  json += ",\"delivery_id\":" + json_string(job.identity.delivery_id);
  json += ",\"mode\":" + json_string(to_string(job.mode));
  json += ",\"state\":" + json_string(to_string(job.execution));
  json += ",\"delivery_state\":" + json_string(to_string(job.delivery));
  json += ",\"was_promoted\":" + std::string(job.was_promoted ? "true" : "false");
  json += ",\"cancel_requested\":" + std::string(job.cancel_requested ? "true" : "false");
  json += ",\"timed_out\":" + std::string(snapshot.timed_out ? "true" : "false");
  json += ",\"started_at\":" + json_string(job.started_at);
  json += ",\"updated_at\":" + json_string(job.updated_at);
  append_optional_string(json, "promoted_at", job.promoted_at);
  append_optional_string(json, "cancel_requested_at", job.cancel_requested_at);
  append_optional_string(json, "terminal_at", job.terminal_at);
  append_optional_string(json, "delivery_pending_at", job.delivery_pending_at);
  append_optional_string(json, "last_delivery_attempt_at", job.last_delivery_attempt_at);
  append_optional_string(json, "delivery_acknowledged_at", job.delivery_acknowledged_at);
  json += ",\"delivery_attempts\":" + std::to_string(job.delivery_attempts);
  json += ",\"summary_truncated\":" + std::string(job.summary_truncated ? "true" : "false");
  json += ",\"error_truncated\":" + std::string(job.error_truncated ? "true" : "false");
  json += ",\"stop_reason_truncated\":" + std::string(job.stop_reason_truncated ? "true" : "false");
  json += ",\"provider_iterations\":" + std::to_string(job.provider_iterations);
  json += ",\"tool_calls\":" + std::to_string(job.tool_calls);
  json += ",\"tool_iterations\":" + std::to_string(job.tool_iterations);
  if (content == PublicJobContent::IncludeTerminalResult && terminal(job.execution))
  {
    json += ",\"result\":{\"status\":" + json_string(to_string(job.execution));
    if (job.execution == SubagentExecutionState::Completed)
      json += ",\"summary\":" + json_string(job.summary.value_or(""));
    else
    {
      json += ",\"message\":" + json_string(stable_terminal_message(job));
      if (job.execution == SubagentExecutionState::Failed)
        json += ",\"error_category\":" + json_string(stable_error_category(job.error_category));
    }
    json += '}';
  }
  json += '}';
  return json;
}

std::string public_job_list_json(std::vector<SubagentCoordinatorJobSnapshot> const& snapshots)
{
  auto const first = snapshots.size() > kMaxPublicJobListEntries ? snapshots.size() - kMaxPublicJobListEntries : 0;
  std::string json = "{\"schema_version\":1,\"jobs\":[";
  bool comma = false;
  for (std::size_t index = first; index < snapshots.size(); ++index)
  {
    if (comma)
      json += ',';
    comma = true;
    json += public_job_snapshot_json(snapshots[index], PublicJobContent::OmitTerminalContent);
  }
  json += "],\"total_jobs\":" + std::to_string(snapshots.size());
  json += ",\"truncated\":" + std::string(first == 0 ? "false" : "true") + '}';
  return json;
}

std::string format_public_job_snapshot(SubagentCoordinatorJobSnapshot const& snapshot, PublicJobContent content)
{
  return public_job_snapshot_json(snapshot, content);
}

std::string format_public_job_list(std::vector<SubagentCoordinatorJobSnapshot> const& snapshots)
{
  return public_job_list_json(snapshots);
}

}  // namespace ava::agent
