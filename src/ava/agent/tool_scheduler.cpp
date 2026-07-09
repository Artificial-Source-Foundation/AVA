#include "sys.h"
#include "ava/agent/tool_scheduler.h"

#include <algorithm>
#include <string_view>
#include <utility>

namespace ava::agent {
namespace {

ToolScheduleClassification classification(ToolScheduleEligibility eligibility, std::string reason)
{
  return ToolScheduleClassification{.eligibility = eligibility, .reason = std::move(reason)};
}

ToolMetadata const* find_metadata(std::string_view name, std::span<ToolMetadata const> tool_metadata) noexcept
{
  auto const match = std::find_if(tool_metadata.begin(), tool_metadata.end(), [name](ToolMetadata const& metadata) { return metadata.name == name; });
  if (match == tool_metadata.end())
    return nullptr;
  return &*match;
}

bool starts_with(std::string_view value, std::string_view prefix) noexcept
{
  return value.size() >= prefix.size() && value.substr(0, prefix.size()) == prefix;
}

bool contains(std::string_view value, std::string_view needle) noexcept
{
  return value.find(needle) != std::string_view::npos;
}

bool has_description_family(ToolMetadata const& metadata, std::string_view family) noexcept
{
  return metadata.description_family && *metadata.description_family == family;
}

bool is_builtin_read_search_candidate(ToolMetadata const& metadata) noexcept
{
  if (metadata.execution_mode != std::string_view("synchronous"))
    return false;
  if (metadata.permission_category != std::string_view("read") && metadata.permission_category != std::string_view("search"))
    return false;
  return metadata.name == std::string_view("read_file") || metadata.name == std::string_view("list_directory") || metadata.name == std::string_view("glob") ||
         metadata.name == std::string_view("grep");
}

bool is_plugin_metadata(ToolMetadata const& metadata) noexcept
{
  return starts_with(metadata.permission_category, "plugin.") || contains(metadata.execution_mode, "plugin") || has_description_family(metadata, "plugin");
}

bool is_mcp_metadata(ToolMetadata const& metadata) noexcept
{
  return starts_with(metadata.permission_category, "mcp.") || contains(metadata.execution_mode, "mcp") || has_description_family(metadata, "mcp");
}

bool is_lsp_metadata(ToolMetadata const& metadata) noexcept
{
  return starts_with(metadata.permission_category, "lsp.") || has_description_family(metadata, "lsp");
}

}  // namespace

ToolScheduleClassification classify_tool_for_scheduling(ProviderToolCall const& call, std::span<ToolMetadata const> tool_metadata)
{
  auto const* metadata = find_metadata(call.name, tool_metadata);
  if (metadata == nullptr)
    return classification(ToolScheduleEligibility::Deferred, "unknown_tool");

  if (is_plugin_metadata(*metadata))
    return classification(ToolScheduleEligibility::Deferred, "plugin_brokered_external");
  if (is_mcp_metadata(*metadata))
    return classification(ToolScheduleEligibility::Deferred, "mcp_brokered_external");
  if (is_lsp_metadata(*metadata))
    return classification(ToolScheduleEligibility::Barrier, "lsp");
  if (metadata->permission_category == std::string_view("edit") || metadata->name == std::string_view("write_file") ||
      metadata->name == std::string_view("edit_file") || metadata->name == std::string_view("apply_patch"))
  {
    return classification(ToolScheduleEligibility::Barrier, "mutation");
  }
  if (metadata->permission_category == std::string_view("bash") || metadata->name == std::string_view("bash"))
    return classification(ToolScheduleEligibility::Barrier, "shell");
  if (metadata->permission_category == std::string_view("user") || metadata->execution_mode == std::string_view("synchronous_user_interaction") ||
      metadata->name == std::string_view("question"))
  {
    return classification(ToolScheduleEligibility::Barrier, "user_interaction");
  }
  if (metadata->permission_category == std::string_view("task") || metadata->execution_mode == std::string_view("subagent") ||
      metadata->name == std::string_view("task"))
  {
    return classification(ToolScheduleEligibility::Barrier, "subagent");
  }
  if (metadata->permission_category == std::string_view("skill") || metadata->name == std::string_view("skill") || has_description_family(*metadata, "skills"))
    return classification(ToolScheduleEligibility::Barrier, "skill");
  if (metadata->execution_mode == std::string_view("synchronous_network") || starts_with(metadata->permission_category, "network."))
    return classification(ToolScheduleEligibility::Deferred, "network");
  if (metadata->execution_mode == std::string_view("synchronous_process"))
    return classification(ToolScheduleEligibility::Barrier, "process");
  if (is_builtin_read_search_candidate(*metadata))
    return classification(ToolScheduleEligibility::ReadOnlyCandidate, "read_search_candidate");
  if (metadata->execution_mode != std::string_view("synchronous"))
    return classification(ToolScheduleEligibility::Barrier, "unreviewed_execution_mode");
  return classification(ToolScheduleEligibility::Barrier, "unreviewed_synchronous_tool");
}

std::vector<ToolScheduleSlot> build_sequential_tool_schedule(std::span<ProviderToolCall const> calls, std::span<ToolMetadata const> tool_metadata)
{
  std::vector<ToolScheduleSlot> schedule;
  schedule.reserve(calls.size());
  for (std::size_t index = 0; index < calls.size(); ++index)
  {
    schedule.push_back(
        ToolScheduleSlot{.provider_index = index, .call = calls[index], .classification = classify_tool_for_scheduling(calls[index], tool_metadata)});
  }
  return schedule;
}

ava::core::Result<std::vector<ToolScheduleOutcome>> run_sequential_tool_schedule(std::span<ToolScheduleSlot const> schedule,
                                                                                 ToolScheduleExecutor const& executor)
{
  if (!executor)
  {
    return std::unexpected(ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "tool schedule executor is required"));
  }

  std::vector<ToolScheduleOutcome> outcomes;
  outcomes.reserve(schedule.size());
  for (auto const& slot : schedule)
  {
    auto executed = executor(slot);
    if (!executed)
      return std::unexpected(std::move(executed.error()));
    auto result = std::move(*executed);
    outcomes.push_back(ToolScheduleOutcome{.slot = slot, .result = std::move(result)});
  }
  return outcomes;
}

}  // namespace ava::agent
