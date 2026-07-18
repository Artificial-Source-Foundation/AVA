#include "sys.h"
#include "ava/provider/openai_reasoning.h"
#include "ava/provider/openai_response_parser.h"
#include "ava/provider/openai_response_parser_detail.h"
#include "ava/provider/openai_stream_parser.h"
#include "ava/provider/provider_utils.h"
#include "ava/core/json.h"

#include <algorithm>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace ava::provider {
namespace {

bool is_ignored_lifecycle_event(std::string_view type)
{
  return type == "response.created" || type == "response.in_progress" || type == "response.content_part.added" || type == "response.content_part.done" ||
         type == "response.output_text.done";
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
    return &existing_item->second;
  }

  if (auto const existing_logical_id = item_ids_by_logical_id.find(*call_id);
      existing_logical_id != item_ids_by_logical_id.end() && existing_logical_id->second != *item_id)
  {
    append_stream_error(events, error_seen, "OpenAI function call logical call ID is already bound to another item");
    return nullptr;
  }

  auto [state, inserted] =
      calls.emplace(*item_id, OpenAIStreamParser::FunctionCallState{
                                  .logical_call_id = *call_id, .name = *name, .arguments = "", .emitted_argument_bytes = 0, .started = false, .ended = false});
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
                               .usage = std::nullopt});
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
                               .usage = std::nullopt});
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
  events.push_back(StreamEvent{
      .type = StreamEventType::ToolCallEnd, .text = "", .tool_call_id = state.logical_call_id, .tool_name = "", .error_message = "", .usage = std::nullopt});
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

void append_start_reasoning_if_needed(std::vector<StreamEvent>& events, bool& reasoning_open)
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
                               .reasoning_format = std::string(detail::kOpenAIResponsesReasoningFormat)});
}

void append_reasoning_delta(std::vector<StreamEvent>& events, bool& reasoning_open, bool& reasoning_text_seen, std::string& active_reasoning_text,
                            std::string_view text)
{
  append_start_reasoning_if_needed(events, reasoning_open);
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
                               .reasoning_format = std::string(detail::kOpenAIResponsesReasoningFormat)});
}

void append_finish_reasoning_if_open(std::vector<StreamEvent>& events, bool& reasoning_open, bool& reasoning_text_seen, std::string& active_reasoning_item_id,
                                     std::string& active_reasoning_text, std::vector<std::string>& completed_reasoning_item_ids,
                                     std::vector<std::string>& completed_reasoning_texts, std::string native_item_json = {})
{
  if (!reasoning_open)
    return;
  reasoning_open = false;
  reasoning_text_seen = false;
  remember_string(completed_reasoning_item_ids, std::move(active_reasoning_item_id));
  remember_string(completed_reasoning_texts, std::move(active_reasoning_text));
  active_reasoning_item_id.clear();
  active_reasoning_text.clear();
  events.push_back(StreamEvent{.type = StreamEventType::ReasoningEnd,
                               .text = "",
                               .tool_call_id = "",
                               .tool_name = "",
                               .error_message = "",
                               .usage = std::nullopt,
                               .reasoning_format = std::string(detail::kOpenAIResponsesReasoningFormat),
                               .reasoning_native_item_json = std::move(native_item_json)});
}

std::string openai_error_message_from_event(std::string_view data)
{
  if (auto const response = ava::core::json::object_field(data, "response"))
  {
    if (auto const error = ava::core::json::object_field(*response, "error"))
    {
      if (auto const message = ava::core::json::string_field(*error, "message"); message && !message->empty())
        return *message;
    }
  }
  if (auto const error = ava::core::json::object_field(data, "error"))
  {
    if (auto const message = ava::core::json::string_field(*error, "message"); message && !message->empty())
      return *message;
  }
  if (auto const message = ava::core::json::string_field(data, "message"); message && !message->empty())
    return *message;
  return "OpenAI provider reported an error";
}

