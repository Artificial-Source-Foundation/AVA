#include "runtime_run_loop.hpp"

#include <algorithm>
#include <stdexcept>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include "ava/agent/response.hpp"
#include "runtime_session_ops.hpp"

namespace ava::agent {
namespace {
using runtime_detail::append_promoted_queue_messages;
using runtime_detail::append_session_message;
using runtime_detail::build_chat_messages;
using runtime_detail::compact_session_for_agent;
using runtime_detail::derive_message_id_counter;
using runtime_detail::emit_event;
using runtime_detail::encode_tool_result_message;
using runtime_detail::estimate_session_tokens;
using runtime_detail::estimate_turn_usage;
using runtime_detail::persist_completion_metadata;
using runtime_detail::persist_error_metadata;
using runtime_detail::record_unprocessed_queue_items;

void merge_stream_usage(ava::types::TokenUsage& target, const ava::types::TokenUsage& update) {
  target.input_tokens = std::max(target.input_tokens, update.input_tokens);
  target.output_tokens = std::max(target.output_tokens, update.output_tokens);
  target.cache_read_tokens = std::max(target.cache_read_tokens, update.cache_read_tokens);
  target.cache_creation_tokens = std::max(target.cache_creation_tokens, update.cache_creation_tokens);
}

class AgentRunLoop {
 public:
  AgentRunLoop(
      const ava::llm::Provider& provider,
      const ava::tools::ToolRegistry& tools,
      const AgentConfig& config,
      ava::types::SessionRecord& session,
      const AgentRunInput& input,
      AgentEventSink on_event,
      std::string system_prompt
  )
      : provider_(provider),
        tools_(tools),
        config_(config),
        session_(session),
        input_(input),
        on_event_(std::move(on_event)),
        run_id_(input.run_id),
        stuck_(config.stuck),
        budget_(config.max_budget_usd),
        system_prompt_(std::move(system_prompt)),
        tool_definitions_(tools.list_tools()) {}

  [[nodiscard]] AgentRunResult run() {
    prepare_session();

    try {
      for(std::size_t turn = 1; turn <= config_.max_turns; ++turn) {
        if(auto terminal = execute_turn(turn); terminal.has_value()) {
          return *terminal;
        }
      }

      return complete(
          AgentCompletionReason::MaxTurns,
          "agent stopped after reaching max_turns without a completion response"
      );
    } catch(const std::exception& ex) {
      result_.reason = AgentCompletionReason::Error;
      result_.error = ex.what();
      persist_error_metadata(session_, result_.turns_used, ex.what());
      emit(AgentEvent{.kind = AgentEventKind::Error, .turn = result_.turns_used, .message = ex.what()});
      return complete(AgentCompletionReason::Error, ex.what());
    } catch(...) {
      constexpr auto* kUnknownError = "agent runtime failed with a non-standard exception";
      result_.reason = AgentCompletionReason::Error;
      result_.error = kUnknownError;
      persist_error_metadata(session_, result_.turns_used, kUnknownError);
      emit(AgentEvent{.kind = AgentEventKind::Error, .turn = result_.turns_used, .message = kUnknownError});
      return complete(AgentCompletionReason::Error, kUnknownError);
    }
  }

 private:
  struct ModelTurnResponse {
    std::string assistant_text;
    std::vector<ava::types::ToolCall> tool_calls;
    bool emitted_stream_deltas{false};
    bool cancelled{false};
    bool stuck_injected{false};
  };

  void prepare_session() {
    emit(AgentEvent{.kind = AgentEventKind::RunStarted, .turn = 0, .message = "agent run started"});

    const auto recovery = ava::types::recover_session_messages(session_);
    if(recovery.changed()) {
      emit(AgentEvent{.kind = AgentEventKind::Checkpoint, .turn = 0, .message = "checkpoint after session recovery"});
    }

    id_counter_ = derive_message_id_counter(session_);
    if(!input_.goal.empty()) {
      append_session_message(session_, id_counter_, "user", input_.goal);
      emit(AgentEvent{.kind = AgentEventKind::Checkpoint, .turn = 0, .message = "checkpoint after user goal"});
    }
  }

