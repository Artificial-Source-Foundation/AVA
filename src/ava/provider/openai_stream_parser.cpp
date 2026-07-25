#include "sys.h"
#include "ava/provider/openai_stream_parser.h"
#include "ava/provider/openai_stream_parser_internal.h"

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace ava::provider {

using namespace openai_stream_parser_internal;

void OpenAIStreamParser::append_events_for_sse_line(std::vector<StreamEvent>& events, std::string line)
{
  if (!line.empty() && line.back() == '\r')
    line.pop_back();
  if (line.empty())
  {
    if (!data_.empty())
    {
      if (++data_records_seen_ > kMaxProviderParserEvents)
      {
        data_record_limit_exceeded_ = true;
        data_.clear();
        return;
      }
      append_event_for_data(events, data_);
      data_.clear();
    }
    return;
  }
  if (line.starts_with("data:"))
  {
    if (!data_.empty())
      data_.push_back('\n');
    auto value = std::string_view(line).substr(5);
    if (!value.empty() && value.front() == ' ')
      value.remove_prefix(1);
    data_.append(value);
  }
}

ava::core::Result<std::vector<StreamEvent>> OpenAIStreamParser::append(std::string_view chunk)
{
  std::vector<StreamEvent> events;
  auto terminal_parser_limit = [&] {
    // Reserve the final permitted event for the fixed terminal diagnostic.
    auto const remaining = kMaxProviderParserEvents - events_emitted_;
    if (events.size() >= remaining)
      events.resize(remaining - 1);
    append_parser_limit_error(events, error_seen_, parser_limit_exceeded_);
    events_emitted_ += events.size();
    pending_line_.clear();
    data_.clear();
    return events;
  };

  if (parser_limit_exceeded_)
    return events;
  if (chunk.size() > kMaxProviderSseBufferedBytes || pending_line_.size() > kMaxProviderSseBufferedBytes - chunk.size())
    return terminal_parser_limit();

  pending_line_.append(chunk);
  std::size_t line_start = 0;
  std::size_t search_from = scan_offset_;
  while (true)
  {
    auto const newline = pending_line_.find('\n', search_from);
    if (newline == std::string::npos)
      break;
    append_events_for_sse_line(events, pending_line_.substr(line_start, newline - line_start));
    bool const nested_array_limit =
        !events.empty() && events.back().type == StreamEventType::Error && events.back().error_message == "OpenAI response parser limit exceeded";
    if (nested_array_limit)
      events.pop_back();
    if (nested_array_limit || data_record_limit_exceeded_ || data_.size() > kMaxProviderSseBufferedBytes ||
        events.size() >= kMaxProviderParserEvents - events_emitted_)
    {
      return terminal_parser_limit();
    }
    line_start = newline + 1;
    search_from = line_start;
  }
  if (line_start > 0)
    pending_line_.erase(0, line_start);
  scan_offset_ = pending_line_.size();
  events_emitted_ += events.size();
  return events;
}

ava::core::Result<std::vector<StreamEvent>> OpenAIStreamParser::finish()
{
  std::vector<StreamEvent> events;
  auto reset = [this] {
    pending_line_.clear();
    data_.clear();
    scan_offset_ = 0;
    saw_content_ = false;
    saw_refusal_ = false;
    reasoning_open_ = false;
    reasoning_text_seen_ = false;
    active_reasoning_item_id_.clear();
    active_reasoning_output_index_ = std::nullopt;
    active_reasoning_text_.clear();
    completed_reasoning_item_ids_.clear();
    completed_reasoning_texts_.clear();
    function_calls_.clear();
    function_call_item_ids_by_logical_id_.clear();
    legacy_function_calls_.clear();
    message_items_.clear();
    documented_output_item_types_.clear();
    documented_output_item_added_ids_.clear();
    documented_output_item_ids_by_index_.clear();
    done_seen_ = false;
    error_seen_ = false;
    parser_limit_exceeded_ = false;
    events_emitted_ = 0;
    data_records_seen_ = 0;
    data_record_limit_exceeded_ = false;
  };
  if (parser_limit_exceeded_)
  {
    reset();
    return events;
  }

  if (!pending_line_.empty())
  {
    append_events_for_sse_line(events, std::move(pending_line_));
    pending_line_.clear();
  }
  scan_offset_ = 0;
  if (!data_.empty())
  {
    if (++data_records_seen_ > kMaxProviderParserEvents || data_.size() > kMaxProviderSseBufferedBytes)
    {
      data_record_limit_exceeded_ = true;
    }
    else
    {
      append_event_for_data(events, data_);
    }
    data_.clear();
  }

  bool const nested_array_limit =
      !events.empty() && events.back().type == StreamEventType::Error && events.back().error_message == "OpenAI response parser limit exceeded";
  if (nested_array_limit)
    events.pop_back();
  auto terminal_parser_limit = [&] {
    auto const remaining = kMaxProviderParserEvents - events_emitted_;
    if (events.size() >= remaining)
      events.resize(remaining - 1);
    append_parser_limit_error(events, error_seen_, parser_limit_exceeded_);
  };
  if (nested_array_limit || data_record_limit_exceeded_ || events.size() >= kMaxProviderParserEvents - events_emitted_)
  {
    terminal_parser_limit();
  }
  else
  {
    if (!done_seen_ && !error_seen_)
    {
      if (!reject_unended_documented_function_calls(events, error_seen_, function_calls_))
        static_cast<void>(reject_unended_documented_message_items(events, error_seen_, message_items_));
    }
    append_finish_reasoning_if_open(events, reasoning_open_, reasoning_text_seen_, active_reasoning_item_id_, active_reasoning_output_index_,
                                    active_reasoning_text_, completed_reasoning_item_ids_, completed_reasoning_texts_);
    if (saw_content_ && !done_seen_ && !error_seen_)
      append_stream_error(events, error_seen_, "OpenAI SSE stream ended before done marker");
    if (events.size() >= kMaxProviderParserEvents - events_emitted_)
      terminal_parser_limit();
  }
  events_emitted_ += events.size();
  reset();
  return events;
}

}  // namespace ava::provider
