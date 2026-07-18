#include "sys.h"
#include "ava/agent/stream_bridge.h"

namespace ava::agent {

ava::core::VoidResult publish_stream_event(AgentLoopOptions const& options, ava::provider::StreamEvent const& event)
{
  if (!options.on_stream_event)
    return {};
  auto safe_event = event;
  safe_event.reasoning_signature_present = safe_event.reasoning_signature_present || !safe_event.reasoning_signature.empty();
  safe_event.reasoning_signature.clear();
  safe_event.reasoning_redacted_data.clear();
  safe_event.reasoning_native_item_json.clear();
  return options.on_stream_event(safe_event);
}

}  // namespace ava::agent