  [[nodiscard]] std::optional<AgentRunResult> execute_turn(std::size_t turn) {
    result_.turns_used = turn;
    emit(AgentEvent{.kind = AgentEventKind::TurnStarted, .turn = turn});

    if(cancelled()) {
      return complete(AgentCompletionReason::Cancelled, "agent run cancelled");
    }
    if(budget_.exhausted()) {
      record_skipped_queue();
      emit_budget_exhaustion_warning();
      return complete(AgentCompletionReason::BudgetExceeded, "agent stopped because the budget was exhausted");
    }

    drain_steering_messages(turn);
    compact_if_needed(turn);

    const auto chat_messages = build_chat_messages(session_, system_prompt_);
    auto turn_response = request_model_turn(turn, chat_messages);
    if(turn_response.cancelled) {
      return complete(AgentCompletionReason::Cancelled, "agent run cancelled");
    }

    if(cancelled()) {
      persist_partial_assistant_response(turn, turn_response.assistant_text, turn_response.tool_calls);
      return complete(AgentCompletionReason::Cancelled, "agent run cancelled");
    }

    emit_assistant_response(turn, turn_response);
    if(auto terminal = handle_stuck_detection(turn, turn_response); terminal.has_value()) {
      return terminal;
    }
    if(turn_response.stuck_injected) {
      return std::nullopt;
    }

    if(turn_response.tool_calls.empty()) {
      return handle_assistant_completion(turn, turn_response.assistant_text);
    }

    append_session_message(
        session_,
        id_counter_,
        "assistant",
        turn_response.assistant_text,
        nlohmann::json(turn_response.tool_calls)
    );
    emit_checkpoint(turn, "checkpoint after assistant tool call response");
    if(!execute_tool_calls(turn, turn_response.tool_calls)) {
      return complete(AgentCompletionReason::Cancelled, "agent run cancelled");
    }
    return std::nullopt;
  }

  [[nodiscard]] ModelTurnResponse request_model_turn(
      std::size_t turn,
      const std::vector<ava::llm::ChatMessage>& chat_messages
  ) {
    if(input_.stream) {
      return request_streaming_turn(turn, chat_messages);
    }
    return request_non_streaming_turn(chat_messages);
  }

  [[nodiscard]] ModelTurnResponse request_non_streaming_turn(
      const std::vector<ava::llm::ChatMessage>& chat_messages
  ) {
    const auto response = provider_.generate(chat_messages, tool_definitions_, config_.thinking);
    ModelTurnResponse turn_response{
        .assistant_text = response.content,
        .tool_calls = response::coalesce_tool_calls(response),
    };
    observe_usage(response.usage.has_value()
                      ? response.usage
                      : std::optional{estimate_turn_usage(chat_messages, turn_response.assistant_text, turn_response.tool_calls, provider_)});
    return turn_response;
  }

  [[nodiscard]] ModelTurnResponse request_streaming_turn(
      std::size_t turn,
      const std::vector<ava::llm::ChatMessage>& chat_messages
  ) {
    ModelTurnResponse turn_response;
    std::vector<ava::agent::response::ToolCallAccumulator> tool_call_accumulators;
    bool cancelled_during_stream = false;
    std::optional<ava::types::TokenUsage> stream_turn_usage;

    const auto stream_result = provider_.stream_generate(
        chat_messages,
        tool_definitions_,
        config_.thinking,
        [&](const ava::types::StreamChunk& chunk) {
          if(cancelled()) {
            cancelled_during_stream = true;
            return false;
          }
          if(chunk.content.has_value() && !chunk.content->empty()) {
            turn_response.assistant_text += *chunk.content;
            turn_response.emitted_stream_deltas = true;
            emit(AgentEvent{.kind = AgentEventKind::AssistantResponseDelta, .turn = turn, .message = *chunk.content});
          }
          if(chunk.tool_call.has_value()) {
            response::accumulate_tool_call(tool_call_accumulators, *chunk.tool_call);
          }
          if(chunk.usage.has_value()) {
            if(stream_turn_usage.has_value()) {
              merge_stream_usage(*stream_turn_usage, *chunk.usage);
            } else {
              stream_turn_usage = chunk.usage;
            }
          }
          return true;
        }
    );

    if(stream_result == ava::llm::Provider::StreamDispatchResult::Unsupported) {
      if(cancelled()) {
        persist_partial_assistant_response(turn, turn_response.assistant_text, {});
        turn_response.cancelled = true;
        return turn_response;
      }

      const auto response = provider_.generate(chat_messages, tool_definitions_, config_.thinking);
      turn_response.assistant_text = response.content;
      turn_response.tool_calls = response::coalesce_tool_calls(response);
      observe_usage(response.usage.has_value()
                        ? response.usage
                        : std::optional{estimate_turn_usage(chat_messages, turn_response.assistant_text, turn_response.tool_calls, provider_)});
      return turn_response;
    }

    turn_response.tool_calls = response::finalize_tool_calls(std::move(tool_call_accumulators));
    if(cancelled_during_stream) {
      persist_partial_assistant_response(turn, turn_response.assistant_text, turn_response.tool_calls);
      turn_response.cancelled = true;
      return turn_response;
    }

    if(turn_response.tool_calls.empty()) {
      turn_response.tool_calls = response::parse_tool_calls_from_content(turn_response.assistant_text);
    }
    observe_usage(stream_turn_usage.has_value()
                      ? stream_turn_usage
                      : std::optional{estimate_turn_usage(chat_messages, turn_response.assistant_text, turn_response.tool_calls, provider_)});
    return turn_response;
  }

