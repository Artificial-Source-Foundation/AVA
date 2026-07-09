#include "ava/agent/tool_scheduler.h"
#include "ava/core/error.h"

#include "tests/support/test_harness.h"

#include <array>
#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace {

using ava::agent::ToolScheduleEligibility;

ava::agent::ProviderToolCall call(std::string_view id, std::string_view name)
{
  return ava::agent::ProviderToolCall{.id = std::string(id), .name = std::string(name), .arguments_json = "{}"};
}

ava::agent::ToolDispatchResult successful_result(ava::agent::ToolScheduleSlot const& slot)
{
  return ava::agent::ToolDispatchResult{.call_id = slot.call.id, .name = slot.call.name, .success = true, .result_text = "ok"};
}

void expect_classification(ava::agent::ToolScheduleClassification const& actual,
                           ToolScheduleEligibility eligibility,
                           std::string_view reason,
                           std::string_view message)
{
  expect(actual.eligibility == eligibility && actual.reason == reason, std::string(message));
}

void expect_builtin_classification(std::string_view name, ToolScheduleEligibility eligibility, std::string_view reason)
{
  auto const actual = ava::agent::classify_tool_for_scheduling(call("call", name), ava::agent::builtin_tool_metadata());
  expect_classification(actual, eligibility, reason,
                        std::string("scheduler classifies builtin ") + std::string(name) + " as " + std::string(reason));
}

ava::agent::ToolMetadata synthetic_metadata(std::string_view name, std::string_view permission_category)
{
  return ava::agent::ToolMetadata{.name = name,
                                  .description = "synthetic tool",
                                  .schema_json = "{}",
                                  .permission_category = permission_category,
                                  .output_bound_summary = "bounded",
                                  .execution_mode = "synchronous",
                                  .event_rendering_hint = "synthetic",
                                  .description_family = std::nullopt};
}

ava::agent::ToolScheduleSlot schedule_slot(std::size_t provider_index, std::string_view id, std::string_view name)
{
  return ava::agent::ToolScheduleSlot{.provider_index = provider_index,
                                      .call = call(id, name),
                                      .classification = ava::agent::ToolScheduleClassification{
                                          .eligibility = ToolScheduleEligibility::Barrier, .reason = "test"}};
}

void test_classify_builtin_tools_for_scheduling()
{
  expect_builtin_classification("read_file", ToolScheduleEligibility::ReadOnlyCandidate, "read_search_candidate");
  expect_builtin_classification("list_directory", ToolScheduleEligibility::ReadOnlyCandidate, "read_search_candidate");
  expect_builtin_classification("glob", ToolScheduleEligibility::ReadOnlyCandidate, "read_search_candidate");
  expect_builtin_classification("grep", ToolScheduleEligibility::ReadOnlyCandidate, "read_search_candidate");

  expect_builtin_classification("write_file", ToolScheduleEligibility::Barrier, "mutation");
  expect_builtin_classification("edit_file", ToolScheduleEligibility::Barrier, "mutation");
  expect_builtin_classification("apply_patch", ToolScheduleEligibility::Barrier, "mutation");
  expect_builtin_classification("bash", ToolScheduleEligibility::Barrier, "shell");
  expect_builtin_classification("question", ToolScheduleEligibility::Barrier, "user_interaction");
  expect_builtin_classification("task", ToolScheduleEligibility::Barrier, "subagent");
  expect_builtin_classification("skill", ToolScheduleEligibility::Barrier, "skill");

  expect_builtin_classification("lsp_diagnostics", ToolScheduleEligibility::Barrier, "lsp");
  expect_builtin_classification("lsp_document_symbols", ToolScheduleEligibility::Barrier, "lsp");
  expect_builtin_classification("lsp_workspace_symbols", ToolScheduleEligibility::Barrier, "lsp");
  expect_builtin_classification("lsp_definition", ToolScheduleEligibility::Barrier, "lsp");
  expect_builtin_classification("lsp_references", ToolScheduleEligibility::Barrier, "lsp");

  expect_builtin_classification("webfetch", ToolScheduleEligibility::Deferred, "network");
  expect_builtin_classification("websearch", ToolScheduleEligibility::Deferred, "network");
  expect_builtin_classification("not_registered", ToolScheduleEligibility::Deferred, "unknown_tool");
}

