#include "ava/llm/providers/openai_provider.hpp"

#include <string_view>
#include <utility>

#include "ava/core/string_utils.hpp"
#include "ava/llm/pricing.hpp"
#include "ava/llm/providers/openai_protocol.hpp"

#include "sse_utils.hpp"

#if AVA_WITH_CPR
#include <cpr/cpr.h>
#endif

namespace ava::llm {
namespace {

[[nodiscard]] std::vector<types::ThinkingLevel> kSupportedThinkingLevels() {
  return {
      types::ThinkingLevel::Low,
      types::ThinkingLevel::Medium,
      types::ThinkingLevel::High,
  };
}

[[nodiscard]] bool supports_xhigh_reasoning_model(std::string_view lower_model) {
  if(lower_model.find("codex") != std::string_view::npos) {
    return lower_model.find("5.2") != std::string_view::npos || lower_model.find("5.3") != std::string_view::npos;
  }

  return lower_model.find("gpt-5.3") != std::string_view::npos
         || lower_model.find("gpt-5.4") != std::string_view::npos
         || lower_model.find("gpt-5.5") != std::string_view::npos;
}

#if AVA_WITH_CPR
constexpr auto kOpenAiTimeoutMs = 120000;

[[nodiscard]] std::string summarize_openai_error_body(std::string_view body) {
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

[[nodiscard]] ProviderError classify_openai_error(const cpr::Response& response, std::string_view provider_label) {
  const auto status = response.status_code > 0
                          ? std::optional<std::uint16_t>(static_cast<std::uint16_t>(response.status_code))
                          : std::nullopt;

  return classify_provider_error(
      std::string(provider_label),
      status,
      summarize_openai_error_body(response.text),
      providers::parse_retry_after_secs(response.header)
  );
}

#endif

}  // namespace

OpenAiProvider::OpenAiProvider(
    std::string model,
    std::string api_key,
    std::string base_url,
    std::optional<std::string> org_id,
    ProviderKind provider_kind,
    std::string provider_label
)
    : model_(std::move(model)),
      api_key_(std::move(api_key)),
      base_url_(std::move(base_url)),
      org_id_(std::move(org_id)),
      provider_kind_(provider_kind),
      provider_label_(std::move(provider_label)) {}

OpenAiProvider OpenAiProvider::from_credential(const std::string& model, const ava::config::ProviderCredential& credential) {
  const auto api_key = credential.effective_api_key();
  if(!api_key.has_value()) {
    throw ProviderException(ProviderError{
        .kind = ProviderErrorKind::AuthFailure,
        .provider = "openai",
        .message = "missing api key for openai provider credential",
    });
  }

  return OpenAiProvider(
      model,
      *api_key,
      credential.base_url.value_or("https://api.openai.com"),
      credential.org_id
  );
}

std::string OpenAiProvider::model_name() const {
  return model_;
}

ProviderKind OpenAiProvider::provider_kind() const {
  return provider_kind_;
}

ProviderCapabilities OpenAiProvider::capabilities() const {
  ProviderCapabilities caps;
#if AVA_WITH_CPR
  caps.supports_streaming = true;
#else
  caps.supports_streaming = false;
#endif
  caps.supports_tool_use = true;
  caps.supports_thinking = supports_thinking();
  caps.supports_thinking_levels = !thinking_levels().empty();
  caps.supports_prompt_caching = false;
  return caps;
}

std::size_t OpenAiProvider::estimate_tokens(std::string_view input) const {
  return ava::llm::estimate_tokens(input);
}

double OpenAiProvider::estimate_cost(std::size_t input_tokens, std::size_t output_tokens) const {
  return estimate_cost_usd(provider_label_, model_, input_tokens, output_tokens, false);
}

bool OpenAiProvider::supports_tools() const {
  return true;
}

bool OpenAiProvider::supports_thinking() const {
  const auto lower = ava::core::lowercase_ascii(model_);
  return lower.find("gpt-5") != std::string::npos || lower.find("o3") != std::string::npos
         || lower.find("o4") != std::string::npos || lower.find("codex") != std::string::npos;
}

std::vector<types::ThinkingLevel> OpenAiProvider::thinking_levels() const {
  if(!supports_thinking()) {
    return {};
  }

  auto levels = kSupportedThinkingLevels();
  const auto lower = ava::core::lowercase_ascii(model_);
  if(supports_xhigh_reasoning_model(lower)) {
    levels.push_back(types::ThinkingLevel::Max);
  }
  return levels;
}

ResolvedThinkingConfig OpenAiProvider::resolve_thinking_config(ThinkingConfig config) const {
  if(!config.is_enabled()) {
    return ResolvedThinkingConfig::disabled();
  }
  if(!supports_thinking()) {
    return ResolvedThinkingConfig::unsupported(config);
  }

  if(!config.budget_tokens.has_value()) {
    return ResolvedThinkingConfig::qualitative(config, std::nullopt);
  }

  constexpr std::uint32_t kMaxBudget = 8192;
  if(*config.budget_tokens > kMaxBudget) {
    return ResolvedThinkingConfig::quantitative(
        config,
        ThinkingConfig{config.level, kMaxBudget},
        ThinkingBudgetFallback::Clamped,
        *config.budget_tokens,
        kMaxBudget
    );
  }

  return ResolvedThinkingConfig::quantitative(config, config, std::nullopt);
}

LlmResponse OpenAiProvider::generate(
    const std::vector<ChatMessage>& messages,
    const std::vector<types::Tool>& tools,
    ThinkingConfig thinking
) const {
#if AVA_WITH_CPR
  const auto resolved_thinking = resolve_thinking_config(thinking);
  const auto request_body = openai::build_chat_completions_request(
      model_, messages, tools, false, resolved_thinking.applied
  );

  cpr::Header headers{
      {"Authorization", "Bearer " + api_key_},
      {"Content-Type", "application/json"},
  };
  if(org_id_.has_value()) {
    headers["OpenAI-Organization"] = *org_id_;
  }

  const auto response = cpr::Post(
      cpr::Url{chat_completions_url()},
      headers,
      cpr::Body{request_body.dump()},
      cpr::Timeout{kOpenAiTimeoutMs}
  );

  if(response.error.code != cpr::ErrorCode::OK) {
    throw ProviderException(classify_provider_error(provider_label_, std::nullopt, response.error.message));
  }

  if(response.status_code < 200 || response.status_code > 299) {
    throw ProviderException(classify_openai_error(response, provider_label_));
  }

  try {
    return openai::parse_chat_completion_response(nlohmann::json::parse(response.text));
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

std::vector<types::StreamChunk> OpenAiProvider::generate_stream(
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

Provider::StreamDispatchResult OpenAiProvider::stream_generate(
    const std::vector<ChatMessage>& messages,
    const std::vector<types::Tool>& tools,
    ThinkingConfig thinking,
    const StreamChunkSink& on_chunk
) const {
#if AVA_WITH_CPR
  const auto resolved_thinking = resolve_thinking_config(thinking);
  const auto request_body = openai::build_chat_completions_request(
      model_, messages, tools, true, resolved_thinking.applied
  );

  cpr::Header headers{
      {"Authorization", "Bearer " + api_key_},
      {"Content-Type", "application/json"},
      {"Accept", "text/event-stream"},
  };
  if(org_id_.has_value()) {
    headers["OpenAI-Organization"] = *org_id_;
  }

  bool emitted_done = false;
  bool stop_requested = false;
  std::optional<std::string> parse_failure;
  providers::SseEventBuffer sse_events;

  auto dispatch_sse_payload = [&](std::string_view payload_view) {
    const auto payload = ava::core::trim_ascii_view(payload_view);
    if(payload.empty()) {
      return true;
    }

    if(payload == "[DONE]") {
      emitted_done = true;
      if(on_chunk && !on_chunk(types::StreamChunk::finished())) {
        stop_requested = true;
        return false;
      }
      return true;
    }

    try {
      const auto event = nlohmann::json::parse(payload);
      const auto parsed_chunks = openai::parse_stream_events(event);
      for(const auto& parsed : parsed_chunks) {
        emitted_done = emitted_done || parsed.done;
        if(on_chunk && !on_chunk(parsed)) {
          stop_requested = true;
          return false;
        }
      }
    } catch(const std::exception& ex) {
      parse_failure = ex.what();
      return false;
    }

    return true;
  };

  auto write_callback = cpr::WriteCallback{
      [&](const std::string_view& data, intptr_t /*userdata*/) {
        try {
          // CPR owns this view only for the callback duration; consume it immediately.
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
      cpr::Url{chat_completions_url()},
      headers,
      cpr::Body{request_body.dump()},
      cpr::Timeout{kOpenAiTimeoutMs},
      write_callback
  );

  if(!stop_requested && response.error.code == cpr::ErrorCode::OK && response.status_code >= 200 && response.status_code <= 299) {
    (void)sse_events.flush(dispatch_sse_payload);
  }

  if(stop_requested) {
    return StreamDispatchResult::Completed;
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
    throw ProviderException(classify_openai_error(response, provider_label_));
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

std::string OpenAiProvider::chat_completions_url() const {
  const auto base = base_url_.ends_with('/') ? base_url_.substr(0, base_url_.size() - 1) : base_url_;
  if(base.ends_with("/v1") || base.ends_with("/v3") || base.ends_with("/v4")) {
    return base + "/chat/completions";
  }
  return base + "/v1/chat/completions";
}

}  // namespace ava::llm
