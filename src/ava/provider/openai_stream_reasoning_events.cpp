#include "sys.h"
#include "ava/provider/openai_reasoning.h"
#include "ava/provider/openai_response_parser_detail.h"
#include "ava/provider/openai_stream_parser.h"
#include "ava/provider/openai_stream_parser_internal.h"
#include "ava/provider/provider_utils.h"
#include "ava/core/json.h"

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace ava::provider::openai_stream_parser_internal {

std::string reasoning_item_id_from_event(std::string_view data, std::optional<std::string_view> item)
{
  if (auto id = detail::first_string_field(data, {"item_id", "output_item_id"}))
    return *id;
  if (item)
  {
    if (auto id = detail::first_string_field(*item, {"id", "item_id"}))
      return *id;
  }
  return {};
}

void set_active_reasoning_item_id(std::string& active_item_id, std::string_view item_id)
{
  if (item_id.empty() || !active_item_id.empty())
    return;
  active_item_id = std::string(item_id);
}

void append_start_reasoning_if_needed(std::vector<StreamEvent>& events, bool& reasoning_open, std::string_view active_reasoning_item_id,
                                      std::optional<std::size_t> active_reasoning_output_index)
{
  if (reasoning_open)
    return;
  reasoning_open = true;
  events.push_back(StreamEvent{.type = StreamEventType::ReasoningStart,
                               .text = "",
                               .tool_call_id = "",
                               .tool_name = "",
                               .error_message = "",
                               .usage = std::nullopt,
                               .provider_item_id = std::string(active_reasoning_item_id),
                               .provider_output_index = active_reasoning_output_index,
                               .reasoning_format = std::string(detail::kOpenAIResponsesReasoningFormat)});
}

void append_reasoning_delta(std::vector<StreamEvent>& events, bool& reasoning_open, bool& reasoning_text_seen, std::string const& active_reasoning_item_id,
                            std::optional<std::size_t> active_reasoning_output_index, std::string& active_reasoning_text, std::string_view text)
{
  append_start_reasoning_if_needed(events, reasoning_open, active_reasoning_item_id, active_reasoning_output_index);
  if (text.empty())
    return;
  reasoning_text_seen = true;
  active_reasoning_text += text;
  events.push_back(StreamEvent{.type = StreamEventType::ReasoningDelta,
                               .text = std::string(text),
                               .tool_call_id = "",
                               .tool_name = "",
                               .error_message = "",
                               .usage = std::nullopt,
                               .provider_item_id = active_reasoning_item_id,
                               .provider_output_index = active_reasoning_output_index,
                               .reasoning_format = std::string(detail::kOpenAIResponsesReasoningFormat)});
}

bool reasoning_event_has_authoritative_complete_text(std::string_view object)
{
  // A bare lifecycle event is not a complete text claim. The documented done
  // forms and completed output item supply one of these fields when they do
  // authoritatively describe the summary/reasoning text.
  return ava::core::json::field_value_start(object, "summary_text").has_value() || ava::core::json::field_value_start(object, "text").has_value() ||
         ava::core::json::field_value_start(object, "summary").has_value() || ava::core::json::field_value_start(object, "part").has_value();
}

bool reconcile_complete_reasoning_text(std::vector<StreamEvent>& events, bool& reasoning_open, bool& reasoning_text_seen,
                                       std::string const& active_reasoning_item_id, std::optional<std::size_t> active_reasoning_output_index,
                                       std::string& active_reasoning_text, std::string_view complete_text, bool& error_seen)
{
  append_start_reasoning_if_needed(events, reasoning_open, active_reasoning_item_id, active_reasoning_output_index);
  if (complete_text == active_reasoning_text)
    return true;
  if (complete_text.starts_with(active_reasoning_text))
  {
    append_reasoning_delta(events, reasoning_open, reasoning_text_seen, active_reasoning_item_id, active_reasoning_output_index, active_reasoning_text,
                           complete_text.substr(active_reasoning_text.size()));
    return true;
  }
  append_stream_error(events, error_seen, "conflicting OpenAI reasoning text");
  return false;
}

void append_finish_reasoning_if_open(std::vector<StreamEvent>& events, bool& reasoning_open, bool& reasoning_text_seen, std::string& active_reasoning_item_id,
                                     std::optional<std::size_t>& active_reasoning_output_index, std::string& active_reasoning_text,
                                     std::vector<std::string>& completed_reasoning_item_ids, std::vector<std::string>& completed_reasoning_texts,
                                     std::string native_item_json)
{
  if (!reasoning_open)
    return;
  reasoning_open = false;
  reasoning_text_seen = false;
  auto finished_item_id = std::move(active_reasoning_item_id);
  auto finished_text = std::move(active_reasoning_text);
  auto const finished_output_index = active_reasoning_output_index;
  remember_string(completed_reasoning_item_ids, finished_item_id);
  remember_string(completed_reasoning_texts, finished_text);
  active_reasoning_item_id.clear();
  active_reasoning_output_index = std::nullopt;
  active_reasoning_text.clear();
  events.push_back(StreamEvent{.type = StreamEventType::ReasoningEnd,
                               .text = "",
                               .tool_call_id = "",
                               .tool_name = "",
                               .error_message = "",
                               .usage = std::nullopt,
                               .provider_item_id = std::move(finished_item_id),
                               .provider_output_index = finished_output_index,
                               .reasoning_format = std::string(detail::kOpenAIResponsesReasoningFormat),
                               .reasoning_native_item_json = std::move(native_item_json)});
}

bool bind_documented_reasoning_event(std::vector<StreamEvent>& events, bool& error_seen, std::string_view data, std::string& active_reasoning_item_id,
                                     std::optional<std::size_t>& active_reasoning_output_index,
                                     std::unordered_map<std::string, std::string> const& documented_output_item_types,
                                     std::unordered_map<std::size_t, std::string> const& documented_output_item_ids_by_index)
{
  auto const item_id_start = ava::core::json::field_value_start(data, "item_id");
  auto const output_item_id_start = ava::core::json::field_value_start(data, "output_item_id");
  if (!item_id_start && !output_item_id_start)
    return true;
  auto const item_id = ava::core::json::string_field(data, "item_id");
  auto const output_item_id = ava::core::json::string_field(data, "output_item_id");
  if ((item_id_start && (!item_id || !is_valid_openai_opaque_id(*item_id))) ||
      (output_item_id_start && (!output_item_id || !is_valid_openai_opaque_id(*output_item_id))) || (item_id && output_item_id && *item_id != *output_item_id))
  {
    append_stream_error(events, error_seen, "OpenAI reasoning event has an invalid or conflicting documented item ID");
    return false;
  }
  auto const id = item_id ? item_id : output_item_id;
  auto const registered = documented_output_item_types.find(*id);
  if (registered == documented_output_item_types.end() || registered->second != "reasoning")
  {
    append_stream_error(events, error_seen, "OpenAI reasoning event references an unbound documented item ID");
    return false;
  }
  auto index = documented_output_index(data, "{}");
  if (!index)
  {
    append_stream_error(events, error_seen, index.error().message());
    return false;
  }
  if (!active_reasoning_item_id.empty() && active_reasoning_item_id != *id)
  {
    append_stream_error(events, error_seen, "OpenAI reasoning event does not match the active item");
    return false;
  }
  active_reasoning_item_id = *id;
  if (*index)
  {
    if (active_reasoning_output_index && active_reasoning_output_index != *index)
    {
      append_stream_error(events, error_seen, "OpenAI reasoning event changed its output_index");
      return false;
    }
    active_reasoning_output_index = *index;
  }
  else if (!active_reasoning_output_index)
  {
    for (auto const& [registered_index, registered_id] : documented_output_item_ids_by_index)
    {
      if (registered_id == *id)
      {
        active_reasoning_output_index = registered_index;
        break;
      }
    }
  }
  return true;
}

}  // namespace ava::provider::openai_stream_parser_internal
