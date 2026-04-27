#pragma once

#include <map>
#include <string>
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
};

class Provider {
 public:
  virtual ~Provider() = default;
  [[nodiscard]] virtual ava::core::Result<HttpRequest> build_request(const ProviderRequest& request,
                                                                     std::string_view access_token) const = 0;
};

class Transport {
 public:
  virtual ~Transport() = default;
  [[nodiscard]] virtual ava::core::Result<HttpResponse> send(const HttpRequest& request) = 0;
};

class FakeTransport final : public Transport {
 public:
  explicit FakeTransport(std::vector<HttpResponse> responses);
  [[nodiscard]] ava::core::Result<HttpResponse> send(const HttpRequest& request) override;
  [[nodiscard]] const std::vector<HttpRequest>& requests() const noexcept;

 private:
  std::vector<HttpResponse> responses_;
  std::vector<HttpRequest> requests_;
};

[[nodiscard]] std::string to_string(StreamEventType type);

}  // namespace ava::provider
