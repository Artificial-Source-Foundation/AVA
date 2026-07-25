#include "sys.h"
#include "ava/provider/openai_reasoning.h"
#include "ava/provider/openai_response_parser_detail.h"
#include "ava/provider/openai_stream_parser.h"
#include "ava/provider/openai_stream_parser_internal.h"
#include "ava/core/json.h"

#include <algorithm>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace ava::provider::openai_stream_parser_internal {

bool is_ignored_lifecycle_event(std::string_view type)
{
  return type == "response.created" || type == "response.in_progress" || type == "response.content_part.added" || type == "response.content_part.done";
}

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

void append_stream_error(std::vector<StreamEvent>& events, bool& error_seen, std::string message)
{
  error_seen = true;
  events.push_back(
      StreamEvent{.type = StreamEventType::Error, .text = "", .tool_call_id = "", .tool_name = "", .error_message = std::move(message), .usage = std::nullopt});
}

void append_parser_limit_error(std::vector<StreamEvent>& events, bool& error_seen, bool& parser_limit_exceeded)
{
  if (parser_limit_exceeded)
    return;
  parser_limit_exceeded = true;
  error_seen = true;
  events.push_back(StreamEvent{.type = StreamEventType::Error,
                               .text = "",
                               .tool_call_id = "",
                               .tool_name = "",
                               .error_message = "OpenAI response parser limit exceeded",
                               .usage = std::nullopt});
}

ava::core::Result<std::optional<std::size_t>> documented_output_index(std::string_view data, std::string_view item)
{
  auto const parse = [](std::string_view object) -> ava::core::Result<std::optional<std::size_t>> {
    if (!ava::core::json::field_value_start(object, "output_index"))
      return std::optional<std::size_t>{};
    auto const value = ava::core::json::integer_field(object, "output_index");
    if (!value || *value < 0 || static_cast<unsigned long long>(*value) > std::numeric_limits<std::size_t>::max())
    {
      return std::unexpected(ava::core::Error(ava::core::ErrorCategory::Provider, "OpenAI output item has an invalid output_index"));
    }
    return std::optional<std::size_t>{static_cast<std::size_t>(*value)};
  };
  auto const event_index = parse(data);
  if (!event_index)
    return std::unexpected(std::move(event_index.error()));
  auto const item_index = parse(item);
  if (!item_index)
    return std::unexpected(std::move(item_index.error()));
  if (*event_index && *item_index && *event_index != *item_index)
  {
    return std::unexpected(ava::core::Error(ava::core::ErrorCategory::Provider, "OpenAI output item has conflicting output_index values"));
  }
  return *event_index ? *event_index : *item_index;
}

ava::core::Result<AssistantPhase> documented_message_phase(std::string_view data, std::string_view item)
{
  auto const parse = [](std::string_view object) -> ava::core::Result<std::optional<AssistantPhase>> {
    if (!ava::core::json::field_value_start(object, "phase") || detail::is_null_field(object, "phase"))
      return std::optional<AssistantPhase>{};
    auto const value = ava::core::json::string_field(object, "phase");
    if (!value || value->empty())
      return std::unexpected(ava::core::Error(ava::core::ErrorCategory::Provider, "OpenAI message output item has an empty or invalid phase"));
    auto const phase = assistant_phase_from_string(*value);
    if (!phase)
      return std::unexpected(ava::core::Error(ava::core::ErrorCategory::Provider, "OpenAI message output item has an unknown phase"));
    return *phase;
  };
  auto const event_phase = parse(data);
  if (!event_phase)
    return std::unexpected(std::move(event_phase.error()));
  auto const item_phase = parse(item);
  if (!item_phase)
    return std::unexpected(std::move(item_phase.error()));
  if (*event_phase && *item_phase && *event_phase != *item_phase)
  {
    return std::unexpected(ava::core::Error(ava::core::ErrorCategory::Provider, "OpenAI message output item has conflicting phase values"));
  }
  return *event_phase ? **event_phase : (*item_phase ? **item_phase : AssistantPhase::Unknown);
}

