#include "ava/provider/provider.h"

#include <algorithm>
#include <utility>

#include "ava/provider/default_provider_parser.h"
#include "ava/provider/retry_policy.h"

namespace ava::provider {

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
  return detail::make_default_stream_parser();
}

ava::core::Result<std::vector<StreamEvent>> Provider::parse_response(HttpResponse const& response, bool stream) const
{
  return detail::parse_default_provider_response(response, stream);
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

}  // namespace ava::provider
