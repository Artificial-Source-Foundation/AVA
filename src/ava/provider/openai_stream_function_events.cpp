#include "sys.h"
#include "ava/provider/openai_reasoning.h"
#include "ava/provider/openai_response_parser_detail.h"
#include "ava/provider/openai_stream_parser.h"
#include "ava/provider/openai_stream_parser_internal.h"
#include "ava/core/json.h"

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace ava::provider {
namespace {

bool is_valid_function_call_arguments_object(std::string_view arguments)
{
  return ava::core::json::is_valid_object(arguments);
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
  openai_stream_parser_internal::append_stream_error(events, error_seen, "conflicting OpenAI function call arguments");
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
        openai_stream_parser_internal::append_stream_error(events, error_seen, "OpenAI function call item has an empty logical call ID or name");
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

}  // namespace

using namespace openai_stream_parser_internal;

OpenAIStreamParser::FunctionCallState* OpenAIStreamParser::bind_documented_function_call_item(std::vector<StreamEvent>& events, std::string_view item,
                                                                                              std::optional<std::size_t> provider_output_index)
{
  auto const item_id = ava::core::json::string_field(item, "id");
  auto const call_id = ava::core::json::string_field(item, "call_id");
  auto const name = ava::core::json::string_field(item, "name");
  auto const arguments = ava::core::json::string_field(item, "arguments");
  if (!item_id || !call_id || !name || !arguments || !is_valid_openai_opaque_id(*item_id) || !is_valid_openai_opaque_id(*call_id) ||
      !is_valid_openai_opaque_id(*name))
  {
    append_stream_error(events, error_seen_, "OpenAI function call item requires bounded item ID, logical call ID, name, and string arguments");
    return nullptr;
  }

  auto const existing_item = function_calls_.find(*item_id);
  if (existing_item != function_calls_.end())
  {
    if (existing_item->second.logical_call_id != *call_id || existing_item->second.name != *name)
    {
      append_stream_error(events, error_seen_, "OpenAI function call item changed its logical call ID or name");
      return nullptr;
    }
    if (provider_output_index && existing_item->second.provider_output_index != provider_output_index)
    {
      append_stream_error(events, error_seen_, "OpenAI function call item changed its output_index");
      return nullptr;
    }
    return &existing_item->second;
  }

  if (auto const existing_logical_id = function_call_item_ids_by_logical_id_.find(*call_id);
      existing_logical_id != function_call_item_ids_by_logical_id_.end() && existing_logical_id->second != *item_id)
  {
    append_stream_error(events, error_seen_, "OpenAI function call logical call ID is already bound to another item");
    return nullptr;
  }

  auto [state, inserted] = function_calls_.emplace(*item_id, OpenAIStreamParser::FunctionCallState{.provider_item_id = *item_id,
                                                                                                   .provider_output_index = provider_output_index,
                                                                                                   .logical_call_id = *call_id,
                                                                                                   .name = *name,
                                                                                                   .arguments = "",
                                                                                                   .emitted_argument_bytes = 0,
                                                                                                   .started = false,
                                                                                                   .ended = false});
  static_cast<void>(inserted);
  function_call_item_ids_by_logical_id_.emplace(*call_id, *item_id);
  return &state->second;
}

OpenAIStreamParser::FunctionCallState* OpenAIStreamParser::documented_function_call_state_for_event(std::vector<StreamEvent>& events, std::string_view data)
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
    append_stream_error(events, error_seen_, "OpenAI function call arguments contain an invalid documented item ID");
    return nullptr;
  }
  if (item_id && output_item_id && *item_id != *output_item_id)
  {
    append_stream_error(events, error_seen_, "OpenAI function call arguments have conflicting documented item IDs");
    return nullptr;
  }

  OpenAIStreamParser::FunctionCallState* state = nullptr;
  auto const documented_item_id = item_id ? item_id : output_item_id;
  if (documented_item_id)
  {
    auto const found = function_calls_.find(*documented_item_id);
    if (found == function_calls_.end())
    {
      append_stream_error(events, error_seen_, "OpenAI function call arguments reference an unbound item ID");
      return nullptr;
    }
    state = &found->second;
  }
  if (call_id_start)
  {
    if (!call_id || !is_valid_openai_opaque_id(*call_id))
    {
      append_stream_error(events, error_seen_, "OpenAI function call arguments contain an invalid logical call ID");
      return nullptr;
    }
    auto const mapped_item = function_call_item_ids_by_logical_id_.find(*call_id);
    if (mapped_item == function_call_item_ids_by_logical_id_.end())
    {
      append_stream_error(events, error_seen_, "OpenAI function call arguments reference an unbound logical call ID");
      return nullptr;
    }
    auto const found = function_calls_.find(mapped_item->second);
    if (found == function_calls_.end() || (state && state != &found->second))
    {
      append_stream_error(events, error_seen_, "OpenAI function call arguments have conflicting item and logical call IDs");
      return nullptr;
    }
    state = &found->second;
  }
  if (!state)
  {
    append_stream_error(events, error_seen_, "OpenAI function call arguments are missing an item ID or logical call ID");
    return nullptr;
  }
  if (state->ended)
  {
    append_stream_error(events, error_seen_, "OpenAI function call arguments emitted after completion");
    return nullptr;
  }
  auto const output_index = documented_output_index(data, "{}");
  if (!output_index)
  {
    append_stream_error(events, error_seen_, output_index.error().message());
    return nullptr;
  }
  if (*output_index && state->provider_output_index != *output_index)
  {
    append_stream_error(events, error_seen_, "OpenAI function call arguments changed their output_index");
    return nullptr;
  }
  if ((call_id && state->logical_call_id != *call_id) || (name_start && (!name || state->name != *name)))
  {
    append_stream_error(events, error_seen_, "OpenAI function call arguments changed their item binding");
    return nullptr;
  }
  return state;
}

