#include <catch2/catch_test_macros.hpp>

#include <array>
#include <span>
#include <stdexcept>
#include <string_view>
#include <vector>

#include "ava/control_plane/control_plane.hpp"

namespace {

template <typename Span, std::size_t Size>
void require_fields(Span actual, const std::array<ava::control_plane::CanonicalEventField, Size>& expected) {
  REQUIRE(actual.size() == expected.size());
  for(std::size_t index = 0; index < expected.size(); ++index) {
    REQUIRE(actual[index] == expected[index]);
  }
}

void require_field_vector(
    std::span<const ava::control_plane::CanonicalEventField> actual,
    const std::vector<ava::control_plane::CanonicalEventField>& expected
) {
  REQUIRE(actual.size() == expected.size());
  for(std::size_t index = 0; index < expected.size(); ++index) {
    REQUIRE(actual[index] == expected[index]);
  }
}

template <typename Span, std::size_t Size>
void require_signals(Span actual, const std::array<ava::control_plane::TerminalClosureSignal, Size>& expected) {
  REQUIRE(actual.size() == expected.size());
  for(std::size_t index = 0; index < expected.size(); ++index) {
    REQUIRE(actual[index] == expected[index]);
  }
}

void require_signal_vector(
    std::span<const ava::control_plane::TerminalClosureSignal> actual,
    const std::vector<ava::control_plane::TerminalClosureSignal>& expected
) {
  REQUIRE(actual.size() == expected.size());
  for(std::size_t index = 0; index < expected.size(); ++index) {
    REQUIRE(actual[index] == expected[index]);
  }
}

void require_correlation_vector(
    std::span<const ava::control_plane::CorrelationIdKey> actual,
    const std::vector<ava::control_plane::CorrelationIdKey>& expected
) {
  REQUIRE(actual.size() == expected.size());
  for(std::size_t index = 0; index < expected.size(); ++index) {
    REQUIRE(actual[index] == expected[index]);
  }
}

struct ExpectedCommandSpec {
  ava::control_plane::ControlPlaneCommand command;
  std::string_view name;
  ava::control_plane::CommandFamily family;
  ava::control_plane::ResponseEnvelope response_envelope;
  ava::control_plane::CompletionMode completion_mode;
  std::vector<ava::control_plane::TerminalClosureSignal> terminal_signals;
  std::vector<ava::control_plane::CorrelationIdKey> accepted_response_ids;
  std::vector<ava::control_plane::CorrelationIdKey> lifecycle_ids;
};

struct ExpectedEventSpec {
  ava::control_plane::CanonicalEventKind kind;
  std::string_view type_tag;
  std::vector<ava::control_plane::CanonicalEventField> required_fields;
};

}  // namespace

TEST_CASE("command inventory count matches frozen contract", "[ava_control_plane]") {
  const auto specs = ava::control_plane::canonical_command_specs();
  REQUIRE(specs.size() == 13);

  constexpr std::array<std::string_view, 13> kExpectedNames{
      "submit_goal",
      "cancel_agent",
      "retry_last_message",
      "edit_and_resend",
      "regenerate_response",
      "resolve_approval",
      "resolve_question",
      "resolve_plan",
      "steer_agent",
      "follow_up_agent",
      "post_complete_agent",
      "clear_message_queue",
      "list_agent_tools",
  };

  for(std::size_t index = 0; index < kExpectedNames.size(); ++index) {
    REQUIRE(specs[index].name == kExpectedNames[index]);
  }
}

