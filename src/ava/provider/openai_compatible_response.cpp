#include "ava/provider/openai_compatible_response.h"

#include <cstddef>
#include <optional>
#include <utility>

#include "ava/provider/openai_compatible_provider.h"
#include "ava/provider/openai_compatible_response_support.h"

namespace ava::provider {

OpenAICompatibleStreamParser::OpenAICompatibleStreamParser(std::string reasoning_format)
    : reasoning_format_(std::move(reasoning_format))
{
}

ava::core::Result<std::vector<StreamEvent>> OpenAICompatibleStreamParser::append(std::string_view chunk)
{
  std::vector<StreamEvent> events;
  pending_line_.append(chunk);
  std::size_t line_start = 0;
  std::size_t search_from = scan_offset_;
  while (true) {
    auto const newline = pending_line_.find('\n', search_from);
    if (newline == std::string::npos) break;
    detail::append_openai_compatible_events_for_sse_line(
        events, open_tool_call_ids_, reasoning_open_, usage_, stop_reason_, saw_data_, done_seen_, error_seen_, data_,
        pending_line_.substr(line_start, newline - line_start), reasoning_format_);
    line_start = newline + 1;
    search_from = line_start;
  }
  if (line_start > 0) pending_line_.erase(0, line_start);
  scan_offset_ = pending_line_.size();
  return events;
}

ava::core::Result<std::vector<StreamEvent>> OpenAICompatibleStreamParser::finish()
{
  std::vector<StreamEvent> events;
  if (!pending_line_.empty()) {
    detail::append_openai_compatible_events_for_sse_line(events, open_tool_call_ids_, reasoning_open_, usage_,
                                                         stop_reason_, saw_data_, done_seen_, error_seen_, data_,
                                                         std::move(pending_line_), reasoning_format_);
    pending_line_.clear();
  }
  scan_offset_ = 0;
  if (!data_.empty()) {
    detail::append_openai_compatible_event_for_data(events, open_tool_call_ids_, reasoning_open_, usage_, stop_reason_,
                                                    saw_data_, done_seen_, error_seen_, data_, reasoning_format_);
    data_.clear();
  }
  detail::append_openai_compatible_tool_call_end_events(events, open_tool_call_ids_);
  detail::append_finish_reasoning_if_open(events, reasoning_open_, reasoning_format_);
  if (saw_data_ && !done_seen_ && !error_seen_) {
    events.push_back(StreamEvent{.type = StreamEventType::Error,
                                 .text = "",
                                 .tool_call_id = "",
                                 .tool_name = "",
                                 .error_message = "OpenAI-compatible SSE stream ended before done marker",
                                 .usage = std::nullopt});
  }
  saw_data_ = false;
  done_seen_ = false;
  error_seen_ = false;
  usage_ = std::nullopt;
  stop_reason_.clear();
  open_tool_call_ids_.clear();
  reasoning_open_ = false;
  return events;
}

ava::core::Result<std::vector<StreamEvent>> parse_openai_compatible_sse(std::string_view sse,
                                                                        std::string reasoning_format)
{
  OpenAICompatibleStreamParser parser(std::move(reasoning_format));
  auto events = parser.append(sse);
  if (!events) return std::unexpected(std::move(events.error()));
  auto final_events = parser.finish();
  if (!final_events) return std::unexpected(std::move(final_events.error()));
  events->insert(events->end(), final_events->begin(), final_events->end());
  return events;
}

}  // namespace ava::provider
