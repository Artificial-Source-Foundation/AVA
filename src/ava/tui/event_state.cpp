#include "sys.h"
#include "ava/app/EventEnvelope.h"
#include "ava/tui/event_state.h"

#include "ava/config/model_profiles.h"

#include "ava/core/json.h"

#include <algorithm>
#include <string_view>
#include <utility>

namespace ava::tui {
namespace {

std::string first_non_empty(std::initializer_list<std::string_view> values)
{
  for (auto const value : values) {
    if (!value.empty()) return std::string(value);
  }
  return {};
}

void assign_if_present(std::string& target, std::string value)
{
  if (!value.empty()) target = std::move(value);
}

void add_unique_string(std::vector<std::string>& values, std::string value)
{
  if (value.empty()) return;
  if (std::ranges::find(values, value) == values.end()) values.push_back(std::move(value));
}

auto find_pending_tool(TuiEventState& state, std::string const& call_id, std::string const& request_id = {},
                       std::string const& correlation_id = {})
{
  return std::ranges::find_if(state.pending_tools, [&](PendingToolItem const& tool) {
    return (!call_id.empty() && tool.call_id == call_id) || (!request_id.empty() && tool.request_id == request_id) ||
           (!correlation_id.empty() && tool.correlation_id == correlation_id);
  });
}

auto find_permission_audit(TuiEventState& state, std::string_view permission_request_id,
                           std::string_view resolver_request_id = {})
{
  return std::ranges::find_if(state.permission_audits, [&](ToolPermissionAuditItem const& audit) {
    return (!permission_request_id.empty() && audit.permission_request_id == permission_request_id) ||
           (!resolver_request_id.empty() && audit.resolver_request_id == resolver_request_id);
  });
}

ToolPermissionAuditItem permission_audit_from_request_envelope(ava::app::EventEnvelope const& envelope)
{
  auto const& payload = envelope.payload_json;
  return ToolPermissionAuditItem{.permission_request_id = ava::core::json::string_field(payload, "permission_request_id").value_or(""),
                                 .resolver_request_id = ava::core::json::string_field(payload, "resolver_request_id").value_or(""),
                                 .decision = {},
                                 .operation = ava::core::json::string_field(payload, "operation").value_or(""),
                                 .tool_name = ava::core::json::string_field(payload, "tool_name").value_or(""),
                                 .risk = ava::core::json::string_field(payload, "risk").value_or(""),
                                 .reason = ava::core::json::string_field(payload, "reason").value_or(""),
                                 .target = ava::core::json::string_field(payload, "target_path").value_or(""),
                                 .command = ava::core::json::string_field(payload, "command").value_or(""),
                                 .resolution_reason = {}};
}

void merge_permission_audit(ToolPermissionAuditItem& target, ToolPermissionAuditItem incoming)
{
  assign_if_present(target.permission_request_id, std::move(incoming.permission_request_id));
  assign_if_present(target.resolver_request_id, std::move(incoming.resolver_request_id));
  assign_if_present(target.decision, std::move(incoming.decision));
  assign_if_present(target.operation, std::move(incoming.operation));
  assign_if_present(target.tool_name, std::move(incoming.tool_name));
  assign_if_present(target.risk, std::move(incoming.risk));
  assign_if_present(target.reason, std::move(incoming.reason));
  assign_if_present(target.target, std::move(incoming.target));
  assign_if_present(target.command, std::move(incoming.command));
  assign_if_present(target.resolution_reason, std::move(incoming.resolution_reason));
}

void remember_permission_audit(TuiEventState& state, ToolPermissionAuditItem audit)
{
  auto existing = find_permission_audit(state, audit.permission_request_id, audit.resolver_request_id);
  if (existing == state.permission_audits.end()) {
    state.permission_audits.push_back(std::move(audit));
    return;
  }
  merge_permission_audit(*existing, std::move(audit));
}

void remember_permission_request_envelope(TuiEventState& state, ava::app::EventEnvelope const& envelope)
{
  auto audit = permission_audit_from_request_envelope(envelope);
  if (audit.permission_request_id.empty() && audit.resolver_request_id.empty()) return;
  remember_permission_audit(state, std::move(audit));
}

void remember_permission_reply_envelope(TuiEventState& state, ava::app::EventEnvelope const& envelope)
{
  auto const& payload = envelope.payload_json;
  auto audit = ToolPermissionAuditItem{
      .permission_request_id = ava::core::json::string_field(payload, "permission_request_id").value_or(""),
      .resolver_request_id = ava::core::json::string_field(payload, "resolver_request_id").value_or(""),
      .decision = ava::core::json::string_field(payload, "decision").value_or(""),
      .resolution_reason = ava::core::json::string_field(payload, "reason").value_or("")};
  if (audit.permission_request_id.empty() && audit.resolver_request_id.empty()) return;
  remember_permission_audit(state, std::move(audit));
}

void remember_permission_provider_event(TuiEventState& state, ava::app::runtime::RuntimeEvent const& event)
{
  if (event.status != "tui:permission_request" && event.status != "tui:permission_allow" &&
      event.status != "tui:permission_deny") {
    return;
  }
  auto audit = ToolPermissionAuditItem{.decision = event.status == "tui:permission_allow" ? std::string("allow")
                                             : event.status == "tui:permission_deny"  ? std::string("deny")
                                                                                      : std::string{},
                                       .reason = event.reason,
                                       .command = event.status == "tui:permission_request" ? event.text : std::string{},
                                       .resolution_reason = event.error_details};
  if (!event.tool_name.empty()) audit.tool_name = event.tool_name;
  if (!event.permission_request_ids.empty()) audit.permission_request_id = event.permission_request_ids.front();
  if (audit.permission_request_id.empty() && audit.resolver_request_id.empty()) return;
  remember_permission_audit(state, std::move(audit));
}

void add_permission_request_id(ToolTimelineItem& item, std::string id)
{
  add_unique_string(item.permission_request_ids, std::move(id));
}

void add_permission_audit(ToolTimelineItem& item, ToolPermissionAuditItem audit)
{
  if (!audit.permission_request_id.empty()) add_permission_request_id(item, audit.permission_request_id);
  auto existing = std::ranges::find_if(item.permissions, [&](ToolPermissionAuditItem const& current) {
    return (!audit.permission_request_id.empty() && current.permission_request_id == audit.permission_request_id) ||
           (!audit.resolver_request_id.empty() && current.resolver_request_id == audit.resolver_request_id);
  });
  if (existing == item.permissions.end()) {
    item.permissions.push_back(std::move(audit));
    return;
  }
  merge_permission_audit(*existing, std::move(audit));
}

void attach_permission_audits(TuiEventState& state, ToolTimelineItem& item)
{
  auto const permission_ids = item.permission_request_ids;
  for (auto const& permission_id : permission_ids) {
    auto existing = find_permission_audit(state, permission_id);
    if (existing == state.permission_audits.end()) {
      add_permission_audit(item, ToolPermissionAuditItem{.permission_request_id = permission_id});
      continue;
    }
    add_permission_audit(item, *existing);
  }
  for (auto const& audit : item.permissions) {
    if (!audit.permission_request_id.empty()) add_unique_string(item.permission_request_ids, audit.permission_request_id);
  }
}

bool bool_field(std::string_view object, std::string_view key)
{
  auto const start = ava::core::json::field_value_start(object, key);
  if (!start) return false;
  auto value = object.substr(*start);
  while (!value.empty()) {
    auto const byte = static_cast<unsigned char>(value.front());
    if (byte != ' ' && byte != '\n' && byte != '\r' && byte != '\t') break;
    value.remove_prefix(1);
  }
  return value.starts_with("true");
}

std::optional<bool> optional_bool_field(std::string_view object, std::string_view key)
{
  auto const start = ava::core::json::field_value_start(object, key);
  if (!start) return std::nullopt;
  auto value = object.substr(*start);
  while (!value.empty()) {
    auto const byte = static_cast<unsigned char>(value.front());
    if (byte != ' ' && byte != '\n' && byte != '\r' && byte != '\t') break;
    value.remove_prefix(1);
  }
  if (value.starts_with("true")) return true;
  if (value.starts_with("false")) return false;
  return std::nullopt;
}

std::size_t size_field(std::string_view object, std::string_view key)
{
  auto const value = ava::core::json::integer_field(object, key);
  return value && *value > 0 ? static_cast<std::size_t>(*value) : std::size_t{0};
}

std::optional<std::size_t> optional_size_field(std::string_view object, std::string_view key)
{
  auto const value = ava::core::json::integer_field(object, key);
  if (!value || *value < 0) return std::nullopt;
  return static_cast<std::size_t>(*value);
}

void upsert_sidebar_activity(TuiEventState& state, SidebarActivityItem item);
std::string title_case_ascii(std::string_view text);

TranscriptItem transcript_text_item(std::string label, std::string text)
{
  auto text_model = text_from_plain(text);
  return TranscriptItem{.label = std::move(label), .text = std::move(text), .text_model = std::move(text_model)};
}

TranscriptItem assistant_transcript_item(std::string text, std::string meta, std::string thinking = {})
{
  auto text_model = text_from_markdown(text);
  auto thinking_model = text_from_plain(thinking);
  return TranscriptItem{.label = "ava",
                        .text = std::move(text),
                        .text_model = std::move(text_model),
                        .meta = std::move(meta),
                        .thinking = std::move(thinking),
                        .thinking_model = std::move(thinking_model)};
}

std::optional<ava::app::runtime::RuntimeEventType> runtime_event_type_from_name(std::string_view name)
{
  using ava::app::runtime::RuntimeEventType;
  if (name == "session_start") return RuntimeEventType::SessionStart;
  if (name == "user_message") return RuntimeEventType::UserMessage;
  if (name == "assistant_message") return RuntimeEventType::AssistantMessage;
  if (name == "message_update") return RuntimeEventType::MessageUpdate;
  if (name == "message_end") return RuntimeEventType::MessageEnd;
  if (name == "reasoning_start") return RuntimeEventType::ReasoningStart;
  if (name == "reasoning_delta") return RuntimeEventType::ReasoningDelta;
  if (name == "reasoning_end") return RuntimeEventType::ReasoningEnd;
  if (name == "provider_event") return RuntimeEventType::ProviderEvent;
  if (name == "tool_start") return RuntimeEventType::ToolStart;
  if (name == "tool_progress") return RuntimeEventType::ToolProgress;
  if (name == "tool_result") return RuntimeEventType::ToolResult;
  if (name == "compaction_start") return RuntimeEventType::CompactionStart;
  if (name == "compaction_end") return RuntimeEventType::CompactionEnd;
  if (name == "retry") return RuntimeEventType::Retry;
  if (name == "retry_tick") return RuntimeEventType::RetryTick;
  if (name == "canceled") return RuntimeEventType::Canceled;
  if (name == "error") return RuntimeEventType::Error;
  if (name == "done") return RuntimeEventType::Done;
  return std::nullopt;
}

std::string prompt_request_text(ava::app::EventEnvelope const& envelope)
{
  auto const& payload = envelope.payload_json;
  if (envelope.name == "permission_requested") {
    auto const tool = ava::core::json::string_field(payload, "tool_name").value_or("");
    auto const operation = ava::core::json::string_field(payload, "operation").value_or("");
    auto const target = ava::core::json::string_field(payload, "target_path").value_or("");
    auto const command = ava::core::json::string_field(payload, "command").value_or("");
    auto text = std::string("permission requested");
    if (!tool.empty()) text += ": " + tool;
    auto const detail = first_non_empty({target, command, operation});
    if (!detail.empty()) text += " " + detail;
    return text;
  }
  if (envelope.name == "question_requested") {
    auto const question = ava::core::json::string_field(payload, "question").value_or("");
    return question.empty() ? std::string("question requested") : "question requested: " + question;
  }
  return {};
}

std::optional<ava::app::runtime::RuntimeEvent> runtime_event_from_envelope(ava::app::EventEnvelope const& envelope)
{
  auto const type = runtime_event_type_from_name(envelope.name);
  if (!type) return std::nullopt;
  ava::app::runtime::RuntimeEvent event;
  event.type = *type;
  event.timestamp = envelope.timestamp;
  event.session_id = envelope.session_id;
  if (auto mode = ava::core::json::string_field(envelope.payload_json, "mode")) {
    if (auto parsed = ava::agent::parse_mode(*mode)) event.mode = *parsed;
  }
  event.provider_id = ava::core::json::string_field(envelope.payload_json, "provider").value_or("");
  event.model_id = ava::core::json::string_field(envelope.payload_json, "model").value_or("");
  event.text = first_non_empty({ava::core::json::string_field(envelope.payload_json, "text").value_or(""),
                                ava::core::json::string_field(envelope.payload_json, "argument_summary").value_or(""),
                                ava::core::json::string_field(envelope.payload_json, "result_summary").value_or(""),
                                ava::core::json::string_field(envelope.payload_json, "progress").value_or("")});
  event.call_id = first_non_empty({ava::core::json::string_field(envelope.payload_json, "call_id").value_or(""),
                                   ava::core::json::string_field(envelope.payload_json, "tool_call_id").value_or(""),
                                   envelope.correlation_id.value_or(""), envelope.request_id.value_or("")});
  event.tool_name = first_non_empty({ava::core::json::string_field(envelope.payload_json, "tool").value_or(""),
                                     ava::core::json::string_field(envelope.payload_json, "tool_name").value_or(""),
                                     ava::core::json::string_field(envelope.payload_json, "name").value_or("")});
  event.tool_arguments_json =
      first_non_empty({ava::core::json::object_field(envelope.payload_json, "args").value_or(""),
                       ava::core::json::string_field(envelope.payload_json, "args_json").value_or("")});
  event.tool_result_json =
      first_non_empty({ava::core::json::object_field(envelope.payload_json, "result").value_or(""),
                       ava::core::json::string_field(envelope.payload_json, "result_json").value_or("")});
  event.status = ava::core::json::string_field(envelope.payload_json, "status").value_or("");
  event.error_category = ava::core::json::string_field(envelope.payload_json, "category").value_or("");
  event.error_message = ava::core::json::string_field(envelope.payload_json, "message").value_or("");
  event.error_details = ava::core::json::string_field(envelope.payload_json, "details").value_or("");
  event.stop_reason = ava::core::json::string_field(envelope.payload_json, "stop_reason").value_or("");
  event.trigger = ava::core::json::string_field(envelope.payload_json, "trigger").value_or("");
  event.reason = ava::core::json::string_field(envelope.payload_json, "reason").value_or("");
  event.reasoning_format = ava::core::json::string_field(envelope.payload_json, "reasoning_format").value_or("");
  event.diff = ava::core::json::string_field(envelope.payload_json, "diff").value_or("");
  event.changed_paths = ava::core::json::strings_in_array_field(envelope.payload_json, "changed_paths");
  if (event.changed_paths.empty()) {
    event.changed_paths = ava::core::json::strings_in_array_field(envelope.payload_json, "changed_files");
  }
  event.permission_request_ids = ava::core::json::strings_in_array_field(envelope.payload_json, "permission_request_ids");
  if (auto permission_id = ava::core::json::string_field(envelope.payload_json, "permission_request_id")) {
    add_unique_string(event.permission_request_ids, *permission_id);
  }
  event.spill_path = ava::core::json::string_field(envelope.payload_json, "spill_path").value_or("");
  event.reasoning_redacted = bool_field(envelope.payload_json, "reasoning_redacted");
  event.reasoning_signature_present = bool_field(envelope.payload_json, "reasoning_signature_present");
  event.diff_truncated = bool_field(envelope.payload_json, "diff_truncated");
  event.truncated = bool_field(envelope.payload_json, "truncated");
  event.byte_limited = bool_field(envelope.payload_json, "byte_limited");
  event.line_limited = bool_field(envelope.payload_json, "line_limited");
  event.spill_truncated = bool_field(envelope.payload_json, "spill_truncated");
  event.provider_iterations = size_field(envelope.payload_json, "provider_iterations");
  event.tool_calls = size_field(envelope.payload_json, "tool_calls");
  event.attempt = size_field(envelope.payload_json, "attempt");
  event.max_attempts = size_field(envelope.payload_json, "max_attempts");
  event.delay_ms = size_field(envelope.payload_json, "delay_ms");
  event.remaining_ms = size_field(envelope.payload_json, "remaining_ms");
  event.estimated_tokens = size_field(envelope.payload_json, "estimated_tokens");
  event.threshold_tokens = size_field(envelope.payload_json, "threshold_tokens");
  event.summary_bytes = size_field(envelope.payload_json, "summary_bytes");
  event.snapshot_entries = size_field(envelope.payload_json, "snapshot_entries");
  event.current_entries = size_field(envelope.payload_json, "current_entries");
  event.output_bytes = size_field(envelope.payload_json, "output_bytes");
  event.total_bytes = size_field(envelope.payload_json, "total_bytes");
  event.output_lines = size_field(envelope.payload_json, "output_lines");
  event.total_lines = size_field(envelope.payload_json, "total_lines");
  event.start_line = size_field(envelope.payload_json, "start_line");
  event.end_line = size_field(envelope.payload_json, "end_line");
  event.next_offset_line = size_field(envelope.payload_json, "next_offset_line");
  event.omitted_bytes = size_field(envelope.payload_json, "omitted_bytes");
  event.omitted_lines = size_field(envelope.payload_json, "omitted_lines");
  event.visible_matches = size_field(envelope.payload_json, "visible_matches");
  event.total_matches = size_field(envelope.payload_json, "total_matches");
  return event;
}

void annotate_pending_tool_ids(TuiEventState& state, ava::app::runtime::RuntimeEvent const& event,
                               ava::app::EventEnvelope const& envelope)
{
  if (event.type != ava::app::runtime::RuntimeEventType::ToolStart && event.type != ava::app::runtime::RuntimeEventType::ToolProgress &&
      event.type != ava::app::runtime::RuntimeEventType::ToolResult) {
    return;
  }
  auto existing =
      find_pending_tool(state, event.call_id, envelope.request_id.value_or(""), envelope.correlation_id.value_or(""));
  if (existing == state.pending_tools.end()) return;
  if (!envelope.request_id.value_or("").empty()) {
    existing->request_id = *envelope.request_id;
    existing->item.request_id = *envelope.request_id;
  }
  if (!envelope.correlation_id.value_or("").empty()) {
    existing->correlation_id = *envelope.correlation_id;
    existing->item.correlation_id = *envelope.correlation_id;
  }
}

void remember_envelope_ids(TuiEventState& state, ava::app::EventEnvelope const& envelope)
{
  if (envelope.run_id) state.active_run_id = envelope.run_id;
  if (envelope.turn_id) state.active_turn_id = envelope.turn_id;
  if (envelope.message_id) state.active_message_id = envelope.message_id;
  if (envelope.request_id) state.active_request_id = envelope.request_id;
  if (envelope.correlation_id) state.active_correlation_id = envelope.correlation_id;
}

void apply_prompt_request_envelope(TuiEventState& state, ava::app::EventEnvelope const& envelope)
{
  auto const text = prompt_request_text(envelope);
  if (text.empty()) return;
  auto const permission = envelope.name == "permission_requested";
  state.transcript.push_back(transcript_text_item("audit", text));
  upsert_sidebar_activity(state, SidebarActivityItem{.id = first_non_empty({envelope.request_id.value_or(""),
                                                                            envelope.correlation_id.value_or(""),
                                                                            permission ? "permission" : "question"}),
                                                     .label = permission ? "permission" : "question",
                                                     .detail = text,
                                                     .status = ToolTimelineStatus::Running});
}

std::string queue_event_label(std::string_view name)
{
  if (name == "steer_queued") return "steer queued";
  if (name == "steer_applied") return "steer applied";
  if (name == "steer_skipped") return "steer skipped";
  if (name == "follow_up_queued") return "follow-up queued";
  if (name == "follow_up_started") return "follow-up started";
  if (name == "follow_up_skipped") return "follow-up skipped";
  return title_case_ascii(name);
}

ToolTimelineStatus queue_event_status(std::string_view name)
{
  if (name.ends_with("_skipped")) return ToolTimelineStatus::Error;
  if (name.ends_with("_applied")) return ToolTimelineStatus::Success;
  return ToolTimelineStatus::Running;
}

std::string queue_event_reason_text(std::string_view name, std::string_view reason)
{
  if (reason == "canceled")
    return "run stopped before delivery; submit it again to continue";
  if (reason == "restored_to_composer")
    return "restored to composer";
  if (reason == "run_completed_before_safe_point") {
    if (name.starts_with("steer")) return "run finished before the next safe steering point";
    return "run finished before delivery";
  }
  return std::string(reason);
}

std::string queue_event_detail(ava::app::EventEnvelope const& envelope)
{
  auto const message = ava::core::json::string_field(envelope.payload_json, "message").value_or("");
  auto const reason = ava::core::json::string_field(envelope.payload_json, "reason").value_or("");
  auto detail = queue_event_label(envelope.name);
  if (!reason.empty()) detail += ": " + queue_event_reason_text(envelope.name, reason);
  if (!message.empty()) detail += " - " + message;
  if (bool_field(envelope.payload_json, "message_truncated")) {
    detail += " [message truncated";
    auto const bytes = size_field(envelope.payload_json, "message_bytes");
    if (bytes > 0) detail += " from " + std::to_string(bytes) + " bytes";
    detail += "]";
  }
  return detail;
}

std::string queue_event_id(ava::app::EventEnvelope const& envelope)
{
  return first_non_empty({envelope.request_id.value_or(""), envelope.correlation_id.value_or(""), envelope.name});
}

void upsert_queued_message(TuiEventState& state, ava::app::EventEnvelope const& envelope)
{
  auto const id = queue_event_id(envelope);
  auto const message = ava::core::json::string_field(envelope.payload_json, "message").value_or("");
  if (id.empty() || message.empty()) return;
  auto const kind = envelope.name.starts_with("steer") ? std::string("steer") : std::string("follow-up");
  auto existing =
      std::ranges::find_if(state.queued_messages, [&](QueuedMessageItem const& item) { return item.id == id; });
  auto item = QueuedMessageItem{.id = id, .kind = kind, .text = message};
  if (existing == state.queued_messages.end()) {
    state.queued_messages.push_back(std::move(item));
  } else {
    *existing = std::move(item);
  }
}

void remove_queued_message(TuiEventState& state, ava::app::EventEnvelope const& envelope)
{
  auto const id = queue_event_id(envelope);
  if (id.empty()) return;
  std::erase_if(state.queued_messages, [&](QueuedMessageItem const& item) { return item.id == id; });
}

std::string_view trim_trailing_ascii_space(std::string_view text)
{
  while (!text.empty()) {
    auto const byte = static_cast<unsigned char>(text.back());
    if (byte != ' ' && byte != '\n' && byte != '\r' && byte != '\t' && byte != '\f' && byte != '\v') break;
    text.remove_suffix(1);
  }
  return text;
}

ToolTimelineStatus tool_status_from_event(ava::app::runtime::RuntimeEvent const& event)
{
  if (event.status == "error") return ToolTimelineStatus::Error;
  if (event.status == "canceled" || event.status == "cancelled" || event.status == "aborted")
    return ToolTimelineStatus::Canceled;
  if (event.status == "running") return ToolTimelineStatus::Running;
  return ToolTimelineStatus::Success;
}

ToolLifecycleState tool_lifecycle_for_status(ToolTimelineStatus status)
{
  switch (status) {
    case ToolTimelineStatus::Running:
      return ToolLifecycleState::ExecutionStarted;
    case ToolTimelineStatus::Success:
      return ToolLifecycleState::Complete;
    case ToolTimelineStatus::Canceled:
      return ToolLifecycleState::Canceled;
    case ToolTimelineStatus::Error:
      return ToolLifecycleState::Error;
  }
  return ToolLifecycleState::Error;
}

std::string title_case_ascii(std::string_view text)
{
  std::string output;
  output.reserve(text.size());
  bool at_word_start = true;
  for (char ch : text) {
    auto const byte = static_cast<unsigned char>(ch);
    if (byte == '_' || byte == '-') {
      output.push_back(' ');
      at_word_start = true;
      continue;
    }
    if (at_word_start && byte >= 'a' && byte <= 'z') {
      output.push_back(static_cast<char>(byte - ('a' - 'A')));
    } else {
      output.push_back(ch);
    }
    at_word_start = byte == ' ';
  }
  return output;
}

std::string assistant_meta_for_event(ava::app::runtime::RuntimeEvent const& event)
{
  if (event.model_id.empty()) return {};
  auto mode = title_case_ascii(ava::agent::to_string(event.mode));
  if (mode.empty()) mode = "AVA";
  return mode + " · " + ava::config::model_display_label(event.provider_id, event.model_id);
}

void update_pending_assistant_meta(TuiEventState& state, ava::app::runtime::RuntimeEvent const& event)
{
  auto meta = assistant_meta_for_event(event);
  if (!meta.empty()) state.pending_assistant_meta = std::move(meta);
}

void upsert_activity(TuiEventState& state, std::string const& call_id, ToolTimelineItem const& item)
{
  auto const id = call_id.empty() ? item.name + ":" + item.argument_summary : call_id;
  auto const label = item.name.empty() ? std::string("tool") : item.name;
  auto const detail = item.result_summary.empty() ? item.argument_summary : item.result_summary;
  auto existing =
      std::ranges::find_if(state.activity, [&](SidebarActivityItem const& activity) { return activity.id == id; });
  if (existing != state.activity.end()) {
    existing->label = label;
    existing->detail = detail;
    existing->status = item.status;
    return;
  }
  state.activity.push_back(SidebarActivityItem{.id = id, .label = label, .detail = detail, .status = item.status});
  constexpr auto kMaxActivityItems = std::size_t{8};
  if (state.activity.size() > kMaxActivityItems) {
    state.activity.erase(
        state.activity.begin(),
        state.activity.begin() + static_cast<std::ptrdiff_t>(state.activity.size() - kMaxActivityItems));
  }
}

void upsert_sidebar_activity(TuiEventState& state, SidebarActivityItem item)
{
  auto existing =
      std::ranges::find_if(state.activity, [&](SidebarActivityItem const& activity) { return activity.id == item.id; });
  if (existing != state.activity.end()) {
    *existing = std::move(item);
    return;
  }
  state.activity.push_back(std::move(item));
  constexpr auto kMaxActivityItems = std::size_t{8};
  if (state.activity.size() > kMaxActivityItems) {
    state.activity.erase(
        state.activity.begin(),
        state.activity.begin() + static_cast<std::ptrdiff_t>(state.activity.size() - kMaxActivityItems));
  }
}

void settle_responding_activity(TuiEventState& state, ToolTimelineStatus status, std::string detail)
{
  auto existing = std::ranges::find_if(state.activity,
                                       [](SidebarActivityItem const& activity) { return activity.id == "responding"; });
  if (existing == state.activity.end()) return;
  existing->status = status;
  existing->detail = std::move(detail);
}

bool is_cancel_error(ava::app::runtime::RuntimeEvent const& event)
{
  return event.error_message == "agent loop canceled" || event.text == "agent loop canceled" ||
         event.error_details.find("agent loop canceled") != std::string::npos;
}

std::string compact_lifecycle_detail(std::string_view label, ava::app::runtime::RuntimeEvent const& event)
{
  std::string detail(label);
  if (!event.trigger.empty()) detail += " (" + event.trigger + ")";
  if (event.attempt > 0) {
    detail += " attempt " + std::to_string(event.attempt);
    if (event.max_attempts > 0) detail += "/" + std::to_string(event.max_attempts);
  }
  if (event.delay_ms > 0) detail += " delay=" + std::to_string(event.delay_ms) + "ms";
  if (event.remaining_ms > 0 || event.type == ava::app::runtime::RuntimeEventType::RetryTick)
    detail += " remaining=" + std::to_string(event.remaining_ms) + "ms";
  if (event.estimated_tokens > 0) detail += " tokens~" + std::to_string(event.estimated_tokens);
  if (event.threshold_tokens > 0) detail += "/" + std::to_string(event.threshold_tokens);
  if (event.summary_bytes > 0) detail += " summary=" + std::to_string(event.summary_bytes) + " bytes";
  if (!event.text.empty()) detail += " - " + event.text;
  return detail;
}

std::string retry_activity_id(ava::app::runtime::RuntimeEvent const& event)
{
  return "retry:" + first_non_empty({event.reason, event.trigger, "retry"});
}

void upsert_retry_countdown_transcript(TuiEventState& state, std::string detail)
{
  auto existing = std::ranges::find_if(state.transcript.rbegin(), state.transcript.rend(), [](TranscriptItem const& item) {
    return item.label == "audit" && item.text.starts_with("retry countdown");
  });
  if (existing != state.transcript.rend()) {
    existing->text = std::move(detail);
    existing->text_model = text_from_plain(existing->text);
    return;
  }
  state.transcript.push_back(transcript_text_item("audit", std::move(detail)));
}

void apply_canceled_event(TuiEventState& state, ava::app::runtime::RuntimeEvent const& event)
{
  state.error_text = "stopped by user";
  state.error_details.clear();
  auto text = event.text.empty() ? std::string("stopped by user") : event.text;
  if (text == "stopped by user") text += ". Submit a new prompt to continue.";
  state.transcript.push_back(assistant_transcript_item(std::move(text), {}));
  settle_responding_activity(state, ToolTimelineStatus::Canceled, "assistant stopped");
  auto detail = event.reason.empty() ? std::string("active work was stopped") : event.reason;
  detail += "; submit a new prompt to continue";
  upsert_sidebar_activity(
      state, SidebarActivityItem{.id = "stopped",
                                 .label = "stopped",
                                 .detail = std::move(detail),
                                 .status = ToolTimelineStatus::Canceled});
  state.run_status = TuiEventRunStatus::Canceled;
}

std::optional<std::string> path_from_argument_summary(std::string_view summary)
{
  // Tool argument summaries are the only stable TUI-side source for file names today. Keep this conservative: if the
  // summary no longer exposes a top-level path= field, the sidebar omits the file instead of guessing.
  constexpr std::string_view marker = "path=";
  auto const start = summary.find(marker);
  if (start == std::string_view::npos) return std::nullopt;
  auto value = summary.substr(start + marker.size());
  auto const comma = value.find(',');
  if (comma != std::string_view::npos) value = value.substr(0, comma);
  while (!value.empty() && value.front() == ' ') value.remove_prefix(1);
  while (!value.empty() && value.back() == ' ') value.remove_suffix(1);
  if (value.empty()) return std::nullopt;
  return std::string(value);
}

void record_modified_file(TuiEventState& state, ToolTimelineItem const& item)
{
  if (item.status != ToolTimelineStatus::Success) return;
  if (item.name != "write_file" && item.name != "edit_file" && item.name != "apply_patch") return;
  auto paths = item.changed_paths;
  if (paths.empty()) {
    auto path = path_from_argument_summary(item.argument_summary);
    if (path) paths.push_back(std::move(*path));
  }
  for (auto& path : paths) {
    if (path.empty()) continue;
    auto existing =
        std::ranges::find_if(state.modified_files, [&](SidebarModifiedFile const& file) { return file.path == path; });
    if (existing == state.modified_files.end()) {
      state.modified_files.push_back(SidebarModifiedFile{.path = std::move(path)});
    }
  }
  constexpr auto kMaxModifiedFiles = std::size_t{12};
  if (state.modified_files.size() > kMaxModifiedFiles) {
    state.modified_files.erase(
        state.modified_files.begin(),
        state.modified_files.begin() + static_cast<std::ptrdiff_t>(state.modified_files.size() - kMaxModifiedFiles));
  }
}

std::string take_pending_reasoning_text(TuiEventState& state)
{
  auto thinking = std::move(state.pending_reasoning_text);
  state.pending_reasoning_text.clear();
  state.pending_reasoning_redacted = false;
  return thinking;
}

void append_assistant_text(TuiEventState& state, std::string text, std::string meta, std::string thinking = {})
{
  if (text.empty() && thinking.empty()) return;
  state.transcript.push_back(assistant_transcript_item(std::move(text), std::move(meta), std::move(thinking)));
  state.stream_assistant_transcript_index = std::nullopt;
}

void commit_pending_assistant_text(TuiEventState& state, std::string meta = {})
{
  auto thinking = take_pending_reasoning_text(state);
  if (state.pending_assistant_text.empty() && thinking.empty()) return;
  if (meta.empty()) meta = std::move(state.pending_assistant_meta);
  state.transcript.push_back(
      assistant_transcript_item(std::move(state.pending_assistant_text), std::move(meta), std::move(thinking)));
  state.pending_assistant_text.clear();
  state.pending_assistant_meta.clear();
  state.stream_assistant_transcript_index = state.transcript.size() - 1;
}

void commit_pending_reasoning_turn(TuiEventState& state, std::string meta = {})
{
  if (state.pending_reasoning_text.empty()) return;
  if (meta.empty()) meta = std::move(state.pending_assistant_meta);
  append_assistant_text(state, {}, std::move(meta), take_pending_reasoning_text(state));
  state.pending_assistant_meta.clear();
}

void apply_assistant_final(TuiEventState& state, ava::app::runtime::RuntimeEvent const& event)
{
  auto final_text = event.text.empty() ? std::move(state.pending_assistant_text) : event.text;
  auto final_meta = assistant_meta_for_event(event);
  if (final_meta.empty()) final_meta = std::move(state.pending_assistant_meta);
  state.pending_assistant_text.clear();
  state.pending_assistant_meta.clear();
  if (final_text.empty() && state.pending_reasoning_text.empty()) return;

  if (state.stream_assistant_transcript_index && *state.stream_assistant_transcript_index < state.transcript.size()) {
    auto& item = state.transcript[*state.stream_assistant_transcript_index];
    if (!item.tool && item.label == "ava") {
      item.text = std::move(final_text);
      item.text_model = text_from_markdown(item.text);
      if (!final_meta.empty()) item.meta = std::move(final_meta);
      if (!state.pending_reasoning_text.empty() && item.thinking.empty()) {
        item.thinking = take_pending_reasoning_text(state);
        item.thinking_model = text_from_plain(item.thinking);
      }
      state.stream_assistant_transcript_index = std::nullopt;
      return;
    }
  }

  if (!state.transcript.empty()) {
    auto& last = state.transcript.back();
    if (!last.tool && last.label == "ava" &&
        trim_trailing_ascii_space(last.text) == trim_trailing_ascii_space(final_text)) {
      if (!final_meta.empty()) last.meta = std::move(final_meta);
      state.stream_assistant_transcript_index = std::nullopt;
      return;
    }
  }

  append_assistant_text(state, std::move(final_text), std::move(final_meta), take_pending_reasoning_text(state));
}

ToolTimelineItem tool_item_from_event(ava::app::runtime::RuntimeEvent const& event, ToolTimelineStatus status)
{
  ToolTimelineItem item{.status = status,
                        .name = event.tool_name,
                        .argument_summary = event.text,
                        .result_summary = {},
                        .arguments_json = event.tool_arguments_json,
                        .result_json = event.tool_result_json,
                        .call_id = event.call_id,
                        .lifecycle = tool_lifecycle_for_status(status)};
  item.diff = event.diff;
  item.diff_truncated = event.diff_truncated;
  item.changed_paths = event.changed_paths;
  item.truncated = event.truncated;
  item.byte_limited = event.byte_limited;
  item.line_limited = event.line_limited;
  item.spill_path = event.spill_path;
  item.spill_truncated = event.spill_truncated;
  if (event.output_bytes > 0) item.output_bytes = event.output_bytes;
  if (event.total_bytes > 0) item.total_bytes = event.total_bytes;
  if (event.output_lines > 0) item.output_lines = event.output_lines;
  if (event.total_lines > 0) item.total_lines = event.total_lines;
  if (event.start_line > 0) item.start_line = event.start_line;
  if (event.end_line > 0) item.end_line = event.end_line;
  if (event.next_offset_line > 0) item.next_offset_line = event.next_offset_line;
  if (event.omitted_bytes > 0) item.omitted_bytes = event.omitted_bytes;
  if (event.omitted_lines > 0) item.omitted_lines = event.omitted_lines;
  if (event.visible_matches > 0) item.visible_matches = event.visible_matches;
  if (event.total_matches > 0) item.total_matches = event.total_matches;
  for (auto const& permission_id : event.permission_request_ids) add_permission_request_id(item, permission_id);
  return item;
}

void apply_tool_start(TuiEventState& state, ava::app::runtime::RuntimeEvent const& event)
{
  auto item = tool_item_from_event(event, ToolTimelineStatus::Running);
  auto existing = find_pending_tool(state, event.call_id);
  if (existing != state.pending_tools.end()) {
    if (!item.name.empty()) existing->item.name = std::move(item.name);
    if (!item.argument_summary.empty()) existing->item.argument_summary = std::move(item.argument_summary);
    if (!item.arguments_json.empty()) existing->item.arguments_json = std::move(item.arguments_json);
    for (auto const& permission_id : item.permission_request_ids) add_permission_request_id(existing->item, permission_id);
    attach_permission_audits(state, existing->item);
    existing->item.call_id = event.call_id;
    existing->item.lifecycle = ToolLifecycleState::ExecutionStarted;
    existing->item.status = ToolTimelineStatus::Running;
    upsert_activity(state, event.call_id, existing->item);
    return;
  }
  attach_permission_audits(state, item);
  state.pending_tools.push_back(
      PendingToolItem{.call_id = event.call_id, .request_id = {}, .correlation_id = {}, .item = std::move(item)});
  upsert_activity(state, event.call_id, state.pending_tools.back().item);
}

void apply_tool_progress(TuiEventState& state, ava::app::runtime::RuntimeEvent const& event)
{
  auto existing = find_pending_tool(state, event.call_id);
  if (existing == state.pending_tools.end()) {
    state.pending_tools.push_back(PendingToolItem{.call_id = event.call_id,
                                                  .request_id = {},
                                                  .correlation_id = {},
                                                  .item = ToolTimelineItem{.status = ToolTimelineStatus::Running,
                                                                           .name = event.tool_name,
                                                                           .argument_summary = {},
                                                                           .result_summary = event.text,
                                                                           .arguments_json = event.tool_arguments_json,
                                                                           .result_json = event.tool_result_json,
                                                                           .call_id = event.call_id,
                                                                           .lifecycle = ToolLifecycleState::Progress}});
    for (auto const& permission_id : event.permission_request_ids) {
      add_permission_request_id(state.pending_tools.back().item, permission_id);
    }
    attach_permission_audits(state, state.pending_tools.back().item);
    return;
  }
  if (!event.tool_name.empty()) existing->item.name = event.tool_name;
  if (!event.tool_arguments_json.empty()) existing->item.arguments_json = event.tool_arguments_json;
  if (!event.tool_result_json.empty()) existing->item.result_json = event.tool_result_json;
  for (auto const& permission_id : event.permission_request_ids) add_permission_request_id(existing->item, permission_id);
  existing->item.status = ToolTimelineStatus::Running;
  existing->item.lifecycle = ToolLifecycleState::Progress;
  existing->item.result_summary = event.text;
  attach_permission_audits(state, existing->item);
  upsert_activity(state, event.call_id, existing->item);
}

void apply_tool_result(TuiEventState& state, ava::app::runtime::RuntimeEvent const& event)
{
  auto status = tool_status_from_event(event);
  auto item = ToolTimelineItem{
      .status = status,
      .name = event.tool_name,
      .argument_summary = {},
      .result_summary = event.text,
      .arguments_json = event.tool_arguments_json,
      .result_json = event.tool_result_json,
      .call_id = event.call_id,
      .lifecycle = tool_lifecycle_for_status(status)};
  item.diff = event.diff;
  item.diff_truncated = event.diff_truncated;
  item.changed_paths = event.changed_paths;
  item.truncated = event.truncated;
  item.byte_limited = event.byte_limited;
  item.line_limited = event.line_limited;
  item.spill_path = event.spill_path;
  item.spill_truncated = event.spill_truncated;
  if (event.output_bytes > 0) item.output_bytes = event.output_bytes;
  if (event.total_bytes > 0) item.total_bytes = event.total_bytes;
  if (event.output_lines > 0) item.output_lines = event.output_lines;
  if (event.total_lines > 0) item.total_lines = event.total_lines;
  if (event.start_line > 0) item.start_line = event.start_line;
  if (event.end_line > 0) item.end_line = event.end_line;
  if (event.next_offset_line > 0) item.next_offset_line = event.next_offset_line;
  if (event.omitted_bytes > 0) item.omitted_bytes = event.omitted_bytes;
  if (event.omitted_lines > 0) item.omitted_lines = event.omitted_lines;
  if (event.visible_matches > 0) item.visible_matches = event.visible_matches;
  if (event.total_matches > 0) item.total_matches = event.total_matches;
  for (auto const& permission_id : event.permission_request_ids) add_permission_request_id(item, permission_id);
  auto existing = find_pending_tool(state, event.call_id);
  if (existing != state.pending_tools.end()) {
    if (item.name.empty()) item.name = existing->item.name;
    item.argument_summary = existing->item.argument_summary;
    if (item.arguments_json.empty()) item.arguments_json = existing->item.arguments_json;
    if (item.result_json.empty()) item.result_json = existing->item.result_json;
    item.request_id = existing->request_id;
    item.correlation_id = existing->correlation_id;
    for (auto const& permission_id : existing->item.permission_request_ids) add_permission_request_id(item, permission_id);
    for (auto const& audit : existing->item.permissions) add_permission_audit(item, audit);
    state.pending_tools.erase(existing);
  }
  attach_permission_audits(state, item);
  upsert_activity(state, event.call_id, item);
  record_modified_file(state, item);
  state.transcript.push_back(TranscriptItem{.tool = std::move(item)});
}

std::string error_text_for_event(ava::app::runtime::RuntimeEvent const& event)
{
  if (!event.error_message.empty()) return event.error_message;
  if (!event.error_details.empty()) return event.error_details;
  return event.text;
}

std::string first_error_line(std::string_view text)
{
  auto const end = text.find_first_of("\r\n");
  auto line = end == std::string_view::npos ? text : text.substr(0, end);
  while (!line.empty()) {
    auto const byte = static_cast<unsigned char>(line.front());
    if (byte != ' ' && byte != '\t') break;
    line.remove_prefix(1);
  }
  while (!line.empty()) {
    auto const byte = static_cast<unsigned char>(line.back());
    if (byte != ' ' && byte != '\t') break;
    line.remove_suffix(1);
  }
  return std::string(line);
}

std::string strip_duplicate_error_summary(std::string details, std::string_view summary)
{
  if (summary.empty() || !details.starts_with(summary)) return details;
  details.erase(0, summary.size());
  while (!details.empty() && (details.front() == '\r' || details.front() == '\n')) details.erase(details.begin());
  return details;
}

TranscriptItem error_transcript_item(std::string text, std::string details)
{
  auto summary = first_error_line(text);
  if (summary.empty()) summary = "error";
  details = strip_duplicate_error_summary(std::move(details), summary);
  auto item = transcript_text_item("error", std::move(summary));
  item.meta = std::move(details);
  return item;
}

bool is_provider_tool_call_status(std::string_view status)
{
  return status == "tool_call_start" || status == "tool_call_delta" || status == "tool_call_end";
}

std::string provider_tool_call_detail(std::string_view status)
{
  if (status == "tool_call_start") return "provider is preparing tool call";
  if (status == "tool_call_delta") return "streaming tool arguments";
  if (status == "tool_call_end") return "tool call ready";
  return {};
}

ToolTimelineStatus provider_tool_call_activity_status(std::string_view status)
{
  return status == "tool_call_end" ? ToolTimelineStatus::Success : ToolTimelineStatus::Running;
}

ToolLifecycleState provider_tool_call_lifecycle(std::string_view status)
{
  if (status == "tool_call_start") return ToolLifecycleState::ProviderAnnounced;
  if (status == "tool_call_delta") return ToolLifecycleState::ArgumentsStreaming;
  if (status == "tool_call_end") return ToolLifecycleState::ArgumentsComplete;
  return ToolLifecycleState::ProviderAnnounced;
}

std::string provider_tool_call_activity_id(ava::app::runtime::RuntimeEvent const& event)
{
  if (!event.call_id.empty()) return event.call_id;
  auto const tool_key = event.tool_name.empty() ? std::string("tool_call") : event.tool_name;
  return "provider:tool_call:" + tool_key;
}

void upsert_provider_tool_call_activity(TuiEventState& state, ava::app::runtime::RuntimeEvent const& event)
{
  auto item = SidebarActivityItem{.id = provider_tool_call_activity_id(event),
                                  .label = event.tool_name.empty() ? std::string("tool call") : event.tool_name,
                                  .detail = provider_tool_call_detail(event.status),
                                  .status = provider_tool_call_activity_status(event.status)};
  auto existing =
      std::ranges::find_if(state.activity, [&](SidebarActivityItem const& activity) { return activity.id == item.id; });
  if (existing != state.activity.end()) {
    if (event.tool_name.empty() && !existing->label.empty()) item.label = existing->label;
    *existing = std::move(item);
    return;
  }
  state.activity.push_back(std::move(item));
  constexpr auto kMaxActivityItems = std::size_t{8};
  if (state.activity.size() > kMaxActivityItems) {
    state.activity.erase(
        state.activity.begin(),
        state.activity.begin() + static_cast<std::ptrdiff_t>(state.activity.size() - kMaxActivityItems));
  }
}

void upsert_provider_tool_call(TuiEventState& state, ava::app::runtime::RuntimeEvent const& event)
{
  auto const tool_id = provider_tool_call_activity_id(event);
  auto existing = find_pending_tool(state, tool_id);
  if (existing == state.pending_tools.end()) {
    state.pending_tools.push_back(PendingToolItem{
        .call_id = tool_id,
        .request_id = {},
        .correlation_id = {},
        .item = ToolTimelineItem{.status = ToolTimelineStatus::Running,
                                 .name = event.tool_name,
                                 .argument_summary = event.status == "tool_call_delta" ? event.text : std::string{},
                                 .result_summary = {},
                                 .call_id = tool_id,
                                 .lifecycle = provider_tool_call_lifecycle(event.status)}});
    upsert_activity(state, tool_id, state.pending_tools.back().item);
    return;
  }

  if (!event.tool_name.empty()) existing->item.name = event.tool_name;
  if (event.status == "tool_call_delta") existing->item.argument_summary += event.text;
  existing->item.status = ToolTimelineStatus::Running;
  existing->item.lifecycle = provider_tool_call_lifecycle(event.status);
  upsert_activity(state, tool_id, existing->item);
}

void apply_provider_event(TuiEventState& state, ava::app::runtime::RuntimeEvent const& event)
{
  if (is_provider_tool_call_status(event.status)) {
    upsert_provider_tool_call(state, event);
    upsert_provider_tool_call_activity(state, event);
    return;
  }

  if (event.status == "tui:permission_request" || event.status == "tui:permission_allow" ||
      event.status == "tui:permission_deny" || event.status == "tui:question_request" ||
      event.status == "tui:question_answer" || event.status == "tui:question_cancel") {
    remember_permission_provider_event(state, event);
    auto const text = event.text.empty() ? event.status : event.text;
    auto const prompt_status = event.status.substr(std::string_view{"tui:"}.size());
    state.transcript.push_back(transcript_text_item("audit", text));
    upsert_sidebar_activity(
        state, SidebarActivityItem{.id = "prompt:" + event.status,
                                   .label = prompt_status.starts_with("permission") ? "permission" : "question",
                                   .detail = text,
                                   .status = prompt_status.ends_with("deny") || prompt_status.ends_with("cancel")
                                                 ? ToolTimelineStatus::Error
                                                 : ToolTimelineStatus::Success});
    return;
  }

  if (event.status == "error") {
    auto detail = error_text_for_event(event);
    if (detail.empty()) return;
    auto id = event.call_id.empty() ? std::string("provider:error") : event.call_id;
    auto label = event.tool_name.empty() ? std::string("provider/error") : event.tool_name;
    upsert_sidebar_activity(state, SidebarActivityItem{.id = std::move(id),
                                                       .label = std::move(label),
                                                       .detail = std::move(detail),
                                                       .status = ToolTimelineStatus::Error});
    return;
  }
}

std::optional<ToolLifecycleState> lifecycle_from_payload(std::string_view payload_json)
{
  auto const value = ava::core::json::string_field(payload_json, "lifecycle");
  if (!value) return std::nullopt;
  if (*value == "announced" || *value == "provider_announced") return ToolLifecycleState::ProviderAnnounced;
  if (*value == "arguments_streaming") return ToolLifecycleState::ArgumentsStreaming;
  if (*value == "arguments_complete") return ToolLifecycleState::ArgumentsComplete;
  if (*value == "execution_started" || *value == "executing") return ToolLifecycleState::ExecutionStarted;
  if (*value == "progress") return ToolLifecycleState::Progress;
  if (*value == "complete" || *value == "completed") return ToolLifecycleState::Complete;
  if (*value == "canceled" || *value == "cancelled" || *value == "aborted") return ToolLifecycleState::Canceled;
  if (*value == "error") return ToolLifecycleState::Error;
  return std::nullopt;
}

void apply_tool_payload_metadata(ToolTimelineItem& item, std::string_view payload_json)
{
  if (auto lifecycle = lifecycle_from_payload(payload_json)) item.lifecycle = *lifecycle;
  if (auto args = ava::core::json::object_field(payload_json, "args")) item.arguments_json = *args;
  if (auto result = ava::core::json::object_field(payload_json, "result")) item.result_json = *result;
  if (auto args = ava::core::json::string_field(payload_json, "args_json")) item.arguments_json = *args;
  if (auto result = ava::core::json::string_field(payload_json, "result_json")) item.result_json = *result;
  if (auto diff = ava::core::json::string_field(payload_json, "diff")) item.diff = *diff;
  if (auto spill = ava::core::json::string_field(payload_json, "spill_path")) item.spill_path = *spill;
  auto changed_paths = ava::core::json::strings_in_array_field(payload_json, "changed_paths");
  if (changed_paths.empty()) changed_paths = ava::core::json::strings_in_array_field(payload_json, "changed_files");
  for (auto& path : changed_paths) {
    if (!path.empty() && std::ranges::find(item.changed_paths, path) == item.changed_paths.end()) {
      item.changed_paths.push_back(std::move(path));
    }
  }
  for (auto const& permission_id : ava::core::json::strings_in_array_field(payload_json, "permission_request_ids")) {
    add_permission_request_id(item, permission_id);
  }
  if (auto permission_id = ava::core::json::string_field(payload_json, "permission_request_id")) {
    add_permission_request_id(item, *permission_id);
  }
  if (auto call_id = ava::core::json::string_field(payload_json, "call_id")) item.call_id = *call_id;
  if (auto request_id = ava::core::json::string_field(payload_json, "request_id")) item.request_id = *request_id;
  if (auto correlation_id = ava::core::json::string_field(payload_json, "correlation_id")) {
    item.correlation_id = *correlation_id;
  }
  item.diff_truncated = item.diff_truncated || bool_field(payload_json, "diff_truncated");
  item.truncated = item.truncated || bool_field(payload_json, "truncated");
  item.byte_limited = item.byte_limited || bool_field(payload_json, "byte_limited");
  item.line_limited = item.line_limited || bool_field(payload_json, "line_limited");
  item.spill_truncated = item.spill_truncated || bool_field(payload_json, "spill_truncated");
  if (auto value = optional_bool_field(payload_json, "details_visible")) item.details_visible = *value;
  if (auto value = optional_size_field(payload_json, "output_bytes")) item.output_bytes = *value;
  if (auto value = optional_size_field(payload_json, "total_bytes")) item.total_bytes = *value;
  if (auto value = optional_size_field(payload_json, "output_lines")) item.output_lines = *value;
  if (auto value = optional_size_field(payload_json, "visible_lines")) item.output_lines = *value;
  if (auto value = optional_size_field(payload_json, "returned_lines")) item.output_lines = *value;
  if (auto value = optional_size_field(payload_json, "total_lines")) item.total_lines = *value;
  if (auto value = optional_size_field(payload_json, "start_line")) item.start_line = *value;
  if (auto value = optional_size_field(payload_json, "end_line")) item.end_line = *value;
  if (auto value = optional_size_field(payload_json, "next_offset_line")) item.next_offset_line = *value;
  if (auto value = optional_size_field(payload_json, "next_offset")) item.next_offset_line = *value;
  if (auto value = optional_size_field(payload_json, "omitted_bytes")) item.omitted_bytes = *value;
  if (auto value = optional_size_field(payload_json, "omitted_output_bytes")) item.omitted_bytes = *value;
  if (auto value = optional_size_field(payload_json, "omitted_lines")) item.omitted_lines = *value;
  if (auto value = optional_size_field(payload_json, "omitted_line_count")) item.omitted_lines = *value;
  if (auto value = optional_size_field(payload_json, "visible_matches")) item.visible_matches = *value;
  if (auto value = optional_size_field(payload_json, "output_matches")) item.visible_matches = *value;
  if (auto value = optional_size_field(payload_json, "returned_matches")) item.visible_matches = *value;
  if (auto value = optional_size_field(payload_json, "total_matches")) item.total_matches = *value;
}

void annotate_tool_payload_metadata(TuiEventState& state, ava::app::runtime::RuntimeEvent const& event,
                                    ava::app::EventEnvelope const& envelope)
{
  if (event.type != ava::app::runtime::RuntimeEventType::ToolStart && event.type != ava::app::runtime::RuntimeEventType::ToolProgress &&
      event.type != ava::app::runtime::RuntimeEventType::ToolResult && event.type != ava::app::runtime::RuntimeEventType::ProviderEvent) {
    return;
  }

  if (auto pending = find_pending_tool(state, event.call_id, envelope.request_id.value_or(""),
                                       envelope.correlation_id.value_or(""));
      pending != state.pending_tools.end()) {
    if (envelope.request_id) pending->item.request_id = *envelope.request_id;
    if (envelope.correlation_id) pending->item.correlation_id = *envelope.correlation_id;
    apply_tool_payload_metadata(pending->item, envelope.payload_json);
    attach_permission_audits(state, pending->item);
    return;
  }

  for (auto item = state.transcript.rbegin(); item != state.transcript.rend(); ++item) {
    if (!item->tool) continue;
    bool const id_matches = (!event.call_id.empty() && item->tool->call_id == event.call_id) ||
                            (envelope.request_id && item->tool->request_id == *envelope.request_id) ||
                            (envelope.correlation_id && item->tool->correlation_id == *envelope.correlation_id);
    if (!id_matches && !event.call_id.empty()) continue;
    if (envelope.request_id) item->tool->request_id = *envelope.request_id;
    if (envelope.correlation_id) item->tool->correlation_id = *envelope.correlation_id;
    apply_tool_payload_metadata(*item->tool, envelope.payload_json);
    attach_permission_audits(state, *item->tool);
    return;
  }
}

}  // namespace

void apply_runtime_event(TuiEventState& state, ava::app::runtime::RuntimeEvent const& event)
{
  using ava::app::runtime::RuntimeEventType;

  state.current_mode = event.mode;
  if (!event.provider_id.empty()) state.current_provider_id = event.provider_id;
  if (!event.model_id.empty()) state.current_model_id = event.model_id;

  switch (event.type) {
    case RuntimeEventType::SessionStart:
      state.run_status = TuiEventRunStatus::Running;
      state.stream_assistant_transcript_index = std::nullopt;
      state.pending_assistant_meta.clear();
      state.pending_reasoning_text.clear();
      state.stop_reason.clear();
      state.error_text.clear();
      state.error_details.clear();
      break;
    case RuntimeEventType::UserMessage:
      state.stream_assistant_transcript_index = std::nullopt;
      state.pending_assistant_meta.clear();
      state.pending_reasoning_text.clear();
      state.transcript.push_back(transcript_text_item("you", event.text));
      state.run_status = TuiEventRunStatus::Running;
      break;
    case RuntimeEventType::MessageUpdate:
      update_pending_assistant_meta(state, event);
      state.pending_assistant_text += event.text;
      if (state.activity.empty() || state.activity.back().label != "responding") {
        state.activity.push_back(
            SidebarActivityItem{.id = "responding", .label = "responding", .detail = "assistant is writing"});
      }
      state.run_status = TuiEventRunStatus::Running;
      break;
    case RuntimeEventType::MessageEnd:
      commit_pending_assistant_text(state, assistant_meta_for_event(event));
      settle_responding_activity(state, ToolTimelineStatus::Success, "assistant responded");
      state.run_status = TuiEventRunStatus::Completed;
      break;
    case RuntimeEventType::ReasoningStart:
      state.pending_reasoning_text.clear();
      state.pending_reasoning_redacted = event.reasoning_redacted;
      update_pending_assistant_meta(state, event);
      upsert_sidebar_activity(
          state, SidebarActivityItem{.id = "reasoning", .label = "reasoning", .detail = "model reasoning started"});
      state.run_status = TuiEventRunStatus::Running;
      break;
    case RuntimeEventType::ReasoningDelta:
      update_pending_assistant_meta(state, event);
      if (event.reasoning_redacted) {
        state.pending_reasoning_redacted = true;
        if (state.pending_reasoning_text.empty()) state.pending_reasoning_text = "[reasoning redacted]";
      } else if (!state.pending_reasoning_redacted) {
        state.pending_reasoning_text += event.text;
      }
      upsert_sidebar_activity(state,
                              SidebarActivityItem{.id = "reasoning",
                                                  .label = "reasoning",
                                                  .detail = state.pending_reasoning_redacted ? "reasoning redacted"
                                                            : event.text.empty()             ? "model is reasoning"
                                                                                             : event.text});
      state.run_status = TuiEventRunStatus::Running;
      break;
    case RuntimeEventType::ReasoningEnd:
      update_pending_assistant_meta(state, event);
      if (event.reasoning_redacted && state.pending_reasoning_text.empty()) {
        state.pending_reasoning_text = "[reasoning redacted]";
        state.pending_reasoning_redacted = true;
      }
      upsert_sidebar_activity(state, SidebarActivityItem{.id = "reasoning",
                                                         .label = "reasoning",
                                                         .detail = "model reasoning completed",
                                                         .status = ToolTimelineStatus::Success});
      state.run_status = TuiEventRunStatus::Running;
      break;
    case RuntimeEventType::AssistantMessage:
      apply_assistant_final(state, event);
      settle_responding_activity(state, ToolTimelineStatus::Success, "assistant responded");
      state.run_status = TuiEventRunStatus::Completed;
      break;
    case RuntimeEventType::ToolStart:
      commit_pending_reasoning_turn(state, assistant_meta_for_event(event));
      apply_tool_start(state, event);
      state.run_status = TuiEventRunStatus::Running;
      break;
    case RuntimeEventType::ToolProgress:
      apply_tool_progress(state, event);
      state.run_status = TuiEventRunStatus::Running;
      break;
    case RuntimeEventType::ToolResult:
      apply_tool_result(state, event);
      state.run_status = TuiEventRunStatus::Completed;
      break;
    case RuntimeEventType::CompactionStart:
      upsert_sidebar_activity(state,
                              SidebarActivityItem{.id = first_non_empty({event.trigger, "compaction"}),
                                                  .label = "compaction",
                                                  .detail = compact_lifecycle_detail("compaction started", event),
                                                  .status = ToolTimelineStatus::Running});
      state.run_status = TuiEventRunStatus::Running;
      break;
    case RuntimeEventType::CompactionEnd: {
      auto detail = compact_lifecycle_detail("compaction completed", event);
      state.transcript.push_back(transcript_text_item("compaction", detail));
      upsert_sidebar_activity(state, SidebarActivityItem{.id = first_non_empty({event.trigger, "compaction"}),
                                                         .label = "compaction",
                                                         .detail = std::move(detail),
                                                         .status = ToolTimelineStatus::Success});
      state.run_status = TuiEventRunStatus::Completed;
      break;
    }
    case RuntimeEventType::Retry: {
      auto detail = std::string("retrying");
      if (!event.reason.empty()) detail += " after " + event.reason;
      if (!event.trigger.empty() && event.trigger != event.reason) detail += " (" + event.trigger + ")";
      if (event.attempt > 0) {
        detail += " attempt " + std::to_string(event.attempt);
        if (event.max_attempts > 0) detail += "/" + std::to_string(event.max_attempts);
      }
      if (event.delay_ms > 0) detail += " delay=" + std::to_string(event.delay_ms) + "ms";
      if (event.estimated_tokens > 0) detail += " tokens~" + std::to_string(event.estimated_tokens);
      if (event.threshold_tokens > 0) detail += "/" + std::to_string(event.threshold_tokens);
      if (event.snapshot_entries > 0 || event.current_entries > 0) {
        detail += " entries=" + std::to_string(event.snapshot_entries) + "/" + std::to_string(event.current_entries);
      }
      if (!event.text.empty()) detail += " - " + event.text;
      state.transcript.push_back(transcript_text_item("audit", detail));
      upsert_sidebar_activity(state, SidebarActivityItem{.id = retry_activity_id(event),
                                                         .label = "retry",
                                                         .detail = std::move(detail),
                                                         .status = ToolTimelineStatus::Running});
      state.run_status = TuiEventRunStatus::Running;
      break;
    }
    case RuntimeEventType::RetryTick: {
      auto detail = std::string("retry countdown");
      if (!event.reason.empty()) detail += " after " + event.reason;
      if (!event.trigger.empty() && event.trigger != event.reason) detail += " (" + event.trigger + ")";
      if (event.attempt > 0) {
        detail += " attempt " + std::to_string(event.attempt);
        if (event.max_attempts > 0) detail += "/" + std::to_string(event.max_attempts);
      }
      if (event.delay_ms > 0) detail += " delay=" + std::to_string(event.delay_ms) + "ms";
      detail += " remaining=" + std::to_string(event.remaining_ms) + "ms";
      if (!event.text.empty()) detail += " - " + event.text;
      upsert_retry_countdown_transcript(state, detail);
      upsert_sidebar_activity(state, SidebarActivityItem{.id = retry_activity_id(event),
                                                         .label = "retry",
                                                         .detail = std::move(detail),
                                                         .status = ToolTimelineStatus::Running});
      state.run_status = TuiEventRunStatus::Running;
      break;
    }
    case RuntimeEventType::Canceled:
      apply_canceled_event(state, event);
      break;
    case RuntimeEventType::Error:
      if (is_cancel_error(event)) {
        apply_canceled_event(state, event);
        break;
      }
      commit_pending_assistant_text(state, assistant_meta_for_event(event));
      state.error_text = error_text_for_event(event);
      state.error_details = event.error_details;
      if (!state.error_text.empty() || !state.error_details.empty()) {
        auto transcript_text = state.error_text.empty() ? state.error_details : state.error_text;
        state.transcript.push_back(error_transcript_item(std::move(transcript_text), state.error_details));
      }
      settle_responding_activity(state, ToolTimelineStatus::Error, "assistant failed");
      state.run_status = TuiEventRunStatus::Error;
      break;
    case RuntimeEventType::Done:
      commit_pending_assistant_text(state, assistant_meta_for_event(event));
      state.stop_reason = event.stop_reason;
      state.provider_iterations = event.provider_iterations;
      state.tool_calls = event.tool_calls;
      settle_responding_activity(state, ToolTimelineStatus::Success, "assistant responded");
      state.run_status = TuiEventRunStatus::Done;
      break;
    case RuntimeEventType::ProviderEvent:
      apply_provider_event(state, event);
      break;
  }
}

void apply_event_envelope(TuiEventState& state, ava::app::EventEnvelope const& envelope)
{
  remember_envelope_ids(state, envelope);

  if (envelope.name == "permission_requested" || envelope.name == "question_requested") {
    if (envelope.name == "permission_requested") remember_permission_request_envelope(state, envelope);
    apply_prompt_request_envelope(state, envelope);
    return;
  }

  if (envelope.name == "permission_replied" || envelope.name == "question_replied") {
    if (envelope.name == "permission_replied") remember_permission_reply_envelope(state, envelope);
    auto const replied =
        envelope.name == "permission_replied" ? std::string("permission replied") : std::string("question replied");
    state.transcript.push_back(transcript_text_item("audit", replied));
    upsert_sidebar_activity(
        state, SidebarActivityItem{.id = first_non_empty({envelope.request_id.value_or(""),
                                                          envelope.correlation_id.value_or(""), replied}),
                                   .label = envelope.name == "permission_replied" ? "permission" : "question",
                                   .detail = replied,
                                   .status = ToolTimelineStatus::Success});
    return;
  }

  if (envelope.name == "cancel_requested") {
    auto detail = std::string("cancel requested");
    if (bool_field(envelope.payload_json, "active_run")) detail += " for active run";
    auto const cleared_steer = size_field(envelope.payload_json, "cleared_steer");
    auto const cleared_follow_up = size_field(envelope.payload_json, "cleared_follow_up");
    if (cleared_steer > 0 || cleared_follow_up > 0) {
      detail += " cleared steer=" + std::to_string(cleared_steer) + " follow-up=" + std::to_string(cleared_follow_up);
    }
    state.transcript.push_back(transcript_text_item("audit", detail));
    upsert_sidebar_activity(state,
                            SidebarActivityItem{.id = first_non_empty({envelope.request_id.value_or(""),
                                                                       envelope.correlation_id.value_or(""), "cancel"}),
                                                .label = "cancel",
                                                .detail = std::move(detail),
                                                .status = ToolTimelineStatus::Error});
    return;
  }

  if (envelope.name == "steer_queued" || envelope.name == "steer_applied" || envelope.name == "steer_skipped" ||
      envelope.name == "follow_up_queued" || envelope.name == "follow_up_started" ||
      envelope.name == "follow_up_skipped") {
    if (envelope.name == "steer_queued" || envelope.name == "follow_up_queued") {
      upsert_queued_message(state, envelope);
    } else {
      remove_queued_message(state, envelope);
    }
    auto detail = queue_event_detail(envelope);
    state.transcript.push_back(transcript_text_item("audit", detail));
    upsert_sidebar_activity(state,
                            SidebarActivityItem{.id = queue_event_id(envelope),
                                                .label = envelope.name.starts_with("steer") ? "steer" : "follow-up",
                                                .detail = std::move(detail),
                                                .status = queue_event_status(envelope.name)});
    return;
  }

  auto event = runtime_event_from_envelope(envelope);
  if (!event) return;
  if (event->provider_id.empty()) event->provider_id = state.current_provider_id;
  if (event->model_id.empty()) event->model_id = state.current_model_id;
  if (!ava::core::json::string_field(envelope.payload_json, "mode")) event->mode = state.current_mode;
  apply_runtime_event(state, *event);
  annotate_pending_tool_ids(state, *event, envelope);
  annotate_tool_payload_metadata(state, *event, envelope);
}

std::vector<TranscriptItem> event_state_transcript_snapshot(TuiEventState const& state)
{
  auto snapshot = state.transcript;
  snapshot.reserve(snapshot.size() + state.pending_tools.size() + (state.pending_assistant_text.empty() ? 0U : 1U) +
                   (state.pending_reasoning_text.empty() ? 0U : 1U));
  if (!state.pending_reasoning_text.empty() || !state.pending_assistant_text.empty()) {
    snapshot.push_back(TranscriptItem{.label = "ava",
                                      .text = state.pending_assistant_text,
                                      .text_model = text_from_markdown(state.pending_assistant_text),
                                      .meta = state.pending_assistant_meta,
                                      .thinking = state.pending_reasoning_text,
                                      .thinking_model = text_from_plain(state.pending_reasoning_text)});
  }
  for (auto const& tool : state.pending_tools) {
    snapshot.push_back(TranscriptItem{.tool = tool.item});
  }
  return snapshot;
}

}  // namespace ava::tui
