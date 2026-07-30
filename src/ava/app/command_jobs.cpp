#include "sys.h"
#include "command_jobs.h"
#include "runtime/Session.h"

#include "ava/agent/job_control.h"
#include "ava/app/command_format.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <chrono>
#include <string>
#include <string_view>
#include <vector>

namespace ava::app {
namespace {

std::vector<std::string_view> split(std::string_view text)
{
  std::vector<std::string_view> parts;
  while (true)
  {
    auto const first = text.find_first_not_of(" \t\r\n");
    if (first == std::string_view::npos)
      break;
    text.remove_prefix(first);
    auto const end = text.find_first_of(" \t\r\n");
    parts.push_back(text.substr(0, end));
    if (end == std::string_view::npos)
      break;
    text.remove_prefix(end);
  }
  return parts;
}

bool safe_job_id(std::string_view id)
{
  if (id.empty() || id.size() > ava::agent::kMaxPublicJobIdBytes)
    return false;
  return std::ranges::all_of(id, [](char ch) {
    auto const byte = static_cast<unsigned char>(ch);
    return byte >= 0x20U && byte != 0x7FU && ch != ' ';
  });
}

ava::core::Result<std::chrono::milliseconds> wait_timeout(std::string_view text)
{
  if (text.empty())
    return std::chrono::milliseconds(ava::agent::kDefaultPublicJobWaitTimeoutMs);
  long long value = 0;
  auto const parsed = std::from_chars(text.data(), text.data() + text.size(), value);
  if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size() || value <= 0)
    return std::unexpected(ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "jobs timeout_ms must be a positive integer"));
  return std::chrono::milliseconds(std::min(value, ava::agent::kMaxPublicJobWaitTimeoutMs));
}

std::string usage()
{
  return "usage: /jobs | /jobs show <id> | /jobs wait <id> [timeout_ms] | /jobs result <id> | /jobs cancel <id> | /jobs promote <id>";
}

void add_error(CommandResult& result, ava::core::Error const& error)
{
  result.output.push_back("jobs: " + error.message());
}

std::string_view human_execution_state(ava::agent::SubagentExecutionState state) noexcept
{
  switch (state)
  {
    case ava::agent::SubagentExecutionState::Starting:
      return "Starting";
    case ava::agent::SubagentExecutionState::Running:
      return "Running";
    case ava::agent::SubagentExecutionState::Completed:
      return "Completed";
    case ava::agent::SubagentExecutionState::Failed:
      return "Failed";
    case ava::agent::SubagentExecutionState::Canceled:
      return "Canceled";
    case ava::agent::SubagentExecutionState::Interrupted:
      return "Interrupted";
  }
  return "Unknown";
}

std::string_view human_job_mode(ava::agent::SubagentJobMode mode) noexcept
{
  switch (mode)
  {
    case ava::agent::SubagentJobMode::Foreground:
      return "Foreground";
    case ava::agent::SubagentJobMode::Background:
      return "Background";
  }
  return "Unknown";
}

std::string_view human_delivery_state(ava::agent::SubagentDeliveryState state) noexcept
{
  switch (state)
  {
    case ava::agent::SubagentDeliveryState::Direct:
      return "direct delivery";
    case ava::agent::SubagentDeliveryState::Pending:
      return "delivery pending";
    case ava::agent::SubagentDeliveryState::Attempting:
      return "delivery attempting";
    case ava::agent::SubagentDeliveryState::Acknowledged:
      return "delivery acknowledged";
  }
  return "delivery unknown";
}

bool terminal_execution(ava::agent::SubagentExecutionState state) noexcept
{
  return state == ava::agent::SubagentExecutionState::Completed || state == ava::agent::SubagentExecutionState::Failed ||
         state == ava::agent::SubagentExecutionState::Canceled || state == ava::agent::SubagentExecutionState::Interrupted;
}