  void drain_steering_messages(std::size_t turn) {
    if(input_.queue == nullptr || !input_.queue->has_steering()) {
      return;
    }
    for(auto& steering_message : input_.queue->drain_steering()) {
      append_session_message(session_, id_counter_, "user", steering_message);
      emit_checkpoint(turn, "checkpoint after steering message");
    }
  }

  void compact_if_needed(std::size_t turn) {
    if(!config_.auto_compact || config_.max_context_tokens == 0) {
      return;
    }
    const auto estimate = provider_.estimate_tokens(system_prompt_) + estimate_session_tokens(session_, provider_);
    const auto threshold = static_cast<double>(config_.max_context_tokens) * config_.compaction_threshold;
    if(static_cast<double>(estimate) <= threshold) {
      return;
    }
    auto [count, tokens] = compact_session_for_agent(session_, provider_, config_.preserve_recent_messages);
    if(count == 0) {
      return;
    }
    emit(AgentEvent{
        .kind = AgentEventKind::ContextCompacted,
        .turn = turn,
        .message = "context compacted",
        .compacted_message_count = count,
        .compacted_token_estimate = tokens,
    });
    emit_checkpoint(turn, "checkpoint after context compaction");
  }

  void emit_assistant_response(std::size_t turn, const ModelTurnResponse& turn_response) {
    if(!turn_response.assistant_text.empty()) {
      emit(AgentEvent{.kind = AgentEventKind::AssistantResponse,
                      .turn = turn,
                      .message = turn_response.assistant_text,
                      .replays_stream_deltas = turn_response.emitted_stream_deltas});
    }
  }

  [[nodiscard]] std::optional<AgentRunResult> handle_stuck_detection(
      std::size_t turn,
      ModelTurnResponse& turn_response
  ) {
    const auto stuck_action = config_.enable_stuck_detector
                                  ? stuck_.check(turn_response.assistant_text, turn_response.tool_calls)
                                  : StuckAction{};
    if(stuck_action.kind == StuckActionKind::InjectMessage) {
      append_session_message(session_, id_counter_, "assistant", turn_response.assistant_text);
      append_session_message(session_, id_counter_, "user", stuck_action.message);
      emit_checkpoint(turn, "checkpoint after stuck-detector injection");
      turn_response.stuck_injected = true;
      return std::nullopt;
    }
    if(stuck_action.kind == StuckActionKind::Stop) {
      append_session_message(
          session_,
          id_counter_,
          "assistant",
          turn_response.assistant_text,
          nlohmann::json(turn_response.tool_calls)
      );
      emit_checkpoint(turn, "checkpoint after stuck-detector stop");
      emit(AgentEvent{.kind = AgentEventKind::Error, .turn = turn, .message = stuck_action.message});
      return complete(AgentCompletionReason::Stuck, stuck_action.message);
    }
    return std::nullopt;
  }

  [[nodiscard]] std::optional<AgentRunResult> handle_assistant_completion(std::size_t turn, std::string assistant_text) {
    append_session_message(session_, id_counter_, "assistant", assistant_text);
    emit_checkpoint(turn, "checkpoint after assistant response");
    if(budget_.exhausted()) {
      record_skipped_queue();
      emit_budget_exhaustion_warning();
      return complete(AgentCompletionReason::BudgetExceeded, "agent stopped because the budget was exhausted");
    }
    if(append_promoted_queue_messages(
           session_,
           input_.queue,
           id_counter_,
           result_.turns_used,
           [this](std::size_t checkpoint_turn, std::string message) {
             emit_checkpoint(checkpoint_turn, std::move(message));
           }
       )) {
      return std::nullopt;
    }
    return complete(AgentCompletionReason::Completed, std::move(assistant_text));
  }

  [[nodiscard]] bool execute_tool_calls(std::size_t turn, const std::vector<ava::types::ToolCall>& tool_calls) {
    for(const auto& tool_call : tool_calls) {
      if(cancelled()) {
        return false;
      }

      emit(AgentEvent{.kind = AgentEventKind::ToolCall, .turn = turn, .tool_call = tool_call});
      auto tool_result = execute_tool_call(tool_call);
      append_session_message(
          session_,
          id_counter_,
          "tool",
          encode_tool_result_message(tool_result),
          nlohmann::json::array(),
          nlohmann::json::array({tool_result}),
          tool_result.call_id
      );
      emit_checkpoint(turn, tool_result.is_error ? "checkpoint after tool error result" : "checkpoint after tool result");
      emit(AgentEvent{.kind = AgentEventKind::ToolResult, .turn = turn, .tool_result = tool_result});
    }
    return true;
  }

