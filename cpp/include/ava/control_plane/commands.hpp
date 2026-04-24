#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

#include "ava/types/message.hpp"

namespace ava::control_plane {

enum class ControlPlaneCommand {
  SubmitGoal,
  CancelAgent,
  RetryLastMessage,
  EditAndResend,
  RegenerateResponse,
  ResolveApproval,
  ResolveQuestion,
  ResolvePlan,
  SteerAgent,
  FollowUpAgent,
  PostCompleteAgent,
  ClearMessageQueue,
  ListAgentTools,
};

enum class CommandFamily {
  GoalSubmission,
  Cancellation,
  RetryAndReplay,
  InteractiveResolution,
  QueueDispatch,
  QueueControl,
  ToolIntrospection,
};

enum class ResponseEnvelope {
  AcceptedRunHandle,
  Ack,
  DirectResult,
  ToolList,
};

enum class CompletionMode {
  CompletionBound,
  AcceptedAndStreaming,
  FireAndForget,
};

enum class TerminalClosureSignal {
  CompleteEvent,
  ErrorEvent,
  RunInterrupted,
  InteractiveResolved,
  InteractiveError,
  DirectResult,
  None,
};

enum class CorrelationIdKey {
  SessionId,
  InteractiveRequestId,
};

struct CorrelationIdRequirements {
  std::span<const CorrelationIdKey> accepted_response;
  std::span<const CorrelationIdKey> lifecycle;
};

struct CommandSpec {
  ControlPlaneCommand command;
  std::string_view name;
  CommandFamily family;
  ResponseEnvelope response_envelope;
  CompletionMode completion_mode;
  std::span<const TerminalClosureSignal> terminal_signals;
  CorrelationIdRequirements correlation_ids;
};

[[nodiscard]] std::span<const CommandSpec> canonical_command_specs();
[[nodiscard]] const CommandSpec& command_spec(ControlPlaneCommand command);
[[nodiscard]] const CommandSpec* command_spec_by_name(std::string_view name);

[[nodiscard]] std::string_view command_to_string(ControlPlaneCommand command);
[[nodiscard]] std::string_view completion_mode_to_string(CompletionMode mode);

[[nodiscard]] ControlPlaneCommand queue_command_from_tier(const ava::types::MessageTier& tier);
[[nodiscard]] std::optional<ava::types::MessageTier> queue_message_tier(
    ControlPlaneCommand command,
    std::optional<std::uint32_t> post_complete_group = std::nullopt
);
[[nodiscard]] std::optional<std::string_view> queue_command_label(ControlPlaneCommand command);
[[nodiscard]] std::optional<ControlPlaneCommand> queue_command_from_alias(std::string_view alias);

}  // namespace ava::control_plane