std::string display_title_text(ava::agent::SubagentJobSnapshot const& job)
{
  if (!job.display_title.empty())
    return sanitize_inline_text(job.display_title);
  return {};
}

std::string primary_job_line(ava::agent::SubagentJobSnapshot const& job)
{
  std::string line;
  line += human_execution_state(job.execution);
  line += " · ";
  line += human_job_mode(job.mode);
  auto const title = display_title_text(job);
  if (!title.empty())
  {
    line += " · ";
    line += title;
  }
  return line;
}

void append_detail_fragment(std::string& details, std::string_view fragment)
{
  if (fragment.empty())
    return;
  if (!details.empty())
    details += " · ";
  details += fragment;
}

std::string secondary_job_details(ava::agent::SubagentCoordinatorJobSnapshot const& snapshot, bool include_job_id, bool include_result_content)
{
  auto const& job = snapshot.job;
  std::string details;
  if (include_job_id)
    append_detail_fragment(details, "id " + sanitize_inline_text(job.identity.job_id));
  if (!job.display_subagent_type.empty())
    append_detail_fragment(details, "type " + sanitize_inline_text(job.display_subagent_type));
  if (job.tool_calls > 0)
    append_detail_fragment(details, "tools " + std::to_string(job.tool_calls));
  if (!job.started_at.empty())
    append_detail_fragment(details, "started " + sanitize_inline_text(job.started_at));
  if (job.cancel_requested)
    append_detail_fragment(details, "cancel requested");
  if (job.was_promoted)
    append_detail_fragment(details, "promoted");
  if (snapshot.timed_out)
    append_detail_fragment(details, "wait timed out");
  if (job.delivery != ava::agent::SubagentDeliveryState::Direct)
    append_detail_fragment(details, human_delivery_state(job.delivery));
  if (include_result_content && terminal_execution(job.execution))
  {
    if (job.execution == ava::agent::SubagentExecutionState::Completed)
    {
      auto summary = sanitize_inline_text(job.summary.value_or(""));
      if (summary.empty())
        summary = "subagent job completed";
      append_detail_fragment(details, "result " + summary);
    }
    else if (job.execution == ava::agent::SubagentExecutionState::Failed)
    {
      auto message = sanitize_inline_text(job.error.value_or("subagent job failed"));
      if (message.empty())
        message = "subagent job failed";
      append_detail_fragment(details, "error " + message);
    }
    else if (job.execution == ava::agent::SubagentExecutionState::Canceled)
      append_detail_fragment(details, "canceled");
    else if (job.execution == ava::agent::SubagentExecutionState::Interrupted)
      append_detail_fragment(details, "interrupted");
  }
  return details;
}

std::string format_human_job_list(std::vector<ava::agent::SubagentCoordinatorJobSnapshot> const& snapshots)
{
  if (snapshots.empty())
    return "Jobs: none";

  auto const first = snapshots.size() > ava::agent::kMaxPublicJobListEntries ? snapshots.size() - ava::agent::kMaxPublicJobListEntries : 0;
  auto const shown = snapshots.size() - first;
  std::string output = "Jobs (" + std::to_string(shown) + "):";
  if (first != 0)
    output += " showing latest " + std::to_string(shown) + " of " + std::to_string(snapshots.size());
  for (std::size_t index = first; index < snapshots.size(); ++index)
  {
    auto const ordinal = index - first + 1;
    auto const& snapshot = snapshots[index];
    output += "\n";
    output += std::to_string(ordinal);
    output += ". ";
    output += primary_job_line(snapshot.job);
    // List rows keep the exact job id secondary so controls remain copyable;
    // ordinals are display-only and never accepted as control authority.
    auto const details = secondary_job_details(snapshot, true, false);
    if (!details.empty())
    {
      output += "\n   ";
      output += details;
    }
  }
  return output;
}

