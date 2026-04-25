#include "ava/llm/providers/anthropic_provider.hpp"

#include <algorithm>
#include <charconv>
#include <cctype>
#include <string_view>
#include <system_error>
#include <utility>

#include "ava/llm/pricing.hpp"
#include "ava/llm/providers/anthropic_protocol.hpp"

#if AVA_WITH_CPR
#include <cpr/cpr.h>
#endif

namespace ava::llm {
namespace {

constexpr std::uint32_t kDefaultMaxTokens = 4096;
constexpr std::string_view kDefaultAnthropicVersion = "2023-06-01";

#if AVA_WITH_CPR
[[nodiscard]] std::string_view trim_ascii(std::string_view value) {
  while(!value.empty() && std::isspace(static_cast<unsigned char>(value.front())) != 0) {
    value.remove_prefix(1);
  }
  while(!value.empty() && std::isspace(static_cast<unsigned char>(value.back())) != 0) {
    value.remove_suffix(1);
  }
  return value;
}

[[nodiscard]] std::optional<std::uint64_t> parse_retry_after_secs(const cpr::Header& headers) {
  for(const auto& [name, value] : headers) {
    std::string lower_name(name);
    std::transform(lower_name.begin(), lower_name.end(), lower_name.begin(), [](unsigned char ch) {
      return static_cast<char>(std::tolower(ch));
    });
    if(lower_name != "retry-after") {
      continue;
    }

    const auto trimmed = trim_ascii(value);
    if(trimmed.empty()) {
      return std::nullopt;
    }

    std::uint64_t parsed = 0;
    const auto* begin = trimmed.data();
    const auto* end = begin + trimmed.size();
    const auto [ptr, ec] = std::from_chars(begin, end, parsed);
    if(ec == std::errc{} && ptr == end) {
      return parsed;
    }
    return std::nullopt;
  }

  return std::nullopt;
}

[[nodiscard]] std::string summarize_anthropic_error_body(std::string_view body) {
  const auto trimmed = trim_ascii(body);
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

[[nodiscard]] ProviderError classify_anthropic_error(const cpr::Response& response) {
  const auto status = response.status_code > 0
                          ? std::optional<std::uint16_t>(static_cast<std::uint16_t>(response.status_code))
                          : std::nullopt;

  return classify_provider_error(
      "anthropic",
      status,
      summarize_anthropic_error_body(response.text),
      parse_retry_after_secs(response.header)
  );
}
#endif

}  // namespace

AnthropicProvider::AnthropicProvider(
    std::string model,
    std::string api_key,
    std::string base_url,
    std::string anthropic_version
)
    : model_(std::move(model)),
      api_key_(std::move(api_key)),
      base_url_(std::move(base_url)),
      anthropic_version_(std::move(anthropic_version)) {}

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
  caps.supports_streaming = false;
  caps.supports_tool_use = true;
  caps.supports_thinking = false;
  caps.supports_thinking_levels = false;
  return caps;
}

std::size_t AnthropicProvider::estimate_tokens(std::string_view input) const {
  return ava::llm::estimate_tokens(input);
}

double AnthropicProvider::estimate_cost(std::size_t input_tokens, std::size_t output_tokens) const {
  return estimate_cost_usd("anthropic", model_, input_tokens, output_tokens, false);
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

  const auto response = cpr::Post(
      cpr::Url{messages_url()},
      cpr::Header{
          {"x-api-key", api_key_},
          {"anthropic-version", anthropic_version_},
          {"Content-Type", "application/json"},
          {"Accept", "application/json"},
      },
      cpr::Body{request_body.dump()},
      cpr::Timeout{120000}
  );

  if(response.error.code != cpr::ErrorCode::OK) {
    throw ProviderException(classify_provider_error("anthropic", std::nullopt, response.error.message));
  }

  if(response.status_code < 200 || response.status_code > 299) {
    throw ProviderException(classify_anthropic_error(response));
  }

  try {
    return anthropic::parse_messages_response(nlohmann::json::parse(response.text));
  } catch(const ProviderException&) {
    throw;
  } catch(const std::exception& ex) {
    throw ProviderException(ProviderError{
        .kind = ProviderErrorKind::Unknown,
        .provider = "anthropic",
        .message = std::string("failed to parse completion response: ") + ex.what(),
    });
  }
#else
  (void)messages;
  (void)tools;
  (void)thinking;
  throw ProviderException(ProviderError{
      .kind = ProviderErrorKind::Unknown,
      .provider = "anthropic",
      .message = "Anthropic provider requires AVA_WITH_CPR=ON",
  });
#endif
}

std::vector<types::StreamChunk> AnthropicProvider::generate_stream(
    const std::vector<ChatMessage>& messages,
    const std::vector<types::Tool>& tools,
    ThinkingConfig thinking
) const {
  (void)messages;
  (void)tools;
  (void)thinking;
  throw ProviderException(ProviderError{
      .kind = ProviderErrorKind::Unknown,
      .provider = "anthropic",
      .message = "Anthropic streaming is not implemented in C++ Milestone 23 scoped provider slice",
  });
}

Provider::StreamDispatchResult AnthropicProvider::stream_generate(
    const std::vector<ChatMessage>& messages,
    const std::vector<types::Tool>& tools,
    ThinkingConfig thinking,
    const StreamChunkSink& on_chunk
) const {
  (void)messages;
  (void)tools;
  (void)thinking;
  (void)on_chunk;
  return StreamDispatchResult::Unsupported;
}

std::string AnthropicProvider::messages_url() const {
  const auto base = base_url_.ends_with('/') ? base_url_.substr(0, base_url_.size() - 1) : base_url_;
  if(base.ends_with("/v1")) {
    return base + "/messages";
  }
  return base + "/v1/messages";
}

}  // namespace ava::llm