void append_event_for_data(std::vector<StreamEvent>& events, std::string_view data, bool& saw_content, bool& reasoning_open, bool& reasoning_text_seen,
                           std::string& active_reasoning_item_id, std::string& active_reasoning_text, std::vector<std::string>& completed_reasoning_item_ids,
                           std::vector<std::string>& completed_reasoning_texts, bool& done_seen, bool& error_seen,
                           std::unordered_map<std::string, OpenAIStreamParser::FunctionCallState>& function_calls,
                           std::unordered_map<std::string, std::string>& function_call_item_ids_by_logical_id,
                           std::unordered_map<std::string, OpenAIStreamParser::FunctionCallState>& legacy_function_calls)
{
  if (data == "[DONE]")
  {
    if (done_seen)
      return;
    done_seen = true;
    if (reject_unended_documented_function_calls(events, error_seen, function_calls))
      return;
    append_finish_reasoning_if_open(events, reasoning_open, reasoning_text_seen, active_reasoning_item_id, active_reasoning_text, completed_reasoning_item_ids,
                                    completed_reasoning_texts);
    events.push_back(StreamEvent{.type = StreamEventType::Done,
                                 .text = "",
                                 .tool_call_id = "",
                                 .tool_name = "",
                                 .error_message = "",
                                 .usage = std::nullopt,
                                 .finish_reason = ProviderFinishReason::Completed});
    return;
  }
  if (!is_json_object_shape(data))
  {
    append_finish_reasoning_if_open(events, reasoning_open, reasoning_text_seen, active_reasoning_item_id, active_reasoning_text, completed_reasoning_item_ids,
                                    completed_reasoning_texts);
    append_stream_error(events, error_seen, "malformed OpenAI SSE event");
    return;
  }
  auto const type = ava::core::json::string_field(data, "type").value_or("");
  if (type == "response.output_item.added")
  {
    auto const item = ava::core::json::object_field(data, "item");
    auto const item_type = item ? ava::core::json::string_field(*item, "type").value_or("") : "";
    if (item_type == "reasoning")
    {
      auto const item_id = reasoning_item_id_from_event(data, *item);
      if (contains_string(completed_reasoning_item_ids, item_id))
        return;
      saw_content = true;
      set_active_reasoning_item_id(active_reasoning_item_id, item_id);
      append_start_reasoning_if_needed(events, reasoning_open);
    }
    else if (item_type == "function_call")
    {
      saw_content = true;
      append_finish_reasoning_if_open(events, reasoning_open, reasoning_text_seen, active_reasoning_item_id, active_reasoning_text,
                                      completed_reasoning_item_ids, completed_reasoning_texts);
      if (auto* state = bind_documented_function_call_item(events, error_seen, *item, function_calls, function_call_item_ids_by_logical_id))
      {
        static_cast<void>(append_function_call_argument_fragment(events, *state, function_call_arguments_from_event(data, *item).value_or("")));
        static_cast<void>(append_function_call_start_if_ready(events, *state));
        append_unemitted_function_call_arguments(events, *state);
      }
    }
    return;
  }
  if (type == "response.output_item.done")
  {
    auto const item = ava::core::json::object_field(data, "item");
    if (item && ava::core::json::string_field(*item, "type").value_or("") == "reasoning")
    {
      auto const item_id = reasoning_item_id_from_event(data, *item);
      auto const summary = detail::reasoning_summary_text_from_object(*item);
      if (contains_string(completed_reasoning_item_ids, item_id))
        return;
      saw_content = true;
      set_active_reasoning_item_id(active_reasoning_item_id, item_id);
      if (!reasoning_text_seen)
        append_reasoning_delta(events, reasoning_open, reasoning_text_seen, active_reasoning_text, summary);
      append_finish_reasoning_if_open(events, reasoning_open, reasoning_text_seen, active_reasoning_item_id, active_reasoning_text,
                                      completed_reasoning_item_ids, completed_reasoning_texts,
                                      is_valid_openai_native_reasoning_item_json(*item) ? *item : std::string{});
    }
    else if (item && ava::core::json::string_field(*item, "type").value_or("") == "function_call")
    {
      saw_content = true;
      append_finish_reasoning_if_open(events, reasoning_open, reasoning_text_seen, active_reasoning_item_id, active_reasoning_text,
                                      completed_reasoning_item_ids, completed_reasoning_texts);
      auto const final_arguments = ava::core::json::string_field(*item, "arguments");
      if (!final_arguments || !is_valid_function_call_arguments_object(*final_arguments))
      {
        append_stream_error(events, error_seen, "OpenAI function call output item completion requires JSON-object string arguments");
        return;
      }
      if (auto* state = bind_documented_function_call_item(events, error_seen, *item, function_calls, function_call_item_ids_by_logical_id))
      {
        if (!reconcile_complete_function_call_arguments(events, *state, final_arguments, error_seen))
          return;
        static_cast<void>(append_function_call_end(events, *state, true, error_seen));
      }
    }
    return;
  }
  if (type == "response.reasoning_summary_part.added")
  {
    auto const item_id = reasoning_item_id_from_event(data);
    if (contains_string(completed_reasoning_item_ids, item_id))
      return;
    saw_content = true;
    set_active_reasoning_item_id(active_reasoning_item_id, item_id);
    append_start_reasoning_if_needed(events, reasoning_open);
    return;
  }
  if (type == "response.reasoning_summary_text.delta" || type == "response.reasoning_text.delta")
  {
    auto const item_id = reasoning_item_id_from_event(data);
    if (contains_string(completed_reasoning_item_ids, item_id))
      return;
    saw_content = true;
    set_active_reasoning_item_id(active_reasoning_item_id, item_id);
    append_reasoning_delta(events, reasoning_open, reasoning_text_seen, active_reasoning_text,
                           detail::first_string_field(data, {"delta", "text"}).value_or(""));
    return;
  }
  if (type == "response.reasoning_summary_text.done" || type == "response.reasoning_summary_part.done" || type == "response.reasoning_text.done")
  {
    auto const item_id = reasoning_item_id_from_event(data);
    auto const summary = detail::reasoning_summary_text_from_object(data);
    if (contains_string(completed_reasoning_item_ids, item_id))
      return;
    saw_content = true;
    set_active_reasoning_item_id(active_reasoning_item_id, item_id);
    if (!reasoning_text_seen)
      append_reasoning_delta(events, reasoning_open, reasoning_text_seen, active_reasoning_text, summary);
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
  if (is_ignored_lifecycle_event(type))
    return;
  if (type == "response.output_text.delta" || type == "response.text.delta")
  {
    saw_content = true;
    append_finish_reasoning_if_open(events, reasoning_open, reasoning_text_seen, active_reasoning_item_id, active_reasoning_text, completed_reasoning_item_ids,
                                    completed_reasoning_texts);
    events.push_back(StreamEvent{
        .type = StreamEventType::TextDelta,
        .text = ava::core::json::string_field(data, "delta").or_else([&data]() { return ava::core::json::string_field(data, "text"); }).value_or(""),
        .tool_call_id = "",
        .tool_name = "",
        .error_message = "",
        .usage = std::nullopt});
    return;
  }
  if (type == "response.function_call_arguments.delta")
  {
    saw_content = true;
    append_finish_reasoning_if_open(events, reasoning_open, reasoning_text_seen, active_reasoning_item_id, active_reasoning_text, completed_reasoning_item_ids,
                                    completed_reasoning_texts);
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
    append_finish_reasoning_if_open(events, reasoning_open, reasoning_text_seen, active_reasoning_item_id, active_reasoning_text, completed_reasoning_item_ids,
                                    completed_reasoning_texts);
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
    append_finish_reasoning_if_open(events, reasoning_open, reasoning_text_seen, active_reasoning_item_id, active_reasoning_text, completed_reasoning_item_ids,
                                    completed_reasoning_texts);
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
    if (reject_unended_documented_function_calls(events, error_seen, function_calls))
      return;
    append_finish_reasoning_if_open(events, reasoning_open, reasoning_text_seen, active_reasoning_item_id, active_reasoning_text, completed_reasoning_item_ids,
                                    completed_reasoning_texts);
    events.push_back(
        StreamEvent{.type = StreamEventType::Done,
                    .text = "",
                    .tool_call_id = "",
                    .tool_name = "",
                    .error_message = "",
                    .usage = parse_openai_usage(data),
                    .finish_reason = type == "response.completed" ? ProviderFinishReason::Completed : detail::openai_response_finish_reason(data)});
    return;
  }
  if (type == "error" || type == "response.error" || type == "response.failed")
  {
    append_finish_reasoning_if_open(events, reasoning_open, reasoning_text_seen, active_reasoning_item_id, active_reasoning_text, completed_reasoning_item_ids,
                                    completed_reasoning_texts);
    append_stream_error(events, error_seen, openai_error_message_from_event(data));
    return;
  }
  // OpenAI may add non-content lifecycle events without changing the assistant turn.
}

void append_events_for_sse_line(std::vector<StreamEvent>& events, std::string& data, bool& saw_content, bool& reasoning_open, bool& reasoning_text_seen,
                                std::string& active_reasoning_item_id, std::string& active_reasoning_text,
                                std::vector<std::string>& completed_reasoning_item_ids, std::vector<std::string>& completed_reasoning_texts, bool& done_seen,
                                bool& error_seen, std::unordered_map<std::string, OpenAIStreamParser::FunctionCallState>& function_calls,
                                std::unordered_map<std::string, std::string>& function_call_item_ids_by_logical_id,
                                std::unordered_map<std::string, OpenAIStreamParser::FunctionCallState>& legacy_function_calls, std::string line)
{
  if (!line.empty() && line.back() == '\r')
    line.pop_back();
  if (line.empty())
  {
    if (!data.empty())
    {
      append_event_for_data(events, data, saw_content, reasoning_open, reasoning_text_seen, active_reasoning_item_id, active_reasoning_text,
                            completed_reasoning_item_ids, completed_reasoning_texts, done_seen, error_seen, function_calls,
                            function_call_item_ids_by_logical_id, legacy_function_calls);
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
  pending_line_.append(chunk);
  std::size_t line_start = 0;
  std::size_t search_from = scan_offset_;
  while (true)
  {
    auto const newline = pending_line_.find('\n', search_from);
    if (newline == std::string::npos)
      break;
    append_events_for_sse_line(events, data_, saw_content_, reasoning_open_, reasoning_text_seen_, active_reasoning_item_id_, active_reasoning_text_,
                               completed_reasoning_item_ids_, completed_reasoning_texts_, done_seen_, error_seen_, function_calls_,
                               function_call_item_ids_by_logical_id_, legacy_function_calls_, pending_line_.substr(line_start, newline - line_start));
    line_start = newline + 1;
    search_from = line_start;
  }
  if (line_start > 0)
    pending_line_.erase(0, line_start);
  scan_offset_ = pending_line_.size();
  return events;
}

ava::core::Result<std::vector<StreamEvent>> OpenAIStreamParser::finish()
{
  std::vector<StreamEvent> events;
  if (!pending_line_.empty())
  {
    append_events_for_sse_line(events, data_, saw_content_, reasoning_open_, reasoning_text_seen_, active_reasoning_item_id_, active_reasoning_text_,
                               completed_reasoning_item_ids_, completed_reasoning_texts_, done_seen_, error_seen_, function_calls_,
                               function_call_item_ids_by_logical_id_, legacy_function_calls_, std::move(pending_line_));
    pending_line_.clear();
  }
  scan_offset_ = 0;
  if (!data_.empty())
  {
    append_event_for_data(events, data_, saw_content_, reasoning_open_, reasoning_text_seen_, active_reasoning_item_id_, active_reasoning_text_,
                          completed_reasoning_item_ids_, completed_reasoning_texts_, done_seen_, error_seen_, function_calls_,
                          function_call_item_ids_by_logical_id_, legacy_function_calls_);
    data_.clear();
  }
  if (!done_seen_ && !error_seen_)
    static_cast<void>(reject_unended_documented_function_calls(events, error_seen_, function_calls_));
  append_finish_reasoning_if_open(events, reasoning_open_, reasoning_text_seen_, active_reasoning_item_id_, active_reasoning_text_,
                                  completed_reasoning_item_ids_, completed_reasoning_texts_);
  if (saw_content_ && !done_seen_ && !error_seen_)
  {
    append_stream_error(events, error_seen_, "OpenAI SSE stream ended before done marker");
  }
  saw_content_ = false;
  reasoning_open_ = false;
  reasoning_text_seen_ = false;
  active_reasoning_item_id_.clear();
  active_reasoning_text_.clear();
  completed_reasoning_item_ids_.clear();
  completed_reasoning_texts_.clear();
  function_calls_.clear();
  function_call_item_ids_by_logical_id_.clear();
  legacy_function_calls_.clear();
  done_seen_ = false;
  error_seen_ = false;
  return events;
}

}  // namespace ava::provider
