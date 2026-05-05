#include "ava/provider/provider_error_support.h"

#include <algorithm>
#include <cctype>
#include <optional>
#include <string>
#include <string_view>

#include "ava/core/error.h"

namespace ava::provider {
namespace detail {

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
  auto const lowered = lower_copy(text);
  return has_any(lowered, {"quota", "billing", "insufficient_quota", "credit balance"});
}

bool looks_like_content_filter(std::string_view text)
{
  auto const lowered = lower_copy(text);
  return has_any(lowered, {"content filter", "content_filter", "safety", "policy violation", "blocked"});
}

bool looks_like_refusal(std::string_view text)
{
  auto const lowered = lower_copy(text);
  return has_any(lowered, {"refusal", "refused", "cannot comply", "can't comply"});
}

}  // namespace detail

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
  auto const body = detail::lower_copy(response.body);
  if (response.status_code == 401 || response.status_code == 403) return ProviderErrorKind::Authentication;
  if (detail::looks_like_context_overflow(body)) return ProviderErrorKind::ContextOverflow;
  if (detail::looks_like_quota(body)) return ProviderErrorKind::Quota;
  if (detail::looks_like_content_filter(body)) return ProviderErrorKind::ContentFilter;
  if (detail::looks_like_refusal(body)) return ProviderErrorKind::Refusal;
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
    if (detail::lower_copy(key) == "retry-after") return value;
  }
  return std::nullopt;
}

bool is_context_overflow_error(ava::core::Error const& error)
{
  if (error.category() != ava::core::ErrorCategory::Provider) return false;
  if (detail::looks_like_context_overflow(error.message())) return true;
  for (auto const& item : error.context()) {
    if (detail::looks_like_context_overflow(item.value)) return true;
  }
  return false;
}

}  // namespace ava::provider
