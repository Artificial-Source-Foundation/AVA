#pragma once

#include "ava/core/result.h"

#include <cstddef>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ava::provider {

enum class ContentPartType
{
  Text,
  Image,
  Reasoning,
  ToolUse,
  ToolResult,
};

struct ContentPart
{
  // Provider-neutral native content item. Providers that do not support native
  // parts can ignore these fields and serialize ChatMessage::content instead.
  ContentPartType type = ContentPartType::Text;
  std::string text = {};
  std::string tool_call_id = {};
  std::string tool_name = {};
  std::string input_json = {};
  bool is_error = false;
  std::string cache_control_ttl = {};
  // Provider-native reasoning/thinking metadata. `text` carries the visible or
  // summarized reasoning content; signatures stay provider-private for replay.
  std::string reasoning_format = {};
  std::string reasoning_signature = {};
  std::string reasoning_redacted_data = {};
  bool redacted = false;
  // Image attachment metadata. Session/RPC records store references and
  // provider serializers may load bytes transiently after validation.
  std::string attachment_id = {};
  std::string mime_type = {};
  std::string storage_path = {};
  std::string sha256 = {};
  std::size_t byte_size = 0;
  // Transient provider payload populated only after session attachment storage
  // has verified size, hash, and path containment. Never persist this field.
  std::string data_base64 = {};
};

struct ChatMessage
{
  std::string role = {};
  // Readable legacy fallback content. This remains populated for providers that
  // do not understand content_parts and for diagnostics/session reconstruction.
  std::string content = {};
  // Optional native structure for providers that support text/tool-use/tool-result
  // content blocks. When non-empty, native-capable providers should serialize
  // these parts as canonical and treat content only as fallback/diagnostic text.
  // Text-only providers should ignore this field.
  std::vector<ContentPart> content_parts = {};
};

struct ProviderReasoningOptions
{
  std::string type = {};
  std::optional<long long> budget_tokens = std::nullopt;
  std::string display = {};
};

struct ProviderRequest
{
  std::string provider_id;
  std::string model_id;
  std::string system_prompt;
  std::vector<ChatMessage> messages;
  std::vector<std::string> tools_json;
  bool stream = true;
  std::optional<long long> max_output_tokens = std::nullopt;
  std::optional<ProviderReasoningOptions> reasoning = std::nullopt;
  std::string system_prompt_cache_ttl = {};
};

struct ProviderAuthContext
{
  std::string access_token;
  std::string credential_type;
  std::string account_id;
};

struct TokenUsage
{
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

struct HttpRequest
{
  std::string method;
  std::string url;
  std::map<std::string, std::string> headers;
  std::string body;
  // Provider transports should honor this deadline and tests should preserve it verbatim.
  int timeout_ms = 60000;
  bool follow_redirects = true;
  bool include_response_headers = false;
  std::vector<std::string> resolve_hosts;
};

struct HttpResponse
{
  int status_code = 0;
  std::map<std::string, std::string> headers;
  std::string body;
};

enum class StreamEventType
{
  TextDelta,
  ReasoningStart,
  ReasoningDelta,
  ReasoningEnd,
  ToolCallStart,
  ToolCallDelta,
  ToolCallEnd,
  Done,
  Error,
};

enum class ProviderErrorKind
{
  Authentication,
  RateLimited,
  Quota,
  InvalidRequest,
  ContextOverflow,
  Refusal,
  ContentFilter,
  Transient,
  Unknown,
};

struct StreamEvent
{
  StreamEventType type = StreamEventType::Done;
  std::string text;
  std::string tool_call_id;
  std::string tool_name;
  std::string error_message;
  std::optional<TokenUsage> usage;
  std::string stop_reason = {};
  std::string reasoning_format = {};
  std::string reasoning_signature = {};
  std::string reasoning_redacted_data = {};
  bool redacted = false;
  bool reasoning_signature_present = false;
};

class StreamParser
{
 public:
  virtual ~StreamParser() = default;
  [[nodiscard]] virtual ava::core::Result<std::vector<StreamEvent>> append(std::string_view chunk) = 0;
  [[nodiscard]] virtual ava::core::Result<std::vector<StreamEvent>> finish() = 0;
};

class Provider
{
 public:
  virtual ~Provider() = default;
  [[nodiscard]] virtual ava::core::Result<HttpRequest> build_request(ProviderRequest const& request, std::string_view access_token) const = 0;
  [[nodiscard]] virtual ava::core::Result<HttpRequest> build_request(ProviderRequest const& request, ProviderAuthContext const& auth) const;
  [[nodiscard]] virtual ava::core::VoidResult apply_auth_options(HttpRequest& request, ProviderAuthContext const& auth) const;
  [[nodiscard]] virtual std::unique_ptr<StreamParser> create_stream_parser() const;
  [[nodiscard]] virtual ava::core::Result<std::vector<StreamEvent>> parse_response(HttpResponse const& response, bool stream) const;
};

class Transport
{
 public:
  using BodyChunkSink = std::function<ava::core::VoidResult(std::string_view)>;
  using CancelCallback = std::function<bool()>;

