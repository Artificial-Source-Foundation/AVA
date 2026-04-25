#include "ava/llm/providers/openai_provider.hpp"

#include <algorithm>
#include <charconv>
#include <cctype>
#include <system_error>
#include <string_view>
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

[[nodiscard]] std::string summarize_openai_error_body(std::string_view body) {
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

[[nodiscard]] ProviderError classify_openai_error(const cpr::Response& response) {
  const auto status = response.status_code > 0
                          ? std::optional<std::uint16_t>(static_cast<std::uint16_t>(response.status_code))
                          : std::nullopt;

  return classify_provider_error(
      "openai",
      status,
      summarize_openai_error_body(response.text),
      parse_retry_after_secs(response.header)
  );
}

[[nodiscard]] std::string normalize_sse_newlines(std::string_view chunk, bool& pending_carriage_return) {
  std::string normalized;
  normalized.reserve(chunk.size() + (pending_carriage_return ? 1U : 0U));

  std::size_t index = 0;
  if(pending_carriage_return) {
    pending_carriage_return = false;
    if(!chunk.empty() && chunk.front() == '\n') {
      normalized.push_back('\n');
      index = 1;
    } else {
      normalized.push_back('\r');
    }
  }

  for(; index < chunk.size(); ++index) {
    const char ch = chunk.at(index);
    if(ch == '\r') {
      if(index + 1 < chunk.size() && chunk.at(index + 1) == '\n') {
        ++index;
        normalized.push_back('\n');
      } else if(index + 1 == chunk.size()) {
        pending_carriage_return = true;
      } else {
        normalized.push_back(ch);
      }
      continue;
    }

    normalized.push_back(ch);
  }

  return normalized;
}

[[nodiscard]] std::optional<std::string> extract_sse_data_line(std::string_view line) {
  if(line.rfind("data:", 0) == 0) {
    auto payload = line.substr(5);
    while(!payload.empty() && payload.front() == ' ') {
      payload.remove_prefix(1);
    }
    return std::string(payload);
  }

  if(line == "data") {
    return std::string{};
  }

  return std::nullopt;
}
#endif

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
    throw ProviderException(classify_openai_error(response));
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
  std::string pending;
  bool pending_carriage_return = false;

  auto dispatch_sse_payload = [&](std::string_view payload_view) {
    const auto payload = trim_ascii(payload_view);
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

  auto process_pending_events = [&](bool flush_remainder) {
    if(flush_remainder && pending_carriage_return) {
      pending.push_back('\n');
      pending_carriage_return = false;
    }
    if(flush_remainder && !pending.empty()) {
      // Treat a cleanly closed stream's final bytes as a complete SSE event.
      pending.append("\n\n");
    }

    while(true) {
      const auto event_end = pending.find("\n\n");
      if(event_end == std::string::npos) {
        break;
      }

      const std::string event_block = pending.substr(0, event_end);
      pending.erase(0, event_end + 2);

      bool saw_data = false;
      std::string payload;
      std::size_t line_start = 0;
      while(line_start <= event_block.size()) {
        const auto line_end = event_block.find('\n', line_start);
        const std::string_view line = line_end == std::string::npos
                                          ? std::string_view(event_block).substr(line_start)
                                          : std::string_view(event_block).substr(line_start, line_end - line_start);
        line_start = line_end == std::string::npos ? event_block.size() + 1 : line_end + 1;

        if(const auto data_line = extract_sse_data_line(line); data_line.has_value()) {
          if(saw_data) {
            payload.push_back('\n');
          }
          payload += *data_line;
          saw_data = true;
        }
      }

      if(!saw_data) {
        continue;
      }

      if(!dispatch_sse_payload(payload)) {
        return false;
      }
    }

    return true;
  };

  auto write_callback = cpr::WriteCallback{
      [&](const std::string_view& data, intptr_t /*userdata*/) {
        try {
          // CPR owns this view only for the callback duration; consume it immediately.
          pending += normalize_sse_newlines(data, pending_carriage_return);
          return process_pending_events(false);
        } catch(...) {
          return false;
        }
      },
      0
  };

  const auto response = cpr::Post(
      cpr::Url{chat_completions_url()},
      headers,
      cpr::Body{request_body.dump()},
      cpr::Timeout{120000},
      write_callback
  );

  if(!stop_requested && response.error.code == cpr::ErrorCode::OK && response.status_code >= 200 && response.status_code <= 299) {
    (void)process_pending_events(true);
  }

  if(stop_requested) {
    return StreamDispatchResult::Completed;
  }

  if(parse_failure.has_value()) {
    throw ProviderException(ProviderError{
        .kind = ProviderErrorKind::Unknown,
        .provider = "openai",
        .message = "failed to parse stream event: " + *parse_failure,
    });
  }

  if(response.error.code != cpr::ErrorCode::OK) {
    throw ProviderException(classify_provider_error("openai", std::nullopt, response.error.message));
  }
  if(response.status_code < 200 || response.status_code > 299) {
    throw ProviderException(classify_openai_error(response));
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
  if(base.ends_with("/v1")) {
    return base + "/chat/completions";
  }
  return base + "/v1/chat/completions";
}

}  // namespace ava::llm