std::string format_human_job_snapshot(ava::agent::SubagentCoordinatorJobSnapshot const& snapshot, ava::agent::PublicJobContent content)
{
  std::string output = primary_job_line(snapshot.job);
  auto const details = secondary_job_details(snapshot, true, content == ava::agent::PublicJobContent::IncludeTerminalResult);
  if (!details.empty())
  {
    output += "\n  ";
    output += details;
  }
  return output;
}

}  // namespace

std::optional<std::string_view> active_jobs_command_arguments(std::string_view submitted) noexcept
{
  while (!submitted.empty() && std::isspace(static_cast<unsigned char>(submitted.front())) != 0) submitted.remove_prefix(1);
  while (!submitted.empty() && std::isspace(static_cast<unsigned char>(submitted.back())) != 0) submitted.remove_suffix(1);
  constexpr std::string_view command = "/jobs";
  if (submitted == command)
    return std::string_view{};
  if (!submitted.starts_with(command) || submitted.size() <= command.size() || std::isspace(static_cast<unsigned char>(submitted[command.size()])) == 0)
    return std::nullopt;
  auto arguments = submitted.substr(command.size() + 1);
  while (!arguments.empty() && std::isspace(static_cast<unsigned char>(arguments.front())) != 0) arguments.remove_prefix(1);
  return arguments;
}

ava::core::Result<CommandResult> run_jobs_command(runtime::Session& session, std::string_view arguments)
{
  return run_jobs_command(session.subagent_coordinator(), session.store.session_id(), arguments, false);
}

ava::core::Result<CommandResult> run_jobs_command(std::shared_ptr<ava::agent::SubagentCoordinator> const& coordinator, std::string_view parent_session_id,
                                                  std::string_view arguments, bool active_run)
{
  CommandResult result;
  result.handled = true;
  if (!coordinator)
  {
    result.output.push_back("jobs: job controls are unavailable");
    return result;
  }
  auto const parts = split(arguments);
  auto const owner = std::string(parent_session_id);
  if (parts.empty())
  {
    result.output.push_back(format_human_job_list(coordinator->list(owner)));
    return result;
  }
  auto const action = parts.front();
  std::size_t const expected_min = action == "wait" ? 2 : 2;
  std::size_t const expected_max = action == "wait" ? 3 : 2;
  if ((action != "show" && action != "wait" && action != "result" && action != "cancel" && action != "promote") || parts.size() < expected_min ||
      parts.size() > expected_max || !safe_job_id(parts[1]))
  {
    result.output.push_back(usage());
    return result;
  }

  if (active_run && (action == "wait" || action == "result"))
  {
    result.output.push_back("jobs: /jobs " + std::string(action) +
                            " is unavailable during an active run because it may block; use /jobs show <id> now or retry after the foreground turn finishes");
    return result;
  }

  ava::core::Result<ava::agent::SubagentCoordinatorJobSnapshot> snapshot =
      std::unexpected(ava::core::Error(ava::core::ErrorCategory::Tool, "jobs action was not dispatched"));
  auto content = ava::agent::PublicJobContent::OmitTerminalContent;
  if (action == "show")
    snapshot = coordinator->snapshot(owner, parts[1]);
  else if (action == "wait")
  {
    auto timeout = wait_timeout(parts.size() == 3 ? parts[2] : std::string_view{});
    if (!timeout)
    {
      add_error(result, timeout.error());
      return result;
    }
    snapshot = coordinator->wait(owner, parts[1], *timeout);
  }
  else if (action == "result")
  {
    snapshot = coordinator->result(owner, parts[1]);
    content = ava::agent::PublicJobContent::IncludeTerminalResult;
  }
  else if (action == "cancel")
    snapshot = coordinator->cancel(owner, parts[1]);
  else if (action == "promote")
    snapshot = coordinator->promote(owner, parts[1]);
  if (!snapshot)
  {
    add_error(result, snapshot.error());
    return result;
  }
  result.output.push_back(format_human_job_snapshot(*snapshot, content));
  return result;
}

}  // namespace ava::app