TEST_CASE("full command spec table matches frozen contract", "[ava_control_plane]") {
  using enum ava::control_plane::CommandFamily;
  using enum ava::control_plane::CompletionMode;
  using enum ava::control_plane::ControlPlaneCommand;
  using enum ava::control_plane::CorrelationIdKey;
  namespace cp = ava::control_plane;

  const std::vector<ExpectedCommandSpec> expected{
      {SubmitGoal, "submit_goal", GoalSubmission, cp::ResponseEnvelope::AcceptedRunHandle, AcceptedAndStreaming, {cp::TerminalClosureSignal::CompleteEvent, cp::TerminalClosureSignal::ErrorEvent}, {SessionId}, {SessionId}},
      {CancelAgent, "cancel_agent", Cancellation, cp::ResponseEnvelope::Ack, FireAndForget, {cp::TerminalClosureSignal::None}, {}, {}},
      {RetryLastMessage, "retry_last_message", RetryAndReplay, cp::ResponseEnvelope::AcceptedRunHandle, AcceptedAndStreaming, {cp::TerminalClosureSignal::CompleteEvent, cp::TerminalClosureSignal::ErrorEvent}, {SessionId}, {SessionId}},
      {EditAndResend, "edit_and_resend", RetryAndReplay, cp::ResponseEnvelope::AcceptedRunHandle, AcceptedAndStreaming, {cp::TerminalClosureSignal::CompleteEvent, cp::TerminalClosureSignal::ErrorEvent}, {SessionId}, {SessionId}},
      {RegenerateResponse, "regenerate_response", RetryAndReplay, cp::ResponseEnvelope::AcceptedRunHandle, AcceptedAndStreaming, {cp::TerminalClosureSignal::CompleteEvent, cp::TerminalClosureSignal::ErrorEvent}, {SessionId}, {SessionId}},
      {ResolveApproval, "resolve_approval", InteractiveResolution, cp::ResponseEnvelope::Ack, CompletionBound, {cp::TerminalClosureSignal::DirectResult, cp::TerminalClosureSignal::InteractiveResolved}, {}, {InteractiveRequestId}},
      {ResolveQuestion, "resolve_question", InteractiveResolution, cp::ResponseEnvelope::Ack, CompletionBound, {cp::TerminalClosureSignal::DirectResult, cp::TerminalClosureSignal::InteractiveResolved}, {}, {InteractiveRequestId}},
      {ResolvePlan, "resolve_plan", InteractiveResolution, cp::ResponseEnvelope::Ack, CompletionBound, {cp::TerminalClosureSignal::DirectResult, cp::TerminalClosureSignal::InteractiveResolved}, {}, {InteractiveRequestId}},
      {SteerAgent, "steer_agent", QueueDispatch, cp::ResponseEnvelope::Ack, AcceptedAndStreaming, {cp::TerminalClosureSignal::CompleteEvent, cp::TerminalClosureSignal::ErrorEvent}, {}, {}},
      {FollowUpAgent, "follow_up_agent", QueueDispatch, cp::ResponseEnvelope::Ack, AcceptedAndStreaming, {cp::TerminalClosureSignal::CompleteEvent, cp::TerminalClosureSignal::ErrorEvent}, {}, {}},
      {PostCompleteAgent, "post_complete_agent", QueueDispatch, cp::ResponseEnvelope::Ack, AcceptedAndStreaming, {cp::TerminalClosureSignal::CompleteEvent, cp::TerminalClosureSignal::ErrorEvent}, {}, {}},
      {ClearMessageQueue, "clear_message_queue", QueueControl, cp::ResponseEnvelope::Ack, FireAndForget, {cp::TerminalClosureSignal::None}, {}, {}},
      {ListAgentTools, "list_agent_tools", ToolIntrospection, cp::ResponseEnvelope::ToolList, CompletionBound, {cp::TerminalClosureSignal::DirectResult}, {}, {}},
  };

  const auto specs = ava::control_plane::canonical_command_specs();
  REQUIRE(specs.size() == expected.size());
  for(std::size_t index = 0; index < expected.size(); ++index) {
    const auto& spec = specs[index];
    const auto& want = expected[index];
    REQUIRE(spec.command == want.command);
    REQUIRE(spec.name == want.name);
    REQUIRE(spec.family == want.family);
    REQUIRE(spec.response_envelope == want.response_envelope);
    REQUIRE(spec.completion_mode == want.completion_mode);
    require_signal_vector(spec.terminal_signals, want.terminal_signals);
    require_correlation_vector(spec.correlation_ids.accepted_response, want.accepted_response_ids);
    require_correlation_vector(spec.correlation_ids.lifecycle, want.lifecycle_ids);
  }
}

