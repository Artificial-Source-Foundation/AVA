#include "ava/provider/openai_stream_parser.h"

#include <string>
#include <vector>

#include "ava/provider/openai_stream_events.h"

namespace ava::provider {

ava::core::Result<std::vector<StreamEvent>> OpenAIStreamParser::append(std::string_view chunk)
{
  std::vector<StreamEvent> events;
  pending_line_.append(chunk);
  std::size_t line_start = 0;
  std::size_t search_from = scan_offset_;
  while (true) {
    auto const newline = pending_line_.find('\n', search_from);
    if (newline == std::string::npos) break;
    detail::append_openai_events_for_sse_line(events, event_state_,
                                             pending_line_.substr(line_start, newline - line_start));
    line_start = newline + 1;
    search_from = line_start;
  }
  if (line_start > 0) pending_line_.erase(0, line_start);
  scan_offset_ = pending_line_.size();
  return events;
}

ava::core::Result<std::vector<StreamEvent>> OpenAIStreamParser::finish()
{
  std::vector<StreamEvent> events;
  if (!pending_line_.empty()) {
    detail::append_openai_events_for_sse_line(events, event_state_, std::move(pending_line_));
    pending_line_.clear();
  }
  scan_offset_ = 0;
  if (!event_state_.data.empty()) {
    detail::append_openai_event_for_data(events, event_state_, event_state_.data);
    event_state_.data.clear();
  }
  detail::finish_openai_stream_events(events, event_state_);
  return events;
}

}  // namespace ava::provider
