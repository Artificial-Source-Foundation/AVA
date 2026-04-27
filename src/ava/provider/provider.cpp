#include "ava/provider/provider.h"

#include <utility>

namespace ava::provider {

FakeTransport::FakeTransport(std::vector<HttpResponse> responses) : responses_(std::move(responses)) {}

ava::core::Result<HttpResponse> FakeTransport::send(const HttpRequest& request) {
  requests_.push_back(request);
  if (responses_.empty()) {
    return std::unexpected(ava::core::Error(ava::core::ErrorCategory::Provider, "fake transport has no response"));
  }
  auto response = responses_.front();
  responses_.erase(responses_.begin());
  return response;
}

const std::vector<HttpRequest>& FakeTransport::requests() const noexcept { return requests_; }

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
