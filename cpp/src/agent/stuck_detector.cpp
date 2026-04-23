#include "ava/agent/stuck_detector.hpp"

#include <utility>

namespace ava::agent {

StuckDetector::StuckDetector(StuckDetectorConfig config)
    : config_(std::move(config)) {}

StuckAction StuckDetector::check(const std::string& response_text, const std::vector<ava::types::ToolCall>& tool_calls) {
  const auto trimmed_empty = response_text.find_first_not_of(" \t\r\n") == std::string::npos;
  if(trimmed_empty && tool_calls.empty()) {
    ++consecutive_empty_;
    consecutive_same_response_ = 0;
    if(consecutive_empty_ >= config_.empty_response_limit) {
      return StuckAction{
          .kind = StuckActionKind::Stop,
          .message = "stuck detector: repeated empty responses",
      };
    }
  } else {
    consecutive_empty_ = 0;
  }

  if(!trimmed_empty) {
    if(response_text == last_response_) {
      ++consecutive_same_response_;
    } else {
      consecutive_same_response_ = 0;
      last_response_ = response_text;
    }

    if(consecutive_same_response_ >= config_.repeated_response_limit) {
      return StuckAction{
          .kind = StuckActionKind::Stop,
          .message = "stuck detector: repeated identical assistant responses",
      };
    }
  }

  if(tool_calls.size() == 1) {
    const auto signature = tool_signature(tool_calls.front());
    if(signature == last_tool_signature_) {
      ++consecutive_same_tool_call_;
    } else {
      consecutive_same_tool_call_ = 0;
      last_tool_signature_ = signature;
      nudge_sent_ = false;
    }

    if(consecutive_same_tool_call_ >= config_.repeated_tool_call_limit) {
      if(!nudge_sent_) {
        nudge_sent_ = true;
        return StuckAction{
            .kind = StuckActionKind::InjectMessage,
            .message = "You're repeating the same tool call. Re-evaluate and try a materially different approach.",
        };
      }

      return StuckAction{
          .kind = StuckActionKind::Stop,
          .message = "stuck detector: repeated same tool call signature",
      };
    }
  } else if(tool_calls.size() > 1) {
    consecutive_same_tool_call_ = 0;
    last_tool_signature_.clear();
    nudge_sent_ = false;
  } else if(tool_calls.empty()) {
    consecutive_same_tool_call_ = 0;
    last_tool_signature_.clear();
    nudge_sent_ = false;
  }

  return {};
}

std::string StuckDetector::tool_signature(const ava::types::ToolCall& tool_call) {
  return tool_call.name + ":" + tool_call.arguments.dump();
}

}  // namespace ava::agent
