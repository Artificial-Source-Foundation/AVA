#include "ava/provider/provider.h"

namespace ava::provider {

std::string to_string(StreamEventType type) {
  switch (type) {
    case StreamEventType::TextDelta:
      return "text_delta";
    case StreamEventType::ToolCallStart:
      return "tool_call_start";
    case StreamEventType::ToolCallDelta:
      return "tool_call_delta";
    case StreamEventType::ToolCallEnd:
      return "tool_call_end";
    case StreamEventType::Done:
      return "done";
    case StreamEventType::Error:
      return "error";
  }
  return "error";
}

}  // namespace ava::provider
