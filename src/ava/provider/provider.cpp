#include "ava/provider/provider.h"

#include <algorithm>
#include <cctype>
#include <initializer_list>
#include <string>
#include <string_view>
#include <utility>

namespace ava::provider {
namespace {

std::string lower_copy(std::string_view value) {
  std::string lowered(value);
  std::ranges::transform(lowered, lowered.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
  return lowered;
}

bool has_any(std::string_view haystack, std::initializer_list<std::string_view> needles) {
  for (const auto needle : needles) {
    if (haystack.find(needle) != std::string_view::npos) return true;
  }
  return false;
}

bool looks_like_context_overflow(std::string_view text) {
  const auto lowered = lower_copy(text);
  const bool mentions_context = has_any(lowered, {"context", "window", "token", "tokens", "prompt"});
  const bool mentions_overflow = has_any(lowered, {"too many", "too much", "exceed", "exceeded", "exceeds",
                                                   "maximum", "max", "limit", "length", "larger than"});
  return mentions_context && mentions_overflow;
}

}  // namespace

bool Transport::supports_streaming() const noexcept { return false; }

ava::core::Result<HttpResponse> Transport::send_streaming(const HttpRequest& request, BodyChunkSink on_body_chunk,
                                                          CancelCallback cancel_requested) {
  if (cancel_requested && cancel_requested()) {
    return std::unexpected(ava::core::Error(ava::core::ErrorCategory::Unknown, "transport request canceled"));
  }
  auto response = send(request);
  if (!response) return std::unexpected(std::move(response.error()));
  if (cancel_requested && cancel_requested()) {
    return std::unexpected(ava::core::Error(ava::core::ErrorCategory::Unknown, "transport request canceled"));
  }
  if (on_body_chunk && !response->body.empty()) {
    if (auto delivered = on_body_chunk(response->body); !delivered)
      return std::unexpected(std::move(delivered.error()));
  }
  if (cancel_requested && cancel_requested()) {
    return std::unexpected(ava::core::Error(ava::core::ErrorCategory::Unknown, "transport request canceled"));
  }
  return response;
}

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

bool is_context_overflow_error(const ava::core::Error& error) {
  if (error.category() != ava::core::ErrorCategory::Provider) return false;
  if (looks_like_context_overflow(error.message())) return true;
  for (const auto& item : error.context()) {
    if (looks_like_context_overflow(item.value)) return true;
  }
  return false;
}

}  // namespace ava::provider
