#include "ava/agent/agent_loop.h"

#include "ava/agent/agent_loop_session.h"
#include "ava/agent/assistant_turn.h"
#include "ava/agent/message_builder.h"
#include "ava/agent/provider_output_validation.h"
#include "ava/agent/stream_bridge.h"
#include "ava/agent/tool_dispatcher.h"
#include "ava/agent/tool_result.h"
#include "ava/agent/tool_summaries.h"
#include "ava/agent/tool_timeline.h"
#include "ava/agent/usage_accounting.h"

#include "ava/core/json.h"

#include <algorithm>
#include <map>
#include <optional>
#include <string_view>
#include <utility>
#include <vector>

namespace ava::agent {
namespace {

std::string dispatch_error_result_json(ProviderToolCall const& call, ava::core::Error const& error)
{
  return "{\"tool\":\"" + ava::core::json::escape(call.name) + "\",\"ok\":false,\"error\":{\"category\":\"" +
         ava::core::json::escape(ava::core::to_string(error.category())) + "\",\"message\":\"" +
         ava::core::json::escape(error.message()) + "\",\"details\":\"" + ava::core::json::escape(error.format()) +
         "\"}}";
}

ToolDispatchResult synthetic_failed_dispatch_result(ProviderToolCall const& call, ava::core::Error const& error)
{
  return with_tool_result_payload(ToolDispatchResult{
      .call_id = call.id, .name = call.name, .success = false, .result_text = dispatch_error_result_json(call, error)});
}

bool is_canceled(AgentLoopOptions const& options)
{
  return options.cancel_requested && options.cancel_requested();
}

ava::core::VoidResult check_canceled(AgentLoopOptions const& options, ava::session::SessionStore& store,
                                     std::string_view boundary)
{
  if (!is_canceled(options)) return {};
  static_cast<void>(append_cancel(store, boundary));
  auto error = ava::core::Error(ava::core::ErrorCategory::Unknown, "agent loop canceled");
  error.with_context("boundary", std::string(boundary));
  return std::unexpected(std::move(error));
}

}  // namespace

AgentLoop::AgentLoop(AgentLoopOptions options) : options_(std::move(options))
{
}

std::string to_string(ToolTimelineStatus status)
{
  switch (status) {
    case ToolTimelineStatus::Running:
      return "running";
    case ToolTimelineStatus::Success:
      return "success";
    case ToolTimelineStatus::Error:
      return "error";
  }
  return "unknown";
}

ava::core::Result<AgentLoopResult> AgentLoop::run_turn(std::string const& user_message,
                                                       ava::session::SessionStore& store,
                                                       ava::provider::Provider const& provider,
                                                       ava::provider::Transport& transport)
{
  auto check_canceled_locked = [&](std::string_view boundary) -> ava::core::VoidResult {
    if (options_.session_mutex) {
      std::lock_guard lock(*options_.session_mutex);
      return check_canceled(options_, store, boundary);
    }
    return check_canceled(options_, store, boundary);
  };
  auto append_user_message_locked = [&](std::string const& text) -> ava::core::Result<std::string> {
    if (options_.session_mutex) {
      std::lock_guard lock(*options_.session_mutex);
      return append_user_message(store, text);
    }
    return append_user_message(store, text);
  };
  auto build_messages_locked = [&]() -> ava::core::Result<BuiltProviderMessages> {
    if (options_.session_mutex) {
      std::lock_guard lock(*options_.session_mutex);
      return build_messages(store, options_.max_tool_result_context_bytes);
    }
    return build_messages(store, options_.max_tool_result_context_bytes);
  };
  auto append_assistant_message_locked = [&](std::string const& text, std::size_t tool_call_count,
                                             ava::provider::TokenUsage const& usage,
                                             std::optional<long double> const& cost_usd) -> ava::core::VoidResult {
    if (options_.session_mutex) {
      std::lock_guard lock(*options_.session_mutex);
      return append_assistant_message(store, text, tool_call_count, usage, cost_usd);
    }
    return append_assistant_message(store, text, tool_call_count, usage, cost_usd);
  };
  auto append_reasoning_blocks_locked = [&](std::vector<ParsedReasoningBlock> const& blocks) -> ava::core::VoidResult {
    auto append_all = [&]() -> ava::core::VoidResult {
      for (auto const& block : blocks) {
        if (auto appended = append_reasoning_block(store, block, options_.provider_id, options_.model_id); !appended) {
          return appended;
        }
      }
      return {};
    };
    if (options_.session_mutex) {
      std::lock_guard lock(*options_.session_mutex);
      return append_all();
    }
    return append_all();
  };
  auto append_tool_call_locked = [&](ProviderToolCall const& call) -> ava::core::VoidResult {
    if (options_.session_mutex) {
      std::lock_guard lock(*options_.session_mutex);
      return append_tool_call(store, call);
    }
    return append_tool_call(store, call);
  };
  auto append_tool_result_locked = [&](ToolDispatchResult const& dispatch_result) -> ava::core::VoidResult {
    if (options_.session_mutex) {
      std::lock_guard lock(*options_.session_mutex);
      return append_tool_result(store, dispatch_result);
    }
    return append_tool_result(store, dispatch_result);
  };
  auto append_error_locked = [&](ava::core::Error const& error) -> ava::core::VoidResult {
    if (options_.session_mutex) {
      std::lock_guard lock(*options_.session_mutex);
      return append_error(store, error);
    }
    return append_error(store, error);
  };
  struct ActiveTurnUserMessage {
    std::string id;
    std::string text;
  };
  std::vector<ActiveTurnUserMessage> active_turn_user_messages;
  auto replayable_active_turn_texts = [&]() {
    std::vector<std::string> messages;
    messages.reserve(active_turn_user_messages.size());
    for (auto const& message : active_turn_user_messages) messages.push_back(message.text);
    return messages;
  };
  auto compact_context = [&](std::string_view trigger) -> ava::core::Result<bool> {
    if (!options_.compact_context) {
      auto error = ava::core::Error(ava::core::ErrorCategory::Provider, "context compaction is unavailable");
      error.with_context("trigger", std::string(trigger));
      return std::unexpected(std::move(error));
    }
    auto const replayed_messages = replayable_active_turn_texts();
    return options_.compact_context(store, trigger, replayed_messages);
  };
  auto append_active_turn_user_message_locked = [&](std::string const& text) -> ava::core::VoidResult {
    auto appended = append_user_message_locked(text);
    if (!appended) return std::unexpected(std::move(appended.error()));
    active_turn_user_messages.push_back(ActiveTurnUserMessage{.id = *appended, .text = text});
    return {};
  };
  auto replay_active_turn_user_messages_locked = [&]() -> ava::core::VoidResult {
    for (auto const& message : active_turn_user_messages) {
      auto replayed = [&]() -> ava::core::VoidResult {
        if (options_.session_mutex) {
          std::lock_guard lock(*options_.session_mutex);
          return append_replay_user_message(store, message.text, message.id);
        }
        return append_replay_user_message(store, message.text, message.id);
      }();
      if (!replayed) return replayed;
    }
    return {};
  };
  bool context_overflow_retry_used = false;
  bool skip_auto_compaction_after_overflow_retry = false;
  auto prepare_context_overflow_retry = [&](ava::core::Error const& error) -> ava::core::Result<bool> {
    if (!ava::provider::is_context_overflow_error(error) || context_overflow_retry_used || !options_.compact_context) {
      return false;
    }
    context_overflow_retry_used = true;
    if (auto not_canceled = check_canceled_locked("before_context_overflow_compaction"); !not_canceled) {
      return std::unexpected(std::move(not_canceled.error()));
    }
    auto compacted = compact_context("context_overflow");
    if (!compacted) {
      auto compact_error = ava::core::Error(ava::core::ErrorCategory::Provider, "context overflow compaction failed");
      compact_error.with_context("provider_error", error.format());
      compact_error.with_context("compaction_error", compacted.error().format());
      return std::unexpected(std::move(compact_error));
    }
    if (*compacted) {
      if (auto replayed = replay_active_turn_user_messages_locked(); !replayed) {
        return std::unexpected(std::move(replayed.error()));
      }
      skip_auto_compaction_after_overflow_retry = true;
    }
    if (auto not_canceled = check_canceled_locked("after_context_overflow_compaction"); !not_canceled) {
      return std::unexpected(std::move(not_canceled.error()));
    }
    return true;
  };

  if (auto not_canceled = check_canceled_locked("before_turn_start"); !not_canceled) {
    return std::unexpected(std::move(not_canceled.error()));
  }
  bool pre_turn_compacted = false;
  if (options_.compact_context) {
    auto compacted = compact_context("auto");
    if (!compacted) return std::unexpected(std::move(compacted.error()));
    pre_turn_compacted = *compacted;
    if (auto not_canceled = check_canceled_locked("after_pre_turn_auto_compaction"); !not_canceled) {
      return std::unexpected(std::move(not_canceled.error()));
    }
  }
  if (auto appended = append_active_turn_user_message_locked(user_message); !appended)
    return std::unexpected(appended.error());

  AgentLoopResult result;
  ava::tools::ToolContext const tool_context{
      .workspace_dir = options_.workspace_dir,
      .spill_dir = store.session_path().parent_path() / "spill",
      .mode = options_.mode,
      .permission_resolver = options_.permission_resolver,
      .permission_audit_sink = [&store, session_mutex = options_.session_mutex](
                                   ava::tools::PermissionAuditEvent const& event) -> ava::core::VoidResult {
        if (session_mutex) {
          std::lock_guard lock(*session_mutex);
          return append_permission_decision(store, event);
        }
        return append_permission_decision(store, event);
      },
      .progress_sink = [this](ava::tools::ToolProgressEvent const& event) -> ava::core::VoidResult {
        return publish_tool_progress(
            options_,
            ToolProgressEntry{
                .call_id = event.call_id, .name = event.tool_name, .text = event.text, .status = event.status});
      },
      .cancel_requested = options_.cancel_requested,
      .question_resolver = options_.question_resolver};
  ToolDispatcher const dispatcher(tool_context);

  std::size_t tool_iterations = 0;
  bool accumulated_cost_known = true;
  while (true) {
    if (auto not_canceled = check_canceled_locked("before_provider_call"); !not_canceled) {
      return std::unexpected(std::move(not_canceled.error()));
    }

    if (options_.take_steering_messages) {
      auto steering_messages = options_.take_steering_messages();
      if (!steering_messages) return std::unexpected(std::move(steering_messages.error()));
      for (auto const& steering_message : *steering_messages) {
        if (auto appended = append_active_turn_user_message_locked(steering_message); !appended) {
          return std::unexpected(appended.error());
        }
      }
    }

    if (skip_auto_compaction_after_overflow_retry) {
      skip_auto_compaction_after_overflow_retry = false;
    } else if (options_.compact_context && result.provider_iterations == 0 && !pre_turn_compacted) {
      auto compacted = compact_context("auto");
      if (!compacted) return std::unexpected(std::move(compacted.error()));
      if (*compacted) {
        if (auto replayed = replay_active_turn_user_messages_locked(); !replayed) {
          return std::unexpected(std::move(replayed.error()));
        }
      }
      if (auto not_canceled = check_canceled_locked("after_auto_compaction"); !not_canceled) {
        return std::unexpected(std::move(not_canceled.error()));
      }
    }

    auto messages = build_messages_locked();
    if (!messages) return std::unexpected(messages.error());
    auto const tool_schemas =
        options_.model_supports_tools ? ToolDispatcher::tool_schemas_json(tool_context) : std::vector<std::string>{};
    ava::provider::ProviderRequest const provider_request{
        .provider_id = options_.provider_id,
        .model_id = options_.model_id,
        .system_prompt = options_.system_prompt,
        .messages = messages->messages,
        .tools_json = tool_schemas,
        .stream = options_.stream && options_.model_supports_streaming,
        .max_output_tokens = options_.model_max_output_tokens,
        .reasoning = options_.reasoning};
    ava::provider::ProviderAuthContext const auth_context{
        .access_token = options_.access_token,
        .credential_type =
            options_.openai_oauth && options_.credential_type == "bearer" ? "oauth" : options_.credential_type,
        .account_id = options_.openai_account_id};
    auto request = provider.build_request(provider_request, auth_context);
    if (!request) {
      if (auto retry = prepare_context_overflow_retry(request.error()); !retry) {
        return std::unexpected(std::move(retry.error()));
      } else if (*retry) {
        continue;
      }
      static_cast<void>(append_error_locked(request.error()));
      return std::unexpected(request.error());
    }
    result.used_compacted_context = result.used_compacted_context || messages->used_compacted_context;
    if (result.provider_iterations == 0) {
      result.initial_context_messages = provider_request.messages.size();
    }
    std::vector<ava::provider::StreamEvent> provider_events;
    std::size_t streamed_assistant_text_bytes = 0;
    std::map<std::string, std::size_t> streamed_tool_argument_bytes;
    bool processed_stream_chunks = false;
    auto append_stream_events = [&](std::vector<ava::provider::StreamEvent> new_events,
                                    bool publish_all_events = true) -> ava::core::VoidResult {
      for (auto& event : new_events) {
        if (options_.max_provider_events > 0 && provider_events.size() >= options_.max_provider_events) {
          return std::unexpected(output_limit_error("provider output event limit exceeded", "max_provider_events",
                                                    options_.max_provider_events));
        }
        if (event.type == ava::provider::StreamEventType::TextDelta) {
          if (would_exceed(streamed_assistant_text_bytes, event.text.size(), options_.max_assistant_text_bytes)) {
            return std::unexpected(output_limit_error("assistant text byte limit exceeded", "max_assistant_text_bytes",
                                                      options_.max_assistant_text_bytes));
          }
          streamed_assistant_text_bytes += event.text.size();
        } else if (event.type == ava::provider::StreamEventType::ReasoningStart ||
                   event.type == ava::provider::StreamEventType::ReasoningDelta ||
                   event.type == ava::provider::StreamEventType::ReasoningEnd) {
          auto const event_bytes =
              event.type == ava::provider::StreamEventType::ReasoningEnd
                  ? event.reasoning_signature.size() + event.reasoning_redacted_data.size()
                  : event.text.size() + event.reasoning_signature.size() + event.reasoning_redacted_data.size();
          if (would_exceed(streamed_assistant_text_bytes, event_bytes, options_.max_assistant_text_bytes)) {
            return std::unexpected(output_limit_error("reasoning byte limit exceeded", "max_assistant_text_bytes",
                                                      options_.max_assistant_text_bytes));
          }
          streamed_assistant_text_bytes += event_bytes;
        } else if (event.type == ava::provider::StreamEventType::ToolCallStart) {
          if (auto valid_id = validate_provider_tool_call_id(event.tool_call_id); !valid_id) {
            return std::unexpected(std::move(valid_id.error()));
          }
        } else if (event.type == ava::provider::StreamEventType::ToolCallDelta) {
          if (auto valid_id = validate_provider_tool_call_id(event.tool_call_id); !valid_id) {
            return std::unexpected(std::move(valid_id.error()));
          }
          auto& bytes = streamed_tool_argument_bytes[event.tool_call_id];
          if (would_exceed(bytes, event.text.size(), options_.max_tool_argument_bytes)) {
            return std::unexpected(output_limit_error("tool argument byte limit exceeded", "max_tool_argument_bytes",
                                                      options_.max_tool_argument_bytes));
          }
          bytes += event.text.size();
        }
        bool const should_publish = publish_all_events ||
                                    event.type == ava::provider::StreamEventType::ReasoningStart ||
                                    event.type == ava::provider::StreamEventType::ReasoningDelta ||
                                    event.type == ava::provider::StreamEventType::ReasoningEnd;
        if (should_publish) {
          if (auto published = publish_stream_event(options_, event); !published) {
            return std::unexpected(std::move(published.error()));
          }
        }
        provider_events.push_back(std::move(event));
      }
      return {};
    };

    if (provider_request.stream && transport.supports_streaming()) {
      auto stream_parser = provider.create_stream_parser();
      auto response = transport.send_streaming(
          *request,
          [&](std::string_view chunk) -> ava::core::VoidResult {
            processed_stream_chunks = true;
            if (is_canceled(options_)) {
              return std::unexpected(ava::core::Error(ava::core::ErrorCategory::Unknown, "agent loop canceled"));
            }
            auto parsed = stream_parser->append(chunk);
            if (!parsed) return std::unexpected(std::move(parsed.error()));
            return append_stream_events(std::move(*parsed));
          },
          [&options = options_]() { return is_canceled(options); });
      if (!response) {
        if (is_canceled(options_)) {
          if (auto not_canceled = check_canceled_locked("during_provider_stream"); !not_canceled) {
            return std::unexpected(std::move(not_canceled.error()));
          }
        }
        if (auto retry = prepare_context_overflow_retry(response.error()); !retry) {
          return std::unexpected(std::move(retry.error()));
        } else if (*retry) {
          continue;
        }
        static_cast<void>(append_error_locked(response.error()));
        return std::unexpected(response.error());
      }
      if (auto not_canceled = check_canceled_locked("after_provider_call"); !not_canceled) {
        return std::unexpected(std::move(not_canceled.error()));
      }
      if (response->status_code < 200 || response->status_code >= 300) {
        auto events = provider.parse_response(*response, provider_request.stream);
        if (!events) {
          if (auto retry = prepare_context_overflow_retry(events.error()); !retry) {
            return std::unexpected(std::move(retry.error()));
          } else if (*retry) {
            continue;
          }
          static_cast<void>(append_error_locked(events.error()));
          return std::unexpected(events.error());
        }
        if (auto appended = append_stream_events(std::move(*events)); !appended) {
          if (auto retry = prepare_context_overflow_retry(appended.error()); !retry) {
            return std::unexpected(std::move(retry.error()));
          } else if (*retry) {
            continue;
          }
          static_cast<void>(append_error_locked(appended.error()));
          return std::unexpected(std::move(appended.error()));
        }
      } else if (!processed_stream_chunks && provider_events.empty() && !response->body.empty()) {
        auto events = provider.parse_response(*response, provider_request.stream);
        if (!events) {
          if (auto retry = prepare_context_overflow_retry(events.error()); !retry) {
            return std::unexpected(std::move(retry.error()));
          } else if (*retry) {
            continue;
          }
          static_cast<void>(append_error_locked(events.error()));
          return std::unexpected(events.error());
        }
        if (auto appended = append_stream_events(std::move(*events)); !appended) {
          if (is_canceled(options_)) {
            if (auto not_canceled = check_canceled_locked("during_provider_stream"); !not_canceled) {
              return std::unexpected(std::move(not_canceled.error()));
            }
          }
          if (auto retry = prepare_context_overflow_retry(appended.error()); !retry) {
            return std::unexpected(std::move(retry.error()));
          } else if (*retry) {
            continue;
          }
          static_cast<void>(append_error_locked(appended.error()));
          return std::unexpected(std::move(appended.error()));
        }
      } else {
        auto parsed = stream_parser->finish();
        if (!parsed) {
          if (auto retry = prepare_context_overflow_retry(parsed.error()); !retry) {
            return std::unexpected(std::move(retry.error()));
          } else if (*retry) {
            continue;
          }
          static_cast<void>(append_error_locked(parsed.error()));
          return std::unexpected(parsed.error());
        }
        if (auto appended = append_stream_events(std::move(*parsed)); !appended) {
          if (is_canceled(options_)) {
            if (auto not_canceled = check_canceled_locked("during_provider_stream"); !not_canceled) {
              return std::unexpected(std::move(not_canceled.error()));
            }
          }
          if (auto retry = prepare_context_overflow_retry(appended.error()); !retry) {
            return std::unexpected(std::move(retry.error()));
          } else if (*retry) {
            continue;
          }
          static_cast<void>(append_error_locked(appended.error()));
          return std::unexpected(std::move(appended.error()));
        }
      }
    } else {
      auto response = transport.send(*request, [&options = options_]() { return is_canceled(options); });
      if (!response) {
        if (is_canceled(options_)) {
          if (auto not_canceled = check_canceled_locked("during_provider_request"); !not_canceled) {
            return std::unexpected(std::move(not_canceled.error()));
          }
        }
        if (auto retry = prepare_context_overflow_retry(response.error()); !retry) {
          return std::unexpected(std::move(retry.error()));
        } else if (*retry) {
          continue;
        }
        static_cast<void>(append_error_locked(response.error()));
        return std::unexpected(response.error());
      }
      if (auto not_canceled = check_canceled_locked("after_provider_call"); !not_canceled) {
        return std::unexpected(std::move(not_canceled.error()));
      }
      auto events = provider.parse_response(*response, provider_request.stream);
      if (!events) {
        if (auto retry = prepare_context_overflow_retry(events.error()); !retry) {
          return std::unexpected(std::move(retry.error()));
        } else if (*retry) {
          continue;
        }
        static_cast<void>(append_error_locked(events.error()));
        return std::unexpected(events.error());
      }
      if (auto appended = append_stream_events(std::move(*events), false); !appended) {
        if (auto retry = prepare_context_overflow_retry(appended.error()); !retry) {
          return std::unexpected(std::move(retry.error()));
        } else if (*retry) {
          continue;
        }
        static_cast<void>(append_error_locked(appended.error()));
        return std::unexpected(std::move(appended.error()));
      }
    }

    auto turn = parse_assistant_turn(provider_events,
                                     ProviderOutputLimits{.max_events = options_.max_provider_events,
                                                          .max_assistant_text_bytes = options_.max_assistant_text_bytes,
                                                          .max_tool_argument_bytes = options_.max_tool_argument_bytes});
    if (!turn) {
      if (auto retry = prepare_context_overflow_retry(turn.error()); !retry) {
        return std::unexpected(std::move(retry.error()));
      } else if (*retry) {
        continue;
      }
      static_cast<void>(append_error_locked(turn.error()));
      return std::unexpected(turn.error());
    }
    if (auto not_canceled = check_canceled_locked("before_assistant_append"); !not_canceled) {
      return std::unexpected(std::move(not_canceled.error()));
    }

    ++result.provider_iterations;
    auto usage = turn->usage ? with_total_tokens(*turn->usage) : estimate_usage_from_turn(request->body, *turn);
    auto const cost_usd = options_.model_pricing && !usage.estimated
                              ? ava::config::usage_cost_usd(*options_.model_pricing, usage)
                              : std::optional<long double>{};
    accumulate_usage(result.usage, usage);
    if (cost_usd && accumulated_cost_known) {
      result.cost_usd = result.cost_usd.value_or(0.0L) + *cost_usd;
    } else {
      accumulated_cost_known = false;
      result.cost_usd = std::nullopt;
    }
    if (auto appended = append_reasoning_blocks_locked(turn->reasoning_blocks); !appended) {
      return std::unexpected(appended.error());
    }
    if (auto appended = append_assistant_message_locked(turn->text, turn->tool_calls.size(), usage, cost_usd);
        !appended) {
      return std::unexpected(appended.error());
    }

    if (turn->tool_calls.empty()) {
      result.final_text = turn->text;
      result.tool_iterations = tool_iterations;
      result.stop_reason = turn->stop_reason.empty() ? "completed" : turn->stop_reason;
      return result;
    }

    for (auto const& call : turn->tool_calls) {
      if (auto not_canceled = check_canceled_locked("before_tool_dispatch"); !not_canceled) {
        return std::unexpected(std::move(not_canceled.error()));
      }
      if (auto appended = append_tool_call_locked(call); !appended) return std::unexpected(appended.error());
      ToolTimelineEntry timeline_entry{.status = ToolTimelineStatus::Running,
                                       .call_id = call.id,
                                       .name = call.name,
                                       .argument_summary = summarize_tool_arguments(call),
                                       .result_summary = "",
                                       .arguments_json = call.arguments_json};
      publish_tool_event(options_, timeline_entry);
      if (auto not_canceled = check_canceled_locked("after_tool_start_event"); !not_canceled) {
        return std::unexpected(std::move(not_canceled.error()));
      }
      auto dispatch = dispatcher.dispatch(call);
      auto dispatch_result = dispatch ? *dispatch : synthetic_failed_dispatch_result(call, dispatch.error());
      dispatch_result.payload.summary = summarize_tool_result(dispatch_result);
      if (auto appended = append_tool_result_locked(dispatch_result); !appended) {
        return std::unexpected(appended.error());
      }
      if (!dispatch) {
        timeline_entry.status = ToolTimelineStatus::Error;
      } else {
        timeline_entry.status = dispatch_result.success ? ToolTimelineStatus::Success : ToolTimelineStatus::Error;
      }
      timeline_entry.result_summary = dispatch_result.payload.summary;
      populate_tool_timeline_metadata(timeline_entry, dispatch_result);
      result.tool_timeline.push_back(timeline_entry);
      publish_tool_event(options_, timeline_entry);
      ++result.tool_calls;
      if (auto not_canceled = check_canceled_locked("after_tool_dispatch"); !not_canceled) {
        return std::unexpected(std::move(not_canceled.error()));
      }
    }

    ++tool_iterations;
    result.tool_iterations = tool_iterations;
    if (tool_iterations >= options_.max_tool_iterations) {
      auto error = ava::core::Error(ava::core::ErrorCategory::Provider, "maximum tool iterations reached");
      error.with_context("max_tool_iterations", std::to_string(options_.max_tool_iterations));
      static_cast<void>(append_error_locked(error));
      return std::unexpected(std::move(error));
    }
  }
}

}  // namespace ava::agent
