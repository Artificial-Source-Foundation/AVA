#include "sys.h"
#include "ava/provider/openai_reasoning.h"
#include "ava/provider/openai_response_parser.h"
#include "ava/provider/openai_response_parser_detail.h"
#include "ava/provider/openai_stream_parser.h"
#include "ava/provider/openai_stream_parser_internal.h"
#include "ava/provider/provider_utils.h"
#include "ava/core/json.h"

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ava::provider {
namespace {

struct DocumentedOutputItemMetadata
{
  std::string id;
  std::optional<std::size_t> output_index = std::nullopt;
  AssistantPhase phase = AssistantPhase::Unknown;
};

struct CompletedMessageContent
{
  std::string output_text;
  std::string refusal_text;
};

}  // namespace

using namespace openai_stream_parser_internal;

void OpenAIStreamParser::append_event_for_data(std::vector<StreamEvent>& events, std::string_view data)
{
  if (data == "[DONE]")
  {
    if (done_seen_)
      return;
    done_seen_ = true;
    if (reject_unended_documented_function_calls(events, error_seen_, function_calls_) ||
        reject_unended_documented_message_items(events, error_seen_, message_items_))
      return;
    append_finish_reasoning_if_open(events, reasoning_open_, reasoning_text_seen_, active_reasoning_item_id_, active_reasoning_output_index_,
                                    active_reasoning_text_, completed_reasoning_item_ids_, completed_reasoning_texts_);
    events.push_back(StreamEvent{.type = StreamEventType::Done,
                                 .text = "",
                                 .tool_call_id = "",
                                 .tool_name = "",
                                 .error_message = "",
                                 .usage = std::nullopt,
                                 .finish_reason = saw_refusal_ ? ProviderFinishReason::Refusal : ProviderFinishReason::Completed});
    return;
  }
  if (done_seen_)
  {
    append_stream_error(events, error_seen_, "OpenAI response emitted an event after its terminal marker");
    return;
  }
  if (!is_json_object_shape(data))
  {
    append_finish_reasoning_if_open(events, reasoning_open_, reasoning_text_seen_, active_reasoning_item_id_, active_reasoning_output_index_,
                                    active_reasoning_text_, completed_reasoning_item_ids_, completed_reasoning_texts_);
    append_stream_error(events, error_seen_, "malformed OpenAI SSE event");
    return;
  }
  auto const type = ava::core::json::string_field(data, "type").value_or("");
  if (type == "response.output_item.added")
  {
    auto const item = ava::core::json::object_field(data, "item");
    auto const item_type = item ? ava::core::json::string_field(*item, "type").value_or("") : "";
    if (item_type == "message" || item_type == "reasoning" || item_type == "function_call")
    {
      DocumentedOutputItemMetadata metadata;
      auto parsed_metadata = documented_output_item_metadata(data, *item, item_type == "message", metadata.id, metadata.output_index, metadata.phase);
      if (!parsed_metadata)
      {
        append_stream_error(events, error_seen_, parsed_metadata.error().message());
        return;
      }
      if (!register_documented_output_item(events, error_seen_, metadata.id, item_type, metadata.output_index, true, documented_output_item_types_,
                                           documented_output_item_added_ids_, documented_output_item_ids_by_index_))
      {
        return;
      }
      saw_content_ = true;
      if (item_type == "message")
      {
        CompletedMessageContent content;
        auto const parsed_content = completed_message_content_from_item(*item, content.output_text, content.refusal_text);
        if (!parsed_content)
        {
          append_stream_error(events, error_seen_, parsed_content.error().message());
          return;
        }
        auto const [state, inserted] = message_items_.emplace(metadata.id, OpenAIStreamParser::MessageItemState{.provider_output_index = metadata.output_index,
                                                                                                                .phase = metadata.phase,
                                                                                                                .text = "",
                                                                                                                .output_text = "",
                                                                                                                .refusal_text = "",
                                                                                                                .output_text_completed = false,
                                                                                                                .refusal_completed = false,
                                                                                                                .ended = false});
        static_cast<void>(state);
        if (!inserted)
        {
          append_stream_error(events, error_seen_, "OpenAI message output item was added more than once");
          return;
        }
        events.push_back(StreamEvent{.type = StreamEventType::TextStart,
                                     .text = "",
                                     .tool_call_id = "",
                                     .tool_name = "",
                                     .error_message = "",
                                     .usage = std::nullopt,
                                     .provider_item_id = metadata.id,
                                     .provider_output_index = metadata.output_index,
                                     .assistant_phase = metadata.phase});
        if (*parsed_content)
        {
          append_documented_message_delta(events, state->second, metadata.id, content.output_text, MessageContentKind::OutputText);
          append_documented_message_delta(events, state->second, metadata.id, content.refusal_text, MessageContentKind::Refusal);
          if (!content.refusal_text.empty())
            saw_refusal_ = true;
        }
      }
      else if (item_type == "reasoning")
      {
        if (reasoning_open_ && active_reasoning_item_id_ != metadata.id)
        {
          append_stream_error(events, error_seen_, "OpenAI reasoning output items overlapped");
          return;
        }
        active_reasoning_item_id_ = metadata.id;
        active_reasoning_output_index_ = metadata.output_index;
        append_start_reasoning_if_needed(events, reasoning_open_, active_reasoning_item_id_, active_reasoning_output_index_);
      }
      else
      {
        append_finish_reasoning_if_open(events, reasoning_open_, reasoning_text_seen_, active_reasoning_item_id_, active_reasoning_output_index_,
                                        active_reasoning_text_, completed_reasoning_item_ids_, completed_reasoning_texts_);
        if (auto* state =
                bind_documented_function_call_item(events, error_seen_, *item, metadata.output_index, function_calls_, function_call_item_ids_by_logical_id_))
        {
          static_cast<void>(append_function_call_argument_fragment(events, *state, function_call_arguments_from_event(data, *item).value_or("")));
          static_cast<void>(append_function_call_start_if_ready(events, *state));
          append_unemitted_function_call_arguments(events, *state);
        }
      }
    }
    else
    {
      append_stream_error(events, error_seen_, "OpenAI response contains an unsupported output item type");
    }
    return;
  }
  if (type == "response.output_item.done")
  {
    auto const item = ava::core::json::object_field(data, "item");
    auto const item_type = item ? ava::core::json::string_field(*item, "type").value_or("") : "";
    if (item_type == "message" || item_type == "reasoning" || item_type == "function_call")
    {
      DocumentedOutputItemMetadata metadata;
      auto parsed_metadata = documented_output_item_metadata(data, *item, item_type == "message", metadata.id, metadata.output_index, metadata.phase);
      if (!parsed_metadata)
      {
        append_stream_error(events, error_seen_, parsed_metadata.error().message());
        return;
      }
      if (!register_documented_output_item(events, error_seen_, metadata.id, item_type, metadata.output_index, false, documented_output_item_types_,
                                           documented_output_item_added_ids_, documented_output_item_ids_by_index_))
      {
        return;
      }
      saw_content_ = true;
      if (item_type == "message")
      {
        auto const state = message_items_.find(metadata.id);
        if (state == message_items_.end())
        {
          append_stream_error(events, error_seen_, "OpenAI message output item completed without one matching add lifecycle event");
          return;
        }
        if (state->second.ended)
        {
          append_stream_error(events, error_seen_, "OpenAI message output item completed more than once");
          return;
        }
        if ((metadata.output_index && state->second.provider_output_index != metadata.output_index) ||
            (metadata.phase != AssistantPhase::Unknown && state->second.phase != AssistantPhase::Unknown && metadata.phase != state->second.phase))
        {
          append_stream_error(events, error_seen_, "OpenAI message output item changed its output_index or phase");
          return;
        }
        if (state->second.phase == AssistantPhase::Unknown && metadata.phase != AssistantPhase::Unknown)
          state->second.phase = metadata.phase;
        CompletedMessageContent complete_content;
        auto const parsed_complete_content = completed_message_content_from_item(*item, complete_content.output_text, complete_content.refusal_text);
        if (!parsed_complete_content)
        {
          append_stream_error(events, error_seen_, parsed_complete_content.error().message());
          return;
        }
        if (*parsed_complete_content)
        {
          if (!reconcile_complete_message_text(events, state->second, metadata.id, complete_content.output_text, MessageContentKind::OutputText, error_seen_) ||
              !reconcile_complete_message_text(events, state->second, metadata.id, complete_content.refusal_text, MessageContentKind::Refusal, error_seen_))
          {
            return;
          }
          if (!complete_content.refusal_text.empty())
            saw_refusal_ = true;
        }
        state->second.ended = true;
        events.push_back(StreamEvent{.type = StreamEventType::TextEnd,
                                     .text = "",
                                     .tool_call_id = "",
                                     .tool_name = "",
                                     .error_message = "",
                                     .usage = std::nullopt,
                                     .provider_item_id = metadata.id,
                                     .provider_output_index = state->second.provider_output_index,
                                     .assistant_phase = state->second.phase});
      }
      else if (item_type == "reasoning")
      {
        if (contains_string(completed_reasoning_item_ids_, metadata.id))
        {
          append_stream_error(events, error_seen_, "OpenAI reasoning output item completed more than once");
          return;
        }
        if (reasoning_open_ && active_reasoning_item_id_ != metadata.id)
        {
          append_stream_error(events, error_seen_, "OpenAI reasoning output item completion did not match the active item");
          return;
        }
        active_reasoning_item_id_ = metadata.id;
        active_reasoning_output_index_ = metadata.output_index;
        std::size_t remaining_summary_parts = kMaxProviderParserArrayItems;
        auto summary = detail::reasoning_summary_text_from_object(*item, remaining_summary_parts);
        if (!summary)
        {
          append_stream_error(events, error_seen_, "OpenAI response parser limit exceeded");
          return;
        }
        if (reasoning_event_has_authoritative_complete_text(*item) &&
            !reconcile_complete_reasoning_text(events, reasoning_open_, reasoning_text_seen_, active_reasoning_item_id_, active_reasoning_output_index_,
                                               active_reasoning_text_, *summary, error_seen_))
        {
          return;
        }
        append_finish_reasoning_if_open(events, reasoning_open_, reasoning_text_seen_, active_reasoning_item_id_, active_reasoning_output_index_,
                                        active_reasoning_text_, completed_reasoning_item_ids_, completed_reasoning_texts_,
                                        is_valid_openai_native_reasoning_item_json(*item) ? *item : std::string{});
      }
      else
      {
        append_finish_reasoning_if_open(events, reasoning_open_, reasoning_text_seen_, active_reasoning_item_id_, active_reasoning_output_index_,
                                        active_reasoning_text_, completed_reasoning_item_ids_, completed_reasoning_texts_);
        auto const final_arguments = ava::core::json::string_field(*item, "arguments");
        if (!final_arguments || !is_valid_function_call_arguments_object(*final_arguments))
        {
          append_stream_error(events, error_seen_, "OpenAI function call output item completion requires JSON-object string arguments");
          return;
        }
        if (auto* state =
                bind_documented_function_call_item(events, error_seen_, *item, metadata.output_index, function_calls_, function_call_item_ids_by_logical_id_))
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
    }
    else
    {
      append_stream_error(events, error_seen_, "OpenAI response contains an unsupported output item type");
    }
    return;
  }
  if (type == "response.reasoning_summary_part.added")
  {
    auto const item_id = reasoning_item_id_from_event(data);
    if (!bind_documented_reasoning_event(events, error_seen_, data, active_reasoning_item_id_, active_reasoning_output_index_, documented_output_item_types_,
                                         documented_output_item_ids_by_index_))
      return;
    if (contains_string(completed_reasoning_item_ids_, item_id))
      return;
    saw_content_ = true;
    set_active_reasoning_item_id(active_reasoning_item_id_, item_id);
    append_start_reasoning_if_needed(events, reasoning_open_, active_reasoning_item_id_, active_reasoning_output_index_);
    return;
  }
  if (type == "response.reasoning_summary_text.delta" || type == "response.reasoning_text.delta")
  {
    auto const item_id = reasoning_item_id_from_event(data);
    if (!bind_documented_reasoning_event(events, error_seen_, data, active_reasoning_item_id_, active_reasoning_output_index_, documented_output_item_types_,
                                         documented_output_item_ids_by_index_))
      return;
    if (contains_string(completed_reasoning_item_ids_, item_id))
      return;
    saw_content_ = true;
    set_active_reasoning_item_id(active_reasoning_item_id_, item_id);
    append_reasoning_delta(events, reasoning_open_, reasoning_text_seen_, active_reasoning_item_id_, active_reasoning_output_index_, active_reasoning_text_,
                           detail::first_string_field(data, {"delta", "text"}).value_or(""));
    return;
  }
  if (type == "response.reasoning_summary_text.done" || type == "response.reasoning_summary_part.done" || type == "response.reasoning_text.done")
  {
    auto const item_id = reasoning_item_id_from_event(data);
    if (!bind_documented_reasoning_event(events, error_seen_, data, active_reasoning_item_id_, active_reasoning_output_index_, documented_output_item_types_,
                                         documented_output_item_ids_by_index_))
      return;
    std::size_t remaining_summary_parts = kMaxProviderParserArrayItems;
    auto summary = detail::reasoning_summary_text_from_object(data, remaining_summary_parts);
    if (!summary)
    {
      append_stream_error(events, error_seen_, "OpenAI response parser limit exceeded");
      return;
    }
    if (contains_string(completed_reasoning_item_ids_, item_id))
      return;
    saw_content_ = true;
    set_active_reasoning_item_id(active_reasoning_item_id_, item_id);
    if (reasoning_event_has_authoritative_complete_text(data) &&
        !reconcile_complete_reasoning_text(events, reasoning_open_, reasoning_text_seen_, active_reasoning_item_id_, active_reasoning_output_index_,
                                           active_reasoning_text_, *summary, error_seen_))
    {
      return;
    }
    // Do not close here: output_item.done carries the opaque reasoning item
    // that must be attached to the same private reasoning block for replay.
    return;
  }
  if (type == "response.function_call_arguments.done")
  {
    bool const documented_mapping = has_documented_function_call_mapping(data, function_call_item_ids_by_logical_id_);
    auto* state = documented_mapping
                      ? documented_function_call_state_for_event(events, error_seen_, data, function_calls_, function_call_item_ids_by_logical_id_)
                      : legacy_function_call_state_for_existing_event(data, legacy_function_calls_);
    if (!state && !documented_mapping)
    {
      if (!has_valid_legacy_function_call_identity(data))
      {
        append_stream_error(events, error_seen_, "OpenAI legacy function call arguments require a nonempty logical call ID");
        return;
      }
      state = &legacy_function_call_state_for(data, legacy_function_calls_);
    }
    auto const complete_arguments = ava::core::json::string_field(data, "arguments");
    if (documented_mapping && (!complete_arguments || !is_valid_function_call_arguments_object(*complete_arguments)))
    {
      append_stream_error(events, error_seen_, "OpenAI documented function call arguments completion requires JSON-object string arguments");
      return;
    }
    if (state)
    {
      if (!reconcile_complete_function_call_arguments(events, *state, complete_arguments, error_seen_))
        return;
      static_cast<void>(append_function_call_start_if_ready(events, *state));
      append_unemitted_function_call_arguments(events, *state);
    }
    return;
  }
  if (type == "response.output_text.done")
  {
    bool documented = false;
    auto* state = documented_message_item_for_event(events, error_seen_, data, message_items_, documented);
    if (!documented)
      return;  // Retain legacy output_text.done compatibility.
    if (!state)
      return;
    if (state->output_text_completed)
    {
      append_stream_error(events, error_seen_, "OpenAI message text completed more than once");
      return;
    }
    auto const text_start = ava::core::json::field_value_start(data, "text");
    auto const complete_text = ava::core::json::string_field(data, "text");
    if (text_start && !complete_text)
    {
      append_stream_error(events, error_seen_, "OpenAI message text completion requires a string text");
      return;
    }
    auto const item_id = ava::core::json::string_field(data, "item_id").or_else([&data] { return ava::core::json::string_field(data, "output_item_id"); });
    if (!reconcile_complete_message_text(events, *state, *item_id, complete_text.value_or(""), MessageContentKind::OutputText, error_seen_))
      return;
    state->output_text_completed = true;
    saw_content_ = true;
    return;
  }
  if (type == "response.refusal.done")
  {
    append_finish_reasoning_if_open(events, reasoning_open_, reasoning_text_seen_, active_reasoning_item_id_, active_reasoning_output_index_,
                                    active_reasoning_text_, completed_reasoning_item_ids_, completed_reasoning_texts_);
    bool documented = false;
    auto* state = documented_message_item_for_event(events, error_seen_, data, message_items_, documented);
    if (!documented)
    {
      append_stream_error(events, error_seen_, "OpenAI refusal completion requires a documented message item ID");
      return;
    }
    if (!state)
      return;
    if (state->refusal_completed)
    {
      append_stream_error(events, error_seen_, "OpenAI message refusal completed more than once");
      return;
    }
    auto const refusal = ava::core::json::string_field(data, "refusal");
    if (!refusal)
    {
      append_stream_error(events, error_seen_, "OpenAI refusal completion requires string refusal");
      return;
    }
    auto const item_id = ava::core::json::string_field(data, "item_id").or_else([&data] { return ava::core::json::string_field(data, "output_item_id"); });
    if (!reconcile_complete_message_text(events, *state, *item_id, *refusal, MessageContentKind::Refusal, error_seen_))
      return;
    state->refusal_completed = true;
    saw_content_ = true;
    saw_refusal_ = true;
    return;
  }
  if (is_ignored_lifecycle_event(type))
    return;
  if (type == "response.refusal.delta")
  {
    append_finish_reasoning_if_open(events, reasoning_open_, reasoning_text_seen_, active_reasoning_item_id_, active_reasoning_output_index_,
                                    active_reasoning_text_, completed_reasoning_item_ids_, completed_reasoning_texts_);
    bool documented = false;
    auto* state = documented_message_item_for_event(events, error_seen_, data, message_items_, documented);
    if (!documented)
    {
      append_stream_error(events, error_seen_, "OpenAI refusal delta requires a documented message item ID");
      return;
    }
    if (!state)
      return;
    if (state->refusal_completed)
    {
      append_stream_error(events, error_seen_, "OpenAI message refusal emitted after completion");
      return;
    }
    auto const delta = ava::core::json::string_field(data, "delta");
    if (!delta)
    {
      append_stream_error(events, error_seen_, "OpenAI refusal delta requires string delta");
      return;
    }
    auto const item_id = ava::core::json::string_field(data, "item_id").or_else([&data] { return ava::core::json::string_field(data, "output_item_id"); });
    append_documented_message_delta(events, *state, *item_id, *delta, MessageContentKind::Refusal);
    saw_content_ = true;
    saw_refusal_ = true;
    return;
  }
  if (type == "response.output_text.delta" || type == "response.text.delta")
  {
    saw_content_ = true;
    append_finish_reasoning_if_open(events, reasoning_open_, reasoning_text_seen_, active_reasoning_item_id_, active_reasoning_output_index_,
                                    active_reasoning_text_, completed_reasoning_item_ids_, completed_reasoning_texts_);
    bool documented = false;
    auto* state = documented_message_item_for_event(events, error_seen_, data, message_items_, documented);
    if (documented)
    {
      if (!state)
        return;
      if (state->output_text_completed)
      {
        append_stream_error(events, error_seen_, "OpenAI message text emitted after completion");
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
    saw_content_ = true;
    append_finish_reasoning_if_open(events, reasoning_open_, reasoning_text_seen_, active_reasoning_item_id_, active_reasoning_output_index_,
                                    active_reasoning_text_, completed_reasoning_item_ids_, completed_reasoning_texts_);
    bool const documented_mapping = has_documented_function_call_mapping(data, function_call_item_ids_by_logical_id_);
    auto* state = documented_mapping
                      ? documented_function_call_state_for_event(events, error_seen_, data, function_calls_, function_call_item_ids_by_logical_id_)
                      : legacy_function_call_state_for_existing_event(data, legacy_function_calls_);
    if (!state && !documented_mapping)
    {
      if (!has_valid_legacy_function_call_identity(data))
      {
        append_stream_error(events, error_seen_, "OpenAI legacy function call arguments require a nonempty logical call ID");
        return;
      }
      state = &legacy_function_call_state_for(data, legacy_function_calls_);
    }
    auto const delta = ava::core::json::string_field(data, "delta");
    if (documented_mapping && !delta)
    {
      append_stream_error(events, error_seen_, "OpenAI documented function call arguments delta requires a string delta");
      return;
    }
    if (state)
      static_cast<void>(append_function_call_argument_fragment(events, *state, delta.value_or("")));
    return;
  }
  if (type == "response.function_call.added")
  {
    saw_content_ = true;
    append_finish_reasoning_if_open(events, reasoning_open_, reasoning_text_seen_, active_reasoning_item_id_, active_reasoning_output_index_,
                                    active_reasoning_text_, completed_reasoning_item_ids_, completed_reasoning_texts_);
    auto& state = legacy_function_call_state_for(data, legacy_function_calls_);
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
      append_stream_error(events, error_seen_, "OpenAI function call item has an empty logical call ID or name");
      return;
    }
    static_cast<void>(append_function_call_argument_fragment(events, state, function_call_arguments_from_event(data).value_or("")));
    static_cast<void>(append_function_call_start_if_ready(events, state));
    append_unemitted_function_call_arguments(events, state);
    return;
  }
  if (type == "response.function_call.done" || type == "response.function_call.completed")
  {
    saw_content_ = true;
    append_finish_reasoning_if_open(events, reasoning_open_, reasoning_text_seen_, active_reasoning_item_id_, active_reasoning_output_index_,
                                    active_reasoning_text_, completed_reasoning_item_ids_, completed_reasoning_texts_);
    auto& state = legacy_function_call_state_for(data, legacy_function_calls_);
    if (!reconcile_complete_function_call_arguments(events, state, function_call_arguments_from_event(data), error_seen_))
      return;
    static_cast<void>(append_function_call_end(events, state, false, error_seen_));
    return;
  }
  if (type == "response.completed" || type == "response.incomplete")
  {
    if (done_seen_)
      return;
    done_seen_ = true;
    if (reject_unended_documented_function_calls(events, error_seen_, function_calls_) ||
        reject_unended_documented_message_items(events, error_seen_, message_items_))
      return;
    append_finish_reasoning_if_open(events, reasoning_open_, reasoning_text_seen_, active_reasoning_item_id_, active_reasoning_output_index_,
                                    active_reasoning_text_, completed_reasoning_item_ids_, completed_reasoning_texts_);
    events.push_back(StreamEvent{
        .type = StreamEventType::Done,
        .text = "",
        .tool_call_id = "",
        .tool_name = "",
        .error_message = "",
        .usage = parse_openai_usage(data),
        .finish_reason = saw_refusal_ ? ProviderFinishReason::Refusal
                                      : (type == "response.completed" ? ProviderFinishReason::Completed : detail::openai_response_finish_reason(data))});
    return;
  }
  if (type == "error" || type == "response.error" || type == "response.failed")
  {
    append_finish_reasoning_if_open(events, reasoning_open_, reasoning_text_seen_, active_reasoning_item_id_, active_reasoning_output_index_,
                                    active_reasoning_text_, completed_reasoning_item_ids_, completed_reasoning_texts_);
    append_stream_error(events, error_seen_, "OpenAI provider reported a streaming error");
    return;
  }
  // OpenAI may add non-content lifecycle events without changing the assistant turn.
}

}  // namespace ava::provider