void OpenAIStreamParser::handle_documented_function_call_output_item(std::vector<StreamEvent>& events, std::string_view data, std::string_view item,
                                                                     std::optional<std::size_t> output_index, OutputItemLifecycle lifecycle)
{
  append_finish_reasoning_if_open(events);
  if (lifecycle == OutputItemLifecycle::Added)
  {
    if (auto* state = bind_documented_function_call_item(events, item, output_index))
    {
      static_cast<void>(append_function_call_argument_fragment(events, *state, function_call_arguments_from_event(data, item).value_or("")));
      static_cast<void>(append_function_call_start_if_ready(events, *state));
      append_unemitted_function_call_arguments(events, *state);
    }
    return;
  }

  auto const final_arguments = ava::core::json::string_field(item, "arguments");
  if (!final_arguments || !is_valid_function_call_arguments_object(*final_arguments))
  {
    append_stream_error(events, error_seen_, "OpenAI function call output item completion requires JSON-object string arguments");
    return;
  }
  if (auto* state = bind_documented_function_call_item(events, item, output_index))
  {
    if (state->ended)
    {
      append_stream_error(events, error_seen_, "OpenAI function call output item completed more than once");
      return;
    }
    if (!reconcile_complete_function_call_arguments(events, *state, final_arguments, error_seen_))
      return;
    static_cast<void>(append_function_call_end(events, *state, true, error_seen_));
  }
}