TEST_CASE("command specs freeze response modes and correlation contracts", "[ava_control_plane]") {
  using enum ava::control_plane::ControlPlaneCommand;
  using enum ava::control_plane::CompletionMode;
  using enum ava::control_plane::CorrelationIdKey;
  using enum ava::control_plane::TerminalClosureSignal;

  const auto& submit = ava::control_plane::command_spec(SubmitGoal);
  REQUIRE(submit.completion_mode == AcceptedAndStreaming);
  require_signals(submit.terminal_signals, std::array{CompleteEvent, ErrorEvent});
  REQUIRE(submit.correlation_ids.accepted_response.size() == 1);
  REQUIRE(submit.correlation_ids.accepted_response[0] == SessionId);
  REQUIRE(submit.correlation_ids.lifecycle.size() == 1);
  REQUIRE(submit.correlation_ids.lifecycle[0] == SessionId);

  const auto& resolve = ava::control_plane::command_spec(ResolveApproval);
  REQUIRE(resolve.completion_mode == CompletionBound);
  require_signals(resolve.terminal_signals, std::array{DirectResult, InteractiveResolved});
  REQUIRE(resolve.correlation_ids.accepted_response.empty());
  REQUIRE(resolve.correlation_ids.lifecycle.size() == 1);
  REQUIRE(resolve.correlation_ids.lifecycle[0] == InteractiveRequestId);

  REQUIRE(ava::control_plane::command_spec(CancelAgent).completion_mode == FireAndForget);
  REQUIRE(ava::control_plane::command_spec(ClearMessageQueue).completion_mode == FireAndForget);
  REQUIRE(ava::control_plane::command_spec(ListAgentTools).completion_mode == CompletionBound);
}

TEST_CASE("command lookup by name exposes submit_goal contract", "[ava_control_plane]") {
  const auto* submit_goal = ava::control_plane::command_spec_by_name("submit_goal");
  REQUIRE(submit_goal != nullptr);
  REQUIRE(submit_goal->command == ava::control_plane::ControlPlaneCommand::SubmitGoal);
  REQUIRE(submit_goal->completion_mode == ava::control_plane::CompletionMode::AcceptedAndStreaming);
  REQUIRE(submit_goal->correlation_ids.accepted_response.size() == 1);
  REQUIRE(
      submit_goal->correlation_ids.accepted_response[0] == ava::control_plane::CorrelationIdKey::SessionId
  );
}

TEST_CASE("event inventory and required fields match canonical table", "[ava_control_plane]") {
  const auto specs = ava::control_plane::canonical_event_specs();
  REQUIRE(specs.size() == 10);

  constexpr std::array<std::string_view, 10> kExpectedTags{
      "approval_request",
      "question_request",
      "plan_request",
      "interactive_request_cleared",
      "plan_created",
      "plan_step_complete",
      "complete",
      "error",
      "subagent_complete",
      "streaming_edit_progress",
  };

  for(std::size_t index = 0; index < kExpectedTags.size(); ++index) {
    REQUIRE(ava::control_plane::canonical_event_kind_to_type_tag(specs[index].kind) == kExpectedTags[index]);
  }

  using enum ava::control_plane::CanonicalEventField;
  using enum ava::control_plane::CanonicalEventKind;
  const std::vector<ExpectedEventSpec> expected{
      {ApprovalRequest, "approval_request", {RunId, Id, ToolCallId, ToolName, Args, RiskLevel, Reason, Warnings}},
      {QuestionRequest, "question_request", {RunId, Id, Question, Options}},
      {PlanRequest, "plan_request", {RunId, Id, Plan}},
      {InteractiveRequestCleared, "interactive_request_cleared", {RunId, RequestId, RequestKind}},
      {PlanCreated, "plan_created", {RunId, Id, Plan}},
      {PlanStepComplete, "plan_step_complete", {RunId, StepId}},
      {Complete, "complete", {RunId, Session}},
      {Error, "error", {RunId, Message}},
      {SubagentComplete, "subagent_complete", {RunId, CallId, SessionId, Description}},
      {StreamingEditProgress, "streaming_edit_progress", {RunId, CallId, ToolName, BytesReceived}},
  };

  REQUIRE(specs.size() == expected.size());
  for(std::size_t index = 0; index < expected.size(); ++index) {
    REQUIRE(specs[index].kind == expected[index].kind);
    REQUIRE(ava::control_plane::canonical_event_kind_to_type_tag(specs[index].kind) == expected[index].type_tag);
    require_field_vector(specs[index].required_fields, expected[index].required_fields);
  }

  const auto* approval = ava::control_plane::canonical_event_spec("approval_request");
  REQUIRE(approval != nullptr);
  require_fields(
      approval->required_fields,
      std::array{
          ava::control_plane::CanonicalEventField::RunId,
          ava::control_plane::CanonicalEventField::Id,
          ava::control_plane::CanonicalEventField::ToolCallId,
          ava::control_plane::CanonicalEventField::ToolName,
          ava::control_plane::CanonicalEventField::Args,
          ava::control_plane::CanonicalEventField::RiskLevel,
          ava::control_plane::CanonicalEventField::Reason,
          ava::control_plane::CanonicalEventField::Warnings,
      }
  );

  const auto* plan_request = ava::control_plane::canonical_event_spec("plan_request");
  REQUIRE(plan_request != nullptr);
  require_fields(
      plan_request->required_fields,
      std::array{
          ava::control_plane::CanonicalEventField::RunId,
          ava::control_plane::CanonicalEventField::Id,
          ava::control_plane::CanonicalEventField::Plan,
      }
  );

  const auto* subagent = ava::control_plane::canonical_event_spec("subagent_complete");
  REQUIRE(subagent != nullptr);
  require_fields(
      subagent->required_fields,
      std::array{
          ava::control_plane::CanonicalEventField::RunId,
          ava::control_plane::CanonicalEventField::CallId,
          ava::control_plane::CanonicalEventField::SessionId,
          ava::control_plane::CanonicalEventField::Description,
      }
  );

  const auto required = ava::control_plane::required_backend_event_kinds();
  REQUIRE(required.size() == 5);
  REQUIRE(required[0] == ava::control_plane::CanonicalEventKind::PlanStepComplete);
  REQUIRE(required[1] == ava::control_plane::CanonicalEventKind::Complete);
  REQUIRE(required[2] == ava::control_plane::CanonicalEventKind::Error);
  REQUIRE(required[3] == ava::control_plane::CanonicalEventKind::SubagentComplete);
  REQUIRE(required[4] == ava::control_plane::CanonicalEventKind::StreamingEditProgress);

  REQUIRE_THROWS_AS(
      ava::control_plane::canonical_event_kind_to_type_tag(static_cast<ava::control_plane::CanonicalEventKind>(999)),
      std::invalid_argument
  );
  REQUIRE_THROWS_AS(
      ava::control_plane::canonical_event_field_to_json_key(static_cast<ava::control_plane::CanonicalEventField>(999)),
      std::invalid_argument
  );
}

