#include "sys.h"
#include "RuntimeEvent.h"

#include <type_traits>

namespace ava::event {
namespace {

template <typename>
inline constexpr bool always_false = false;

}  // namespace

RuntimeEventMetadata const& RuntimeEvent::metadata() const noexcept
{
  return metadata_;
}

RuntimeEventPayload const& RuntimeEvent::payload() const noexcept
{
  return payload_;
}

RuntimeEventType RuntimeEvent::type() const noexcept
{
  return std::visit(
      [](auto const& event) -> RuntimeEventType {
        using Event = std::remove_cvref_t<decltype(event)>;
        if constexpr (std::is_same_v<Event, SessionStartEvent>)
          return RuntimeEventType::SessionStart;
        else if constexpr (std::is_same_v<Event, UserMessageEvent>)
          return RuntimeEventType::UserMessage;
        else if constexpr (std::is_same_v<Event, AssistantMessageEvent>)
          return RuntimeEventType::AssistantMessage;
        else if constexpr (std::is_same_v<Event, MessageUpdateEvent>)
          return RuntimeEventType::MessageUpdate;
        else if constexpr (std::is_same_v<Event, MessageEndEvent>)
          return RuntimeEventType::MessageEnd;
        else if constexpr (std::is_same_v<Event, ReasoningStartEvent>)
          return RuntimeEventType::ReasoningStart;
        else if constexpr (std::is_same_v<Event, ReasoningDeltaEvent>)
          return RuntimeEventType::ReasoningDelta;
        else if constexpr (std::is_same_v<Event, ReasoningEndEvent>)
          return RuntimeEventType::ReasoningEnd;
        else if constexpr (std::is_same_v<Event, ProviderEvent>)
          return RuntimeEventType::ProviderEvent;
        else if constexpr (std::is_same_v<Event, ToolStartEvent>)
          return RuntimeEventType::ToolStart;
        else if constexpr (std::is_same_v<Event, ToolProgressEvent>)
          return RuntimeEventType::ToolProgress;
        else if constexpr (std::is_same_v<Event, ToolResultEvent>)
          return RuntimeEventType::ToolResult;
        else if constexpr (std::is_same_v<Event, CompactionStartEvent>)
          return RuntimeEventType::CompactionStart;
        else if constexpr (std::is_same_v<Event, CompactionEndEvent>)
          return RuntimeEventType::CompactionEnd;
        else if constexpr (std::is_same_v<Event, RetryEvent>)
          return RuntimeEventType::Retry;
        else if constexpr (std::is_same_v<Event, RetryTickEvent>)
          return RuntimeEventType::RetryTick;
        else if constexpr (std::is_same_v<Event, CancellationEvent>)
          return RuntimeEventType::Canceled;
        else if constexpr (std::is_same_v<Event, ErrorEvent>)
          return RuntimeEventType::Error;
        else if constexpr (std::is_same_v<Event, CompletionEvent>)
          return RuntimeEventType::Done;
        else
          static_assert(always_false<Event>, "unhandled RuntimeEvent alternative");
      },
      payload_);
}

}  // namespace ava::event
