#include "ava/provider/openai_stream_reasoning.h"

#include <algorithm>
#include <optional>
#include <utility>

#include "ava/provider/openai_response_parser_detail.h"
#include "ava/provider/provider_utils.h"

namespace ava::provider::detail {

bool openai_stream_remembers(std::vector<std::string> const& values, std::string_view value)
{
  return std::ranges::find(values, value) != values.end();
}

void remember_openai_stream_value(std::vector<std::string>& values, std::string value)
{
  if (value.empty() || openai_stream_remembers(values, value)) return;
  values.push_back(std::move(value));
}

std::string openai_reasoning_item_id_from_event(std::string_view data, std::optional<std::string_view> item)
{
  if (auto id = detail::first_string_field(data, {"item_id", "output_item_id"})) return *id;
  if (item) {
    if (auto id = detail::first_string_field(*item, {"id", "item_id"})) return *id;
  }
  return {};
}

void set_active_openai_reasoning_item_id(OpenAIStreamEventState& state, std::string_view item_id)
{
  if (item_id.empty() || !state.active_reasoning_item_id.empty()) return;
  state.active_reasoning_item_id = std::string(item_id);
}

void append_openai_reasoning_start_if_needed(std::vector<StreamEvent>& events, OpenAIStreamEventState& state)
{
  if (state.reasoning_open) return;
  state.reasoning_open = true;
  events.push_back(StreamEvent{.type = StreamEventType::ReasoningStart,
                               .text = "",
                               .tool_call_id = "",
                               .tool_name = "",
                               .error_message = "",
                               .usage = std::nullopt,
                               .reasoning_format = std::string(detail::kOpenAIResponsesReasoningFormat)});
}

void append_openai_reasoning_delta(std::vector<StreamEvent>& events, OpenAIStreamEventState& state,
                                   std::string_view text)
{
  append_openai_reasoning_start_if_needed(events, state);
  if (text.empty()) return;
  state.reasoning_text_seen = true;
  state.active_reasoning_text += text;
  events.push_back(StreamEvent{.type = StreamEventType::ReasoningDelta,
                               .text = std::string(text),
                               .tool_call_id = "",
                               .tool_name = "",
                               .error_message = "",
                               .usage = std::nullopt,
                               .reasoning_format = std::string(detail::kOpenAIResponsesReasoningFormat)});
}

void append_openai_reasoning_end_if_open(std::vector<StreamEvent>& events, OpenAIStreamEventState& state)
{
  if (!state.reasoning_open) return;
  state.reasoning_open = false;
  state.reasoning_text_seen = false;
  remember_openai_stream_value(state.completed_reasoning_item_ids, std::move(state.active_reasoning_item_id));
  remember_openai_stream_value(state.completed_reasoning_texts, std::move(state.active_reasoning_text));
  state.active_reasoning_item_id.clear();
  state.active_reasoning_text.clear();
  events.push_back(StreamEvent{.type = StreamEventType::ReasoningEnd,
                               .text = "",
                               .tool_call_id = "",
                               .tool_name = "",
                               .error_message = "",
                               .usage = std::nullopt,
                               .reasoning_format = std::string(detail::kOpenAIResponsesReasoningFormat)});
}

}  // namespace ava::provider::detail