ava::core::VoidResult documented_output_item_metadata(std::string_view data, std::string_view item, bool is_message, std::string& id,
                                                      std::optional<std::size_t>& output_index, AssistantPhase& phase)
{
  auto const parsed_id = ava::core::json::string_field(item, "id");
  if (!parsed_id || !is_valid_openai_opaque_id(*parsed_id))
  {
    return std::unexpected(ava::core::Error(ava::core::ErrorCategory::Provider, "OpenAI output item requires a bounded item ID"));
  }
  auto parsed_output_index = documented_output_index(data, item);
  if (!parsed_output_index)
    return std::unexpected(std::move(parsed_output_index.error()));
  AssistantPhase parsed_phase = AssistantPhase::Unknown;
  if (is_message)
  {
    auto message_phase = documented_message_phase(data, item);
    if (!message_phase)
      return std::unexpected(std::move(message_phase.error()));
    parsed_phase = *message_phase;
  }
  id = *parsed_id;
  output_index = *parsed_output_index;
  phase = parsed_phase;
  return {};
}

bool register_documented_output_item(std::vector<StreamEvent>& events, bool& error_seen, std::string_view id, std::string_view type,
                                     std::optional<std::size_t> output_index, bool added, std::unordered_map<std::string, std::string>& item_types,
                                     std::unordered_set<std::string>& added_item_ids, std::unordered_map<std::size_t, std::string>& item_ids_by_output_index)
{
  auto const existing = item_types.find(std::string(id));
  if (existing != item_types.end())
  {
    if (existing->second != type)
    {
      append_stream_error(events, error_seen, "OpenAI output item ID changed its item type");
      return false;
    }
    if (added)
    {
      append_stream_error(events, error_seen, "OpenAI output item was added more than once");
      return false;
    }
  }
  else
  {
    item_types.emplace(std::string(id), std::string(type));
  }
  if (added && !added_item_ids.insert(std::string(id)).second)
  {
    append_stream_error(events, error_seen, "OpenAI output item was added more than once");
    return false;
  }
  if (output_index)
  {
    auto const index = item_ids_by_output_index.find(*output_index);
    if (index != item_ids_by_output_index.end() && index->second != id)
    {
      append_stream_error(events, error_seen, "OpenAI output item index is already bound to another item ID");
      return false;
    }
    item_ids_by_output_index.emplace(*output_index, std::string(id));
  }
  return true;
}

bool reject_unended_documented_function_calls(std::vector<StreamEvent>& events, bool& error_seen,
                                              std::unordered_map<std::string, OpenAIStreamParser::FunctionCallState> const& function_calls)
{
  for (auto const& [item_id, state] : function_calls)
  {
    static_cast<void>(item_id);
    if (!state.ended)
    {
      append_stream_error(events, error_seen, "OpenAI response ended before documented function call item completion");
      return true;
    }
  }
  return false;
}

bool reject_unended_documented_message_items(std::vector<StreamEvent>& events, bool& error_seen,
                                             std::unordered_map<std::string, OpenAIStreamParser::MessageItemState> const& message_items)
{
  for (auto const& [item_id, state] : message_items)
  {
    static_cast<void>(item_id);
    if (!state.ended)
    {
      append_stream_error(events, error_seen, "OpenAI response ended before documented message item completion");
      return true;
    }
  }
  return false;
}

ava::core::Result<bool> completed_message_content_from_item(std::string_view item, std::string& output_text, std::string& refusal_text)
{
  if (!ava::core::json::field_value_start(item, "content"))
    return false;
  auto const contents = ava::core::json::strict_objects_in_array_field(item, "content", kMaxProviderParserArrayItems);
  if (!contents)
  {
    return std::unexpected(ava::core::Error(ava::core::ErrorCategory::Provider, "OpenAI response parser limit exceeded"));
  }

  std::string complete_output_text;
  std::string complete_refusal_text;
  for (auto const& content : *contents)
  {
    auto const type = ava::core::json::string_field(content, "type").value_or("");
    if (type == "output_text")
    {
      auto const text = ava::core::json::string_field(content, "text");
      if (!text)
        return std::unexpected(ava::core::Error(ava::core::ErrorCategory::Provider, "OpenAI output_text content part requires string text"));
      complete_output_text += *text;
      continue;
    }
    if (type == "refusal")
    {
      auto const refusal = ava::core::json::string_field(content, "refusal");
      if (!refusal)
        return std::unexpected(ava::core::Error(ava::core::ErrorCategory::Provider, "OpenAI refusal content part requires string refusal"));
      complete_refusal_text += *refusal;
      continue;
    }
    return std::unexpected(ava::core::Error(ava::core::ErrorCategory::Provider, "OpenAI message output item has an unsupported content-part type"));
  }
  output_text = std::move(complete_output_text);
  refusal_text = std::move(complete_refusal_text);
  return true;
}

