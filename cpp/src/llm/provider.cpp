#include "ava/llm/provider.hpp"

#include <algorithm>
#include <cctype>

namespace ava::llm {
namespace {

[[nodiscard]] std::string to_lower_ascii(std::string_view value) {
  std::string lower(value);
  std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char ch) {
    return static_cast<char>(std::tolower(ch));
  });
  return lower;
}

}  // namespace

ProviderError classify_provider_error(
    std::string provider,
    std::optional<std::uint16_t> status,
    std::string_view body_or_message,
    std::optional<std::uint64_t> retry_after_secs
) {
  const std::string lower = to_lower_ascii(body_or_message);

  ProviderError error{
      .kind = ProviderErrorKind::Unknown,
      .provider = std::move(provider),
      .message = std::string(body_or_message),
      .retry_after_secs = retry_after_secs,
      .status = status,
  };

  if(status.has_value()) {
    if(*status == 429) {
      error.kind = ProviderErrorKind::RateLimit;
      return error;
    }
    if(*status == 401 || *status == 403 || *status == 402) {
      error.kind = ProviderErrorKind::AuthFailure;
      return error;
    }
    if(*status >= 500 && *status <= 599) {
      error.kind = ProviderErrorKind::ServerError;
      return error;
    }
    if(*status == 404 && lower.find("model") != std::string::npos) {
      error.kind = ProviderErrorKind::ModelNotFound;
      return error;
    }
  }

  if(lower.find("rate limit") != std::string::npos || lower.find("too many requests") != std::string::npos) {
    error.kind = ProviderErrorKind::RateLimit;
  } else if(
      lower.find("unauthorized") != std::string::npos || lower.find("forbidden") != std::string::npos
      || lower.find("auth") != std::string::npos
  ) {
    error.kind = ProviderErrorKind::AuthFailure;
  } else if(
      lower.find("context_length_exceeded") != std::string::npos
      || lower.find("maximum context length") != std::string::npos
      || lower.find("token limit") != std::string::npos || lower.find("too many tokens") != std::string::npos
      || lower.find("context window") != std::string::npos
  ) {
    error.kind = ProviderErrorKind::ContextWindowExceeded;
  } else if(
      (lower.find("model") != std::string::npos && lower.find("not found") != std::string::npos)
      || lower.find("does not exist") != std::string::npos || lower.find("no such model") != std::string::npos
      || lower.find("engine not found") != std::string::npos
  ) {
    error.kind = ProviderErrorKind::ModelNotFound;
  } else if(lower.find("timed out") != std::string::npos || lower.find("timeout") != std::string::npos) {
    error.kind = ProviderErrorKind::Timeout;
  } else if(
      lower.find("network") != std::string::npos || lower.find("connection") != std::string::npos
      || lower.find("dns") != std::string::npos
  ) {
    error.kind = ProviderErrorKind::NetworkError;
  } else if(
      lower.find("server error") != std::string::npos || lower.find("internal server error") != std::string::npos
  ) {
    error.kind = ProviderErrorKind::ServerError;
  }

  return error;
}

bool is_retryable(ProviderErrorKind kind) {
  return kind == ProviderErrorKind::RateLimit || kind == ProviderErrorKind::Timeout
         || kind == ProviderErrorKind::NetworkError || kind == ProviderErrorKind::ServerError;
}

bool is_retryable(const ProviderError& error) {
  return is_retryable(error.kind);
}

ProviderException::ProviderException(ProviderError error)
    : std::runtime_error(error.provider + ": " + error.message), error_(std::move(error)) {}

const ProviderError& ProviderException::error() const noexcept {
  return error_;
}

ChatMessage ChatMessage::system(std::string text) {
  return ChatMessage{.role = types::Role::System, .content = std::move(text)};
}

ChatMessage ChatMessage::user(std::string text) {
  return ChatMessage{.role = types::Role::User, .content = std::move(text)};
}

ChatMessage ChatMessage::assistant(std::string text) {
  return ChatMessage{.role = types::Role::Assistant, .content = std::move(text)};
}

ChatMessage ChatMessage::tool(std::string text, std::string call_id) {
  return ChatMessage{
      .role = types::Role::Tool,
      .content = std::move(text),
      .tool_calls = {},
      .tool_call_id = std::move(call_id),
  };
}

}  // namespace ava::llm
