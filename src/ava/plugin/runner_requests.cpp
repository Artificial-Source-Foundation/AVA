#include <chrono>
#include <string>
#include <string_view>
#include <utility>

#include "ava/core/json.h"
#include "ava/plugin/protocol.h"
#include "ava/plugin/runner.h"
#include "ava/plugin/runner_support.h"

namespace ava::plugin {
namespace {

using detail::canceled_error;
using detail::is_canceled;
using detail::plugin_error;
using detail::protocol_error;

}  // namespace

ava::core::Result<PluginToolCallResult> PluginProcess::call_tool(std::string_view tool_name,
                                                                 std::string_view arguments_json,
                                                                 std::string_view call_id,
                                                                 CancelCallback cancel_requested)
{
  if (tool_name.empty()) {
    return std::unexpected(
        plugin_error(ava::core::ErrorCategory::InvalidArgument, "plugin tool name must not be empty", manifest_));
  }
  if (!ava::core::json::is_valid_object(arguments_json)) {
    auto error = plugin_error(ava::core::ErrorCategory::InvalidArgument, "plugin tool arguments must be a JSON object",
                              manifest_);
    error.with_context("tool", std::string(tool_name));
    return std::unexpected(std::move(error));
  }
  if (is_canceled(cancel_requested)) {
    auto error = canceled_error("plugin tool call canceled", manifest_);
    error.with_context("tool", std::string(tool_name));
    return std::unexpected(std::move(error));
  }

  auto const deadline = std::chrono::steady_clock::now() + options_.request_timeout;
  std::string const request_id =
      call_id.empty() ? "ava_" + std::to_string(next_request_id_++) : "ava_tool_" + std::string(call_id);
  std::string request =
      plugin_tool_call_request_json(request_id, tool_name, arguments_json, call_id, options_.workspace_dir.string());
  if (auto written = write_record(request, deadline, options_.request_timeout, "timed out writing plugin tool request",
                                  cancel_requested);
      !written) {
    return std::unexpected(std::move(written.error()));
  }

  auto record = read_record(deadline, options_.request_timeout, "timed out waiting for plugin tool result",
                            "plugin process closed stdout before plugin tool result", cancel_requested);
  if (!record) return std::unexpected(std::move(record.error()));
  auto result = parse_plugin_tool_result_response(*record, request_id);
  if (!result) {
    auto error = protocol_error("plugin tool result is malformed", manifest_);
    error.with_context("tool", std::string(tool_name));
    error.with_context("response", record->substr(0, 512));
    return std::unexpected(std::move(error));
  }
  return *result;
}

ava::core::Result<PluginCommandCallResult> PluginProcess::call_command(std::string_view command_name,
                                                                       std::string_view arguments_json,
                                                                       std::string_view call_id,
                                                                       CancelCallback cancel_requested)
{
  if (command_name.empty()) {
    return std::unexpected(
        plugin_error(ava::core::ErrorCategory::InvalidArgument, "plugin command name must not be empty", manifest_));
  }
  if (!ava::core::json::is_valid_object(arguments_json)) {
    auto error = plugin_error(ava::core::ErrorCategory::InvalidArgument,
                              "plugin command arguments must be a JSON object", manifest_);
    error.with_context("command", std::string(command_name));
    return std::unexpected(std::move(error));
  }
  if (is_canceled(cancel_requested)) {
    auto error = canceled_error("plugin command call canceled", manifest_);
    error.with_context("command", std::string(command_name));
    return std::unexpected(std::move(error));
  }

  auto const deadline = std::chrono::steady_clock::now() + options_.request_timeout;
  std::string const request_id =
      call_id.empty() ? "ava_" + std::to_string(next_request_id_++) : "ava_command_" + std::string(call_id);
  std::string request = plugin_command_call_request_json(request_id, command_name, arguments_json, call_id,
                                                         options_.workspace_dir.string());
  if (auto written = write_record(request, deadline, options_.request_timeout,
                                  "timed out writing plugin command request", cancel_requested);
      !written) {
    return std::unexpected(std::move(written.error()));
  }

  auto record = read_record(deadline, options_.request_timeout, "timed out waiting for plugin command result",
                            "plugin process closed stdout before plugin command result", cancel_requested);
  if (!record) return std::unexpected(std::move(record.error()));
  auto result = parse_plugin_command_result_response(*record, request_id);
  if (!result) {
    auto error = protocol_error("plugin command result is malformed", manifest_);
    error.with_context("command", std::string(command_name));
    error.with_context("response", record->substr(0, 512));
    return std::unexpected(std::move(error));
  }
  return *result;
}

ava::core::Result<PluginEventObserveResult> PluginProcess::observe_event(std::string_view event_name,
                                                                         std::string_view payload_json,
                                                                         std::string_view call_id,
                                                                         CancelCallback cancel_requested)
{
  if (event_name.empty()) {
    return std::unexpected(
        plugin_error(ava::core::ErrorCategory::InvalidArgument, "plugin event name must not be empty", manifest_));
  }
  if (!ava::core::json::is_valid_object(payload_json)) {
    auto error = plugin_error(ava::core::ErrorCategory::InvalidArgument, "plugin event payload must be a JSON object",
                              manifest_);
    error.with_context("event", std::string(event_name));
    return std::unexpected(std::move(error));
  }
  if (is_canceled(cancel_requested)) {
    auto error = canceled_error("plugin event observation canceled", manifest_);
    error.with_context("event", std::string(event_name));
    return std::unexpected(std::move(error));
  }

  auto const deadline = std::chrono::steady_clock::now() + options_.request_timeout;
  std::string const request_id =
      call_id.empty() ? "ava_" + std::to_string(next_request_id_++) : "ava_event_" + std::string(call_id);
  std::string request =
      plugin_event_observe_request_json(request_id, event_name, payload_json, call_id, options_.workspace_dir.string());
  if (auto written = write_record(request, deadline, options_.request_timeout, "timed out writing plugin event request",
                                  cancel_requested);
      !written) {
    return std::unexpected(std::move(written.error()));
  }

  auto record = read_record(deadline, options_.request_timeout, "timed out waiting for plugin event response",
                            "plugin process closed stdout before plugin event response", cancel_requested);
  if (!record) return std::unexpected(std::move(record.error()));
  auto result = parse_plugin_event_observed_response(*record, request_id);
  if (!result) {
    auto error = protocol_error("plugin event response is malformed", manifest_);
    error.with_context("event", std::string(event_name));
    error.with_context("response", record->substr(0, 512));
    return std::unexpected(std::move(error));
  }
  return *result;
}

}  // namespace ava::plugin
