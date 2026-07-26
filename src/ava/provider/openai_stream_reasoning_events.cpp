#include "sys.h"
#include "ava/provider/openai_reasoning.h"
#include "ava/provider/openai_response_parser_detail.h"
#include "ava/provider/openai_stream_parser.h"
#include "ava/provider/openai_stream_parser_internal.h"
#include "ava/core/json.h"

#include <algorithm>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace ava::provider {
namespace {

bool contains_string(std::vector<std::string> const& values, std::string_view value)
{
  return std::find(values.begin(), values.end(), value) != values.end();
}

void remember_string(std::vector<std::string>& values, std::string value)
{
  if (value.empty() || contains_string(values, value))
    return;
  values.push_back(std::move(value));
}

std::string reasoning_item_id_from_event(std::string_view data, std::optional<std::string_view> item = std::nullopt)
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

bool reasoning_event_has_authoritative_complete_text(std::string_view object)
{
  // A bare lifecycle event is not a complete text claim. The documented done
  // forms and completed output item supply one of these fields when they do
  // authoritatively describe the summary/reasoning text.
  return ava::core::json::field_value_start(object, "summary_text").has_value() || ava::core::json::field_value_start(object, "text").has_value() ||
         ava::core::json::field_value_start(object, "summary").has_value() || ava::core::json::field_value_start(object, "part").has_value();
}

}  // namespace

using namespace openai_stream_parser_internal;

void OpenAIStreamParser::append_start_reasoning_if_needed(std::vector<StreamEvent>& events)
{
  if (reasoning_open_)
    return;
  reasoning_open_ = true;
  events.push_back(StreamEvent{.type = StreamEventType::ReasoningStart,
                               .text = "",
                               .tool_call_id = "",
                               .tool_name = "",
                               .error_message = "",
                               .usage = std::nullopt,
                               .provider_item_id = active_reasoning_item_id_,
                               .provider_output_index = active_reasoning_output_index_,
                               .reasoning_format = std::string(detail::kOpenAIResponsesReasoningFormat)});
}

void OpenAIStreamParser::append_reasoning_delta(std::vector<StreamEvent>& events, std::string_view text)
{
  append_start_reasoning_if_needed(events);
  if (text.empty())
    return;
  reasoning_text_seen_ = true;
  active_reasoning_text_ += text;
  events.push_back(StreamEvent{.type = StreamEventType::ReasoningDelta,
                               .text = std::string(text),
                               .tool_call_id = "",
                               .tool_name = "",
                               .error_message = "",
                               .usage = std::nullopt,
                               .provider_item_id = active_reasoning_item_id_,
                               .provider_output_index = active_reasoning_output_index_,
                               .reasoning_format = std::string(detail::kOpenAIResponsesReasoningFormat)});
}

bool OpenAIStreamParser::reconcile_complete_reasoning_text(std::vector<StreamEvent>& events, std::string_view complete_text)
{
  append_start_reasoning_if_needed(events);
  if (complete_text == active_reasoning_text_)
    return true;
  if (complete_text.starts_with(active_reasoning_text_))
  {
    append_reasoning_delta(events, complete_text.substr(active_reasoning_text_.size()));
    return true;
  }
  append_stream_error(events, error_seen_, "conflicting OpenAI reasoning text");
  return false;
}

void OpenAIStreamParser::append_finish_reasoning_if_open(std::vector<StreamEvent>& events, std::string native_item_json)
{
  if (!reasoning_open_)
    return;
  reasoning_open_ = false;
  reasoning_text_seen_ = false;
  auto finished_item_id = std::move(active_reasoning_item_id_);
  auto finished_text = std::move(active_reasoning_text_);
  auto const finished_output_index = active_reasoning_output_index_;
  remember_string(completed_reasoning_item_ids_, finished_item_id);
  remember_string(completed_reasoning_texts_, finished_text);
  active_reasoning_item_id_.clear();
  active_reasoning_output_index_ = std::nullopt;
  active_reasoning_text_.clear();
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

bool OpenAIStreamParser::bind_documented_reasoning_event(std::vector<StreamEvent>& events, std::string_view data)
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
    append_stream_error(events, error_seen_, "OpenAI reasoning event has an invalid or conflicting documented item ID");
    return false;
  }
  auto const id = item_id ? item_id : output_item_id;
  auto const registered = documented_output_item_types_.find(*id);
  if (registered == documented_output_item_types_.end() || registered->second != "reasoning")
  {
    append_stream_error(events, error_seen_, "OpenAI reasoning event references an unbound documented item ID");
    return false;
  }
  auto index = documented_output_index(data, "{}");
  if (!index)
  {
    append_stream_error(events, error_seen_, index.error().message());
    return false;
  }
  if (!active_reasoning_item_id_.empty() && active_reasoning_item_id_ != *id)
  {
    append_stream_error(events, error_seen_, "OpenAI reasoning event does not match the active item");
    return false;
  }
  active_reasoning_item_id_ = *id;
  if (*index)
  {
    if (active_reasoning_output_index_ && active_reasoning_output_index_ != *index)
    {
      append_stream_error(events, error_seen_, "OpenAI reasoning event changed its output_index");
      return false;
    }
    active_reasoning_output_index_ = *index;
  }
  else if (!active_reasoning_output_index_)
  {
    for (auto const& [registered_index, registered_id] : documented_output_item_ids_by_index_)
    {
      if (registered_id == *id)
      {
        active_reasoning_output_index_ = registered_index;
        break;
      }
    }
  }
  return true;
}

