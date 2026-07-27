#pragma once

namespace ava::event {

enum class RuntimeEventType
{
  SessionStart,
  UserMessage,
  AssistantMessage,
  MessageUpdate,
  MessageEnd,
  ReasoningStart,
  ReasoningDelta,
  ReasoningEnd,
  ProviderEvent,
  ToolStart,
  ToolProgress,
  ToolResult,
  CompactionStart,
  CompactionEnd,
  Retry,
  RetryTick,
  Canceled,
  Error,
  Done,
};

}  // namespace ava::event
