#include "ava/llm/providers/anthropic_provider.hpp"

#include <string_view>
#include <utility>

#include "ava/core/string_utils.hpp"
#include "ava/llm/pricing.hpp"
#include "ava/llm/providers/anthropic_protocol.hpp"

#include "sse_utils.hpp"

#if AVA_WITH_CPR
#include <cpr/cpr.h>
#endif

namespace ava::llm {
namespace {

constexpr std::uint32_t kDefaultMaxTokens = 4096;
constexpr std::string_view kDefaultAnthropicVersion = "2023-06-01";

#if AVA_WITH_CPR
constexpr auto kAnthropicTimeoutMs = 120000;

[[nodiscard]] std::string summarize_anthropic_error_body(std::string_view body) {
  const auto trimmed = ava::core::trim_ascii_view(body);
  if(trimmed.empty()) {
    return "request failed";
  }

  try {
    const auto parsed = nlohmann::json::parse(trimmed);
    if(parsed.contains("error") && parsed.at("error").is_object()) {
      const auto& error = parsed.at("error");
      const auto message = error.value("message", std::string{});
      if(!message.empty()) {
        return message;
      }
    }
    if(parsed.contains("message") && parsed.at("message").is_string()) {
      const auto message = parsed.at("message").get<std::string>();
      if(!message.empty()) {
        return message;
      }
    }
  } catch(const std::exception&) {
    // Fall back to the raw body when the payload is not JSON.
  }

  return std::string(trimmed);
}

[[nodiscard]] ProviderError classify_anthropic_error(
    const cpr::Response& response,
    std::string_view provider_label,
    std::optional<std::string_view> body_override = std::nullopt
) {
  const auto status = response.status_code > 0
                          ? std::optional<std::uint16_t>(static_cast<std::uint16_t>(response.status_code))
                          : std::nullopt;

  return classify_provider_error(
      std::string(provider_label),
      status,
      summarize_anthropic_error_body(body_override.value_or(response.text)),
      providers::parse_retry_after_secs(response.header)
  );
}

#endif

}  // namespace

AnthropicProvider::AnthropicProvider(
    std::string model,
    std::string api_key,
    std::string base_url,
    std::string anthropic_version,
    std::string provider_label
)
    : model_(std::move(model)),
      api_key_(std::move(api_key)),
      base_url_(std::move(base_url)),
      anthropic_version_(std::move(anthropic_version)),
      provider_label_(std::move(provider_label)) {}

AnthropicProvider AnthropicProvider::from_credential(
    const std::string& model,
    const ava::config::ProviderCredential& credential
) {
  const auto api_key = credential.effective_api_key();
  if(!api_key.has_value()) {
    throw ProviderException(ProviderError{
        .kind = ProviderErrorKind::AuthFailure,
        .provider = "anthropic",
        .message = "missing api key for anthropic provider credential",
    });
  }

  return AnthropicProvider(
      model,
      *api_key,
      credential.base_url.value_or("https://api.anthropic.com"),
      std::string(kDefaultAnthropicVersion)
  );
}

std::string AnthropicProvider::model_name() const {
  return model_;
}

ProviderKind AnthropicProvider::provider_kind() const {
  return ProviderKind::Anthropic;
}

ProviderCapabilities AnthropicProvider::capabilities() const {
  ProviderCapabilities caps;
#if AVA_WITH_CPR
  caps.supports_streaming = true;
#else
  caps.supports_streaming = false;
#endif
  caps.supports_tool_use = true;
  caps.supports_thinking = false;
  caps.supports_thinking_levels = false;
  return caps;
}

std::size_t AnthropicProvider::estimate_tokens(std::string_view input) const {
  return ava::llm::estimate_tokens(input);
}

double AnthropicProvider::estimate_cost(std::size_t input_tokens, std::size_t output_tokens) const {
  return estimate_cost_usd(provider_label_, model_, input_tokens, output_tokens, false);
}

bool AnthropicProvider::supports_tools() const {
  return true;
}

LlmResponse AnthropicProvider::generate(
    const std::vector<ChatMessage>& messages,
    const std::vector<types::Tool>& tools,
    ThinkingConfig thinking
) const {
#if AVA_WITH_CPR
  const auto resolved_thinking = resolve_thinking_config(thinking);
  const auto request_body = anthropic::build_messages_request(
      model_, messages, tools, kDefaultMaxTokens, resolved_thinking.applied
  );

  cpr::Header headers{
      {"x-api-key", api_key_},
      {"anthropic-version", anthropic_version_},
      {"Content-Type", "application/json"},
      {"Accept", "application/json"},
  };
  if(provider_label_ != "anthropic") {
    headers["User-Agent"] = "ava/coding-agent";
  }

  const auto response = cpr::Post(
      cpr::Url{messages_url()},
      headers,
      cpr::Body{request_body.dump()},
      cpr::Timeout{kAnthropicTimeoutMs}
  );

  if(response.error.code != cpr::ErrorCode::OK) {
    throw ProviderException(classify_provider_error(provider_label_, std::nullopt, response.error.message));
  }

  if(response.status_code < 200 || response.status_code > 299) {
    throw ProviderException(classify_anthropic_error(response, provider_label_));
  }

  try {
    return anthropic::parse_messages_response(nlohmann::json::parse(response.text));
  } catch(const ProviderException& ex) {
    auto error = ex.error();
    error.provider = provider_label_;
    throw ProviderException(std::move(error));
  } catch(const std::exception& ex) {
    throw ProviderException(ProviderError{
        .kind = ProviderErrorKind::Unknown,
        .provider = provider_label_,
        .message = std::string("failed to parse completion response: ") + ex.what(),
    });
  }
#else
  (void)messages;
  (void)tools;
  (void)thinking;
  throw ProviderException(ProviderError{
      .kind = ProviderErrorKind::Unknown,
      .provider = provider_label_,
      .message = provider_label_ + " provider requires AVA_WITH_CPR=ON",
  });
#endif
}

std::vector<types::StreamChunk> AnthropicProvider::generate_stream(
    const std::vector<ChatMessage>& messages,
    const std::vector<types::Tool>& tools,
    ThinkingConfig thinking
) const {
  std::vector<types::StreamChunk> chunks;
  (void)stream_generate(messages, tools, thinking, [&](const types::StreamChunk& chunk) {
    chunks.push_back(chunk);
    return true;
  });
  return chunks;
}

Provider::StreamDispatchResult AnthropicProvider::stream_generate(
    const std::vector<ChatMessage>& messages,
    const std::vector<types::Tool>& tools,
    ThinkingConfig thinking,
    const StreamChunkSink& on_chunk
) const {
#if AVA_WITH_CPR
  const auto resolved_thinking = resolve_thinking_config(thinking);
  const auto request_body = anthropic::build_messages_request(
      model_, messages, tools, kDefaultMaxTokens, resolved_thinking.applied, true
  );

  cpr::Header headers{
      {"x-api-key", api_key_},
      {"anthropic-version", anthropic_version_},
      {"Content-Type", "application/json"},
      {"Accept", "text/event-stream"},
  };
  if(provider_label_ != "anthropic") {
    headers["User-Agent"] = "ava/coding-agent";
  }

  bool emitted_done = false;
  bool stop_requested = false;
  std::optional<ProviderError> stream_error;
  std::optional<std::string> parse_failure;
  std::string raw_response_body;
  providers::SseEventBuffer sse_events;

  auto dispatch_sse_payload = [&](std::string_view payload_view) {
    const auto payload = ava::core::trim_ascii_view(payload_view);
    if(payload.empty()) {
      return true;
    }

    try {
      const auto event = nlohmann::json::parse(payload);
      const auto parsed_chunks = anthropic::parse_stream_events(event);
      for(const auto& parsed : parsed_chunks) {
        emitted_done = emitted_done || parsed.done;
        if(on_chunk && !on_chunk(parsed)) {
          stop_requested = true;
          return false;
        }
      }
    } catch(const ProviderException& ex) {
      stream_error = ex.error();
      stream_error->provider = provider_label_;
      return false;
    } catch(const std::exception& ex) {
      parse_failure = ex.what();
      return false;
    }

    return true;
  };

  auto write_callback = cpr::WriteCallback{
      [&](const std::string_view& data, intptr_t /*userdata*/) {
        try {
          raw_response_body.append(data.data(), data.size());
          return sse_events.append(data, dispatch_sse_payload);
        } catch(const std::exception& ex) {
          parse_failure = ex.what();
          return false;
        } catch(...) {
          parse_failure = "unknown stream callback failure";
          return false;
        }
      },
      0
  };

  const auto response = cpr::Post(
      cpr::Url{messages_url()},
      headers,
      cpr::Body{request_body.dump()},
      cpr::Timeout{kAnthropicTimeoutMs},
      write_callback
  );

  if(!stop_requested && response.error.code == cpr::ErrorCode::OK && response.status_code >= 200 && response.status_code <= 299) {
    (void)sse_events.flush(dispatch_sse_payload);
  }

  if(stop_requested) {
    return StreamDispatchResult::Completed;
  }

  if(stream_error.has_value()) {
    throw ProviderException(*stream_error);
  }

  if(parse_failure.has_value()) {
    throw ProviderException(ProviderError{
        .kind = ProviderErrorKind::Unknown,
        .provider = provider_label_,
        .message = "failed to parse stream event: " + *parse_failure,
    });
  }

  if(response.error.code != cpr::ErrorCode::OK) {
    throw ProviderException(classify_provider_error(provider_label_, std::nullopt, response.error.message));
  }
  if(response.status_code < 200 || response.status_code > 299) {
    throw ProviderException(classify_anthropic_error(response, provider_label_, raw_response_body));
  }

  if(!emitted_done && on_chunk) {
    (void)on_chunk(types::StreamChunk::finished());
  }
  return StreamDispatchResult::Completed;
#else
  (void)messages;
  (void)tools;
  (void)thinking;
  (void)on_chunk;
  return StreamDispatchResult::Unsupported;
#endif
}

std::string AnthropicProvider::messages_url() const {
  const auto base = base_url_.ends_with('/') ? base_url_.substr(0, base_url_.size() - 1) : base_url_;
  if(base.ends_with("/v1")) {
    return base + "/messages";
  }
  return base + "/v1/messages";
}

}  // namespace ava::llm