void OpenAIStreamParser::handle_documented_reasoning_output_item(std::vector<StreamEvent>& events, std::string const& item, std::string const& item_id,
                                                                 std::optional<std::size_t> output_index, OutputItemLifecycle lifecycle)
{
  if (lifecycle == OutputItemLifecycle::Added)
  {
    if (reasoning_open_ && active_reasoning_item_id_ != item_id)
    {
      append_stream_error(events, error_seen_, "OpenAI reasoning output items overlapped");
      return;
    }
    active_reasoning_item_id_ = item_id;
    active_reasoning_output_index_ = output_index;
    append_start_reasoning_if_needed(events);
    return;
  }

  if (contains_string(completed_reasoning_item_ids_, item_id))
  {
    append_stream_error(events, error_seen_, "OpenAI reasoning output item completed more than once");
    return;
  }
  if (reasoning_open_ && active_reasoning_item_id_ != item_id)
  {
    append_stream_error(events, error_seen_, "OpenAI reasoning output item completion did not match the active item");
    return;
  }
  active_reasoning_item_id_ = item_id;
  active_reasoning_output_index_ = output_index;
  std::size_t remaining_summary_parts = kMaxProviderParserArrayItems;
  auto summary = detail::reasoning_summary_text_from_object(item, remaining_summary_parts);
  if (!summary)
  {
    append_stream_error(events, error_seen_, "OpenAI response parser limit exceeded");
    return;
  }
  if (reasoning_event_has_authoritative_complete_text(item) && !reconcile_complete_reasoning_text(events, *summary))
    return;
  append_finish_reasoning_if_open(events, is_valid_openai_native_reasoning_item_json(item) ? item : std::string{});
}

OpenAIStreamParser::EventHandling OpenAIStreamParser::handle_reasoning_event(std::vector<StreamEvent>& events, std::string_view data, std::string_view type)
{
  if (type == "response.reasoning_summary_part.added")
  {
    auto const item_id = reasoning_item_id_from_event(data);
    if (!bind_documented_reasoning_event(events, data))
      return EventHandling::Handled;
    if (contains_string(completed_reasoning_item_ids_, item_id))
      return EventHandling::Handled;
    saw_content_ = true;
    if (!item_id.empty() && active_reasoning_item_id_.empty())
      active_reasoning_item_id_ = item_id;
    append_start_reasoning_if_needed(events);
    return EventHandling::Handled;
  }
  if (type == "response.reasoning_summary_text.delta" || type == "response.reasoning_text.delta")
  {
    auto const item_id = reasoning_item_id_from_event(data);
    if (!bind_documented_reasoning_event(events, data))
      return EventHandling::Handled;
    if (contains_string(completed_reasoning_item_ids_, item_id))
      return EventHandling::Handled;
    saw_content_ = true;
    if (!item_id.empty() && active_reasoning_item_id_.empty())
      active_reasoning_item_id_ = item_id;
    append_reasoning_delta(events, detail::first_string_field(data, {"delta", "text"}).value_or(""));
    return EventHandling::Handled;
  }
  if (type == "response.reasoning_summary_text.done" || type == "response.reasoning_summary_part.done" || type == "response.reasoning_text.done")
  {
    auto const item_id = reasoning_item_id_from_event(data);
    if (!bind_documented_reasoning_event(events, data))
      return EventHandling::Handled;
    std::size_t remaining_summary_parts = kMaxProviderParserArrayItems;
    auto summary = detail::reasoning_summary_text_from_object(data, remaining_summary_parts);
    if (!summary)
    {
      append_stream_error(events, error_seen_, "OpenAI response parser limit exceeded");
      return EventHandling::Handled;
    }
    if (contains_string(completed_reasoning_item_ids_, item_id))
      return EventHandling::Handled;
    saw_content_ = true;
    if (!item_id.empty() && active_reasoning_item_id_.empty())
      active_reasoning_item_id_ = item_id;
    if (reasoning_event_has_authoritative_complete_text(data) && !reconcile_complete_reasoning_text(events, *summary))
      return EventHandling::Handled;
    // Do not close here: output_item.done carries the opaque reasoning item
    // that must be attached to the same private reasoning block for replay.
    return EventHandling::Handled;
  }
  return EventHandling::Unhandled;
}

}  // namespace ava::provider