TEST_CASE("queue command and tier conversion are wired", "[ava_control_plane]") {
  const auto command = ava::control_plane::queue_command_from_tier(ava::types::MessageTier::post_complete(3));
  REQUIRE(command == ava::control_plane::ControlPlaneCommand::PostCompleteAgent);

  const auto tier = ava::control_plane::queue_message_tier(command, 7);
  REQUIRE(tier.has_value());
  REQUIRE(tier->kind == ava::types::MessageTierKind::PostComplete);
  REQUIRE(tier->post_complete_group == 7);

  REQUIRE(ava::control_plane::queue_command_label(ava::control_plane::ControlPlaneCommand::FollowUpAgent)
          == std::optional<std::string_view>{"follow-up"});
  REQUIRE(
      ava::control_plane::queue_command_from_alias("post_complete_agent")
      == std::optional<ava::control_plane::ControlPlaneCommand>{ava::control_plane::ControlPlaneCommand::PostCompleteAgent}
  );
  REQUIRE_FALSE(ava::control_plane::queue_command_from_alias("unknown").has_value());
}

TEST_CASE("clear queue target parsing and semantics follow contract", "[ava_control_plane]") {
  REQUIRE(ava::control_plane::parse_clear_queue_target("all") == std::optional{ava::control_plane::ClearQueueTarget::All});
  REQUIRE(
      ava::control_plane::parse_clear_queue_target("follow-up")
      == std::optional{ava::control_plane::ClearQueueTarget::FollowUp}
  );
  REQUIRE(
      ava::control_plane::parse_clear_queue_target("followup")
      == std::optional{ava::control_plane::ClearQueueTarget::FollowUp}
  );
  REQUIRE(
      ava::control_plane::parse_clear_queue_target("post_complete")
      == std::optional{ava::control_plane::ClearQueueTarget::PostComplete}
  );
  REQUIRE(
      ava::control_plane::parse_clear_queue_target("postcomplete")
      == std::optional{ava::control_plane::ClearQueueTarget::PostComplete}
  );
  REQUIRE_FALSE(ava::control_plane::parse_clear_queue_target("nope").has_value());

  REQUIRE(
      ava::control_plane::clear_queue_semantics(ava::control_plane::ClearQueueTarget::All)
      == ava::control_plane::QueueClearSemantics::CancelRunAndClearSteering
  );
  REQUIRE(
      ava::control_plane::clear_queue_semantics(ava::control_plane::ClearQueueTarget::Steering)
      == ava::control_plane::QueueClearSemantics::CancelRunAndClearSteering
  );
  REQUIRE(
      ava::control_plane::clear_queue_semantics(ava::control_plane::ClearQueueTarget::FollowUp)
      == ava::control_plane::QueueClearSemantics::Unsupported
  );
  REQUIRE(
      ava::control_plane::clear_queue_semantics(ava::control_plane::ClearQueueTarget::PostComplete)
      == ava::control_plane::QueueClearSemantics::Unsupported
  );
}