void test_classify_brokered_external_metadata_for_scheduling()
{
  std::array metadata{
      synthetic_metadata("plugin_tool", "plugin.tool.call"),
      synthetic_metadata("mcp_tool", "mcp.tool.call"),
  };

  expect_classification(ava::agent::classify_tool_for_scheduling(call("plugin_call", "plugin_tool"), metadata),
                        ToolScheduleEligibility::Deferred,
                        "plugin_brokered_external",
                        "scheduler defers plugin brokered external tools");
  expect_classification(ava::agent::classify_tool_for_scheduling(call("mcp_call", "mcp_tool"), metadata),
                        ToolScheduleEligibility::Deferred,
                        "mcp_brokered_external",
                        "scheduler defers MCP brokered external tools");
}

void test_build_sequential_tool_schedule_preserves_provider_order()
{
  std::vector calls{
      call("call_0", "grep"),
      call("call_1", "bash"),
      call("call_2", "webfetch"),
  };

  auto const schedule = ava::agent::build_sequential_tool_schedule(calls, ava::agent::builtin_tool_metadata());

  expect(schedule.size() == calls.size(), "sequential tool schedule keeps one slot per provider call");
  expect(schedule.size() == 3 && schedule[0].provider_index == 0 && schedule[0].call.id == "call_0" && schedule[0].call.name == "grep",
         "sequential tool schedule keeps the first provider call at index 0");
  expect(schedule.size() == 3 && schedule[1].provider_index == 1 && schedule[1].call.id == "call_1" && schedule[1].call.name == "bash",
         "sequential tool schedule keeps the second provider call at index 1");
  expect(schedule.size() == 3 && schedule[2].provider_index == 2 && schedule[2].call.id == "call_2" && schedule[2].call.name == "webfetch",
         "sequential tool schedule keeps the third provider call at index 2");
}

void test_run_sequential_tool_schedule_executes_in_order()
{
  std::vector schedule{
      schedule_slot(0, "call_0", "first"),
      schedule_slot(1, "call_1", "second"),
      schedule_slot(2, "call_2", "third"),
  };
  std::vector<std::size_t> execution_order;

  auto outcomes = ava::agent::run_sequential_tool_schedule(schedule, [&](ava::agent::ToolScheduleSlot const& slot) {
    execution_order.push_back(slot.provider_index);
    return successful_result(slot);
  });

  expect(outcomes.has_value(), outcomes ? "sequential scheduler executor succeeds" : outcomes.error().format());
  expect(execution_order == std::vector<std::size_t>{0, 1, 2}, "sequential scheduler invokes the executor in provider order");
  expect(outcomes && outcomes->size() == 3 && (*outcomes)[0].slot.provider_index == 0 && (*outcomes)[1].slot.provider_index == 1 &&
             (*outcomes)[2].slot.provider_index == 2,
         "sequential scheduler returns outcomes in provider order");
}

void test_run_sequential_tool_schedule_short_circuits_on_executor_error()
{
  std::vector schedule{
      schedule_slot(0, "call_0", "first"),
      schedule_slot(1, "call_1", "second"),
      schedule_slot(2, "call_2", "third"),
  };
  std::vector<std::size_t> execution_order;

  auto outcomes = ava::agent::run_sequential_tool_schedule(schedule, [&](ava::agent::ToolScheduleSlot const& slot) {
    execution_order.push_back(slot.provider_index);
    if (slot.provider_index == 1)
      return ava::core::Result<ava::agent::ToolDispatchResult>(std::unexpected(
          ava::core::Error(ava::core::ErrorCategory::Tool, "scheduler executor failed")));
    return ava::core::Result<ava::agent::ToolDispatchResult>(successful_result(slot));
  });

  expect(!outcomes && outcomes.error().message() == "scheduler executor failed", "sequential scheduler returns the first executor error");
  expect(execution_order == std::vector<std::size_t>{0, 1}, "sequential scheduler does not run slots after an executor error");
}

}  // namespace

void run_tool_scheduler_tests()
{
  test_classify_builtin_tools_for_scheduling();
  test_classify_brokered_external_metadata_for_scheduling();
  test_build_sequential_tool_schedule_preserves_provider_order();
  test_run_sequential_tool_schedule_executes_in_order();
  test_run_sequential_tool_schedule_short_circuits_on_executor_error();
}
