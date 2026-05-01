#pragma once

#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "ava/core/result.h"

namespace ava::provider {

struct ChatMessage {
  std::string role;
  std::string content;
};

struct ProviderRequest {
  std::string provider_id;
  std::string model_id;
  std::string system_prompt;
  std::vector<ChatMessage> messages;
  std::vector<std::string> tools_json;
  bool stream = true;
};

struct TokenUsage {
  std::optional<long long> input_tokens;
  std::optional<long long> output_tokens;
  std::optional<long long> reasoning_tokens;
  std::optional<long long> cache_read_tokens;
  std::optional<long long> cache_write_tokens;
  std::optional<long long> total_tokens;
  std::optional<long long> estimated_input_bytes;
  std::optional<long long> estimated_output_bytes;
  std::optional<long long> estimated_total_bytes;
  bool estimated = false;
};

struct HttpRequest {
  std::string method;
  std::string url;
  std::map<std::string, std::string> headers;
  std::string body;
  // Provider transports should honor this deadline and tests should preserve it verbatim.
  int timeout_ms = 60000;
};

struct HttpResponse {
  int status_code = 0;
  std::map<std::string, std::string> headers;
  std::string body;
};

enum class StreamEventType {
  TextDelta,
  ToolCallStart,
  ToolCallDelta,
  ToolCallEnd,
  Done,
  Error,
};

struct StreamEvent {
  StreamEventType type = StreamEventType::Done;
  std::string text;
  std::string tool_call_id;
  std::string tool_name;
  std::string error_message;
  std::optional<TokenUsage> usage;
};

class Provider {
 public:
  virtual ~Provider() = default;
  [[nodiscard]] virtual ava::core::Result<HttpRequest> build_request(const ProviderRequest& request,
                                                                     std::string_view access_token) const = 0;
};

class Transport {
 public:
  using BodyChunkSink = std::function<ava::core::VoidResult(std::string_view)>;
  using CancelCallback = std::function<bool()>;

  virtual ~Transport() = default;
  [[nodiscard]] virtual ava::core::Result<HttpResponse> send(const HttpRequest& request) = 0;
  [[nodiscard]] virtual bool supports_streaming() const noexcept;
  [[nodiscard]] virtual ava::core::Result<HttpResponse> send_streaming(const HttpRequest& request,
                                                                       BodyChunkSink on_body_chunk,
                                                                       CancelCallback cancel_requested = nullptr);
};

[[nodiscard]] std::string to_string(StreamEventType type);
[[nodiscard]] bool is_context_overflow_error(const ava::core::Error& error);

}  // namespace ava::provider
