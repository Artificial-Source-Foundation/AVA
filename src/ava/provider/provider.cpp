#include "ava/provider/provider.h"

#include <algorithm>
#include <cctype>
#include <initializer_list>
#include <string>
#include <string_view>
#include <utility>

#include "ava/core/json.h"
#include "ava/provider/retry_policy.h"

namespace ava::provider {
namespace {

class DefaultStreamParser final : public StreamParser {
 public:
  [[nodiscard]] ava::core::Result<std::vector<StreamEvent>> append(std::string_view chunk) override
  {
    pending_.append(chunk);
    return std::vector<StreamEvent>{};
  }

  [[nodiscard]] ava::core::Result<std::vector<StreamEvent>> finish() override;

 private:
  std::string pending_;
};

std::string lower_copy(std::string_view value)
{
  std::string lowered(value);
  std::ranges::transform(lowered, lowered.begin(),
                         [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
  return lowered;
}

bool has_any(std::string_view haystack, std::initializer_list<std::string_view> needles)
{
  for (auto const needle : needles) {
    if (haystack.find(needle) != std::string_view::npos) return true;
  }
  return false;
}

bool looks_like_context_overflow(std::string_view text)
{
  auto const lowered = lower_copy(text);
  bool const mentions_context = has_any(lowered, {"context", "window", "token", "tokens", "prompt"});
  bool const mentions_overflow = has_any(lowered, {"too many", "too much", "exceed", "exceeded", "exceeds", "maximum",
                                                   "max", "limit", "length", "larger than"});
  return mentions_context && mentions_overflow;
}

bool looks_like_quota(std::string_view text)
{
  return has_any(text, {"quota", "billing", "insufficient_quota", "credit balance"});
}

bool looks_like_content_filter(std::string_view text)
{
  return has_any(text, {"content filter", "content_filter", "safety", "policy violation", "blocked"});
}

bool looks_like_refusal(std::string_view text)
{
  return has_any(text, {"refusal", "refused", "cannot comply", "can't comply"});
}

std::optional<std::string> default_text_from_json(std::string_view body)
{
  if (auto output = ava::core::json::string_field(body, "output_text")) return output;
  if (auto text = ava::core::json::string_field(body, "text")) return text;
  if (auto delta = ava::core::json::string_field(body, "delta")) return delta;
  return std::nullopt;
}

ava::core::Result<std::vector<StreamEvent>> default_parse_response(HttpResponse const& response, bool stream)
{
  if (response.status_code < 200 || response.status_code >= 300) {
    auto error = ava::core::Error(ava::core::ErrorCategory::Provider,
                                  "provider HTTP request failed with status " + std::to_string(response.status_code));
    error.with_context("status", std::to_string(response.status_code));
    error.with_context("provider_error_kind", to_string(classify_provider_error(response)));
    if (auto const retry_after = retry_after_header(response)) error.with_context("retry_after", *retry_after);
    return std::unexpected(std::move(error));
  }
  if (stream) {
    std::vector<StreamEvent> events;
    std::size_t line_start = 0;
    while (line_start <= response.body.size()) {
      auto const newline = response.body.find('\n', line_start);
      auto line = newline == std::string::npos
                      ? std::string_view(response.body).substr(line_start)
                      : std::string_view(response.body).substr(line_start, newline - line_start);
      if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
      if (line.starts_with("data:")) {
        line.remove_prefix(5);
        if (!line.empty() && line.front() == ' ') line.remove_prefix(1);
        if (line == "[DONE]") {
          events.push_back(StreamEvent{.type = StreamEventType::Done,
                                       .text = "",
                                       .tool_call_id = "",
                                       .tool_name = "",
                                       .error_message = "",
                                       .usage = std::nullopt});
        } else if (auto text = default_text_from_json(line)) {
          events.push_back(StreamEvent{.type = StreamEventType::TextDelta,
                                       .text = *text,
                                       .tool_call_id = "",
                                       .tool_name = "",
                                       .error_message = "",
                                       .usage = std::nullopt});
        }
      }
      if (newline == std::string::npos) break;
      line_start = newline + 1;
    }
    return events;
  }
  auto text = default_text_from_json(response.body);
  if (!text) {
    return std::unexpected(ava::core::Error(ava::core::ErrorCategory::Provider, "provider response text is missing"));
  }
  return std::vector<StreamEvent>{StreamEvent{.type = StreamEventType::TextDelta,
                                              .text = *text,
                                              .tool_call_id = "",
                                              .tool_name = "",
                                              .error_message = "",
                                              .usage = std::nullopt},
                                  StreamEvent{.type = StreamEventType::Done,
                                              .text = "",
                                              .tool_call_id = "",
                                              .tool_name = "",
                                              .error_message = "",
                                              .usage = std::nullopt}};
}

ava::core::Result<std::vector<StreamEvent>> DefaultStreamParser::finish()
{
  return default_parse_response(HttpResponse{.status_code = 200, .headers = {}, .body = std::move(pending_)}, true);
}

}  // namespace

ava::core::Result<HttpRequest> Provider::build_request(ProviderRequest const& request,
                                                       ProviderAuthContext const& auth) const
{
  auto http_request = build_request(request, auth.access_token);
  if (!http_request) return http_request;
  if (auto applied = apply_auth_options(*http_request, auth); !applied) {
    return std::unexpected(std::move(applied.error()));
  }
  return http_request;
}

ava::core::VoidResult Provider::apply_auth_options(HttpRequest&, ProviderAuthContext const&) const
{
  return {};
}

std::unique_ptr<StreamParser> Provider::create_stream_parser() const
{
  return std::make_unique<DefaultStreamParser>();
}

ava::core::Result<std::vector<StreamEvent>> Provider::parse_response(HttpResponse const& response, bool stream) const
{
  return default_parse_response(response, stream);
}

bool Transport::supports_streaming() const noexcept
{
  return false;
}

ava::core::Result<HttpResponse> Transport::send(HttpRequest const& request, CancelCallback cancel_requested)
{
  if (cancel_requested && cancel_requested()) {
    return std::unexpected(ava::core::Error(ava::core::ErrorCategory::Unknown, "transport request canceled"));
  }
  auto response = send(request);
  if (!response) return std::unexpected(std::move(response.error()));
  if (cancel_requested && cancel_requested()) {
    return std::unexpected(ava::core::Error(ava::core::ErrorCategory::Unknown, "transport request canceled"));
  }
  return response;
}

ava::core::Result<HttpResponse> Transport::send_streaming(HttpRequest const& request, BodyChunkSink on_body_chunk,
                                                          CancelCallback cancel_requested)
{
  if (cancel_requested && cancel_requested()) {
    return std::unexpected(ava::core::Error(ava::core::ErrorCategory::Unknown, "transport request canceled"));
  }
  auto response = send(request, cancel_requested);
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

RetryTransport::RetryTransport(Transport& inner, RetryOptions options) : inner_(inner), options_(options)
{
}

ava::core::Result<HttpResponse> RetryTransport::send(HttpRequest const& request)
{
  return send(request, nullptr);
}

ava::core::Result<HttpResponse> RetryTransport::send(HttpRequest const& request, CancelCallback cancel_requested)
{
  int const max_attempts = std::max(1, options_.max_attempts);
  if (detail::retry_cancel_requested(options_, cancel_requested))
    return std::unexpected(detail::retry_canceled_error());
  ava::core::Result<HttpResponse> response = inner_.send(request, cancel_requested);
  for (int attempt = 1; attempt < max_attempts; ++attempt) {
    if (detail::retry_cancel_requested(options_, cancel_requested))
      return std::unexpected(detail::retry_canceled_error());
    if (response) {
      if (!detail::is_retryable_kind(classify_provider_error(*response))) break;
      auto const reason = to_string(classify_provider_error(*response));
      int const delay_ms = detail::retry_after_ms(*response, options_.max_retry_after_ms)
                               .value_or(detail::exponential_delay_ms(options_, attempt));
      if (auto published = detail::publish_retry_event(
              options_, static_cast<std::size_t>(attempt + 1), static_cast<std::size_t>(max_attempts), delay_ms,
              static_cast<std::size_t>(delay_ms), reason, response->status_code, false);
          !published) {
        return std::unexpected(std::move(published.error()));
      }
      if (auto slept = detail::sleep_before_retry(options_, static_cast<std::size_t>(attempt + 1),
                                                  static_cast<std::size_t>(max_attempts), delay_ms, reason,
                                                  response->status_code, false, cancel_requested);
          !slept) {
        return std::unexpected(std::move(slept.error()));
      }
    } else {
      if (!detail::is_retryable_transport_error(response.error())) break;
      int const delay_ms = detail::exponential_delay_ms(options_, attempt);
      if (auto published = detail::publish_retry_event(options_, static_cast<std::size_t>(attempt + 1),
                                                       static_cast<std::size_t>(max_attempts), delay_ms,
                                                       static_cast<std::size_t>(delay_ms), "transport_io", 0, false);
          !published) {
        return std::unexpected(std::move(published.error()));
      }
      if (auto slept = detail::sleep_before_retry(options_, static_cast<std::size_t>(attempt + 1),
                                                  static_cast<std::size_t>(max_attempts), delay_ms, "transport_io", 0,
                                                  false, cancel_requested);
          !slept) {
        return std::unexpected(std::move(slept.error()));
      }
    }
    if (detail::retry_cancel_requested(options_, cancel_requested))
      return std::unexpected(detail::retry_canceled_error());
    response = inner_.send(request, cancel_requested);
  }
  return response;
}

bool RetryTransport::supports_streaming() const noexcept
{
  return inner_.supports_streaming();
}

ava::core::Result<HttpResponse> RetryTransport::send_streaming(HttpRequest const& request, BodyChunkSink on_body_chunk,
                                                               CancelCallback cancel_requested)
{
  int const max_attempts = std::max(1, options_.max_attempts);
  ava::core::Result<HttpResponse> response =
      std::unexpected(ava::core::Error(ava::core::ErrorCategory::Unknown, "streaming request was not attempted"));
  std::string final_body;
  for (int attempt = 1; attempt <= max_attempts; ++attempt) {
    std::string attempt_body;
    bool delivered_chunks = false;
    response = inner_.send_streaming(
        request,
        [&](std::string_view chunk) -> ava::core::VoidResult {
          attempt_body.append(chunk);
          if (!on_body_chunk) return ava::core::VoidResult{};
          delivered_chunks = true;
          return on_body_chunk(chunk);
        },
        cancel_requested);

    bool const last_attempt = attempt == max_attempts;
    if (response) {
      if (!last_attempt && !delivered_chunks && detail::is_retryable_kind(classify_provider_error(*response))) {
        auto const reason = to_string(classify_provider_error(*response));
        int const delay_ms = detail::retry_after_ms(*response, options_.max_retry_after_ms)
                                 .value_or(detail::exponential_delay_ms(options_, attempt));
        if (auto published = detail::publish_retry_event(
                options_, static_cast<std::size_t>(attempt + 1), static_cast<std::size_t>(max_attempts), delay_ms,
                static_cast<std::size_t>(delay_ms), reason, response->status_code, true);
            !published) {
          return std::unexpected(std::move(published.error()));
        }
        if (auto slept = detail::sleep_before_retry(options_, static_cast<std::size_t>(attempt + 1),
                                                    static_cast<std::size_t>(max_attempts), delay_ms, reason,
                                                    response->status_code, true, cancel_requested);
            !slept) {
          return std::unexpected(std::move(slept.error()));
        }
        continue;
      }
      if (!delivered_chunks) final_body = std::move(attempt_body);
      break;
    }

    if (last_attempt || delivered_chunks || !detail::is_retryable_transport_error(response.error())) break;
    int const delay_ms = detail::exponential_delay_ms(options_, attempt);
    if (auto published = detail::publish_retry_event(options_, static_cast<std::size_t>(attempt + 1),
                                                     static_cast<std::size_t>(max_attempts), delay_ms,
                                                     static_cast<std::size_t>(delay_ms), "transport_io", 0, true);
        !published) {
      return std::unexpected(std::move(published.error()));
    }
    if (auto slept = detail::sleep_before_retry(options_, static_cast<std::size_t>(attempt + 1),
                                                static_cast<std::size_t>(max_attempts), delay_ms, "transport_io", 0,
                                                true, cancel_requested);
        !slept) {
      return std::unexpected(std::move(slept.error()));
    }
  }
  if (!response) return std::unexpected(std::move(response.error()));
  if (on_body_chunk && !final_body.empty()) {
    if (auto delivered = on_body_chunk(final_body); !delivered) return std::unexpected(std::move(delivered.error()));
  }
  return response;
}

std::string to_string(StreamEventType type)
{
  switch (type) {
    case StreamEventType::TextDelta:
      return "text_delta";
    case StreamEventType::ReasoningStart:
      return "reasoning_start";
    case StreamEventType::ReasoningDelta:
      return "reasoning_delta";
    case StreamEventType::ReasoningEnd:
      return "reasoning_end";
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

std::string to_string(ProviderErrorKind kind)
{
  switch (kind) {
    case ProviderErrorKind::Authentication:
      return "authentication";
    case ProviderErrorKind::RateLimited:
      return "rate_limited";
    case ProviderErrorKind::Quota:
      return "quota";
    case ProviderErrorKind::InvalidRequest:
      return "invalid_request";
    case ProviderErrorKind::ContextOverflow:
      return "context_overflow";
    case ProviderErrorKind::Refusal:
      return "refusal";
    case ProviderErrorKind::ContentFilter:
      return "content_filter";
    case ProviderErrorKind::Transient:
      return "transient";
    case ProviderErrorKind::Unknown:
      return "unknown";
  }
  return "unknown";
}

ProviderErrorKind classify_provider_error(HttpResponse const& response)
{
  if (response.status_code >= 200 && response.status_code < 300) return ProviderErrorKind::Unknown;
  auto const body = lower_copy(response.body);
  if (response.status_code == 401 || response.status_code == 403) return ProviderErrorKind::Authentication;
  if (looks_like_context_overflow(body)) return ProviderErrorKind::ContextOverflow;
  if (looks_like_quota(body)) return ProviderErrorKind::Quota;
  if (looks_like_content_filter(body)) return ProviderErrorKind::ContentFilter;
  if (looks_like_refusal(body)) return ProviderErrorKind::Refusal;
  if (response.status_code == 429) return ProviderErrorKind::RateLimited;
  if (response.status_code == 400 || response.status_code == 404 || response.status_code == 422) {
    return ProviderErrorKind::InvalidRequest;
  }
  if (response.status_code == 408 || response.status_code == 409 ||
      (response.status_code >= 500 && response.status_code < 600)) {
    return ProviderErrorKind::Transient;
  }
  return ProviderErrorKind::Unknown;
}

std::optional<std::string> retry_after_header(HttpResponse const& response)
{
  for (auto const& [key, value] : response.headers) {
    if (lower_copy(key) == "retry-after") return value;
  }
  return std::nullopt;
}

bool is_context_overflow_error(ava::core::Error const& error)
{
  if (error.category() != ava::core::ErrorCategory::Provider) return false;
  if (looks_like_context_overflow(error.message())) return true;
  for (auto const& item : error.context()) {
    if (looks_like_context_overflow(item.value)) return true;
  }
  return false;
}

}  // namespace ava::provider