TEST_CASE("deferred queue session ownership checks requested and active ids", "[ava_control_plane]") {
  ava::control_plane::DeferredQueueSessionError error;

  const auto active = std::optional<std::string>{"session-a"};
  REQUIRE(ava::control_plane::resolve_deferred_queue_session(active, active, &error) == active);
  REQUIRE(ava::control_plane::resolve_deferred_queue_session(std::nullopt, active, &error) == active);

  REQUIRE_FALSE(
      ava::control_plane::resolve_deferred_queue_session(std::optional<std::string>{"session-b"}, active, &error)
          .has_value()
  );
  REQUIRE(error.kind == ava::control_plane::DeferredQueueSessionErrorKind::SessionMismatch);

  REQUIRE_FALSE(
      ava::control_plane::resolve_deferred_queue_session(std::optional<std::string>{"session-a"}, std::nullopt, &error)
          .has_value()
  );
  REQUIRE(error.kind == ava::control_plane::DeferredQueueSessionErrorKind::MissingActiveSession);

  REQUIRE_FALSE(
      ava::control_plane::resolve_deferred_queue_session(std::optional<std::string>{"session-b"}, active, nullptr)
          .has_value()
  );
  REQUIRE_FALSE(ava::control_plane::resolve_deferred_queue_session(std::nullopt, std::nullopt, nullptr).has_value());
}

TEST_CASE("interactive request string conversions reject unknown values", "[ava_control_plane]") {
  REQUIRE(ava::control_plane::interactive_request_kind_to_string(ava::control_plane::InteractiveRequestKind::Approval) == "approval");
  REQUIRE(ava::control_plane::interactive_request_kind_to_string(ava::control_plane::InteractiveRequestKind::Question) == "question");
  REQUIRE(ava::control_plane::interactive_request_kind_to_string(ava::control_plane::InteractiveRequestKind::Plan) == "plan");

  REQUIRE(ava::control_plane::interactive_request_state_to_string(ava::control_plane::InteractiveRequestState::Pending) == "pending");
  REQUIRE(ava::control_plane::interactive_request_state_to_string(ava::control_plane::InteractiveRequestState::Resolved) == "resolved");
  REQUIRE(ava::control_plane::interactive_request_state_to_string(ava::control_plane::InteractiveRequestState::Cancelled) == "cancelled");
  REQUIRE(ava::control_plane::interactive_request_state_to_string(ava::control_plane::InteractiveRequestState::TimedOut) == "timeout");

  REQUIRE_THROWS_AS(
      ava::control_plane::interactive_request_kind_to_string(
          static_cast<ava::control_plane::InteractiveRequestKind>(999)
      ),
      std::invalid_argument
  );
  REQUIRE_THROWS_AS(
      ava::control_plane::interactive_request_state_to_string(
          static_cast<ava::control_plane::InteractiveRequestState>(999)
      ),
      std::invalid_argument
  );
}

