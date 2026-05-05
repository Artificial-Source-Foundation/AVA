#include "ava/app/command_tool_events.h"

#include <utility>

#include "ava/app/events.h"
#include "ava/core/json.h"
#include "ava/session/session_store.h"

namespace ava::app {
namespace {

RuntimeEvent command_event(RuntimeSession const& session, RuntimeEventType type)
{
  RuntimeEvent event;
  event.type = type;
  event.timestamp = ava::session::now_timestamp();
  event.session_id = session.store.session_id();
  event.mode = session.mode;
  event.provider_id = session.model.provider_id;
  event.model_id = session.model.model_id;
  return event;
}

ava::core::VoidResult emit_tool_event(RuntimeSession const& session, RuntimeEventSink const& sink,
                                      ava::agent::ToolTimelineEntry const& entry)
{
  auto event =
      command_event(session, entry.status == ava::agent::ToolTimelineStatus::Running ? RuntimeEventType::ToolStart
                                                                                     : RuntimeEventType::ToolResult);
  event.call_id = entry.call_id;
  event.tool_name = entry.name;
  event.status = ava::agent::to_string(entry.status);
  event.text = entry.status == ava::agent::ToolTimelineStatus::Running ? entry.argument_summary : entry.result_summary;
  event.tool_arguments_json = entry.arguments_json;
  event.tool_result_json = entry.result_json;
  event.tool_structured_result_json = entry.structured_result_json;
  event.content_type = entry.content_type;
  event.error_category = entry.error_category;
  event.error_code = entry.error_code;
  event.error_message = entry.error_message;
  event.error_details = entry.error_details;
  event.diff = entry.diff;
  event.diff_truncated = entry.diff_truncated;
  event.changed_paths = entry.changed_paths;
  event.truncated = entry.truncated;
  event.spill_path = entry.spill_path;
  event.spill_truncated = entry.spill_truncated;
  if (entry.output_bytes) event.output_bytes = *entry.output_bytes;
  if (entry.total_bytes) event.total_bytes = *entry.total_bytes;
  if (entry.omitted_bytes) event.omitted_bytes = *entry.omitted_bytes;
  if (entry.omitted_lines) event.omitted_lines = *entry.omitted_lines;
  if (entry.visible_matches) event.visible_matches = *entry.visible_matches;
  if (entry.total_matches) event.total_matches = *entry.total_matches;
  return emit_event(sink, event);
}

ava::agent::ToolTimelineEntry command_result_entry(std::string const& call_id, std::string name,
                                                   ava::agent::ToolTimelineStatus status, std::string result_summary,
                                                   std::string result_content)
{
  if (result_content.empty()) result_content = result_summary;
  auto dispatch_result = ava::agent::with_tool_result_payload(ava::agent::ToolDispatchResult{
      .call_id = call_id,
      .name = name,
      .success = status == ava::agent::ToolTimelineStatus::Success,
      .result_text = result_content,
  });
  dispatch_result.payload.summary = result_summary;
  if (status == ava::agent::ToolTimelineStatus::Error && !ava::core::json::is_valid_object(result_content)) {
    dispatch_result.payload.error_message = result_summary;
    if (result_content != result_summary) dispatch_result.payload.error_details = result_content;
  }

  auto const result_json = ava::core::json::is_valid_object(result_content) ? result_content : std::string{};
  auto const& payload = dispatch_result.payload;
  return ava::agent::ToolTimelineEntry{
      .status = status,
      .call_id = call_id,
      .name = std::move(name),
      .result_summary = std::move(result_summary),
      .result_json = result_json,
      .structured_result_json = ava::agent::serialize_tool_result_payload_json(dispatch_result),
      .content_type = payload.content_type,
      .error_category = payload.error_category,
      .error_code = payload.error_code,
      .error_message = payload.error_message,
      .error_details = payload.error_details,
      .diff = payload.diff,
      .diff_truncated = payload.diff_truncated,
      .changed_paths = payload.changed_paths,
      .truncated = payload.truncated,
      .output_bytes = payload.output_bytes,
      .total_bytes = payload.total_bytes,
      .omitted_bytes = payload.omitted_bytes,
      .omitted_lines = payload.omitted_lines,
      .visible_matches = payload.visible_matches,
      .total_matches = payload.total_matches,
      .spill_path = payload.spill_path,
      .spill_truncated = payload.spill_truncated,
  };
}

ava::core::VoidResult record_tool_event(RuntimeSession const& session, RuntimeEventSink const& sink,
                                        CommandResult& result, ava::agent::ToolTimelineEntry entry)
{
  if (auto emitted = emit_tool_event(session, sink, entry); !emitted)
    return std::unexpected(std::move(emitted.error()));
  result.tool_timeline.push_back(std::move(entry));
  return {};
}

}  // namespace

ava::core::VoidResult record_tool_start(RuntimeSession const& session, RuntimeEventSink const& sink,
                                        CommandResult& result, std::string const& call_id, std::string name,
                                        std::string argument_summary)
{
  return record_tool_event(session, sink, result,
                           ava::agent::ToolTimelineEntry{.status = ava::agent::ToolTimelineStatus::Running,
                                                         .call_id = call_id,
                                                         .name = std::move(name),
                                                         .argument_summary = std::move(argument_summary)});
}

ava::core::VoidResult record_tool_result(RuntimeSession const& session, RuntimeEventSink const& sink,
                                         CommandResult& result, std::string const& call_id, std::string name,
                                         ava::agent::ToolTimelineStatus status, std::string result_summary,
                                         std::string result_content)
{
  return record_tool_event(
      session, sink, result,
      command_result_entry(call_id, std::move(name), status, std::move(result_summary), std::move(result_content)));
}

}  // namespace ava::app