  [[nodiscard]] ava::types::ToolResult execute_tool_call(const ava::types::ToolCall& tool_call) const {
    try {
      return tools_.execute(tool_call);
    } catch(const std::exception& ex) {
      return ava::types::ToolResult{.call_id = tool_call.id, .content = ex.what(), .is_error = true};
    }
  }

  void persist_partial_assistant_response(
      std::size_t turn,
      const std::string& assistant_text,
      const std::vector<ava::types::ToolCall>& tool_calls
  ) {
    if(assistant_text.empty() && tool_calls.empty()) {
      return;
    }
    append_session_message(session_, id_counter_, "assistant", assistant_text, nlohmann::json(tool_calls));
    emit_checkpoint(turn, "checkpoint after partial assistant response");
  }

  [[nodiscard]] AgentRunResult complete(AgentCompletionReason reason, std::string message) {
    if(reason != AgentCompletionReason::Completed && !queue_skips_recorded_ && input_.queue != nullptr) {
      record_unprocessed_queue_items(input_.queue, budget_);
      queue_skips_recorded_ = true;
    }
    result_.reason = reason;
    result_.final_response = std::move(message);
    result_.usage = budget_.usage();
    persist_completion_metadata(session_, reason, result_.turns_used, budget_);
    emit(AgentEvent{
        .kind = AgentEventKind::Completion,
        .turn = result_.turns_used,
        .message = result_.final_response,
        .completion_reason = reason,
    });
    return result_;
  }

  void observe_usage(const std::optional<ava::types::TokenUsage>& usage) {
    if(!usage.has_value()) {
      return;
    }
    const auto cost = provider_.estimate_cost(usage->input_tokens, usage->output_tokens);
    emit(AgentEvent{
        .kind = AgentEventKind::TokenUsage,
        .turn = result_.turns_used,
        .message = "token usage",
        .token_usage = *usage,
        .token_cost_usd = cost,
    });
    for(const auto& warning : budget_.observe(*usage, cost)) {
      emit(AgentEvent{
          .kind = AgentEventKind::BudgetWarning,
          .turn = result_.turns_used,
          .message = "budget warning",
          .budget_warning = warning,
      });
    }
    const auto accumulated_usage = budget_.usage();
    result_.usage = accumulated_usage;
    session_.token_usage = nlohmann::json{
        {"input_tokens", accumulated_usage.input_tokens},
        {"output_tokens", accumulated_usage.output_tokens},
        {"cache_read_tokens", accumulated_usage.cache_read_tokens},
        {"cache_creation_tokens", accumulated_usage.cache_creation_tokens},
    };
  }

  void emit_budget_exhaustion_warning() {
    for(const auto& warning : budget_.exhaustion_warning()) {
      emit(AgentEvent{
          .kind = AgentEventKind::BudgetWarning,
          .turn = result_.turns_used,
          .message = "budget exhausted",
          .budget_warning = warning,
      });
    }
  }

  void record_skipped_queue() {
    if(queue_skips_recorded_) {
      return;
    }
    record_unprocessed_queue_items(input_.queue, budget_);
    queue_skips_recorded_ = true;
  }

  void emit_checkpoint(std::size_t turn, std::string message) {
    emit(AgentEvent{.kind = AgentEventKind::Checkpoint, .turn = turn, .message = std::move(message)});
  }

  void emit(AgentEvent event) {
    event.run_id = run_id_;
    emit_event(on_event_, std::move(event));
  }

  [[nodiscard]] bool cancelled() const {
    return input_.is_cancelled && input_.is_cancelled();
  }

  const ava::llm::Provider& provider_;
  const ava::tools::ToolRegistry& tools_;
  const AgentConfig& config_;
  ava::types::SessionRecord& session_;
  const AgentRunInput& input_;
  AgentEventSink on_event_;
  std::optional<std::string> run_id_;
  StuckDetector stuck_;
  BudgetTracker budget_;
  AgentRunResult result_{};
  std::string system_prompt_;
  std::vector<ava::types::Tool> tool_definitions_;
  std::uint64_t id_counter_{0};
  bool queue_skips_recorded_{false};
};

}  // namespace

AgentRunResult run_agent_loop(
    const ava::llm::Provider& provider,
    const ava::tools::ToolRegistry& tools,
    const AgentConfig& config,
    ava::types::SessionRecord& session,
    const AgentRunInput& input,
    AgentEventSink on_event,
    std::string system_prompt
) {
  AgentRunLoop loop(provider, tools, config, session, input, std::move(on_event), std::move(system_prompt));
  return loop.run();
}

}  // namespace ava::agent