TEST_CASE("session precedence and replay payload helpers use latest user turn", "[ava_control_plane]") {
  const std::vector<ava::types::Message> messages{
      ava::types::Message{.id = "s", .role = ava::types::Role::System, .content = "sys", .timestamp = "t0"},
      ava::types::Message{.id = "u1", .role = ava::types::Role::User, .content = "first", .timestamp = "t1"},
      ava::types::Message{.id = "a1", .role = ava::types::Role::Assistant, .content = "reply", .timestamp = "t2"},
      ava::types::Message{
          .id = "u2",
          .role = ava::types::Role::User,
          .content = "latest",
          .timestamp = "t3",
          .images = {ava::types::ImageContent{.data = "img", .media_type = "image/png"}},
      },
  };

  const auto existing = ava::control_plane::resolve_existing_session("requested", "last");
  REQUIRE(existing.has_value());
  REQUIRE(existing->session_id == "requested");
  REQUIRE(existing->source == ava::control_plane::SessionSelectionSource::Requested);

  const auto fallback_to_last = ava::control_plane::resolve_existing_session(std::optional<std::string>{""}, "last");
  REQUIRE(fallback_to_last.has_value());
  REQUIRE(fallback_to_last->session_id == "last");
  REQUIRE(fallback_to_last->source == ava::control_plane::SessionSelectionSource::LastActive);

  REQUIRE_FALSE(
      ava::control_plane::resolve_existing_session(std::optional<std::string>{""}, std::optional<std::string>{""})
          .has_value()
  );

  const auto selected = ava::control_plane::resolve_session_precedence(std::nullopt, std::nullopt, "new-session");
  REQUIRE(selected.session_id == "new-session");
  REQUIRE(selected.source == ava::control_plane::SessionSelectionSource::New);

  const auto prompt = ava::control_plane::load_prompt_context(messages);
  REQUIRE(prompt.goal == "latest");
  REQUIRE(prompt.history.size() == 3);
  REQUIRE(prompt.images.size() == 1);

  const auto retry = ava::control_plane::build_retry_replay_payload(messages);
  REQUIRE(retry.payload.has_value());
  REQUIRE(retry.payload->goal == "latest");

  const auto edit = ava::control_plane::build_edit_replay_payload(messages, std::optional<std::string>{"u1"}, "edited");
  REQUIRE(edit.payload.has_value());
  REQUIRE(edit.payload->goal == "edited");
  REQUIRE(edit.payload->history.size() == 1);

  const auto regenerate = ava::control_plane::build_regenerate_replay_payload(messages);
  REQUIRE(regenerate.payload.has_value());
  REQUIRE(regenerate.payload->goal == retry.payload->goal);
}

TEST_CASE("session replay helpers report canonical error cases", "[ava_control_plane]") {
  const std::vector<ava::types::Message> messages{
      ava::types::Message{.id = "a1", .role = ava::types::Role::Assistant, .content = "reply", .timestamp = "t1"},
  };

  const auto retry = ava::control_plane::build_retry_replay_payload(messages);
  REQUIRE_FALSE(retry.payload.has_value());
  REQUIRE(retry.error == ava::control_plane::SessionReplayPayloadError::MissingUserMessage);

  const auto missing_target = ava::control_plane::build_edit_replay_payload(messages, std::nullopt, "edited");
  REQUIRE(missing_target.error == ava::control_plane::SessionReplayPayloadError::InvalidEditTarget);

  const auto unknown = ava::control_plane::build_edit_replay_payload(messages, std::optional<std::string>{"missing"}, "edited");
  REQUIRE(unknown.error == ava::control_plane::SessionReplayPayloadError::MessageNotFound);

  const auto non_user = ava::control_plane::build_edit_replay_payload(messages, std::optional<std::string>{"a1"}, "edited");
  REQUIRE(non_user.error == ava::control_plane::SessionReplayPayloadError::NonUserEditTarget);

  const auto prompt = ava::control_plane::load_prompt_context(messages);
  REQUIRE(prompt.goal.empty());
  REQUIRE(prompt.history.empty());
  REQUIRE(prompt.images.empty());

  const auto history = ava::control_plane::collect_history_before_last_user(messages);
  REQUIRE(history.empty());
}