  virtual ~Transport() = default;
  [[nodiscard]] virtual ava::core::Result<HttpResponse> send(HttpRequest const& request) = 0;
  [[nodiscard]] virtual ava::core::Result<HttpResponse> send(HttpRequest const& request, CancelCallback cancel_requested);
  [[nodiscard]] virtual bool supports_streaming() const noexcept;
  [[nodiscard]] virtual ava::core::Result<HttpResponse> send_streaming(HttpRequest const& request, BodyChunkSink on_body_chunk,
                                                                       CancelCallback cancel_requested = nullptr);
};

struct RetryOptions
{
  int max_attempts = 3;
  int base_delay_ms = 250;
  int max_retry_after_ms = 60'000;
  int countdown_tick_ms = 1000;
  struct Event
  {
    std::size_t attempt = 0;
    std::size_t max_attempts = 0;
    std::size_t delay_ms = 0;
    std::size_t remaining_ms = 0;
    std::string reason = {};
    int status_code = 0;
    bool streaming = false;
    bool countdown_tick = false;
  };
  std::function<ava::core::VoidResult(Event const&)> on_retry = nullptr;
  Transport::CancelCallback cancel_requested = nullptr;
};

class RetryTransport final : public Transport
{
 public:
  RetryTransport(Transport& inner, RetryOptions options = {});
  [[nodiscard]] ava::core::Result<HttpResponse> send(HttpRequest const& request) override;
  [[nodiscard]] ava::core::Result<HttpResponse> send(HttpRequest const& request, CancelCallback cancel_requested) override;
  [[nodiscard]] bool supports_streaming() const noexcept override;
  [[nodiscard]] ava::core::Result<HttpResponse> send_streaming(HttpRequest const& request, BodyChunkSink on_body_chunk,
                                                               CancelCallback cancel_requested = nullptr) override;

 private:
  Transport& inner_;
  RetryOptions options_;
};

[[nodiscard]] std::string to_string(StreamEventType type);
[[nodiscard]] std::string to_string(ProviderErrorKind kind);
[[nodiscard]] ProviderErrorKind classify_provider_error(HttpResponse const& response);
[[nodiscard]] std::optional<std::string> retry_after_header(HttpResponse const& response);
[[nodiscard]] bool is_context_overflow_error(ava::core::Error const& error);
[[nodiscard]] bool is_supported_image_mime_type(std::string_view mime_type);
[[nodiscard]] bool request_has_image_parts(ProviderRequest const& request);
[[nodiscard]] ava::core::VoidResult validate_image_content_parts(ProviderRequest const& request,
                                                                 bool model_supports_images);

}  // namespace ava::provider
