#include "ava/tui/event_state.h"

#include <algorithm>
#include <string_view>
#include <utility>

namespace ava::tui {
namespace {

auto find_pending_tool(TuiEventState& state, const std::string& call_id) {
  return std::ranges::find_if(state.pending_tools,
                              [&](const PendingToolItem& tool) { return tool.call_id == call_id; });
}

std::string_view trim_trailing_ascii_space(std::string_view text) {
  while (!text.empty()) {
    const auto byte = static_cast<unsigned char>(text.back());
    if (byte != ' ' && byte != '\n' && byte != '\r' && byte != '\t' && byte != '\f' && byte != '\v') break;
    text.remove_suffix(1);
  }
  return text;
}

ToolTimelineStatus tool_status_from_event(const ava::app::RuntimeEvent& event) {
  return event.status == "error" ? ToolTimelineStatus::Error : ToolTimelineStatus::Success;
}

void upsert_activity(TuiEventState& state, const std::string& call_id, const ToolTimelineItem& item) {
  const auto id = call_id.empty() ? item.name + ":" + item.argument_summary : call_id;
  const auto label = item.name.empty() ? std::string("tool") : item.name;
  const auto detail = item.result_summary.empty() ? item.argument_summary : item.result_summary;
  auto existing =
      std::ranges::find_if(state.activity, [&](const SidebarActivityItem& activity) { return activity.id == id; });
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

void upsert_sidebar_activity(TuiEventState& state, SidebarActivityItem item) {
  auto existing =
      std::ranges::find_if(state.activity, [&](const SidebarActivityItem& activity) { return activity.id == item.id; });
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

bool is_cancel_error(const ava::app::RuntimeEvent& event) {
  return event.error_message == "agent loop canceled" || event.text == "agent loop canceled" ||
         event.error_details.find("agent loop canceled") != std::string::npos;
}

std::optional<std::string> path_from_argument_summary(std::string_view summary) {
  // Tool argument summaries are the only stable TUI-side source for file names today. Keep this conservative: if the
  // summary no longer exposes a top-level path= field, the sidebar omits the file instead of guessing.
  constexpr std::string_view marker = "path=";
  const auto start = summary.find(marker);
  if (start == std::string_view::npos) return std::nullopt;
  auto value = summary.substr(start + marker.size());
  const auto comma = value.find(',');
  if (comma != std::string_view::npos) value = value.substr(0, comma);
  while (!value.empty() && value.front() == ' ') value.remove_prefix(1);
  while (!value.empty() && value.back() == ' ') value.remove_suffix(1);
  if (value.empty()) return std::nullopt;
  return std::string(value);
}

void record_modified_file(TuiEventState& state, const ToolTimelineItem& item) {
  if (item.status != ToolTimelineStatus::Success) return;
  if (item.name != "write_file" && item.name != "edit_file" && item.name != "apply_patch") return;
  auto path = path_from_argument_summary(item.argument_summary);
  if (!path) return;
  auto existing =
      std::ranges::find_if(state.modified_files, [&](const SidebarModifiedFile& file) { return file.path == *path; });
  if (existing == state.modified_files.end()) {
    state.modified_files.push_back(SidebarModifiedFile{.path = std::move(*path)});
  }
  constexpr auto kMaxModifiedFiles = std::size_t{12};
  if (state.modified_files.size() > kMaxModifiedFiles) {
    state.modified_files.erase(
        state.modified_files.begin(),
        state.modified_files.begin() + static_cast<std::ptrdiff_t>(state.modified_files.size() - kMaxModifiedFiles));
  }
}

void append_assistant_text(TuiEventState& state, std::string text) {
  if (text.empty()) return;
  state.transcript.push_back(TranscriptItem{.label = "ava", .text = std::move(text)});
  state.stream_assistant_transcript_index = std::nullopt;
}

void commit_pending_assistant_text(TuiEventState& state) {
  if (state.pending_assistant_text.empty()) return;
  state.transcript.push_back(TranscriptItem{.label = "ava", .text = std::move(state.pending_assistant_text)});
  state.pending_assistant_text.clear();
  state.stream_assistant_transcript_index = state.transcript.size() - 1;
}

void apply_assistant_final(TuiEventState& state, const ava::app::RuntimeEvent& event) {
  auto final_text = event.text.empty() ? std::move(state.pending_assistant_text) : event.text;
  state.pending_assistant_text.clear();
  if (final_text.empty()) return;

  if (state.stream_assistant_transcript_index && *state.stream_assistant_transcript_index < state.transcript.size()) {
    auto& item = state.transcript[*state.stream_assistant_transcript_index];
    if (!item.tool && item.label == "ava") {
      item.text = std::move(final_text);
      state.stream_assistant_transcript_index = std::nullopt;
      return;
    }
  }

  if (!state.transcript.empty()) {
    const auto& last = state.transcript.back();
    if (!last.tool && last.label == "ava" &&
        trim_trailing_ascii_space(last.text) == trim_trailing_ascii_space(final_text)) {
      state.stream_assistant_transcript_index = std::nullopt;
      return;
    }
  }

  append_assistant_text(state, std::move(final_text));
}

ToolTimelineItem tool_item_from_event(const ava::app::RuntimeEvent& event, ToolTimelineStatus status) {
  return ToolTimelineItem{.status = status, .name = event.tool_name, .argument_summary = event.text};
}

void apply_tool_start(TuiEventState& state, const ava::app::RuntimeEvent& event) {
  auto item = tool_item_from_event(event, ToolTimelineStatus::Running);
  auto existing = find_pending_tool(state, event.call_id);
  if (existing != state.pending_tools.end()) {
    existing->item = std::move(item);
    upsert_activity(state, event.call_id, existing->item);
    return;
  }
  state.pending_tools.push_back(PendingToolItem{.call_id = event.call_id, .item = std::move(item)});
  upsert_activity(state, event.call_id, state.pending_tools.back().item);
}

void apply_tool_progress(TuiEventState& state, const ava::app::RuntimeEvent& event) {
  auto existing = find_pending_tool(state, event.call_id);
  if (existing == state.pending_tools.end()) {
    state.pending_tools.push_back(PendingToolItem{
        .call_id = event.call_id,
        .item = ToolTimelineItem{
            .status = ToolTimelineStatus::Running, .name = event.tool_name, .result_summary = event.text}});
    return;
  }
  if (!event.tool_name.empty()) existing->item.name = event.tool_name;
  existing->item.result_summary = event.text;
  upsert_activity(state, event.call_id, existing->item);
}

void apply_tool_result(TuiEventState& state, const ava::app::RuntimeEvent& event) {
  auto item =
      ToolTimelineItem{.status = tool_status_from_event(event), .name = event.tool_name, .result_summary = event.text};
  auto existing = find_pending_tool(state, event.call_id);
  if (existing != state.pending_tools.end()) {
    if (item.name.empty()) item.name = existing->item.name;
    item.argument_summary = existing->item.argument_summary;
    state.pending_tools.erase(existing);
  }
  upsert_activity(state, event.call_id, item);
  record_modified_file(state, item);
  state.transcript.push_back(TranscriptItem{.tool = std::move(item)});
}

std::string error_text_for_event(const ava::app::RuntimeEvent& event) {
  if (!event.error_message.empty()) return event.error_message;
  if (!event.error_details.empty()) return event.error_details;
  return event.text;
}

}  // namespace

void apply_runtime_event(TuiEventState& state, const ava::app::RuntimeEvent& event) {
  using ava::app::RuntimeEventType;

  switch (event.type) {
    case RuntimeEventType::SessionStart:
      state.run_status = TuiEventRunStatus::Running;
      state.stream_assistant_transcript_index = std::nullopt;
      state.stop_reason.clear();
      state.error_text.clear();
      state.error_details.clear();
      break;
    case RuntimeEventType::UserMessage:
      state.stream_assistant_transcript_index = std::nullopt;
      state.transcript.push_back(TranscriptItem{.label = "you", .text = event.text});
      state.run_status = TuiEventRunStatus::Running;
      break;
    case RuntimeEventType::MessageUpdate:
      state.pending_assistant_text += event.text;
      if (state.activity.empty() || state.activity.back().label != "responding") {
        state.activity.push_back(
            SidebarActivityItem{.id = "responding", .label = "responding", .detail = "assistant is writing"});
      }
      state.run_status = TuiEventRunStatus::Running;
      break;
    case RuntimeEventType::MessageEnd:
      commit_pending_assistant_text(state);
      state.run_status = TuiEventRunStatus::Completed;
      break;
    case RuntimeEventType::AssistantMessage:
      apply_assistant_final(state, event);
      state.run_status = TuiEventRunStatus::Completed;
      break;
    case RuntimeEventType::ToolStart:
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
    case RuntimeEventType::Error:
      if (is_cancel_error(event)) {
        state.error_text = "stopped by user";
        state.error_details.clear();
        state.transcript.push_back(TranscriptItem{.label = "ava", .text = "stopped by user"});
        upsert_sidebar_activity(state, SidebarActivityItem{.id = "stopped",
                                                           .label = "stopped",
                                                           .detail = "active work was stopped",
                                                           .status = ToolTimelineStatus::Error});
        state.run_status = TuiEventRunStatus::Canceled;
        break;
      }
      state.error_text = error_text_for_event(event);
      state.error_details = event.error_details;
      if (!state.error_text.empty() || !state.error_details.empty()) {
        state.transcript.push_back(TranscriptItem{
            .label = "error", .text = state.error_details.empty() ? state.error_text : state.error_details});
      }
      state.run_status = TuiEventRunStatus::Error;
      break;
    case RuntimeEventType::Done:
      commit_pending_assistant_text(state);
      state.stop_reason = event.stop_reason;
      state.provider_iterations = event.provider_iterations;
      state.tool_calls = event.tool_calls;
      state.run_status = TuiEventRunStatus::Done;
      break;
    case RuntimeEventType::ProviderEvent:
      break;
  }
}

std::vector<TranscriptItem> event_state_transcript_snapshot(const TuiEventState& state) {
  auto snapshot = state.transcript;
  snapshot.reserve(snapshot.size() + state.pending_tools.size() + (state.pending_assistant_text.empty() ? 0U : 1U));
  for (const auto& tool : state.pending_tools) {
    snapshot.push_back(TranscriptItem{.tool = tool.item});
  }
  if (!state.pending_assistant_text.empty()) {
    snapshot.push_back(TranscriptItem{.label = "ava", .text = state.pending_assistant_text});
  }
  return snapshot;
}

}  // namespace ava::tui