TEST_CASE("interactive request store tracks pending and terminal lifecycle", "[ava_control_plane]") {
  ava::control_plane::InteractiveRequestStore store(ava::control_plane::InteractiveRequestKind::Question);

  const auto first = store.register_request("run-a");
  const auto second = store.register_request("run-b");
  const auto third = store.register_request("run-c");
  const auto unscoped = store.register_request();

  REQUIRE(first.kind == ava::control_plane::InteractiveRequestKind::Question);
  REQUIRE(first.state == ava::control_plane::InteractiveRequestState::Pending);
  REQUIRE(first.run_id == std::optional<std::string>{"run-a"});
  REQUIRE(first.request_id == "question-1");
  REQUIRE(second.request_id == "question-2");
  REQUIRE(third.request_id == "question-3");
  REQUIRE(unscoped.request_id == "question-4");
  REQUIRE_FALSE(unscoped.run_id.has_value());

  const auto pending = store.pending_requests();
  REQUIRE(pending.size() == 4);
  REQUIRE(pending.at(0).request_id == first.request_id);
  REQUIRE(pending.at(1).request_id == second.request_id);
  REQUIRE(pending.at(2).request_id == third.request_id);
  REQUIRE(pending.at(3).request_id == unscoped.request_id);

  const auto resolved = store.resolve(first.request_id);
  REQUIRE(resolved.has_value());
  REQUIRE(resolved->state == ava::control_plane::InteractiveRequestState::Resolved);
  REQUIRE(resolved->run_id == std::optional<std::string>{"run-a"});

  const auto timed_out = store.timeout(second.request_id);
  REQUIRE(timed_out.has_value());
  REQUIRE(timed_out->state == ava::control_plane::InteractiveRequestState::TimedOut);
  REQUIRE(timed_out->run_id == std::optional<std::string>{"run-b"});

  const auto cancelled = store.cancel(third.request_id);
  REQUIRE(cancelled.has_value());
  REQUIRE(cancelled->state == ava::control_plane::InteractiveRequestState::Cancelled);
  REQUIRE(cancelled->run_id == std::optional<std::string>{"run-c"});

  const auto resolved_unscoped = store.resolve(unscoped.request_id);
  REQUIRE(resolved_unscoped.has_value());
  REQUIRE_FALSE(resolved_unscoped->run_id.has_value());

  REQUIRE_FALSE(store.current_pending().has_value());

  const auto by_id = store.request_by_id(second.request_id);
  REQUIRE(by_id.has_value());
  REQUIRE(by_id->state == ava::control_plane::InteractiveRequestState::TimedOut);
  REQUIRE(by_id->run_id == std::optional<std::string>{"run-b"});
}

TEST_CASE("interactive request store preserves pending order after all terminal transitions", "[ava_control_plane]") {
  auto require_middle_transition_preserves_order = [](auto transition) {
    ava::control_plane::InteractiveRequestStore store(ava::control_plane::InteractiveRequestKind::Plan);

    const auto first = store.register_request("run-a");
    const auto second = store.register_request("run-b");
    const auto third = store.register_request("run-c");

    const auto terminal = transition(store, second.request_id);
    REQUIRE(terminal.has_value());

    const auto current = store.current_pending();
    REQUIRE(current.has_value());
    REQUIRE(current->request_id == first.request_id);

    const auto pending = store.pending_requests();
    REQUIRE(pending.size() == 2);
    REQUIRE(pending.at(0).request_id == first.request_id);
    REQUIRE(pending.at(1).request_id == third.request_id);
  };

  require_middle_transition_preserves_order(
      [](ava::control_plane::InteractiveRequestStore& store, const std::string& id) { return store.resolve(id); }
  );
  require_middle_transition_preserves_order(
      [](ava::control_plane::InteractiveRequestStore& store, const std::string& id) { return store.cancel(id); }
  );
  require_middle_transition_preserves_order(
      [](ava::control_plane::InteractiveRequestStore& store, const std::string& id) { return store.timeout(id); }
  );
}

TEST_CASE("interactive request store preserves pending order after interleaved resolution", "[ava_control_plane]") {
  ava::control_plane::InteractiveRequestStore store(ava::control_plane::InteractiveRequestKind::Plan);

  const auto first = store.register_request("run-a");
  const auto second = store.register_request("run-b");
  const auto third = store.register_request("run-c");

  const auto resolved_middle = store.resolve(second.request_id);
  REQUIRE(resolved_middle.has_value());
  REQUIRE(resolved_middle->state == ava::control_plane::InteractiveRequestState::Resolved);

  const auto current = store.current_pending();
  REQUIRE(current.has_value());
  REQUIRE(current->request_id == first.request_id);

  const auto pending = store.pending_requests();
  REQUIRE(pending.size() == 2);
  REQUIRE(pending.at(0).request_id == first.request_id);
  REQUIRE(pending.at(1).request_id == third.request_id);

  REQUIRE(store.resolve(first.request_id).has_value());
  const auto next = store.current_pending();
  REQUIRE(next.has_value());
  REQUIRE(next->request_id == third.request_id);
}

