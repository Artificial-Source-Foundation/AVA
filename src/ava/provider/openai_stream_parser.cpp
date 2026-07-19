#include "sys.h"
#include "ava/provider/openai_reasoning.h"
#include "ava/provider/openai_response_parser.h"
#include "ava/provider/openai_response_parser_detail.h"
#include "ava/provider/openai_stream_parser.h"
#include "ava/provider/provider_utils.h"
#include "ava/core/json.h"

#include <algorithm>
#include <limits>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace ava::provider {
namespace {

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

std::string function_call_item_id_from_event(std::string_view data, std::optional<std::string_view> item = std::nullopt)
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

std::string logical_function_call_id_from_event(std::string_view data, std::optional<std::string_view> item = std::nullopt)
{
  if (auto call_id = detail::first_string_field(data, {"call_id"}))
    return *call_id;
  if (item)
  {
    if (auto call_id = detail::first_string_field(*item, {"call_id"}))
      return *call_id;
  }
  return {};
}

std::string function_call_name_from_event(std::string_view data, std::optional<std::string_view> item = std::nullopt)
{
  if (item)
  {
    if (auto name = ava::core::json::string_field(*item, "name"))
      return *name;
  }
  return ava::core::json::string_field(data, "name").value_or("");
}

std::optional<std::string> function_call_arguments_from_event(std::string_view data, std::optional<std::string_view> item = std::nullopt)
{
  if (item)
  {
    if (auto arguments = ava::core::json::string_field(*item, "arguments"))
      return arguments;
  }
  return ava::core::json::string_field(data, "arguments");
}

OpenAIStreamParser::FunctionCallState& legacy_function_call_state_for(std::string_view data,
                                                                      std::unordered_map<std::string, OpenAIStreamParser::FunctionCallState>& calls,
                                                                      std::optional<std::string_view> item = std::nullopt)
{
  auto item_id = function_call_item_id_from_event(data, item);
  auto const call_id = logical_function_call_id_from_event(data, item);
  auto const name = function_call_name_from_event(data, item);
  if (item_id.empty() && !call_id.empty())
  {
    for (auto& [existing_item_id, existing_state] : calls)
    {
      static_cast<void>(existing_item_id);
      if (existing_state.logical_call_id != call_id)
        continue;
      if (!name.empty())
        existing_state.name = name;
      return existing_state;
    }
  }
  if (item_id.empty())
    item_id = call_id;
  auto& state = calls[item_id];
  if (!call_id.empty())
    state.logical_call_id = call_id;
  if (state.logical_call_id.empty())
    state.logical_call_id = item_id;
  if (!name.empty())
    state.name = name;
  return state;
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

struct DocumentedOutputItemMetadata
{
  std::string id;
  std::optional<std::size_t> output_index = std::nullopt;
  AssistantPhase phase = AssistantPhase::Unknown;
};

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

ava::core::Result<DocumentedOutputItemMetadata> documented_output_item_metadata(std::string_view data, std::string_view item, bool is_message)
{
  auto const id = ava::core::json::string_field(item, "id");
  if (!id || !is_valid_openai_opaque_id(*id))
  {
    return std::unexpected(ava::core::Error(ava::core::ErrorCategory::Provider, "OpenAI output item requires a bounded item ID"));
  }
  auto output_index = documented_output_index(data, item);
  if (!output_index)
    return std::unexpected(std::move(output_index.error()));
  AssistantPhase phase = AssistantPhase::Unknown;
  if (is_message)
  {
    auto parsed_phase = documented_message_phase(data, item);
    if (!parsed_phase)
      return std::unexpected(std::move(parsed_phase.error()));
    phase = *parsed_phase;
  }
  return DocumentedOutputItemMetadata{.id = *id, .output_index = *output_index, .phase = phase};
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

bool is_valid_function_call_arguments_object(std::string_view arguments)
{
  return ava::core::json::is_valid_object(arguments);
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

struct CompletedMessageContent
{
  std::string output_text;
  std::string refusal_text;
};

ava::core::Result<std::optional<CompletedMessageContent>> completed_message_content_from_item(std::string_view item)
{
  if (!ava::core::json::field_value_start(item, "content"))
    return std::optional<CompletedMessageContent>{};
  auto const contents = ava::core::json::strict_objects_in_array_field(item, "content", kMaxProviderParserArrayItems);
  if (!contents)
  {
    return std::unexpected(ava::core::Error(ava::core::ErrorCategory::Provider, "OpenAI response parser limit exceeded"));
  }

  CompletedMessageContent complete;
  for (auto const& content : *contents)
  {
    auto const type = ava::core::json::string_field(content, "type").value_or("");
    if (type == "output_text")
    {
      auto const text = ava::core::json::string_field(content, "text");
      if (!text)
        return std::unexpected(ava::core::Error(ava::core::ErrorCategory::Provider, "OpenAI output_text content part requires string text"));
      complete.output_text += *text;
      continue;
    }
    if (type == "refusal")
    {
      auto const refusal = ava::core::json::string_field(content, "refusal");
      if (!refusal)
        return std::unexpected(ava::core::Error(ava::core::ErrorCategory::Provider, "OpenAI refusal content part requires string refusal"));
      complete.refusal_text += *refusal;
      continue;
    }
    return std::unexpected(ava::core::Error(ava::core::ErrorCategory::Provider, "OpenAI message output item has an unsupported content-part type"));
  }
  return std::optional<CompletedMessageContent>{std::move(complete)};
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

enum class MessageContentKind
{
  OutputText,
  Refusal,
};

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

OpenAIStreamParser::FunctionCallState* legacy_function_call_state_for_existing_event(
    std::string_view data, std::unordered_map<std::string, OpenAIStreamParser::FunctionCallState>& calls)
{
  auto const item_id = function_call_item_id_from_event(data);
  if (!item_id.empty())
  {
    if (auto const found = calls.find(item_id); found != calls.end())
      return &found->second;
  }
  auto const call_id = logical_function_call_id_from_event(data);
  if (call_id.empty())
    return nullptr;
  for (auto& [item, state] : calls)
  {
    static_cast<void>(item);
    if (state.logical_call_id == call_id)
      return &state;
  }
  return nullptr;
}

bool has_documented_function_call_mapping(std::string_view data, std::unordered_map<std::string, std::string> const& item_ids_by_logical_id)
{
  // item_id/output_item_id identify documented Responses output items. Their
  // mere presence commits the event to the documented path: an unknown, empty,
  // or non-string identity must not be reinterpreted as a legacy call ID.
  if (ava::core::json::field_value_start(data, "item_id") || ava::core::json::field_value_start(data, "output_item_id"))
    return true;
  auto const call_id = ava::core::json::string_field(data, "call_id");
  return call_id && item_ids_by_logical_id.contains(*call_id);
}

bool has_valid_legacy_function_call_identity(std::string_view data)
{
  auto const call_id = ava::core::json::string_field(data, "call_id");
  return call_id && is_valid_openai_opaque_id(*call_id);
}

OpenAIStreamParser::FunctionCallState* bind_documented_function_call_item(std::vector<StreamEvent>& events, bool& error_seen, std::string_view item,
                                                                          std::optional<std::size_t> provider_output_index,
                                                                          std::unordered_map<std::string, OpenAIStreamParser::FunctionCallState>& calls,
                                                                          std::unordered_map<std::string, std::string>& item_ids_by_logical_id)
{
  auto const item_id = ava::core::json::string_field(item, "id");
  auto const call_id = ava::core::json::string_field(item, "call_id");
  auto const name = ava::core::json::string_field(item, "name");
  auto const arguments = ava::core::json::string_field(item, "arguments");
  if (!item_id || !call_id || !name || !arguments || !is_valid_openai_opaque_id(*item_id) || !is_valid_openai_opaque_id(*call_id) ||
      !is_valid_openai_opaque_id(*name))
  {
    append_stream_error(events, error_seen, "OpenAI function call item requires bounded item ID, logical call ID, name, and string arguments");
    return nullptr;
  }

  auto const existing_item = calls.find(*item_id);
  if (existing_item != calls.end())
  {
    if (existing_item->second.logical_call_id != *call_id || existing_item->second.name != *name)
    {
      append_stream_error(events, error_seen, "OpenAI function call item changed its logical call ID or name");
      return nullptr;
    }
    if (provider_output_index && existing_item->second.provider_output_index != provider_output_index)
    {
      append_stream_error(events, error_seen, "OpenAI function call item changed its output_index");
      return nullptr;
    }
    return &existing_item->second;
  }

  if (auto const existing_logical_id = item_ids_by_logical_id.find(*call_id);
      existing_logical_id != item_ids_by_logical_id.end() && existing_logical_id->second != *item_id)
  {
    append_stream_error(events, error_seen, "OpenAI function call logical call ID is already bound to another item");
    return nullptr;
  }

  auto [state, inserted] = calls.emplace(*item_id, OpenAIStreamParser::FunctionCallState{.provider_item_id = *item_id,
                                                                                         .provider_output_index = provider_output_index,
                                                                                         .logical_call_id = *call_id,
                                                                                         .name = *name,
                                                                                         .arguments = "",
                                                                                         .emitted_argument_bytes = 0,
                                                                                         .started = false,
                                                                                         .ended = false});
  static_cast<void>(inserted);
  item_ids_by_logical_id.emplace(*call_id, *item_id);
  return &state->second;
}

OpenAIStreamParser::FunctionCallState* documented_function_call_state_for_event(std::vector<StreamEvent>& events, bool& error_seen, std::string_view data,
                                                                                std::unordered_map<std::string, OpenAIStreamParser::FunctionCallState>& calls,
                                                                                std::unordered_map<std::string, std::string> const& item_ids_by_logical_id)
{
  auto const item_id_start = ava::core::json::field_value_start(data, "item_id");
  auto const item_id = ava::core::json::string_field(data, "item_id");
  auto const output_item_id_start = ava::core::json::field_value_start(data, "output_item_id");
  auto const output_item_id = ava::core::json::string_field(data, "output_item_id");
  auto const call_id_start = ava::core::json::field_value_start(data, "call_id");
  auto const call_id = ava::core::json::string_field(data, "call_id");
  auto const name_start = ava::core::json::field_value_start(data, "name");
  auto const name = ava::core::json::string_field(data, "name");

  if ((item_id_start && (!item_id || !is_valid_openai_opaque_id(*item_id))) ||
      (output_item_id_start && (!output_item_id || !is_valid_openai_opaque_id(*output_item_id))))
  {
    append_stream_error(events, error_seen, "OpenAI function call arguments contain an invalid documented item ID");
    return nullptr;
  }
  if (item_id && output_item_id && *item_id != *output_item_id)
  {
    append_stream_error(events, error_seen, "OpenAI function call arguments have conflicting documented item IDs");
    return nullptr;
  }

  OpenAIStreamParser::FunctionCallState* state = nullptr;
  auto const documented_item_id = item_id ? item_id : output_item_id;
  if (documented_item_id)
  {
    auto const found = calls.find(*documented_item_id);
    if (found == calls.end())
    {
      append_stream_error(events, error_seen, "OpenAI function call arguments reference an unbound item ID");
      return nullptr;
    }
    state = &found->second;
  }
  if (call_id_start)
  {
    if (!call_id || !is_valid_openai_opaque_id(*call_id))
    {
      append_stream_error(events, error_seen, "OpenAI function call arguments contain an invalid logical call ID");
      return nullptr;
    }
    auto const mapped_item = item_ids_by_logical_id.find(*call_id);
    if (mapped_item == item_ids_by_logical_id.end())
    {
      append_stream_error(events, error_seen, "OpenAI function call arguments reference an unbound logical call ID");
      return nullptr;
    }
    auto const found = calls.find(mapped_item->second);
    if (found == calls.end() || (state && state != &found->second))
    {
      append_stream_error(events, error_seen, "OpenAI function call arguments have conflicting item and logical call IDs");
      return nullptr;
    }
    state = &found->second;
  }
  if (!state)
  {
    append_stream_error(events, error_seen, "OpenAI function call arguments are missing an item ID or logical call ID");
    return nullptr;
  }
  if (state->ended)
  {
    append_stream_error(events, error_seen, "OpenAI function call arguments emitted after completion");
    return nullptr;
  }
  auto const output_index = documented_output_index(data, "{}");
  if (!output_index)
  {
    append_stream_error(events, error_seen, output_index.error().message());
    return nullptr;
  }
  if (*output_index && state->provider_output_index != *output_index)
  {
    append_stream_error(events, error_seen, "OpenAI function call arguments changed their output_index");
    return nullptr;
  }
  if ((call_id && state->logical_call_id != *call_id) || (name_start && (!name || state->name != *name)))
  {
    append_stream_error(events, error_seen, "OpenAI function call arguments changed their item binding");
    return nullptr;
  }
  return state;
}

bool append_function_call_start_if_ready(std::vector<StreamEvent>& events, OpenAIStreamParser::FunctionCallState& state)
{
  if (state.started)
    return true;
  if (state.logical_call_id.empty() || state.name.empty())
    return false;
  state.started = true;
  events.push_back(StreamEvent{.type = StreamEventType::ToolCallStart,
                               .text = "",
                               .tool_call_id = state.logical_call_id,
                               .tool_name = state.name,
                               .error_message = "",
                               .usage = std::nullopt,
                               .provider_item_id = state.provider_item_id,
                               .provider_output_index = state.provider_output_index});
  return true;
}

void append_unemitted_function_call_arguments(std::vector<StreamEvent>& events, OpenAIStreamParser::FunctionCallState& state)
{
  if (!state.started || state.emitted_argument_bytes >= state.arguments.size())
    return;
  events.push_back(StreamEvent{.type = StreamEventType::ToolCallDelta,
                               .text = state.arguments.substr(state.emitted_argument_bytes),
                               .tool_call_id = state.logical_call_id,
                               .tool_name = "",
                               .error_message = "",
                               .usage = std::nullopt,
                               .provider_item_id = state.provider_item_id,
                               .provider_output_index = state.provider_output_index});
  state.emitted_argument_bytes = state.arguments.size();
}

bool append_function_call_argument_fragment(std::vector<StreamEvent>& events, OpenAIStreamParser::FunctionCallState& state, std::string_view fragment)
{
  if (fragment.empty())
    return true;
  state.arguments += fragment;
  static_cast<void>(append_function_call_start_if_ready(events, state));
  append_unemitted_function_call_arguments(events, state);
  return true;
}

bool reconcile_complete_function_call_arguments(std::vector<StreamEvent>& events, OpenAIStreamParser::FunctionCallState& state,
                                                std::optional<std::string> const& complete_arguments, bool& error_seen)
{
  if (!complete_arguments)
    return true;
  if (*complete_arguments == state.arguments)
    return true;
  if (complete_arguments->starts_with(state.arguments))
  {
    state.arguments = *complete_arguments;
    static_cast<void>(append_function_call_start_if_ready(events, state));
    append_unemitted_function_call_arguments(events, state);
    return true;
  }
  append_stream_error(events, error_seen, "conflicting OpenAI function call arguments");
  return false;
}

bool append_function_call_end(std::vector<StreamEvent>& events, OpenAIStreamParser::FunctionCallState& state, bool require_named_start, bool& error_seen)
{
  if (state.ended)
    return true;
  if (!state.started)
  {
    if (!append_function_call_start_if_ready(events, state))
    {
      if (require_named_start)
      {
        append_stream_error(events, error_seen, "OpenAI function call item has an empty logical call ID or name");
        return false;
      }
    }
    else
    {
      append_unemitted_function_call_arguments(events, state);
    }
  }
  state.ended = true;
  events.push_back(StreamEvent{.type = StreamEventType::ToolCallEnd,
                               .text = "",
                               .tool_call_id = state.logical_call_id,
                               .tool_name = "",
                               .error_message = "",
                               .usage = std::nullopt,
                               .provider_item_id = state.provider_item_id,
                               .provider_output_index = state.provider_output_index});
  return true;
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
                                     std::string native_item_json = {})
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

void append_event_for_data(std::vector<StreamEvent>& events, std::string_view data, bool& saw_content, bool& saw_refusal, bool& reasoning_open,
                           bool& reasoning_text_seen, std::string& active_reasoning_item_id, std::optional<std::size_t>& active_reasoning_output_index,
                           std::string& active_reasoning_text, std::vector<std::string>& completed_reasoning_item_ids,
                           std::vector<std::string>& completed_reasoning_texts, bool& done_seen, bool& error_seen,
                           std::unordered_map<std::string, OpenAIStreamParser::FunctionCallState>& function_calls,
                           std::unordered_map<std::string, std::string>& function_call_item_ids_by_logical_id,
                           std::unordered_map<std::string, OpenAIStreamParser::FunctionCallState>& legacy_function_calls,
                           std::unordered_map<std::string, OpenAIStreamParser::MessageItemState>& message_items,
                           std::unordered_map<std::string, std::string>& documented_output_item_types,
                           std::unordered_set<std::string>& documented_output_item_added_ids,
                           std::unordered_map<std::size_t, std::string>& documented_output_item_ids_by_index)
{
  if (data == "[DONE]")
  {
    if (done_seen)
      return;
    done_seen = true;
    if (reject_unended_documented_function_calls(events, error_seen, function_calls) ||
        reject_unended_documented_message_items(events, error_seen, message_items))
      return;
    append_finish_reasoning_if_open(events, reasoning_open, reasoning_text_seen, active_reasoning_item_id, active_reasoning_output_index, active_reasoning_text,
                                    completed_reasoning_item_ids, completed_reasoning_texts);
    events.push_back(StreamEvent{.type = StreamEventType::Done,
                                 .text = "",
                                 .tool_call_id = "",
                                 .tool_name = "",
                                 .error_message = "",
                                 .usage = std::nullopt,
                                 .finish_reason = saw_refusal ? ProviderFinishReason::Refusal : ProviderFinishReason::Completed});
    return;
  }
  if (done_seen)
  {
    append_stream_error(events, error_seen, "OpenAI response emitted an event after its terminal marker");
    return;
  }
  if (!is_json_object_shape(data))
  {
    append_finish_reasoning_if_open(events, reasoning_open, reasoning_text_seen, active_reasoning_item_id, active_reasoning_output_index, active_reasoning_text,
                                    completed_reasoning_item_ids, completed_reasoning_texts);
    append_stream_error(events, error_seen, "malformed OpenAI SSE event");
    return;
  }
  auto const type = ava::core::json::string_field(data, "type").value_or("");
  if (type == "response.output_item.added")
  {
    auto const item = ava::core::json::object_field(data, "item");
    auto const item_type = item ? ava::core::json::string_field(*item, "type").value_or("") : "";
    if (item_type == "message" || item_type == "reasoning" || item_type == "function_call")
    {
      auto metadata = documented_output_item_metadata(data, *item, item_type == "message");
      if (!metadata)
      {
        append_stream_error(events, error_seen, metadata.error().message());
        return;
      }
      if (!register_documented_output_item(events, error_seen, metadata->id, item_type, metadata->output_index, true, documented_output_item_types,
                                           documented_output_item_added_ids, documented_output_item_ids_by_index))
      {
        return;
      }
      saw_content = true;
      if (item_type == "message")
      {
        auto const content = completed_message_content_from_item(*item);
        if (!content)
        {
          append_stream_error(events, error_seen, content.error().message());
          return;
        }
        auto const [state, inserted] = message_items.emplace(metadata->id, OpenAIStreamParser::MessageItemState{.provider_output_index = metadata->output_index,
                                                                                                                .phase = metadata->phase,
                                                                                                                .text = "",
                                                                                                                .output_text = "",
                                                                                                                .refusal_text = "",
                                                                                                                .output_text_completed = false,
                                                                                                                .refusal_completed = false,
                                                                                                                .ended = false});
        static_cast<void>(state);
        if (!inserted)
        {
          append_stream_error(events, error_seen, "OpenAI message output item was added more than once");
          return;
        }
        events.push_back(StreamEvent{.type = StreamEventType::TextStart,
                                     .text = "",
                                     .tool_call_id = "",
                                     .tool_name = "",
                                     .error_message = "",
                                     .usage = std::nullopt,
                                     .provider_item_id = metadata->id,
                                     .provider_output_index = metadata->output_index,
                                     .assistant_phase = metadata->phase});
        if (*content)
        {
          append_documented_message_delta(events, state->second, metadata->id, (*content)->output_text, MessageContentKind::OutputText);
          append_documented_message_delta(events, state->second, metadata->id, (*content)->refusal_text, MessageContentKind::Refusal);
          if (!(*content)->refusal_text.empty())
            saw_refusal = true;
        }
      }
      else if (item_type == "reasoning")
      {
        if (reasoning_open && active_reasoning_item_id != metadata->id)
        {
          append_stream_error(events, error_seen, "OpenAI reasoning output items overlapped");
          return;
        }
        active_reasoning_item_id = metadata->id;
        active_reasoning_output_index = metadata->output_index;
        append_start_reasoning_if_needed(events, reasoning_open, active_reasoning_item_id, active_reasoning_output_index);
      }
      else
      {
        append_finish_reasoning_if_open(events, reasoning_open, reasoning_text_seen, active_reasoning_item_id, active_reasoning_output_index,
                                        active_reasoning_text, completed_reasoning_item_ids, completed_reasoning_texts);
        if (auto* state =
                bind_documented_function_call_item(events, error_seen, *item, metadata->output_index, function_calls, function_call_item_ids_by_logical_id))
        {
          static_cast<void>(append_function_call_argument_fragment(events, *state, function_call_arguments_from_event(data, *item).value_or("")));
          static_cast<void>(append_function_call_start_if_ready(events, *state));
          append_unemitted_function_call_arguments(events, *state);
        }
      }
    }
    else
    {
      append_stream_error(events, error_seen, "OpenAI response contains an unsupported output item type");
    }
    return;
  }
  if (type == "response.output_item.done")
  {
    auto const item = ava::core::json::object_field(data, "item");
    auto const item_type = item ? ava::core::json::string_field(*item, "type").value_or("") : "";
    if (item_type == "message" || item_type == "reasoning" || item_type == "function_call")
    {
      auto metadata = documented_output_item_metadata(data, *item, item_type == "message");
      if (!metadata)
      {
        append_stream_error(events, error_seen, metadata.error().message());
        return;
      }
      if (!register_documented_output_item(events, error_seen, metadata->id, item_type, metadata->output_index, false, documented_output_item_types,
                                           documented_output_item_added_ids, documented_output_item_ids_by_index))
      {
        return;
      }
      saw_content = true;
      if (item_type == "message")
      {
        auto const state = message_items.find(metadata->id);
        if (state == message_items.end())
        {
          append_stream_error(events, error_seen, "OpenAI message output item completed without one matching add lifecycle event");
          return;
        }
        if (state->second.ended)
        {
          append_stream_error(events, error_seen, "OpenAI message output item completed more than once");
          return;
        }
        if ((metadata->output_index && state->second.provider_output_index != metadata->output_index) ||
            (metadata->phase != AssistantPhase::Unknown && state->second.phase != AssistantPhase::Unknown && metadata->phase != state->second.phase))
        {
          append_stream_error(events, error_seen, "OpenAI message output item changed its output_index or phase");
          return;
        }
        if (state->second.phase == AssistantPhase::Unknown && metadata->phase != AssistantPhase::Unknown)
          state->second.phase = metadata->phase;
        auto const complete_content = completed_message_content_from_item(*item);
        if (!complete_content)
        {
          append_stream_error(events, error_seen, complete_content.error().message());
          return;
        }
        if (*complete_content)
        {
          if (!reconcile_complete_message_text(events, state->second, metadata->id, (*complete_content)->output_text, MessageContentKind::OutputText,
                                               error_seen) ||
              !reconcile_complete_message_text(events, state->second, metadata->id, (*complete_content)->refusal_text, MessageContentKind::Refusal, error_seen))
          {
            return;
          }
          if (!(*complete_content)->refusal_text.empty())
            saw_refusal = true;
        }
        state->second.ended = true;
        events.push_back(StreamEvent{.type = StreamEventType::TextEnd,
                                     .text = "",
                                     .tool_call_id = "",
                                     .tool_name = "",
                                     .error_message = "",
                                     .usage = std::nullopt,
                                     .provider_item_id = metadata->id,
                                     .provider_output_index = state->second.provider_output_index,
                                     .assistant_phase = state->second.phase});
      }
      else if (item_type == "reasoning")
      {
        if (contains_string(completed_reasoning_item_ids, metadata->id))
        {
          append_stream_error(events, error_seen, "OpenAI reasoning output item completed more than once");
          return;
        }
        if (reasoning_open && active_reasoning_item_id != metadata->id)
        {
          append_stream_error(events, error_seen, "OpenAI reasoning output item completion did not match the active item");
          return;
        }
        active_reasoning_item_id = metadata->id;
        active_reasoning_output_index = metadata->output_index;
        std::size_t remaining_summary_parts = kMaxProviderParserArrayItems;
        auto summary = detail::reasoning_summary_text_from_object(*item, remaining_summary_parts);
        if (!summary)
        {
          append_stream_error(events, error_seen, "OpenAI response parser limit exceeded");
          return;
        }
        if (reasoning_event_has_authoritative_complete_text(*item) &&
            !reconcile_complete_reasoning_text(events, reasoning_open, reasoning_text_seen, active_reasoning_item_id, active_reasoning_output_index,
                                               active_reasoning_text, *summary, error_seen))
        {
          return;
        }
        append_finish_reasoning_if_open(events, reasoning_open, reasoning_text_seen, active_reasoning_item_id, active_reasoning_output_index,
                                        active_reasoning_text, completed_reasoning_item_ids, completed_reasoning_texts,
                                        is_valid_openai_native_reasoning_item_json(*item) ? *item : std::string{});
      }
      else
      {
        append_finish_reasoning_if_open(events, reasoning_open, reasoning_text_seen, active_reasoning_item_id, active_reasoning_output_index,
                                        active_reasoning_text, completed_reasoning_item_ids, completed_reasoning_texts);
        auto const final_arguments = ava::core::json::string_field(*item, "arguments");
        if (!final_arguments || !is_valid_function_call_arguments_object(*final_arguments))
        {
          append_stream_error(events, error_seen, "OpenAI function call output item completion requires JSON-object string arguments");
          return;
        }
        if (auto* state =
                bind_documented_function_call_item(events, error_seen, *item, metadata->output_index, function_calls, function_call_item_ids_by_logical_id))
        {
          if (state->ended)
          {
            append_stream_error(events, error_seen, "OpenAI function call output item completed more than once");
            return;
          }
          if (!reconcile_complete_function_call_arguments(events, *state, final_arguments, error_seen))
            return;
          static_cast<void>(append_function_call_end(events, *state, true, error_seen));
        }
      }
    }
    else
    {
      append_stream_error(events, error_seen, "OpenAI response contains an unsupported output item type");
    }
    return;
  }
  if (type == "response.reasoning_summary_part.added")
  {
    auto const item_id = reasoning_item_id_from_event(data);
    if (!bind_documented_reasoning_event(events, error_seen, data, active_reasoning_item_id, active_reasoning_output_index, documented_output_item_types,
                                         documented_output_item_ids_by_index))
      return;
    if (contains_string(completed_reasoning_item_ids, item_id))
      return;
    saw_content = true;
    set_active_reasoning_item_id(active_reasoning_item_id, item_id);
    append_start_reasoning_if_needed(events, reasoning_open, active_reasoning_item_id, active_reasoning_output_index);
    return;
  }
  if (type == "response.reasoning_summary_text.delta" || type == "response.reasoning_text.delta")
  {
    auto const item_id = reasoning_item_id_from_event(data);
    if (!bind_documented_reasoning_event(events, error_seen, data, active_reasoning_item_id, active_reasoning_output_index, documented_output_item_types,
                                         documented_output_item_ids_by_index))
      return;
    if (contains_string(completed_reasoning_item_ids, item_id))
      return;
    saw_content = true;
    set_active_reasoning_item_id(active_reasoning_item_id, item_id);
    append_reasoning_delta(events, reasoning_open, reasoning_text_seen, active_reasoning_item_id, active_reasoning_output_index, active_reasoning_text,
                           detail::first_string_field(data, {"delta", "text"}).value_or(""));
    return;
  }
  if (type == "response.reasoning_summary_text.done" || type == "response.reasoning_summary_part.done" || type == "response.reasoning_text.done")
  {
    auto const item_id = reasoning_item_id_from_event(data);
    if (!bind_documented_reasoning_event(events, error_seen, data, active_reasoning_item_id, active_reasoning_output_index, documented_output_item_types,
                                         documented_output_item_ids_by_index))
      return;
    std::size_t remaining_summary_parts = kMaxProviderParserArrayItems;
    auto summary = detail::reasoning_summary_text_from_object(data, remaining_summary_parts);
    if (!summary)
    {
      append_stream_error(events, error_seen, "OpenAI response parser limit exceeded");
      return;
    }
    if (contains_string(completed_reasoning_item_ids, item_id))
      return;
    saw_content = true;
    set_active_reasoning_item_id(active_reasoning_item_id, item_id);
    if (reasoning_event_has_authoritative_complete_text(data) &&
        !reconcile_complete_reasoning_text(events, reasoning_open, reasoning_text_seen, active_reasoning_item_id, active_reasoning_output_index,
                                           active_reasoning_text, *summary, error_seen))
    {
      return;
    }
    // Do not close here: output_item.done carries the opaque reasoning item
    // that must be attached to the same private reasoning block for replay.
    return;
  }
  if (type == "response.function_call_arguments.done")
  {
    bool const documented_mapping = has_documented_function_call_mapping(data, function_call_item_ids_by_logical_id);
    auto* state = documented_mapping ? documented_function_call_state_for_event(events, error_seen, data, function_calls, function_call_item_ids_by_logical_id)
                                     : legacy_function_call_state_for_existing_event(data, legacy_function_calls);
    if (!state && !documented_mapping)
    {
      if (!has_valid_legacy_function_call_identity(data))
      {
        append_stream_error(events, error_seen, "OpenAI legacy function call arguments require a nonempty logical call ID");
        return;
      }
      state = &legacy_function_call_state_for(data, legacy_function_calls);
    }
    auto const complete_arguments = ava::core::json::string_field(data, "arguments");
    if (documented_mapping && (!complete_arguments || !is_valid_function_call_arguments_object(*complete_arguments)))
    {
      append_stream_error(events, error_seen, "OpenAI documented function call arguments completion requires JSON-object string arguments");
      return;
    }
    if (state)
    {
      if (!reconcile_complete_function_call_arguments(events, *state, complete_arguments, error_seen))
        return;
      static_cast<void>(append_function_call_start_if_ready(events, *state));
      append_unemitted_function_call_arguments(events, *state);
    }
    return;
  }
  if (type == "response.output_text.done")
  {
    bool documented = false;
    auto* state = documented_message_item_for_event(events, error_seen, data, message_items, documented);
    if (!documented)
      return;  // Retain legacy output_text.done compatibility.
    if (!state)
      return;
    if (state->output_text_completed)
    {
      append_stream_error(events, error_seen, "OpenAI message text completed more than once");
      return;
    }
    auto const text_start = ava::core::json::field_value_start(data, "text");
    auto const complete_text = ava::core::json::string_field(data, "text");
    if (text_start && !complete_text)
    {
      append_stream_error(events, error_seen, "OpenAI message text completion requires a string text");
      return;
    }
    auto const item_id = ava::core::json::string_field(data, "item_id").or_else([&data] { return ava::core::json::string_field(data, "output_item_id"); });
    if (!reconcile_complete_message_text(events, *state, *item_id, complete_text.value_or(""), MessageContentKind::OutputText, error_seen))
      return;
    state->output_text_completed = true;
    saw_content = true;
    return;
  }
  if (type == "response.refusal.done")
  {
    append_finish_reasoning_if_open(events, reasoning_open, reasoning_text_seen, active_reasoning_item_id, active_reasoning_output_index, active_reasoning_text,
                                    completed_reasoning_item_ids, completed_reasoning_texts);
    bool documented = false;
    auto* state = documented_message_item_for_event(events, error_seen, data, message_items, documented);
    if (!documented)
    {
      append_stream_error(events, error_seen, "OpenAI refusal completion requires a documented message item ID");
      return;
    }
    if (!state)
      return;
    if (state->refusal_completed)
    {
      append_stream_error(events, error_seen, "OpenAI message refusal completed more than once");
      return;
    }
    auto const refusal = ava::core::json::string_field(data, "refusal");
    if (!refusal)
    {
      append_stream_error(events, error_seen, "OpenAI refusal completion requires string refusal");
      return;
    }
    auto const item_id = ava::core::json::string_field(data, "item_id").or_else([&data] { return ava::core::json::string_field(data, "output_item_id"); });
    if (!reconcile_complete_message_text(events, *state, *item_id, *refusal, MessageContentKind::Refusal, error_seen))
      return;
    state->refusal_completed = true;
    saw_content = true;
    saw_refusal = true;
    return;
  }
  if (is_ignored_lifecycle_event(type))
    return;
  if (type == "response.refusal.delta")
  {
    append_finish_reasoning_if_open(events, reasoning_open, reasoning_text_seen, active_reasoning_item_id, active_reasoning_output_index, active_reasoning_text,
                                    completed_reasoning_item_ids, completed_reasoning_texts);
    bool documented = false;
    auto* state = documented_message_item_for_event(events, error_seen, data, message_items, documented);
    if (!documented)
    {
      append_stream_error(events, error_seen, "OpenAI refusal delta requires a documented message item ID");
      return;
    }
    if (!state)
      return;
    if (state->refusal_completed)
    {
      append_stream_error(events, error_seen, "OpenAI message refusal emitted after completion");
      return;
    }
    auto const delta = ava::core::json::string_field(data, "delta");
    if (!delta)
    {
      append_stream_error(events, error_seen, "OpenAI refusal delta requires string delta");
      return;
    }
    auto const item_id = ava::core::json::string_field(data, "item_id").or_else([&data] { return ava::core::json::string_field(data, "output_item_id"); });
    append_documented_message_delta(events, *state, *item_id, *delta, MessageContentKind::Refusal);
    saw_content = true;
    saw_refusal = true;
    return;
  }
  if (type == "response.output_text.delta" || type == "response.text.delta")
  {
    saw_content = true;
    append_finish_reasoning_if_open(events, reasoning_open, reasoning_text_seen, active_reasoning_item_id, active_reasoning_output_index, active_reasoning_text,
                                    completed_reasoning_item_ids, completed_reasoning_texts);
    bool documented = false;
    auto* state = documented_message_item_for_event(events, error_seen, data, message_items, documented);
    if (documented)
    {
      if (!state)
        return;
      if (state->output_text_completed)
      {
        append_stream_error(events, error_seen, "OpenAI message text emitted after completion");
        return;
      }
      auto const item_id = ava::core::json::string_field(data, "item_id").or_else([&data] { return ava::core::json::string_field(data, "output_item_id"); });
      append_documented_message_delta(
          events, *state, *item_id,
          ava::core::json::string_field(data, "delta").or_else([&data] { return ava::core::json::string_field(data, "text"); }).value_or(""),
          MessageContentKind::OutputText);
      return;
    }
    events.push_back(
        StreamEvent{.type = StreamEventType::TextDelta,
                    .text = ava::core::json::string_field(data, "delta").or_else([&data] { return ava::core::json::string_field(data, "text"); }).value_or(""),
                    .tool_call_id = "",
                    .tool_name = "",
                    .error_message = "",
                    .usage = std::nullopt});
    return;
  }
  if (type == "response.function_call_arguments.delta")
  {
    saw_content = true;
    append_finish_reasoning_if_open(events, reasoning_open, reasoning_text_seen, active_reasoning_item_id, active_reasoning_output_index, active_reasoning_text,
                                    completed_reasoning_item_ids, completed_reasoning_texts);
    bool const documented_mapping = has_documented_function_call_mapping(data, function_call_item_ids_by_logical_id);
    auto* state = documented_mapping ? documented_function_call_state_for_event(events, error_seen, data, function_calls, function_call_item_ids_by_logical_id)
                                     : legacy_function_call_state_for_existing_event(data, legacy_function_calls);
    if (!state && !documented_mapping)
    {
      if (!has_valid_legacy_function_call_identity(data))
      {
        append_stream_error(events, error_seen, "OpenAI legacy function call arguments require a nonempty logical call ID");
        return;
      }
      state = &legacy_function_call_state_for(data, legacy_function_calls);
    }
    auto const delta = ava::core::json::string_field(data, "delta");
    if (documented_mapping && !delta)
    {
      append_stream_error(events, error_seen, "OpenAI documented function call arguments delta requires a string delta");
      return;
    }
    if (state)
      static_cast<void>(append_function_call_argument_fragment(events, *state, delta.value_or("")));
    return;
  }
  if (type == "response.function_call.added")
  {
    saw_content = true;
    append_finish_reasoning_if_open(events, reasoning_open, reasoning_text_seen, active_reasoning_item_id, active_reasoning_output_index, active_reasoning_text,
                                    completed_reasoning_item_ids, completed_reasoning_texts);
    auto& state = legacy_function_call_state_for(data, legacy_function_calls);
    if (state.logical_call_id.empty())
    {
      // Preserve the provider-neutral empty-ID signal so the agent's existing
      // call-ID validator rejects it before any session mutation.
      events.push_back(StreamEvent{
          .type = StreamEventType::ToolCallStart, .text = "", .tool_call_id = "", .tool_name = state.name, .error_message = "", .usage = std::nullopt});
      return;
    }
    if (state.name.empty())
    {
      append_stream_error(events, error_seen, "OpenAI function call item has an empty logical call ID or name");
      return;
    }
    static_cast<void>(append_function_call_argument_fragment(events, state, function_call_arguments_from_event(data).value_or("")));
    static_cast<void>(append_function_call_start_if_ready(events, state));
    append_unemitted_function_call_arguments(events, state);
    return;
  }
  if (type == "response.function_call.done" || type == "response.function_call.completed")
  {
    saw_content = true;
    append_finish_reasoning_if_open(events, reasoning_open, reasoning_text_seen, active_reasoning_item_id, active_reasoning_output_index, active_reasoning_text,
                                    completed_reasoning_item_ids, completed_reasoning_texts);
    auto& state = legacy_function_call_state_for(data, legacy_function_calls);
    if (!reconcile_complete_function_call_arguments(events, state, function_call_arguments_from_event(data), error_seen))
      return;
    static_cast<void>(append_function_call_end(events, state, false, error_seen));
    return;
  }
  if (type == "response.completed" || type == "response.incomplete")
  {
    if (done_seen)
      return;
    done_seen = true;
    if (reject_unended_documented_function_calls(events, error_seen, function_calls) ||
        reject_unended_documented_message_items(events, error_seen, message_items))
      return;
    append_finish_reasoning_if_open(events, reasoning_open, reasoning_text_seen, active_reasoning_item_id, active_reasoning_output_index, active_reasoning_text,
                                    completed_reasoning_item_ids, completed_reasoning_texts);
    events.push_back(StreamEvent{
        .type = StreamEventType::Done,
        .text = "",
        .tool_call_id = "",
        .tool_name = "",
        .error_message = "",
        .usage = parse_openai_usage(data),
        .finish_reason = saw_refusal ? ProviderFinishReason::Refusal
                                     : (type == "response.completed" ? ProviderFinishReason::Completed : detail::openai_response_finish_reason(data))});
    return;
  }
  if (type == "error" || type == "response.error" || type == "response.failed")
  {
    append_finish_reasoning_if_open(events, reasoning_open, reasoning_text_seen, active_reasoning_item_id, active_reasoning_output_index, active_reasoning_text,
                                    completed_reasoning_item_ids, completed_reasoning_texts);
    append_stream_error(events, error_seen, "OpenAI provider reported a streaming error");
    return;
  }
  // OpenAI may add non-content lifecycle events without changing the assistant turn.
}

void append_events_for_sse_line(std::vector<StreamEvent>& events, std::string& data, bool& saw_content, bool& saw_refusal, bool& reasoning_open,
                                bool& reasoning_text_seen, std::string& active_reasoning_item_id, std::optional<std::size_t>& active_reasoning_output_index,
                                std::string& active_reasoning_text, std::vector<std::string>& completed_reasoning_item_ids,
                                std::vector<std::string>& completed_reasoning_texts, bool& done_seen, bool& error_seen,
                                std::unordered_map<std::string, OpenAIStreamParser::FunctionCallState>& function_calls,
                                std::unordered_map<std::string, std::string>& function_call_item_ids_by_logical_id,
                                std::unordered_map<std::string, OpenAIStreamParser::FunctionCallState>& legacy_function_calls,
                                std::unordered_map<std::string, OpenAIStreamParser::MessageItemState>& message_items,
                                std::unordered_map<std::string, std::string>& documented_output_item_types,
                                std::unordered_set<std::string>& documented_output_item_added_ids,
                                std::unordered_map<std::size_t, std::string>& documented_output_item_ids_by_index, std::size_t& data_records_seen,
                                bool& data_record_limit_exceeded, std::string line)
{
  if (!line.empty() && line.back() == '\r')
    line.pop_back();
  if (line.empty())
  {
    if (!data.empty())
    {
      if (++data_records_seen > kMaxProviderParserEvents)
      {
        data_record_limit_exceeded = true;
        data.clear();
        return;
      }
      append_event_for_data(events, data, saw_content, saw_refusal, reasoning_open, reasoning_text_seen, active_reasoning_item_id,
                            active_reasoning_output_index, active_reasoning_text, completed_reasoning_item_ids, completed_reasoning_texts, done_seen,
                            error_seen, function_calls, function_call_item_ids_by_logical_id, legacy_function_calls, message_items,
                            documented_output_item_types, documented_output_item_added_ids, documented_output_item_ids_by_index);
      data.clear();
    }
    return;
  }
  if (line.starts_with("data:"))
  {
    if (!data.empty())
      data.push_back('\n');
    auto value = std::string_view(line).substr(5);
    if (!value.empty() && value.front() == ' ')
      value.remove_prefix(1);
    data.append(value);
  }
}

}  // namespace

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
    append_events_for_sse_line(events, data_, saw_content_, saw_refusal_, reasoning_open_, reasoning_text_seen_, active_reasoning_item_id_,
                               active_reasoning_output_index_, active_reasoning_text_, completed_reasoning_item_ids_, completed_reasoning_texts_, done_seen_,
                               error_seen_, function_calls_, function_call_item_ids_by_logical_id_, legacy_function_calls_, message_items_,
                               documented_output_item_types_, documented_output_item_added_ids_, documented_output_item_ids_by_index_, data_records_seen_,
                               data_record_limit_exceeded_, pending_line_.substr(line_start, newline - line_start));
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
    append_events_for_sse_line(events, data_, saw_content_, saw_refusal_, reasoning_open_, reasoning_text_seen_, active_reasoning_item_id_,
                               active_reasoning_output_index_, active_reasoning_text_, completed_reasoning_item_ids_, completed_reasoning_texts_, done_seen_,
                               error_seen_, function_calls_, function_call_item_ids_by_logical_id_, legacy_function_calls_, message_items_,
                               documented_output_item_types_, documented_output_item_added_ids_, documented_output_item_ids_by_index_, data_records_seen_,
                               data_record_limit_exceeded_, std::move(pending_line_));
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
      append_event_for_data(events, data_, saw_content_, saw_refusal_, reasoning_open_, reasoning_text_seen_, active_reasoning_item_id_,
                            active_reasoning_output_index_, active_reasoning_text_, completed_reasoning_item_ids_, completed_reasoning_texts_, done_seen_,
                            error_seen_, function_calls_, function_call_item_ids_by_logical_id_, legacy_function_calls_, message_items_,
                            documented_output_item_types_, documented_output_item_added_ids_, documented_output_item_ids_by_index_);
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
