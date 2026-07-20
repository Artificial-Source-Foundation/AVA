#include "sys.h"
#include "ava/agent/subagent_job.h"

#include <algorithm>

namespace ava::agent {
namespace {

ava::core::Error invalid_enum(std::string_view field, std::string_view value)
{
  auto error = ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "invalid subagent job contract enum value");
  error.with_context("field", std::string(field)).with_context("value", std::string(value));
  return error;
}

}  // namespace

SubagentJobSnapshot const* SubagentJobProjection::find(std::string_view job_id) const noexcept
{
  auto const match = std::ranges::find_if(jobs, [&](SubagentJobSnapshot const& job) { return job.identity.job_id == job_id; });
  return match == jobs.end() ? nullptr : &*match;
}

std::string_view to_string(SubagentJobMode value) noexcept
{
  switch (value)
  {
    case SubagentJobMode::Foreground:
      return "foreground";
    case SubagentJobMode::Background:
      return "background";
  }
  return "unknown";
}

std::string_view to_string(SubagentExecutionState value) noexcept
{
  switch (value)
  {
    case SubagentExecutionState::Starting:
      return "starting";
    case SubagentExecutionState::Running:
      return "running";
    case SubagentExecutionState::Completed:
      return "completed";
    case SubagentExecutionState::Failed:
      return "failed";
    case SubagentExecutionState::Canceled:
      return "canceled";
    case SubagentExecutionState::Interrupted:
      return "interrupted";
  }
  return "unknown";
}

std::string_view to_string(SubagentDeliveryState value) noexcept
{
  switch (value)
  {
    case SubagentDeliveryState::Direct:
      return "direct";
    case SubagentDeliveryState::Pending:
      return "pending";
    case SubagentDeliveryState::Attempting:
      return "attempting";
    case SubagentDeliveryState::Acknowledged:
      return "acknowledged";
  }
  return "unknown";
}

std::string_view to_string(SubagentTerminalState value) noexcept
{
  switch (value)
  {
    case SubagentTerminalState::Completed:
      return "completed";
    case SubagentTerminalState::Failed:
      return "failed";
    case SubagentTerminalState::Canceled:
      return "canceled";
  }
  return "unknown";
}

ava::core::Result<SubagentJobMode> parse_subagent_job_mode(std::string_view value)
{
  if (value == "foreground")
    return SubagentJobMode::Foreground;
  if (value == "background")
    return SubagentJobMode::Background;
  return std::unexpected(invalid_enum("mode", value));
}

ava::core::Result<SubagentTerminalState> parse_subagent_terminal_state(std::string_view value)
{
  if (value == "completed")
    return SubagentTerminalState::Completed;
  if (value == "failed")
    return SubagentTerminalState::Failed;
  if (value == "canceled")
    return SubagentTerminalState::Canceled;
  return std::unexpected(invalid_enum("terminal_state", value));
}

}  // namespace ava::agent
