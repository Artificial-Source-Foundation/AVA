#include "sys.h"
#include "ava/provider/provider.h"
#include "ava/provider/provider_utils.h"
#include "ava/core/json.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <initializer_list>
#include <limits>
#include <string>
#include <string_view>
#include <thread>
#include <utility>

namespace ava::provider {
namespace {

constexpr std::size_t kMaxImageAttachmentIdBytes = 128;
constexpr std::size_t kMaxImageStoragePathBytes = 4096;

class DefaultStreamParser final : public StreamParser
{
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
  std::ranges::transform(lowered, lowered.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
  return lowered;
}

bool has_any(std::string_view haystack, std::initializer_list<std::string_view> needles)
{
  for (auto const needle : needles)
  {
    if (haystack.find(needle) != std::string_view::npos)
      return true;
  }
  return false;
}

bool has_control_byte(std::string_view value)
{
  return std::ranges::any_of(value, [](char ch) {
    auto const byte = static_cast<unsigned char>(ch);
    return byte < 0x20 || byte == 0x7f;
  });
}

bool is_hex_string(std::string_view value)
{
  return std::ranges::all_of(value, [](char ch) { return (ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f') || (ch >= 'A' && ch <= 'F'); });
}

ava::core::Error image_part_error(std::string message, std::size_t message_index, std::size_t part_index)
{
  auto error = ava::core::Error(ava::core::ErrorCategory::InvalidArgument, std::move(message));
  error.with_context("message_index", std::to_string(message_index));
  error.with_context("content_part_index", std::to_string(part_index));
  return error;
}

bool looks_like_context_overflow(std::string_view text)
{
  auto const lowered = lower_copy(text);
  bool const mentions_context = has_any(lowered, {"context", "window", "token", "tokens", "prompt"});
  bool const mentions_overflow =
      has_any(lowered, {"too many", "too much", "exceed", "exceeded", "exceeds", "maximum", "max", "limit", "length", "larger than"});
  return mentions_context && mentions_overflow;
}

bool looks_like_quota(std::string_view text)
{
  return has_any(text, {"quota", "billing", "insufficient_quota", "credit balance", "insufficient credit", "insufficient credits", "payment required"});
}

bool looks_like_content_filter(std::string_view text)
{
  return has_any(text, {"content filter", "content_filter", "safety", "policy violation", "blocked"});
}

bool looks_like_refusal(std::string_view text)
{
  return has_any(text, {"refusal", "refused", "cannot comply", "can't comply"});
}

bool is_retryable_kind(ProviderErrorKind kind) noexcept
{
  return kind == ProviderErrorKind::RateLimited || kind == ProviderErrorKind::Transient;
}

bool is_retryable_transport_error(ava::core::Error const& error) noexcept
{
  return error.category() == ava::core::ErrorCategory::Io;
}

int exponential_delay_ms(RetryOptions const& options, int attempt) noexcept
{
  if (options.base_delay_ms <= 0)
    return 0;
  long long delay = options.base_delay_ms;
  for (int index = 1; index < attempt; ++index)
  {
    if (delay > std::numeric_limits<int>::max() / 2)
      return std::numeric_limits<int>::max();
    delay *= 2;
  }
  return static_cast<int>(delay);
}

ava::core::VoidResult publish_retry_event(RetryOptions const& options, std::size_t attempt, std::size_t max_attempts, int delay_ms, std::size_t remaining_ms,
                                          std::string_view reason, int status_code, bool streaming, bool countdown_tick = false)
{
  // Countdown ticks are UI progress only; one trace records the actual retry.
  if (!countdown_tick)
    observe_transport_retry(options.observation, attempt, max_attempts, static_cast<std::size_t>(std::max(0, delay_ms)), reason, status_code, streaming);
  if (!options.on_retry)
    return {};
  return options.on_retry(RetryOptions::Event{.attempt = attempt,
                                              .max_attempts = max_attempts,
                                              .delay_ms = static_cast<std::size_t>(std::max(0, delay_ms)),
                                              .remaining_ms = remaining_ms,
                                              .reason = std::string(reason),
                                              .status_code = status_code,
                                              .streaming = streaming,
                                              .countdown_tick = countdown_tick});
}

bool retry_cancel_requested(RetryOptions const& options, Transport::CancelCallback const& cancel_requested)
{
  return (options.cancel_requested && options.cancel_requested()) || (cancel_requested && cancel_requested());
}

ava::core::Error retry_canceled_error()
{
  return ava::core::Error(ava::core::ErrorCategory::Unknown, "transport retry canceled");
}

ava::core::VoidResult sleep_before_retry(RetryOptions const& options, std::size_t attempt, std::size_t max_attempts, int delay_ms, std::string_view reason,
                                         int status_code, bool streaming, Transport::CancelCallback const& cancel_requested = nullptr)
{
  if (delay_ms <= 0)
    return {};
  auto const tick_ms = std::max(0, options.countdown_tick_ms);
  auto remaining_ms = delay_ms;
  while (remaining_ms > 0)
  {
    if (retry_cancel_requested(options, cancel_requested))
      return std::unexpected(retry_canceled_error());
    auto const chunk_ms = tick_ms > 0 ? std::min(tick_ms, remaining_ms) : remaining_ms;
    std::this_thread::sleep_for(std::chrono::milliseconds(chunk_ms));
    remaining_ms -= chunk_ms;
    if (retry_cancel_requested(options, cancel_requested))
      return std::unexpected(retry_canceled_error());
    if (tick_ms > 0)
    {
      if (auto published =
              publish_retry_event(options, attempt, max_attempts, delay_ms, static_cast<std::size_t>(remaining_ms), reason, status_code, streaming, true);
          !published)
      {
        return std::unexpected(std::move(published.error()));
      }
    }
  }
  return {};
}

std::optional<std::string> default_text_from_json(std::string_view body)
{
  if (auto output = ava::core::json::string_field(body, "output_text"))
    return output;
  if (auto text = ava::core::json::string_field(body, "text"))
    return text;
  if (auto delta = ava::core::json::string_field(body, "delta"))
    return delta;
  return std::nullopt;
}

ava::core::Result<std::vector<StreamEvent>> default_parse_response(HttpResponse const& response, bool stream)
{
  if (response.status_code < 200 || response.status_code >= 300)
  {
    auto error = ava::core::Error(ava::core::ErrorCategory::Provider, "provider HTTP request failed with status " + std::to_string(response.status_code));
    error.with_context("status", std::to_string(response.status_code));
    error.with_context("provider_error_kind", to_string(classify_provider_error(response)));
    if (auto const retry_after = retry_after_header(response))
      error.with_context("retry_after", *retry_after);
    return std::unexpected(std::move(error));
  }
  if (stream)
  {
    std::vector<StreamEvent> events;
    std::size_t line_start = 0;
    while (line_start <= response.body.size())
    {
      auto const newline = response.body.find('\n', line_start);
      auto line = newline == std::string::npos ? std::string_view(response.body).substr(line_start)
                                               : std::string_view(response.body).substr(line_start, newline - line_start);
      if (!line.empty() && line.back() == '\r')
        line.remove_suffix(1);
      if (line.starts_with("data:"))
      {
        line.remove_prefix(5);
        if (!line.empty() && line.front() == ' ')
          line.remove_prefix(1);
        if (line == "[DONE]")
        {
          events.push_back(
              StreamEvent{.type = StreamEventType::Done, .text = "", .tool_call_id = "", .tool_name = "", .error_message = "", .usage = std::nullopt});
        }
        else if (auto text = default_text_from_json(line))
        {
          events.push_back(
              StreamEvent{.type = StreamEventType::TextDelta, .text = *text, .tool_call_id = "", .tool_name = "", .error_message = "", .usage = std::nullopt});
        }
      }
      if (newline == std::string::npos)
        break;
      line_start = newline + 1;
    }
    return events;
  }
  auto text = default_text_from_json(response.body);
  if (!text)
  {
    return std::unexpected(ava::core::Error(ava::core::ErrorCategory::Provider, "provider response text is missing"));
  }
  return std::vector<StreamEvent>{
      StreamEvent{.type = StreamEventType::TextDelta, .text = *text, .tool_call_id = "", .tool_name = "", .error_message = "", .usage = std::nullopt},
      StreamEvent{.type = StreamEventType::Done, .text = "", .tool_call_id = "", .tool_name = "", .error_message = "", .usage = std::nullopt}};
}

ava::core::Result<std::vector<StreamEvent>> DefaultStreamParser::finish()
{
  return default_parse_response(HttpResponse{.status_code = 200, .headers = {}, .body = std::move(pending_)}, true);
}

std::optional<int> retry_after_ms(HttpResponse const& response, int max_retry_after_ms)
{
  auto const value = retry_after_header(response);
  if (!value)
    return std::nullopt;
  std::size_t index = 0;
  while (index < value->size() && std::isspace(static_cast<unsigned char>((*value)[index])) != 0) ++index;
  auto const start = index;
  while (index < value->size() && std::isdigit(static_cast<unsigned char>((*value)[index])) != 0) ++index;
  if (index == start)
    return std::nullopt;
  try
  {
    auto const seconds = std::stoi(value->substr(start, index - start));
    if (seconds < 0)
      return std::nullopt;
    long long const delay_ms = static_cast<long long>(seconds) * 1000LL;
    return static_cast<int>(std::min(delay_ms, static_cast<long long>(max_retry_after_ms)));
  }
  catch (...)
  {
    return std::nullopt;
  }
}

}  // namespace

ava::core::Result<HttpRequest> Provider::build_request(ProviderRequest const& request, ProviderAuthContext const& auth) const
{
  auto http_request = build_request(request, auth.access_token);
  if (!http_request)
    return http_request;
  if (auto applied = apply_auth_options(*http_request, auth); !applied)
  {
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
  if (cancel_requested && cancel_requested())
  {
    return std::unexpected(ava::core::Error(ava::core::ErrorCategory::Unknown, "transport request canceled"));
  }
  auto response = send(request);
  if (!response)
    return std::unexpected(std::move(response.error()));
  if (cancel_requested && cancel_requested())
  {
    return std::unexpected(ava::core::Error(ava::core::ErrorCategory::Unknown, "transport request canceled"));
  }
  return response;
}

ava::core::Result<HttpResponse> Transport::send_streaming(HttpRequest const& request, BodyChunkSink on_body_chunk, CancelCallback cancel_requested)
{
  if (cancel_requested && cancel_requested())
  {
    return std::unexpected(ava::core::Error(ava::core::ErrorCategory::Unknown, "transport request canceled"));
  }
  auto response = send(request, cancel_requested);
  if (!response)
    return std::unexpected(std::move(response.error()));
  if (cancel_requested && cancel_requested())
  {
    return std::unexpected(ava::core::Error(ava::core::ErrorCategory::Unknown, "transport request canceled"));
  }
  if (on_body_chunk && !response->body.empty())
  {
    if (auto delivered = on_body_chunk(response->body); !delivered)
      return std::unexpected(std::move(delivered.error()));
  }
  if (cancel_requested && cancel_requested())
  {
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
  auto combined_cancel_requested = [this, cancel_requested] { return retry_cancel_requested(options_, cancel_requested); };
  if (combined_cancel_requested())
    return std::unexpected(retry_canceled_error());
  ava::core::Result<HttpResponse> response = inner_.send(request, combined_cancel_requested);
  bool canceled_after_attempt = combined_cancel_requested();
  observe_transport_result(options_.observation, request, response, canceled_after_attempt, true);
  if (canceled_after_attempt)
    return std::unexpected(retry_canceled_error());
  for (int attempt = 1; attempt < max_attempts; ++attempt)
  {
    if (combined_cancel_requested())
      return std::unexpected(retry_canceled_error());
    if (response)
    {
      if (!is_retryable_kind(classify_provider_error(*response)))
        break;
      auto const reason = to_string(classify_provider_error(*response));
      int const delay_ms = retry_after_ms(*response, options_.max_retry_after_ms).value_or(exponential_delay_ms(options_, attempt));
      if (auto published = publish_retry_event(options_, static_cast<std::size_t>(attempt + 1), static_cast<std::size_t>(max_attempts), delay_ms,
                                               static_cast<std::size_t>(delay_ms), reason, response->status_code, false);
          !published)
      {
        return std::unexpected(std::move(published.error()));
      }
      if (auto slept = sleep_before_retry(options_, static_cast<std::size_t>(attempt + 1), static_cast<std::size_t>(max_attempts), delay_ms, reason,
                                          response->status_code, false, cancel_requested);
          !slept)
      {
        return std::unexpected(std::move(slept.error()));
      }
    }
    else
    {
      if (!is_retryable_transport_error(response.error()))
        break;
      int const delay_ms = exponential_delay_ms(options_, attempt);
      if (auto published = publish_retry_event(options_, static_cast<std::size_t>(attempt + 1), static_cast<std::size_t>(max_attempts), delay_ms,
                                               static_cast<std::size_t>(delay_ms), "transport_io", 0, false);
          !published)
      {
        return std::unexpected(std::move(published.error()));
      }
      if (auto slept = sleep_before_retry(options_, static_cast<std::size_t>(attempt + 1), static_cast<std::size_t>(max_attempts), delay_ms, "transport_io", 0,
                                          false, cancel_requested);
          !slept)
      {
        return std::unexpected(std::move(slept.error()));
      }
    }
    if (combined_cancel_requested())
      return std::unexpected(retry_canceled_error());
    response = inner_.send(request, combined_cancel_requested);
    canceled_after_attempt = combined_cancel_requested();
    observe_transport_result(options_.observation, request, response, canceled_after_attempt, true);
    if (canceled_after_attempt)
      return std::unexpected(retry_canceled_error());
  }
  return response;
}

bool RetryTransport::supports_streaming() const noexcept
{
  return inner_.supports_streaming();
}

ava::core::Result<HttpResponse> RetryTransport::send_streaming(HttpRequest const& request, BodyChunkSink on_body_chunk, CancelCallback cancel_requested)
{
  int const max_attempts = std::max(1, options_.max_attempts);
  auto combined_cancel_requested = [this, cancel_requested] { return retry_cancel_requested(options_, cancel_requested); };
  if (combined_cancel_requested())
    return std::unexpected(retry_canceled_error());
  ava::core::Result<HttpResponse> response = std::unexpected(ava::core::Error(ava::core::ErrorCategory::Unknown, "streaming request was not attempted"));
  std::string final_body;
  for (int attempt = 1; attempt <= max_attempts; ++attempt)
  {
    if (combined_cancel_requested())
      return std::unexpected(retry_canceled_error());
    std::string attempt_body;
    bool delivered_chunks = false;
    response = inner_.send_streaming(
        request,
        [&](std::string_view chunk) -> ava::core::VoidResult {
          attempt_body.append(chunk);
          if (!on_body_chunk)
            return ava::core::VoidResult{};
          delivered_chunks = true;
          return on_body_chunk(chunk);
        },
        combined_cancel_requested);
    bool const canceled_after_attempt = combined_cancel_requested();
    observe_transport_result(options_.observation, request, response, canceled_after_attempt, true);
    if (canceled_after_attempt)
      return std::unexpected(retry_canceled_error());

    bool const last_attempt = attempt == max_attempts;
    if (response)
    {
      if (!last_attempt && !delivered_chunks && is_retryable_kind(classify_provider_error(*response)))
      {
        auto const reason = to_string(classify_provider_error(*response));
        int const delay_ms = retry_after_ms(*response, options_.max_retry_after_ms).value_or(exponential_delay_ms(options_, attempt));
        if (auto published = publish_retry_event(options_, static_cast<std::size_t>(attempt + 1), static_cast<std::size_t>(max_attempts), delay_ms,
                                                 static_cast<std::size_t>(delay_ms), reason, response->status_code, true);
            !published)
        {
          return std::unexpected(std::move(published.error()));
        }
        if (auto slept = sleep_before_retry(options_, static_cast<std::size_t>(attempt + 1), static_cast<std::size_t>(max_attempts), delay_ms, reason,
                                            response->status_code, true, cancel_requested);
            !slept)
        {
          return std::unexpected(std::move(slept.error()));
        }
        continue;
      }
      if (!delivered_chunks)
        final_body = std::move(attempt_body);
      break;
    }

    if (last_attempt || delivered_chunks || !is_retryable_transport_error(response.error()))
      break;
    int const delay_ms = exponential_delay_ms(options_, attempt);
    if (auto published = publish_retry_event(options_, static_cast<std::size_t>(attempt + 1), static_cast<std::size_t>(max_attempts), delay_ms,
                                             static_cast<std::size_t>(delay_ms), "transport_io", 0, true);
        !published)
    {
      return std::unexpected(std::move(published.error()));
    }
    if (auto slept = sleep_before_retry(options_, static_cast<std::size_t>(attempt + 1), static_cast<std::size_t>(max_attempts), delay_ms, "transport_io", 0,
                                        true, cancel_requested);
        !slept)
    {
      return std::unexpected(std::move(slept.error()));
    }
  }
  if (!response)
    return std::unexpected(std::move(response.error()));
  if (combined_cancel_requested())
    return std::unexpected(retry_canceled_error());
  if (on_body_chunk && !final_body.empty())
  {
    if (auto delivered = on_body_chunk(final_body); !delivered)
      return std::unexpected(std::move(delivered.error()));
  }
  return response;
}

ObservedTransport::ObservedTransport(Transport& inner, TransportObservation observation) : inner_(inner), observation_(std::move(observation))
{
}

ava::core::Result<HttpResponse> ObservedTransport::send(HttpRequest const& request)
{
  return send(request, nullptr);
}

ava::core::Result<HttpResponse> ObservedTransport::send(HttpRequest const& request, CancelCallback cancel_requested)
{
  bool cancellation_observed = false;
  auto forwarded_cancel_requested = cancel_requested ? CancelCallback{[&cancel_requested, &cancellation_observed] {
    bool const canceled = cancel_requested();
    cancellation_observed = cancellation_observed || canceled;
    return canceled;
  }}
                                                     : CancelCallback{};
  auto result = inner_.send(request, std::move(forwarded_cancel_requested));
  observe_transport_result(observation_, request, result, cancellation_observed);
  return result;
}

bool ObservedTransport::supports_streaming() const noexcept
{
  return inner_.supports_streaming();
}

ava::core::Result<HttpResponse> ObservedTransport::send_streaming(HttpRequest const& request, BodyChunkSink on_body_chunk, CancelCallback cancel_requested)
{
  bool cancellation_observed = false;
  auto forwarded_cancel_requested = cancel_requested ? CancelCallback{[&cancel_requested, &cancellation_observed] {
    bool const canceled = cancel_requested();
    cancellation_observed = cancellation_observed || canceled;
    return canceled;
  }}
                                                     : CancelCallback{};
  auto result = inner_.send_streaming(request, std::move(on_body_chunk), std::move(forwarded_cancel_requested));
  observe_transport_result(observation_, request, result, cancellation_observed);
  return result;
}

void observe_transport_result(TransportObservation const& observation, HttpRequest const& request, ava::core::Result<HttpResponse> const& result, bool canceled,
                              bool attempt) noexcept
{
  if (!observation.observation)
    return;
  observation.observation->emit(
      attempt ? ava::observability::TraceEventType::TransportAttemptResult : ava::observability::TraceEventType::TransportRequestResult, observation.context,
      [&](auto& event) {
        event.phase = ava::observability::TracePhase::Transport;
        event.outcome = canceled ? ava::observability::TraceOutcome::Canceled
                        : result ? (result->status_code >= 200 && result->status_code < 300 ? ava::observability::TraceOutcome::Success
                                                                                            : ava::observability::TraceOutcome::Error)
                                 : ava::observability::TraceOutcome::Error;
        event.fields = {{.key = "request_bytes", .value = std::to_string(request.body.size())},
                        {.key = "status_code", .value = result ? std::to_string(result->status_code) : "0"}};
      });
}

void observe_transport_retry(TransportObservation const& observation, std::size_t next_attempt, std::size_t max_attempts, std::size_t delay_ms,
                             std::string_view reason, int status_code, bool streaming) noexcept
{
  if (!observation.observation)
    return;
  observation.observation->emit(ava::observability::TraceEventType::TransportRetry, observation.context, [&](auto& event) {
    event.phase = ava::observability::TracePhase::Transport;
    event.outcome = ava::observability::TraceOutcome::Retrying;
    event.fields = {{.key = "next_attempt", .value = std::to_string(next_attempt)}, {.key = "max_attempts", .value = std::to_string(max_attempts)},
                    {.key = "delay_ms", .value = std::to_string(delay_ms)},         {.key = "status_code", .value = std::to_string(status_code)},
                    {.key = "streaming", .value = streaming ? "true" : "false"},    {.key = "reason", .value = std::string(reason)}};
  });
}

std::string to_string(AssistantPhase phase)
{
  switch (phase)
  {
    case AssistantPhase::Unknown:
      return "unknown";
    case AssistantPhase::Commentary:
      return "commentary";
    case AssistantPhase::FinalAnswer:
      return "final_answer";
  }
  return "unknown";
}

std::optional<AssistantPhase> assistant_phase_from_string(std::string_view value)
{
  if (value == "commentary")
    return AssistantPhase::Commentary;
  if (value == "final_answer")
    return AssistantPhase::FinalAnswer;
  return std::nullopt;
}

bool is_known_assistant_phase(AssistantPhase phase) noexcept
{
  return phase == AssistantPhase::Commentary || phase == AssistantPhase::FinalAnswer;
}

std::string to_string(StreamEventType type)
{
  switch (type)
  {
    case StreamEventType::TextStart:
      return "text_start";
    case StreamEventType::TextDelta:
      return "text_delta";
    case StreamEventType::TextEnd:
      return "text_end";
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
  switch (kind)
  {
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
  if (response.status_code >= 200 && response.status_code < 300)
    return ProviderErrorKind::Unknown;
  auto const body = lower_copy(response.body);
  if (response.status_code == 401 || response.status_code == 403)
    return ProviderErrorKind::Authentication;
  if (response.status_code == 402)
    return ProviderErrorKind::Quota;
  if (looks_like_context_overflow(body))
    return ProviderErrorKind::ContextOverflow;
  if (looks_like_quota(body))
    return ProviderErrorKind::Quota;
  if (looks_like_content_filter(body))
    return ProviderErrorKind::ContentFilter;
  if (looks_like_refusal(body))
    return ProviderErrorKind::Refusal;
  if (response.status_code == 429)
    return ProviderErrorKind::RateLimited;
  if (response.status_code == 400 || response.status_code == 404 || response.status_code == 422)
  {
    return ProviderErrorKind::InvalidRequest;
  }
  if (response.status_code == 408 || response.status_code == 409 || (response.status_code >= 500 && response.status_code < 600))
  {
    return ProviderErrorKind::Transient;
  }
  return ProviderErrorKind::Unknown;
}

std::optional<std::string> retry_after_header(HttpResponse const& response)
{
  constexpr unsigned int kMaxRetryAfterSeconds = 86'400;
  for (auto const& [key, value] : response.headers)
  {
    if (lower_copy(key) != "retry-after" || value.empty() || value.size() > 5)
      continue;
    unsigned int seconds = 0;
    for (unsigned char const ch : value)
    {
      if (std::isdigit(ch) == 0 || seconds > (kMaxRetryAfterSeconds - static_cast<unsigned int>(ch - '0')) / 10U)
        return std::nullopt;
      seconds = seconds * 10U + static_cast<unsigned int>(ch - '0');
    }
    return std::to_string(seconds);
  }
  return std::nullopt;
}

bool is_context_overflow_error(ava::core::Error const& error)
{
  if (error.category() != ava::core::ErrorCategory::Provider)
    return false;
  if (looks_like_context_overflow(error.message()))
    return true;
  for (auto const& item : error.context())
  {
    if ((item.key == "provider_error_kind" && item.value == "context_overflow") || looks_like_context_overflow(item.value))
      return true;
  }
  return false;
}

ImageInputPolicy image_input_policy_for_api_family(std::string_view api_family) noexcept
{
  auto policy = ImageInputPolicy{};
  if (api_family == "anthropic_messages")
    policy.max_bytes_per_image = 5 * 1024 * 1024;
  return policy;
}

bool is_supported_image_mime_type(std::string_view mime_type)
{
  return mime_type == "image/png" || mime_type == "image/jpeg" || mime_type == "image/webp" || mime_type == "image/gif";
}

bool request_has_image_parts(ProviderRequest const& request)
{
  for (auto const& message : request.messages)
  {
    for (auto const& part : message.content_parts)
    {
      if (part.type == ContentPartType::Image)
        return true;
    }
  }
  return false;
}

bool valid_image_storage_path(std::string_view path)
{
  if (path.empty() || path.size() > kMaxImageStoragePathBytes || has_control_byte(path))
    return false;
  if (!path.starts_with("attachments/"))
    return false;
  if (path.starts_with('/') || path.starts_with('~') || path.find('\\') != std::string_view::npos)
    return false;
  if (path.find(':') != std::string_view::npos)
    return false;
  std::size_t segment_start = 0;
  while (segment_start <= path.size())
  {
    auto const slash = path.find('/', segment_start);
    auto const segment = path.substr(segment_start, slash == std::string_view::npos ? std::string_view::npos : slash - segment_start);
    if (segment.empty() || segment == "." || segment == "..")
      return false;
    if (slash == std::string_view::npos)
      break;
    segment_start = slash + 1;
  }
  return true;
}

ava::core::VoidResult validate_image_content_parts(ProviderRequest const& request, bool model_supports_images)
{
  auto const policy = image_input_policy_for_api_family({});
  std::size_t image_count = 0;
  std::size_t total_image_bytes = 0;
  for (std::size_t message_index = 0; message_index < request.messages.size(); ++message_index)
  {
    auto const& message = request.messages[message_index];
    for (std::size_t part_index = 0; part_index < message.content_parts.size(); ++part_index)
    {
      auto const& part = message.content_parts[part_index];
      if (part.type != ContentPartType::Image)
        continue;
      if (!model_supports_images)
      {
        return std::unexpected(image_part_error("selected model does not support image input", message_index, part_index));
      }
      if (message.role != "user")
      {
        return std::unexpected(image_part_error("image content requires user role", message_index, part_index));
      }
      if (part.attachment_id.empty() || part.attachment_id.size() > kMaxImageAttachmentIdBytes || has_control_byte(part.attachment_id))
      {
        return std::unexpected(image_part_error("image attachment id is invalid", message_index, part_index));
      }
      if (!is_supported_image_mime_type(part.mime_type))
      {
        return std::unexpected(image_part_error("image attachment MIME type is not supported", message_index, part_index));
      }
      if (!valid_image_storage_path(part.storage_path))
      {
        return std::unexpected(image_part_error("image attachment storage path is invalid", message_index, part_index));
      }
      if (part.byte_size == 0 || part.byte_size > policy.max_bytes_per_image)
      {
        return std::unexpected(image_part_error("image attachment byte size is outside supported limits", message_index, part_index));
      }
      if (part.sha256.size() != 64 || !is_hex_string(part.sha256))
      {
        return std::unexpected(image_part_error("image attachment sha256 is invalid", message_index, part_index));
      }
      if (!part.data_base64.empty() && !is_valid_base64(part.data_base64))
      {
        return std::unexpected(image_part_error("image attachment base64 payload is invalid", message_index, part_index));
      }
      ++image_count;
      if (image_count > policy.max_attachments_per_request)
      {
        return std::unexpected(image_part_error("image attachment count exceeds supported limits", message_index, part_index));
      }
      if (part.byte_size > policy.max_bytes_per_request - total_image_bytes)
      {
        return std::unexpected(image_part_error("image attachment total byte size exceeds supported limits", message_index, part_index));
      }
      total_image_bytes += part.byte_size;
    }
  }
  return {};
}

}  // namespace ava::provider