OpenAIStreamParser::EventHandling OpenAIStreamParser::handle_function_call_event(std::vector<StreamEvent>& events, std::string_view data, std::string_view type,
                                                                                 EventRoutingPhase routing_phase)
{
  if (routing_phase == EventRoutingPhase::BeforeIgnoredLifecycle && type == "response.function_call_arguments.done")
  {
    bool const documented_mapping = has_documented_function_call_mapping(data, function_call_item_ids_by_logical_id_);
    auto* state = documented_mapping ? documented_function_call_state_for_event(events, data)
                                     : legacy_function_call_state_for_existing_event(data, legacy_function_calls_);
    if (!state && !documented_mapping)
    {
      if (!has_valid_legacy_function_call_identity(data))
      {
        append_stream_error(events, error_seen_, "OpenAI legacy function call arguments require a nonempty logical call ID");
        return EventHandling::Handled;
      }
      state = &legacy_function_call_state_for(data, legacy_function_calls_);
    }
    auto const complete_arguments = ava::core::json::string_field(data, "arguments");
    if (documented_mapping && (!complete_arguments || !is_valid_function_call_arguments_object(*complete_arguments)))
    {
      append_stream_error(events, error_seen_, "OpenAI documented function call arguments completion requires JSON-object string arguments");
      return EventHandling::Handled;
    }
    if (state)
    {
      if (!reconcile_complete_function_call_arguments(events, *state, complete_arguments, error_seen_))
        return EventHandling::Handled;
      static_cast<void>(append_function_call_start_if_ready(events, *state));
      append_unemitted_function_call_arguments(events, *state);
    }
    return EventHandling::Handled;
  }
  if (routing_phase == EventRoutingPhase::BeforeIgnoredLifecycle)
    return EventHandling::Unhandled;
  if (type == "response.function_call_arguments.delta")
  {
    saw_content_ = true;
    append_finish_reasoning_if_open(events);
    bool const documented_mapping = has_documented_function_call_mapping(data, function_call_item_ids_by_logical_id_);
    auto* state = documented_mapping ? documented_function_call_state_for_event(events, data)
                                     : legacy_function_call_state_for_existing_event(data, legacy_function_calls_);
    if (!state && !documented_mapping)
    {
      if (!has_valid_legacy_function_call_identity(data))
      {
        append_stream_error(events, error_seen_, "OpenAI legacy function call arguments require a nonempty logical call ID");
        return EventHandling::Handled;
      }
      state = &legacy_function_call_state_for(data, legacy_function_calls_);
    }
    auto const delta = ava::core::json::string_field(data, "delta");
    if (documented_mapping && !delta)
    {
      append_stream_error(events, error_seen_, "OpenAI documented function call arguments delta requires a string delta");
      return EventHandling::Handled;
    }
    if (state)
      static_cast<void>(append_function_call_argument_fragment(events, *state, delta.value_or("")));
    return EventHandling::Handled;
  }
  if (type == "response.function_call.added")
  {
    saw_content_ = true;
    append_finish_reasoning_if_open(events);
    auto& state = legacy_function_call_state_for(data, legacy_function_calls_);
    if (state.logical_call_id.empty())
    {
      // Preserve the provider-neutral empty-ID signal so the agent's existing
      // call-ID validator rejects it before any session mutation.
      events.push_back(StreamEvent{
          .type = StreamEventType::ToolCallStart, .text = "", .tool_call_id = "", .tool_name = state.name, .error_message = "", .usage = std::nullopt});
      return EventHandling::Handled;
    }
    if (state.name.empty())
    {
      append_stream_error(events, error_seen_, "OpenAI function call item has an empty logical call ID or name");
      return EventHandling::Handled;
    }
    static_cast<void>(append_function_call_argument_fragment(events, state, function_call_arguments_from_event(data).value_or("")));
    static_cast<void>(append_function_call_start_if_ready(events, state));
    append_unemitted_function_call_arguments(events, state);
    return EventHandling::Handled;
  }
  if (type == "response.function_call.done" || type == "response.function_call.completed")
  {
    saw_content_ = true;
    append_finish_reasoning_if_open(events);
    auto& state = legacy_function_call_state_for(data, legacy_function_calls_);
    if (!reconcile_complete_function_call_arguments(events, state, function_call_arguments_from_event(data), error_seen_))
      return EventHandling::Handled;
    static_cast<void>(append_function_call_end(events, state, false, error_seen_));
    return EventHandling::Handled;
  }
  return EventHandling::Unhandled;
}

bool OpenAIStreamParser::reject_unended_documented_function_calls(std::vector<StreamEvent>& events)
{
  for (auto const& [item_id, state] : function_calls_)
  {
    static_cast<void>(item_id);
    if (!state.ended)
    {
      append_stream_error(events, error_seen_, "OpenAI response ended before documented function call item completion");
      return true;
    }
  }
  return false;
}

}  // namespace ava::provider
