#include "ava/control_plane/commands.hpp"

#include <array>
#include <stdexcept>

namespace ava::control_plane {
namespace {

constexpr std::array<TerminalClosureSignal, 2> kCompleteOrError{
    TerminalClosureSignal::CompleteEvent,
    TerminalClosureSignal::ErrorEvent,
};
constexpr std::array<TerminalClosureSignal, 1> kDirectResultOnly{
    TerminalClosureSignal::DirectResult,
};
constexpr std::array<TerminalClosureSignal, 2> kDirectResultAndInteractiveResolved{
    TerminalClosureSignal::DirectResult,
    TerminalClosureSignal::InteractiveResolved,
};
constexpr std::array<TerminalClosureSignal, 1> kNoTerminalSignal{
    TerminalClosureSignal::None,
};

constexpr std::array<CorrelationIdKey, 0> kNoCorrelationIds{};
constexpr std::array<CorrelationIdKey, 1> kSessionIdOnly{CorrelationIdKey::SessionId};
constexpr std::array<CorrelationIdKey, 1> kInteractiveRequestIdOnly{CorrelationIdKey::InteractiveRequestId};

constexpr std::array<CommandSpec, 13> kCanonicalCommandSpecs{ {
    {ControlPlaneCommand::SubmitGoal,
     "submit_goal",
     CommandFamily::GoalSubmission,
     ResponseEnvelope::AcceptedRunHandle,
     CompletionMode::AcceptedAndStreaming,
     kCompleteOrError,
     {kSessionIdOnly, kSessionIdOnly}},
    {ControlPlaneCommand::CancelAgent,
     "cancel_agent",
     CommandFamily::Cancellation,
     ResponseEnvelope::Ack,
     CompletionMode::FireAndForget,
     kNoTerminalSignal,
     {kNoCorrelationIds, kNoCorrelationIds}},
    {ControlPlaneCommand::RetryLastMessage,
     "retry_last_message",
     CommandFamily::RetryAndReplay,
     ResponseEnvelope::AcceptedRunHandle,
     CompletionMode::AcceptedAndStreaming,
     kCompleteOrError,
     {kSessionIdOnly, kSessionIdOnly}},
    {ControlPlaneCommand::EditAndResend,
     "edit_and_resend",
     CommandFamily::RetryAndReplay,
     ResponseEnvelope::AcceptedRunHandle,
     CompletionMode::AcceptedAndStreaming,
     kCompleteOrError,
     {kSessionIdOnly, kSessionIdOnly}},
    {ControlPlaneCommand::RegenerateResponse,
     "regenerate_response",
     CommandFamily::RetryAndReplay,
     ResponseEnvelope::AcceptedRunHandle,
     CompletionMode::AcceptedAndStreaming,
     kCompleteOrError,
     {kSessionIdOnly, kSessionIdOnly}},
    {ControlPlaneCommand::ResolveApproval,
     "resolve_approval",
     CommandFamily::InteractiveResolution,
     ResponseEnvelope::Ack,
     CompletionMode::CompletionBound,
     kDirectResultAndInteractiveResolved,
     {kNoCorrelationIds, kInteractiveRequestIdOnly}},
    {ControlPlaneCommand::ResolveQuestion,
     "resolve_question",
     CommandFamily::InteractiveResolution,
     ResponseEnvelope::Ack,
     CompletionMode::CompletionBound,
     kDirectResultAndInteractiveResolved,
     {kNoCorrelationIds, kInteractiveRequestIdOnly}},
    {ControlPlaneCommand::ResolvePlan,
     "resolve_plan",
     CommandFamily::InteractiveResolution,
     ResponseEnvelope::Ack,
     CompletionMode::CompletionBound,
     kDirectResultAndInteractiveResolved,
     {kNoCorrelationIds, kInteractiveRequestIdOnly}},
    {ControlPlaneCommand::SteerAgent,
     "steer_agent",
     CommandFamily::QueueDispatch,
     ResponseEnvelope::Ack,
     CompletionMode::AcceptedAndStreaming,
     kCompleteOrError,
     {kNoCorrelationIds, kNoCorrelationIds}},
    {ControlPlaneCommand::FollowUpAgent,
     "follow_up_agent",
     CommandFamily::QueueDispatch,
     ResponseEnvelope::Ack,
     CompletionMode::AcceptedAndStreaming,
     kCompleteOrError,
     {kNoCorrelationIds, kNoCorrelationIds}},
    {ControlPlaneCommand::PostCompleteAgent,
     "post_complete_agent",
     CommandFamily::QueueDispatch,
     ResponseEnvelope::Ack,
     CompletionMode::AcceptedAndStreaming,
     kCompleteOrError,
     {kNoCorrelationIds, kNoCorrelationIds}},
    {ControlPlaneCommand::ClearMessageQueue,
     "clear_message_queue",
     CommandFamily::QueueControl,
     ResponseEnvelope::Ack,
     CompletionMode::FireAndForget,
     kNoTerminalSignal,
     {kNoCorrelationIds, kNoCorrelationIds}},
    {ControlPlaneCommand::ListAgentTools,
     "list_agent_tools",
     CommandFamily::ToolIntrospection,
     ResponseEnvelope::ToolList,
     CompletionMode::CompletionBound,
     kDirectResultOnly,
     {kNoCorrelationIds, kNoCorrelationIds}},
} };

}  // namespace

std::span<const CommandSpec> canonical_command_specs() {
  return kCanonicalCommandSpecs;
}

const CommandSpec& command_spec(ControlPlaneCommand command) {
  for(const auto& spec : kCanonicalCommandSpecs) {
    if(spec.command == command) {
      return spec;
    }
  }
  throw std::logic_error("canonical command spec missing");
}

const CommandSpec* command_spec_by_name(std::string_view name) {
  for(const auto& spec : kCanonicalCommandSpecs) {
    if(spec.name == name) {
      return &spec;
    }
  }
  return nullptr;
}

std::string_view command_to_string(ControlPlaneCommand command) {
  return command_spec(command).name;
}

std::string_view completion_mode_to_string(CompletionMode mode) {
  switch(mode) {
    case CompletionMode::CompletionBound:
      return "completion-bound";
    case CompletionMode::AcceptedAndStreaming:
      return "accepted-and-streaming";
    case CompletionMode::FireAndForget:
      return "fire-and-forget";
  }
  return "completion-bound";
}

ControlPlaneCommand queue_command_from_tier(const ava::types::MessageTier& tier) {
  switch(tier.kind) {
    case ava::types::MessageTierKind::Steering:
      return ControlPlaneCommand::SteerAgent;
    case ava::types::MessageTierKind::FollowUp:
      return ControlPlaneCommand::FollowUpAgent;
    case ava::types::MessageTierKind::PostComplete:
      return ControlPlaneCommand::PostCompleteAgent;
  }
  return ControlPlaneCommand::SteerAgent;
}

std::optional<ava::types::MessageTier> queue_message_tier(
    ControlPlaneCommand command,
    std::optional<std::uint32_t> post_complete_group
) {
  switch(command) {
    case ControlPlaneCommand::SteerAgent:
      return ava::types::MessageTier::steering();
    case ControlPlaneCommand::FollowUpAgent:
      return ava::types::MessageTier::follow_up();
    case ControlPlaneCommand::PostCompleteAgent:
      return ava::types::MessageTier::post_complete(post_complete_group.value_or(1));
    default:
      return std::nullopt;
  }
}

std::optional<std::string_view> queue_command_label(ControlPlaneCommand command) {
  switch(command) {
    case ControlPlaneCommand::SteerAgent:
      return "steering";
    case ControlPlaneCommand::FollowUpAgent:
      return "follow-up";
    case ControlPlaneCommand::PostCompleteAgent:
      return "post-complete";
    default:
      return std::nullopt;
  }
}

std::optional<ControlPlaneCommand> queue_command_from_alias(std::string_view alias) {
  if(alias == "steering" || alias == "steer_agent") {
    return ControlPlaneCommand::SteerAgent;
  }
  if(alias == "follow-up" || alias == "followup" || alias == "follow_up" || alias == "follow_up_agent") {
    return ControlPlaneCommand::FollowUpAgent;
  }
  if(alias == "post-complete" || alias == "postcomplete" || alias == "post_complete"
     || alias == "post_complete_agent") {
    return ControlPlaneCommand::PostCompleteAgent;
  }
  return std::nullopt;
}

}  // namespace ava::control_plane
