#include "ava/llm/providers/openai_provider.hpp"

#include <algorithm>
#include <cctype>
#include <sstream>
#include <utility>

#include "ava/llm/pricing.hpp"
#include "ava/llm/providers/openai_protocol.hpp"

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
      types::ThinkingLevel::Max,
  };
}

[[nodiscard]] ProviderError classify_openai_error(long status, const std::string& body) {
  return classify_provider_error(
      "openai",
      status > 0 ? std::optional<std::uint16_t>(static_cast<std::uint16_t>(status)) : std::nullopt,
      body
  );
}

}  // namespace

OpenAiProvider::OpenAiProvider(
    std::string model,
    std::string api_key,
    std::string base_url,
    std::optional<std::string> org_id
)
    : model_(std::move(model)),
      api_key_(std::move(api_key)),
      base_url_(std::move(base_url)),
      org_id_(std::move(org_id)) {}

OpenAiProvider OpenAiProvider::from_credential(const std::string& model, const ava::config::ProviderCredential& credential) {
  const auto api_key = credential.effective_api_key();
  if(!api_key.has_value()) {
    throw ProviderException(ProviderError{
        .kind = ProviderErrorKind::AuthFailure,
        .provider = "openai",
        .message = "missing api key",
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
  return ProviderKind::OpenAI;
}

ProviderCapabilities OpenAiProvider::capabilities() const {
  ProviderCapabilities caps;
  caps.supports_streaming = true;
  caps.supports_tool_use = true;
  caps.supports_thinking = true;
  caps.supports_thinking_levels = true;
  caps.supports_prompt_caching = false;
  return caps;
}

std::size_t OpenAiProvider::estimate_tokens(std::string_view input) const {
  return ava::llm::estimate_tokens(input);
}

double OpenAiProvider::estimate_cost(std::size_t input_tokens, std::size_t output_tokens) const {
  return estimate_cost_usd("openai", model_, input_tokens, output_tokens, false);
}

bool OpenAiProvider::supports_tools() const {
  return true;
}

bool OpenAiProvider::supports_thinking() const {
  auto lower = model_;
  std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char ch) {
    return static_cast<char>(std::tolower(ch));
  });
  return lower.find("gpt-5") != std::string::npos || lower.find("o3") != std::string::npos
         || lower.find("o4") != std::string::npos || lower.find("codex") != std::string::npos;
}

std::vector<types::ThinkingLevel> OpenAiProvider::thinking_levels() const {
  if(!supports_thinking()) {
    return {};
  }
  return kSupportedThinkingLevels();
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
      cpr::Timeout{120000}
  );

  if(response.error.code != cpr::ErrorCode::OK) {
    throw ProviderException(classify_provider_error("openai", std::nullopt, response.error.message));
  }

  if(response.status_code < 200 || response.status_code > 299) {
    throw ProviderException(classify_openai_error(response.status_code, response.text));
  }

  try {
    return openai::parse_chat_completion_response(nlohmann::json::parse(response.text));
  } catch(const std::exception& ex) {
    throw ProviderException(ProviderError{
        .kind = ProviderErrorKind::Unknown,
        .provider = "openai",
        .message = std::string("failed to parse completion response: ") + ex.what(),
    });
  }
#else
  (void)messages;
  (void)tools;
  (void)thinking;
  throw ProviderException(ProviderError{
      .kind = ProviderErrorKind::Unknown,
      .provider = "openai",
      .message = "OpenAI provider requires AVA_WITH_CPR=ON",
  });
#endif
}

std::vector<types::StreamChunk> OpenAiProvider::generate_stream(
    const std::vector<ChatMessage>& messages,
    const std::vector<types::Tool>& tools,
    ThinkingConfig thinking
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

  std::vector<types::StreamChunk> chunks;
  std::string pending;

  auto write_callback = cpr::WriteCallback{
      [&chunks, &pending](std::string data, intptr_t /*userdata*/) {
        try {
          pending += data;
          std::size_t cursor = 0;

          while(true) {
            const auto line_end = pending.find('\n', cursor);
            if(line_end == std::string::npos) {
              pending.erase(0, cursor);
              break;
            }

            auto line = pending.substr(cursor, line_end - cursor);
            cursor = line_end + 1;

            if(!line.empty() && line.back() == '\r') {
              line.pop_back();
            }

            if(line.rfind("data:", 0) != 0) {
              continue;
            }

            auto payload = line.substr(5);
            while(!payload.empty() && payload.front() == ' ') {
              payload.erase(payload.begin());
            }

            if(payload == "[DONE]") {
              chunks.push_back(types::StreamChunk::finished());
              continue;
            }

            try {
              const auto event = nlohmann::json::parse(payload);
              if(const auto parsed = openai::parse_stream_event(event); parsed.has_value()) {
                chunks.push_back(*parsed);
              }
            } catch(const std::exception&) {
              // Ignore malformed chunk and continue.
            }
          }
        } catch(...) {
          return false;
        }

        return true;
      },
      nullptr
  };

  const auto response = cpr::Post(
      cpr::Url{chat_completions_url()},
      headers,
      cpr::Body{request_body.dump()},
      cpr::Timeout{120000},
      write_callback
  );

  if(response.error.code != cpr::ErrorCode::OK) {
    throw ProviderException(classify_provider_error("openai", std::nullopt, response.error.message));
  }
  if(response.status_code < 200 || response.status_code > 299) {
    throw ProviderException(classify_openai_error(response.status_code, response.text));
  }

  if(chunks.empty() || !chunks.back().done) {
    chunks.push_back(types::StreamChunk::finished());
  }
  return chunks;
#else
  (void)messages;
  (void)tools;
  (void)thinking;
  throw ProviderException(ProviderError{
      .kind = ProviderErrorKind::Unknown,
      .provider = "openai",
      .message = "OpenAI provider requires AVA_WITH_CPR=ON",
  });
#endif
}

std::string OpenAiProvider::chat_completions_url() const {
  const auto base = base_url_.ends_with('/') ? base_url_.substr(0, base_url_.size() - 1) : base_url_;
  if(base.ends_with("/v1")) {
    return base + "/chat/completions";
  }
  return base + "/v1/chat/completions";
}

}  // namespace ava::llm