TEST_CASE("interactive request store rejects stale and non-existent requests", "[ava_control_plane]") {
  ava::control_plane::InteractiveRequestStore store(ava::control_plane::InteractiveRequestKind::Approval);

  REQUIRE_FALSE(store.resolve("approval-missing").has_value());
  REQUIRE_FALSE(store.cancel("approval-missing").has_value());
  REQUIRE_FALSE(store.timeout("approval-missing").has_value());
  REQUIRE_FALSE(store.request_by_id("approval-missing").has_value());

  const auto first = store.register_request("run-a");
  const auto second = store.register_request("run-b");

  REQUIRE_FALSE(store.resolve("approval-stale").has_value());
  REQUIRE_FALSE(store.cancel("approval-stale").has_value());
  REQUIRE_FALSE(store.timeout("approval-stale").has_value());
  REQUIRE_FALSE(store.request_by_id("approval-stale").has_value());

  const auto pending_after_stale = store.pending_requests();
  REQUIRE(pending_after_stale.size() == 2);
  REQUIRE(pending_after_stale.at(0).request_id == first.request_id);
  REQUIRE(pending_after_stale.at(1).request_id == second.request_id);

  const auto resolved = store.resolve(first.request_id);
  REQUIRE(resolved.has_value());
  REQUIRE(resolved->state == ava::control_plane::InteractiveRequestState::Resolved);
  REQUIRE(resolved->request_id == first.request_id);
  REQUIRE(resolved->run_id == std::optional<std::string>{"run-a"});

  REQUIRE_FALSE(store.resolve(first.request_id).has_value());
  REQUIRE_FALSE(store.cancel(first.request_id).has_value());
  REQUIRE_FALSE(store.timeout(first.request_id).has_value());

  const auto terminal = store.request_by_id(first.request_id);
  REQUIRE(terminal.has_value());
  REQUIRE(terminal->request_id == first.request_id);
  REQUIRE(terminal->kind == ava::control_plane::InteractiveRequestKind::Approval);
  REQUIRE(terminal->state == ava::control_plane::InteractiveRequestState::Resolved);
  REQUIRE(terminal->run_id == std::optional<std::string>{"run-a"});

  const auto current = store.current_pending();
  REQUIRE(current.has_value());
  REQUIRE(current->request_id == second.request_id);
  REQUIRE(current->run_id == std::optional<std::string>{"run-b"});

  const auto cancelled = store.cancel(second.request_id);
  REQUIRE(cancelled.has_value());
  REQUIRE_FALSE(store.resolve(second.request_id).has_value());
  REQUIRE_FALSE(store.cancel(second.request_id).has_value());
  REQUIRE_FALSE(store.timeout(second.request_id).has_value());

  const auto third = store.register_request("run-c");
  const auto timed_out = store.timeout(third.request_id);
  REQUIRE(timed_out.has_value());
  REQUIRE_FALSE(store.resolve(third.request_id).has_value());
  REQUIRE_FALSE(store.cancel(third.request_id).has_value());
  REQUIRE_FALSE(store.timeout(third.request_id).has_value());
}

TEST_CASE("interactive request store bounds terminal retention deterministically", "[ava_control_plane]") {
  ava::control_plane::InteractiveRequestStore store(ava::control_plane::InteractiveRequestKind::Question);

  // Mirrors InteractiveRequestStore's private retention limit; this test locks the M3 contract.
  constexpr int kExpectedTerminalRetention = 64;
  constexpr int kOverflowCount = 6;
  std::vector<std::string> ids;
  ids.reserve(kExpectedTerminalRetention + kOverflowCount);
  for(int i = 0; i < kExpectedTerminalRetention + kOverflowCount; ++i) {
    const auto handle = store.register_request("run");
    ids.push_back(handle.request_id);
    const auto resolved = store.resolve(handle.request_id);
    REQUIRE(resolved.has_value());
  }

  REQUIRE_FALSE(store.request_by_id(ids.front()).has_value());
  REQUIRE_FALSE(store.request_by_id(ids[kOverflowCount - 1]).has_value());
  REQUIRE(store.request_by_id(ids[kOverflowCount]).has_value());
  REQUIRE(store.request_by_id(ids.back()).has_value());
}