OpenAIStreamParser::MessageItemState* documented_message_item_for_event(std::vector<StreamEvent>& events, bool& error_seen, std::string_view data,
                                                                        std::unordered_map<std::string, OpenAIStreamParser::MessageItemState>& message_items,
                                                                        bool& documented)
{
  auto const item_id_start = ava::core::json::field_value_start(data, "item_id");
  auto const output_item_id_start = ava::core::json::field_value_start(data, "output_item_id");
  documented = item_id_start || output_item_id_start;
  if (!documented)
    return nullptr;
  auto const item_id = ava::core::json::string_field(data, "item_id");
  auto const output_item_id = ava::core::json::string_field(data, "output_item_id");
  if ((item_id_start && (!item_id || !is_valid_openai_opaque_id(*item_id))) ||
      (output_item_id_start && (!output_item_id || !is_valid_openai_opaque_id(*output_item_id))) || (item_id && output_item_id && *item_id != *output_item_id))
  {
    append_stream_error(events, error_seen, "OpenAI message text event has an invalid or conflicting documented item ID");
    return nullptr;
  }
  auto const id = item_id ? item_id : output_item_id;
  auto const state = message_items.find(*id);
  if (state == message_items.end())
  {
    append_stream_error(events, error_seen, "OpenAI message text event references an unbound item ID");
    return nullptr;
  }
  if (state->second.ended)
  {
    append_stream_error(events, error_seen, "OpenAI message text event emitted after item completion");
    return nullptr;
  }
  auto const output_index = documented_output_index(data, "{}");
  if (!output_index)
  {
    append_stream_error(events, error_seen, output_index.error().message());
    return nullptr;
  }
  if (*output_index && state->second.provider_output_index != *output_index)
  {
    append_stream_error(events, error_seen, "OpenAI message text event changed its output_index");
    return nullptr;
  }
  auto const phase = documented_message_phase(data, "{}");
  if (!phase)
  {
    append_stream_error(events, error_seen, phase.error().message());
    return nullptr;
  }
  if (*phase != AssistantPhase::Unknown && state->second.phase != AssistantPhase::Unknown && *phase != state->second.phase)
  {
    append_stream_error(events, error_seen, "OpenAI message text event changed its phase");
    return nullptr;
  }
  if (state->second.phase == AssistantPhase::Unknown && *phase != AssistantPhase::Unknown)
    state->second.phase = *phase;
  return &state->second;
}

void append_documented_message_delta(std::vector<StreamEvent>& events, OpenAIStreamParser::MessageItemState& state, std::string_view item_id,
                                     std::string_view text, MessageContentKind kind)
{
  if (text.empty())
    return;
  state.text += text;
  if (kind == MessageContentKind::OutputText)
    state.output_text += text;
  else
    state.refusal_text += text;
  events.push_back(StreamEvent{.type = StreamEventType::TextDelta,
                               .text = std::string(text),
                               .tool_call_id = "",
                               .tool_name = "",
                               .error_message = "",
                               .usage = std::nullopt,
                               .provider_item_id = std::string(item_id),
                               .provider_output_index = state.provider_output_index,
                               .assistant_phase = state.phase});
}

bool reconcile_complete_message_text(std::vector<StreamEvent>& events, OpenAIStreamParser::MessageItemState& state, std::string_view item_id,
                                     std::string_view complete_text, MessageContentKind kind, bool& error_seen)
{
  auto const& captured = kind == MessageContentKind::OutputText ? state.output_text : state.refusal_text;
  if (complete_text == captured)
    return true;
  if (complete_text.starts_with(captured))
  {
    auto const suffix = complete_text.substr(captured.size());
    append_documented_message_delta(events, state, item_id, suffix, kind);
    return true;
  }
  append_stream_error(events, error_seen, kind == MessageContentKind::Refusal ? "conflicting OpenAI refusal text" : "conflicting OpenAI message text");
  return false;
}

}  // namespace ava::provider::openai_stream_parser_internal
