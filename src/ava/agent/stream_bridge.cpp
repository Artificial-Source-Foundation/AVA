#include "sys.h"
#include "ava/agent/stream_bridge.h"

#include <optional>

namespace ava::agent {

ava::core::VoidResult publish_stream_event(AgentLoopOptions const& options, ava::provider::StreamEvent const& event)
{
  // TextStart/TextEnd are private parser lifecycle markers used only to
  // reconstruct the assistant turn. Public TUI/RPC consumers historically
  // receive text deltas and terminal events, never those markers.
  if (event.type == ava::provider::StreamEventType::TextStart || event.type == ava::provider::StreamEventType::TextEnd)
    return {};
  if (!options.on_stream_event)
    return {};
  auto safe_event = event;
  safe_event.reasoning_signature_present = safe_event.reasoning_signature_present || !safe_event.reasoning_signature.empty();
  safe_event.reasoning_signature.clear();
  safe_event.reasoning_redacted_data.clear();
  safe_event.reasoning_native_item_json.clear();
  // Native Responses item identities are provider-private replay metadata.
  // Public runtime/RPC events retain only the existing logical tool call ID.
  safe_event.provider_item_id.clear();
  safe_event.provider_output_index = std::nullopt;
  // Text phase is native ordered-output metadata. Keep it internal until a
  // public logical-output callback contract is explicitly versioned.
  safe_event.assistant_phase = ava::provider::AssistantPhase::Unknown;
  if (safe_event.type == ava::provider::StreamEventType::Error)
    safe_event.error_message = "Provider streaming error";
  return options.on_stream_event(safe_event);
}

}  // namespace ava::agent
