#include "ava/agent/stream_bridge.h"

namespace ava::agent {

ava::core::VoidResult publish_stream_event(const AgentLoopOptions& options, const ava::provider::StreamEvent& event) {
  if (!options.on_stream_event) return {};
  auto safe_event = event;
  safe_event.reasoning_signature_present =
      safe_event.reasoning_signature_present || !safe_event.reasoning_signature.empty();
  safe_event.reasoning_signature.clear();
  safe_event.reasoning_redacted_data.clear();
  return options.on_stream_event(safe_event);
}

}  // namespace ava::agent
