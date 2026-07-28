#pragma once

#include "CancellationPayload.h"
#include "CompactionPayload.h"
#include "CompletionPayload.h"
#include "ErrorPayload.h"
#include "MessagePayload.h"
#include "ProviderPayload.h"
#include "ReasoningPayload.h"
#include "RetryPayload.h"
#include "RuntimeEventType.h"
#include "SessionPayload.h"
#include "ToolPayload.h"
#include "ava/debug/print_members_on.h"
#include "ava/core/result.h"

#include <concepts>
#include <cstddef>
#include <functional>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>

namespace ava::event {

struct RuntimeEventMetadata
{
  std::string timestamp;
  std::string session_id;

  AVA_DEBUG_PRINT_MEMBERS_ON
};

struct SessionStartEvent
{
  SessionPayload payload;

  AVA_DEBUG_PRINT_MEMBERS_ON
};

struct UserMessageEvent
{
  MessagePayload payload;

  AVA_DEBUG_PRINT_MEMBERS_ON
};

struct AssistantMessageEvent
{
  MessagePayload payload;

  AVA_DEBUG_PRINT_MEMBERS_ON
};

struct MessageUpdateEvent
{
  MessagePayload payload;

  AVA_DEBUG_PRINT_MEMBERS_ON
};

struct MessageEndEvent
{
  MessagePayload payload;

  AVA_DEBUG_PRINT_MEMBERS_ON
};

struct ReasoningStartEvent
{
  ReasoningPayload payload;

  AVA_DEBUG_PRINT_MEMBERS_ON
};

struct ReasoningDeltaEvent
{
  ReasoningPayload payload;

  AVA_DEBUG_PRINT_MEMBERS_ON
};

struct ReasoningEndEvent
{
  ReasoningPayload payload;

  AVA_DEBUG_PRINT_MEMBERS_ON
};

struct ProviderEvent
{
  ProviderPayload payload;

  AVA_DEBUG_PRINT_MEMBERS_ON
};

struct ToolStartEvent
{
  ToolPayload payload;

  AVA_DEBUG_PRINT_MEMBERS_ON
};

struct ToolProgressEvent
{
  ToolPayload payload;

  AVA_DEBUG_PRINT_MEMBERS_ON
};

struct ToolResultEvent
{
  ToolPayload payload;

  AVA_DEBUG_PRINT_MEMBERS_ON
};

struct CompactionStartEvent
{
  CompactionPayload payload;

  AVA_DEBUG_PRINT_MEMBERS_ON
};

struct CompactionEndEvent
{
  CompactionPayload payload;

  AVA_DEBUG_PRINT_MEMBERS_ON
};

struct RetryDiagnostics
{
  std::size_t estimated_tokens = 0;
  std::size_t threshold_tokens = 0;
  std::size_t snapshot_entries = 0;
  std::size_t current_entries = 0;
  std::size_t summary_bytes = 0;

  AVA_DEBUG_PRINT_MEMBERS_ON
};

struct RetryEvent
{
  RetryPayload payload;
  // Internal diagnostics are retained for typed live consumers but intentionally omitted from the public v1 envelope payload.
  RetryDiagnostics diagnostics;

  AVA_DEBUG_PRINT_MEMBERS_ON
};

struct RetryTickEvent
{
  RetryPayload payload;
  // Internal diagnostics are retained for typed live consumers but intentionally omitted from the public v1 envelope payload.
  RetryDiagnostics diagnostics;

  AVA_DEBUG_PRINT_MEMBERS_ON
};

struct CancellationEvent
{
  CancellationPayload payload;

  AVA_DEBUG_PRINT_MEMBERS_ON
};

struct ErrorEvent
{
  ErrorPayload payload;

  AVA_DEBUG_PRINT_MEMBERS_ON
};

struct CompletionEvent
{
  CompletionPayload payload;

  AVA_DEBUG_PRINT_MEMBERS_ON
};

using RuntimeEventPayload = std::variant<SessionStartEvent, UserMessageEvent, AssistantMessageEvent, MessageUpdateEvent, MessageEndEvent, ReasoningStartEvent,
                                         ReasoningDeltaEvent, ReasoningEndEvent, ProviderEvent, ToolStartEvent, ToolProgressEvent, ToolResultEvent,
                                         CompactionStartEvent, CompactionEndEvent, RetryEvent, RetryTickEvent, CancellationEvent, ErrorEvent, CompletionEvent>;

template <typename Event>
concept RuntimeEventAlternative =
    std::same_as<std::remove_cvref_t<Event>, SessionStartEvent> || std::same_as<std::remove_cvref_t<Event>, UserMessageEvent> ||
    std::same_as<std::remove_cvref_t<Event>, AssistantMessageEvent> || std::same_as<std::remove_cvref_t<Event>, MessageUpdateEvent> ||
    std::same_as<std::remove_cvref_t<Event>, MessageEndEvent> || std::same_as<std::remove_cvref_t<Event>, ReasoningStartEvent> ||
    std::same_as<std::remove_cvref_t<Event>, ReasoningDeltaEvent> || std::same_as<std::remove_cvref_t<Event>, ReasoningEndEvent> ||
    std::same_as<std::remove_cvref_t<Event>, ProviderEvent> || std::same_as<std::remove_cvref_t<Event>, ToolStartEvent> ||
    std::same_as<std::remove_cvref_t<Event>, ToolProgressEvent> || std::same_as<std::remove_cvref_t<Event>, ToolResultEvent> ||
    std::same_as<std::remove_cvref_t<Event>, CompactionStartEvent> || std::same_as<std::remove_cvref_t<Event>, CompactionEndEvent> ||
    std::same_as<std::remove_cvref_t<Event>, RetryEvent> || std::same_as<std::remove_cvref_t<Event>, RetryTickEvent> ||
    std::same_as<std::remove_cvref_t<Event>, CancellationEvent> || std::same_as<std::remove_cvref_t<Event>, ErrorEvent> ||
    std::same_as<std::remove_cvref_t<Event>, CompletionEvent>;

class RuntimeEvent
{
 public:
  template <RuntimeEventAlternative Event>
  RuntimeEvent(RuntimeEventMetadata metadata, Event&& event)
      : metadata_(std::move(metadata)), payload_(std::in_place_type<std::remove_cvref_t<Event>>, std::forward<Event>(event))
  {
  }

  RuntimeEvent(RuntimeEvent const&) = default;
  RuntimeEvent(RuntimeEvent&&) = default;
  RuntimeEvent& operator=(RuntimeEvent const&) = delete;
  RuntimeEvent& operator=(RuntimeEvent&&) = delete;

  [[nodiscard]] RuntimeEventMetadata const& metadata() const noexcept;
  [[nodiscard]] RuntimeEventPayload const& payload() const noexcept;
  [[nodiscard]] RuntimeEventType type() const noexcept;

  AVA_DEBUG_PRINT_MEMBERS_ON

 private:
  RuntimeEventMetadata metadata_;
  RuntimeEventPayload payload_;
};

using RuntimeEventSink = std::function<ava::core::VoidResult(RuntimeEvent const&)>;

}  // namespace ava::event
