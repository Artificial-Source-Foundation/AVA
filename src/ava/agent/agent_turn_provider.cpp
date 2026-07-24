#include "sys.h"
#include "ava/agent/agent_turn_executor_internal.h"
#include "ava/agent/assistant_turn.h"
#include "ava/agent/provider_output_validation.h"
#include "ava/agent/stream_bridge.h"
#include "ava/agent/usage_accounting.h"
#include "ava/provider/provider_utils.h"

#include <algorithm>
#include <cstddef>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace ava::agent::detail {

ava::core::Result<ProviderTurn> AgentTurnExecutor::request_provider_turn()
{
  ToolDispatcher const& dispatcher = *dispatcher_storage_;

  while (true)
  {
    if (auto not_canceled = session_.check_canceled("before_provider_call"); !not_canceled)
    {
      return std::unexpected(std::move(not_canceled.error()));
    }

    if (options_.take_steering_messages)
    {
      auto steering_messages = options_.take_steering_messages();
      if (!steering_messages)
        return std::unexpected(std::move(steering_messages.error()));
      for (auto const& steering_message : *steering_messages)
      {
        if (auto appended = append_active_turn_user_message(steering_message, {}); !appended)
        {
          return std::unexpected(appended.error());
        }
      }
    }

    if (skip_auto_compaction_after_overflow_retry_)
    {
      skip_auto_compaction_after_overflow_retry_ = false;
    }
    else if (options_.compact_context && result_.provider_iterations == 0 && !pre_turn_compacted_)
    {
      auto compacted = compact_context("auto");
      if (!compacted)
        return std::unexpected(std::move(compacted.error()));
      if (*compacted)
      {
        if (auto replayed = replay_active_turn_user_messages(); !replayed)
        {
          return std::unexpected(std::move(replayed.error()));
        }
      }
      if (auto not_canceled = session_.check_canceled("after_auto_compaction"); !not_canceled)
      {
        return std::unexpected(std::move(not_canceled.error()));
      }
    }

    auto messages = build_messages();
    if (!messages)
      return std::unexpected(messages.error());
    auto const tool_schemas = options_.model_supports_tools ? dispatcher.registered_tool_schemas_json() : std::vector<std::string>{};
    ava::provider::ProviderRequest provider_request{.provider_id = options_.provider_id,
                                                    .model_id = options_.model_id,
                                                    .system_prompt = options_.system_prompt,
                                                    .messages = messages->messages,
                                                    .tools_json = tool_schemas,
                                                    .stream = options_.stream && options_.model_supports_streaming,
                                                    .max_output_tokens = options_.model_max_output_tokens,
                                                    .reasoning = options_.reasoning};
    bool const model_supports_images =
        std::find(options_.model_input_modalities.begin(), options_.model_input_modalities.end(), "image") != options_.model_input_modalities.end();
    if (auto valid_images = ava::provider::validate_image_content_parts(provider_request, model_supports_images); !valid_images)
    {
      static_cast<void>(session_.append_error(valid_images.error()));
      return std::unexpected(std::move(valid_images.error()));
    }
    if (auto attached_images = session_.attach_verified_image_payloads(provider_request); !attached_images)
    {
      static_cast<void>(session_.append_error(attached_images.error()));
      return std::unexpected(std::move(attached_images.error()));
    }
    ava::provider::ProviderAuthContext const auth_context{
        .access_token = options_.access_token,
        .credential_type = options_.openai_oauth && options_.credential_type == "bearer" ? "oauth" : options_.credential_type,
        .account_id = options_.openai_account_id};
    auto request = provider_.build_request(provider_request, auth_context);
    if (!request)
    {
      if (auto retry = prepare_context_overflow_retry(request.error()); !retry)
      {
        return std::unexpected(std::move(retry.error()));
      }
      else if (*retry)
      {
        continue;
      }
      static_cast<void>(session_.append_error(request.error()));
      return std::unexpected(request.error());
    }
    result_.used_compacted_context = result_.used_compacted_context || messages->used_compacted_context;
    if (result_.provider_iterations == 0)
    {
      result_.initial_context_messages = provider_request.messages.size();
    }
    std::vector<ava::provider::StreamEvent> provider_events;
    std::size_t streamed_assistant_text_bytes = 0;
    std::map<std::string, std::size_t> streamed_tool_argument_bytes;
    std::unordered_map<std::string, std::string> streamed_tool_names;
    std::unordered_set<std::string> current_provider_tool_call_ids;
    bool processed_stream_chunks = false;
    auto append_stream_events = [&](std::vector<ava::provider::StreamEvent> new_events, bool publish_all_events = true) -> ava::core::VoidResult {
      for (auto& event : new_events)
      {
        if (options_.max_provider_events > 0 && provider_events.size() >= options_.max_provider_events)
        {
          return std::unexpected(output_limit_error("provider output event limit exceeded", "max_provider_events", options_.max_provider_events));
        }
        if (event.type == ava::provider::StreamEventType::TextDelta)
        {
          if (would_exceed(streamed_assistant_text_bytes, event.text.size(), options_.max_assistant_text_bytes))
          {
            return std::unexpected(output_limit_error("assistant text byte limit exceeded", "max_assistant_text_bytes", options_.max_assistant_text_bytes));
          }
          streamed_assistant_text_bytes += event.text.size();
        }
        else if (event.type == ava::provider::StreamEventType::ReasoningStart || event.type == ava::provider::StreamEventType::ReasoningDelta ||
                 event.type == ava::provider::StreamEventType::ReasoningEnd)
        {
          auto const event_bytes =
              event.type == ava::provider::StreamEventType::ReasoningEnd
                  ? event.reasoning_signature.size() + event.reasoning_redacted_data.size() + event.reasoning_native_item_json.size()
                  : event.text.size() + event.reasoning_signature.size() + event.reasoning_redacted_data.size() + event.reasoning_native_item_json.size();
          if (would_exceed(streamed_assistant_text_bytes, event_bytes, options_.max_assistant_text_bytes))
          {
            return std::unexpected(output_limit_error("reasoning byte limit exceeded", "max_assistant_text_bytes", options_.max_assistant_text_bytes));
          }
          streamed_assistant_text_bytes += event_bytes;
        }
        else if (event.type == ava::provider::StreamEventType::ToolCallStart || event.type == ava::provider::StreamEventType::ToolCallDelta ||
                 event.type == ava::provider::StreamEventType::ToolCallEnd)
        {
          if (auto valid_id = validate_provider_tool_call_id(event.tool_call_id); !valid_id)
          {
            return std::unexpected(std::move(valid_id.error()));
          }
          if (finalized_provider_tool_call_ids_.contains(event.tool_call_id))
          {
            auto error = ava::core::Error(ava::core::ErrorCategory::Provider, "provider reused a finalized tool call id in the persistent session");
            error.with_context("tool_call_id", event.tool_call_id);
            error.with_context("provider_iteration", std::to_string(result_.provider_iterations + 1));
            error.with_context("hint", "provider tool call ids must remain unique for the complete persistent session");
            return std::unexpected(std::move(error));
          }
          current_provider_tool_call_ids.insert(event.tool_call_id);
          if (event.type == ava::provider::StreamEventType::ToolCallDelta)
          {
            auto& bytes = streamed_tool_argument_bytes[event.tool_call_id];
            if (would_exceed(bytes, event.text.size(), options_.max_tool_argument_bytes))
            {
              return std::unexpected(output_limit_error("tool argument byte limit exceeded", "max_tool_argument_bytes", options_.max_tool_argument_bytes));
            }
            bytes += event.text.size();
          }
        }
        // Trace parser output before publishing the product event: an observer
        // failure is isolated and cannot suppress or reorder product output.
        if (options_.observation)
        {
          options_.observation->emit(ava::observability::TraceEventType::ProviderStreamEvent, trace_context_, [&event](auto& trace) {
            trace.phase = ava::observability::TracePhase::Provider;
            switch (event.type)
            {
              case ava::provider::StreamEventType::TextStart:
              case ava::provider::StreamEventType::TextDelta:
              case ava::provider::StreamEventType::TextEnd:
                trace.outcome = ava::observability::TraceOutcome::TextDelta;
                break;
              case ava::provider::StreamEventType::ReasoningStart:
                trace.outcome = ava::observability::TraceOutcome::ReasoningStart;
                break;
              case ava::provider::StreamEventType::ReasoningDelta:
                trace.outcome = ava::observability::TraceOutcome::ReasoningDelta;
                break;
              case ava::provider::StreamEventType::ReasoningEnd:
                trace.outcome = ava::observability::TraceOutcome::ReasoningEnd;
                break;
              case ava::provider::StreamEventType::ToolCallStart:
                trace.outcome = ava::observability::TraceOutcome::ToolCallStart;
                break;
              case ava::provider::StreamEventType::ToolCallDelta:
                trace.outcome = ava::observability::TraceOutcome::ToolCallDelta;
                break;
              case ava::provider::StreamEventType::ToolCallEnd:
                trace.outcome = ava::observability::TraceOutcome::ToolCallEnd;
                break;
              case ava::provider::StreamEventType::Done:
                trace.outcome = ava::observability::TraceOutcome::Done;
                break;
              case ava::provider::StreamEventType::Error:
                trace.outcome = ava::observability::TraceOutcome::Error;
                break;
            }
            trace.fields = {{.key = "text_bytes", .value = std::to_string(event.text.size())},
                            {.key = "tool_name", .value = "[omitted]", .provenance = ava::observability::FieldProvenance::Content},
                            {.key = "usage_present", .value = event.usage ? "true" : "false"}};
          });
        }
        if (event.type == ava::provider::StreamEventType::ToolCallStart && !event.tool_call_id.empty() && !event.tool_name.empty())
          streamed_tool_names[event.tool_call_id] = event.tool_name;

        bool const should_publish = publish_all_events || event.type == ava::provider::StreamEventType::ReasoningStart ||
                                    event.type == ava::provider::StreamEventType::ReasoningDelta || event.type == ava::provider::StreamEventType::ReasoningEnd;
        if (should_publish)
        {
          auto public_event = event;
          if (event.type == ava::provider::StreamEventType::ToolCallStart || event.type == ava::provider::StreamEventType::ToolCallEnd)
          {
            // Start/end carry only lifecycle identity. Never surface an
            // unexpected provider payload from those lifecycle records.
            public_event.text.clear();
          }
          else if (event.type == ava::provider::StreamEventType::ToolCallDelta)
          {
            auto const found = streamed_tool_names.find(event.tool_call_id);
            if (found == streamed_tool_names.end())
            {
              // A malformed stream without a start cannot prove that this is
              // not a shell request. Suppress its arguments rather than
              // exposing a payload before permission mediation.
              public_event.text = "<redacted tool arguments>";
            }
            else if (found->second == "bash")
            {
              public_event.tool_name = found->second;
              public_event.text = std::string(kRedactedRunCommand);
            }
          }
          if (auto published = publish_stream_event(options_, public_event); !published)
          {
            return std::unexpected(std::move(published.error()));
          }
        }
        if (event.type == ava::provider::StreamEventType::ToolCallEnd)
          streamed_tool_names.erase(event.tool_call_id);
        provider_events.push_back(std::move(event));
      }
      return {};
    };

    if (auto phase = publish_phase(RunPhase::AwaitingProvider); !phase)
      return std::unexpected(std::move(phase.error()));
    if (auto not_canceled = session_.check_canceled("before_provider_transport"); !not_canceled)
      return std::unexpected(std::move(not_canceled.error()));
    if (provider_request.stream && effective_transport_->supports_streaming())
    {
      auto stream_parser = provider_.create_stream_parser();
      auto response = effective_transport_->send_streaming(
          *request,
          [&](std::string_view chunk) -> ava::core::VoidResult {
            processed_stream_chunks = true;
            if (session_.is_canceled())
            {
              return std::unexpected(ava::core::Error(ava::core::ErrorCategory::Unknown, "agent loop canceled"));
            }
            auto parsed = stream_parser->append(chunk);
            if (!parsed)
              return std::unexpected(std::move(parsed.error()));
            return append_stream_events(std::move(*parsed));
          },
          [cancel_requested = options_.cancel_requested]() { return cancel_requested && cancel_requested(); });
      if (!response)
      {
        if (session_.is_canceled())
        {
          if (auto not_canceled = session_.check_canceled("during_provider_stream"); !not_canceled)
          {
            return std::unexpected(std::move(not_canceled.error()));
          }
        }
        if (auto retry = prepare_context_overflow_retry(response.error()); !retry)
        {
          return std::unexpected(std::move(retry.error()));
        }
        else if (*retry)
        {
          continue;
        }
        static_cast<void>(session_.append_error(response.error()));
        return std::unexpected(response.error());
      }
      if (auto not_canceled = session_.check_canceled("after_provider_call"); !not_canceled)
      {
        return std::unexpected(std::move(not_canceled.error()));
      }
      if (response->status_code < 200 || response->status_code >= 300)
      {
        auto events = provider_.parse_response(*response, provider_request.stream);
        if (!events)
        {
          if (auto retry = prepare_context_overflow_retry(events.error()); !retry)
          {
            return std::unexpected(std::move(retry.error()));
          }
          else if (*retry)
          {
            continue;
          }
          static_cast<void>(session_.append_error(events.error()));
          return std::unexpected(events.error());
        }
        if (auto appended = append_stream_events(std::move(*events)); !appended)
        {
          if (auto retry = prepare_context_overflow_retry(appended.error()); !retry)
          {
            return std::unexpected(std::move(retry.error()));
          }
          else if (*retry)
          {
            continue;
          }
          static_cast<void>(session_.append_error(appended.error()));
          return std::unexpected(std::move(appended.error()));
        }
      }
      else if (!processed_stream_chunks && provider_events.empty() && !response->body.empty())
      {
        auto events = provider_.parse_response(*response, provider_request.stream);
        if (!events)
        {
          if (auto retry = prepare_context_overflow_retry(events.error()); !retry)
          {
            return std::unexpected(std::move(retry.error()));
          }
          else if (*retry)
          {
            continue;
          }
          static_cast<void>(session_.append_error(events.error()));
          return std::unexpected(events.error());
        }
        if (auto appended = append_stream_events(std::move(*events)); !appended)
        {
          if (session_.is_canceled())
          {
            if (auto not_canceled = session_.check_canceled("during_provider_stream"); !not_canceled)
            {
              return std::unexpected(std::move(not_canceled.error()));
            }
          }
          if (auto retry = prepare_context_overflow_retry(appended.error()); !retry)
          {
            return std::unexpected(std::move(retry.error()));
          }
          else if (*retry)
          {
            continue;
          }
          static_cast<void>(session_.append_error(appended.error()));
          return std::unexpected(std::move(appended.error()));
        }
      }
      else
      {
        auto parsed = stream_parser->finish();
        if (!parsed)
        {
          if (auto retry = prepare_context_overflow_retry(parsed.error()); !retry)
          {
            return std::unexpected(std::move(retry.error()));
          }
          else if (*retry)
          {
            continue;
          }
          static_cast<void>(session_.append_error(parsed.error()));
          return std::unexpected(parsed.error());
        }
        if (auto appended = append_stream_events(std::move(*parsed)); !appended)
        {
          if (session_.is_canceled())
          {
            if (auto not_canceled = session_.check_canceled("during_provider_stream"); !not_canceled)
            {
              return std::unexpected(std::move(not_canceled.error()));
            }
          }
          if (auto retry = prepare_context_overflow_retry(appended.error()); !retry)
          {
            return std::unexpected(std::move(retry.error()));
          }
          else if (*retry)
          {
            continue;
          }
          static_cast<void>(session_.append_error(appended.error()));
          return std::unexpected(std::move(appended.error()));
        }
      }
    }
    else
    {
      auto response = effective_transport_->send(*request, [cancel_requested = options_.cancel_requested]() { return cancel_requested && cancel_requested(); });
      if (!response)
      {
        if (session_.is_canceled())
        {
          if (auto not_canceled = session_.check_canceled("during_provider_request"); !not_canceled)
          {
            return std::unexpected(std::move(not_canceled.error()));
          }
        }
        if (auto retry = prepare_context_overflow_retry(response.error()); !retry)
        {
          return std::unexpected(std::move(retry.error()));
        }
        else if (*retry)
        {
          continue;
        }
        static_cast<void>(session_.append_error(response.error()));
        return std::unexpected(response.error());
      }
      if (auto not_canceled = session_.check_canceled("after_provider_call"); !not_canceled)
      {
        return std::unexpected(std::move(not_canceled.error()));
      }
      auto events = provider_.parse_response(*response, provider_request.stream);
      if (!events)
      {
        if (auto retry = prepare_context_overflow_retry(events.error()); !retry)
        {
          return std::unexpected(std::move(retry.error()));
        }
        else if (*retry)
        {
          continue;
        }
        static_cast<void>(session_.append_error(events.error()));
        return std::unexpected(events.error());
      }
      if (auto appended = append_stream_events(std::move(*events), false); !appended)
      {
        if (auto retry = prepare_context_overflow_retry(appended.error()); !retry)
        {
          return std::unexpected(std::move(retry.error()));
        }
        else if (*retry)
        {
          continue;
        }
        static_cast<void>(session_.append_error(appended.error()));
        return std::unexpected(std::move(appended.error()));
      }
    }

    auto turn = parse_assistant_turn(provider_events, ProviderOutputLimits{.max_events = options_.max_provider_events,
                                                                           .max_assistant_text_bytes = options_.max_assistant_text_bytes,
                                                                           .max_tool_argument_bytes = options_.max_tool_argument_bytes});
    if (!turn)
    {
      if (auto retry = prepare_context_overflow_retry(turn.error()); !retry)
      {
        return std::unexpected(std::move(retry.error()));
      }
      else if (*retry)
      {
        continue;
      }
      static_cast<void>(session_.append_error(turn.error()));
      return std::unexpected(turn.error());
    }
    std::unordered_set<std::string> iteration_tool_call_ids;
    for (auto const& call : turn->tool_calls)
    {
      if (!iteration_tool_call_ids.insert(call.id).second)
        continue;  // One provider turn may merge multiple deltas for one finalized call.
      if (!current_provider_tool_call_ids.contains(call.id) || finalized_provider_tool_call_ids_.contains(call.id))
      {
        auto error =
            ava::core::Error(ava::core::ErrorCategory::Provider, "provider reused or inconsistently finalized a tool call id in the persistent session");
        error.with_context("tool_call_id", call.id);
        error.with_context("provider_iteration", std::to_string(result_.provider_iterations + 1));
        error.with_context("hint", "provider tool call ids must remain unique for the complete persistent session");
        static_cast<void>(session_.append_error(error));
        return std::unexpected(std::move(error));
      }
    }
    if (auto not_canceled = session_.check_canceled("before_assistant_append"); !not_canceled)
    {
      return std::unexpected(std::move(not_canceled.error()));
    }

    ++result_.provider_iterations;
    auto usage = turn->usage ? with_total_tokens(*turn->usage) : estimate_usage_from_turn(request->body, *turn);
    auto const cost_usd =
        options_.model_pricing && !usage.estimated ? ava::config::usage_cost_usd(*options_.model_pricing, usage) : std::optional<long double>{};
    accumulate_usage(result_.usage, usage);
    if (cost_usd && accumulated_cost_known_)
    {
      result_.cost_usd = result_.cost_usd.value_or(0.0L) + *cost_usd;
    }
    else
    {
      accumulated_cost_known_ = false;
      result_.cost_usd = std::nullopt;
    }
    auto const disposition =
        *turn->finish_reason == ava::provider::ProviderFinishReason::Cancelled ? ProviderTurnDisposition::TerminalCancelled : ProviderTurnDisposition::Parsed;
    return ProviderTurn{.disposition = disposition,
                        .assistant_turn = std::move(*turn),
                        .usage = std::move(usage),
                        .cost_usd = cost_usd,
                        .iteration_tool_call_ids = std::move(iteration_tool_call_ids)};
  }
}

}  // namespace ava::agent::detail
