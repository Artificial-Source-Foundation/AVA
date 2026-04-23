#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "ava/types/message.hpp"
#include "ava/types/streaming.hpp"
#include "ava/types/thinking.hpp"
#include "ava/types/tool.hpp"

namespace ava::llm {

enum class ProviderKind {
  Anthropic,
  OpenAI,
  Gemini,
  Ollama,
  OpenRouter,
  Copilot,
  Inception,
};

struct ProviderCapabilities {
  bool supports_streaming{true};
  bool supports_tool_use{false};
  bool supports_thinking{false};
  bool supports_thinking_levels{false};
  bool supports_images{false};
  std::size_t max_context_window{0};
  bool supports_prompt_caching{false};
  bool is_subscription{false};
};

enum class ProviderErrorKind {
  RateLimit,
  AuthFailure,
  ContextWindowExceeded,
  ModelNotFound,
  NetworkError,
  Timeout,
  ServerError,
  Unknown,
};

struct ProviderError {
  ProviderErrorKind kind{ProviderErrorKind::Unknown};
  std::string provider;
  std::string message;
  std::optional<std::uint64_t> retry_after_secs;
  std::optional<std::uint16_t> status;
};

[[nodiscard]] ProviderError classify_provider_error(
    std::string provider,
    std::optional<std::uint16_t> status,
    std::string_view body_or_message,
    std::optional<std::uint64_t> retry_after_secs = std::nullopt
);

[[nodiscard]] bool is_retryable(ProviderErrorKind kind);
[[nodiscard]] bool is_retryable(const ProviderError& error);

class ProviderException final : public std::runtime_error {
public:
  explicit ProviderException(ProviderError error);

  [[nodiscard]] const ProviderError& error() const noexcept;

private:
  ProviderError error_;
};

struct ChatMessage {
  types::Role role{types::Role::User};
  std::string content;
  std::vector<types::ToolCall> tool_calls;
  std::optional<std::string> tool_call_id;

  [[nodiscard]] static ChatMessage system(std::string text);
  [[nodiscard]] static ChatMessage user(std::string text);
  [[nodiscard]] static ChatMessage assistant(std::string text);
  [[nodiscard]] static ChatMessage tool(std::string text, std::string call_id);
};

struct LlmResponse {
  std::string content;
  std::vector<types::ToolCall> tool_calls;
  std::optional<types::TokenUsage> usage;
  std::optional<std::string> thinking;
};

enum class ThinkingBudgetFallback {
  Unsupported,
  Ignored,
  Clamped,
};

enum class ThinkingBudgetSupport {
  None,
  Qualitative,
  Quantitative,
};

struct ThinkingConfig {
  types::ThinkingLevel level{types::ThinkingLevel::Off};
  std::optional<std::uint32_t> budget_tokens;

  [[nodiscard]] static constexpr ThinkingConfig disabled() {
    return ThinkingConfig{types::ThinkingLevel::Off, std::nullopt};
  }

  [[nodiscard]] bool is_enabled() const { return level != types::ThinkingLevel::Off; }
};

struct ResolvedThinkingConfig {
  ThinkingConfig requested{ThinkingConfig::disabled()};
  ThinkingConfig applied{ThinkingConfig::disabled()};
  ThinkingBudgetSupport budget_support{ThinkingBudgetSupport::None};
  std::optional<ThinkingBudgetFallback> fallback;
  std::optional<std::uint32_t> clamped_requested_tokens;
  std::optional<std::uint32_t> clamped_applied_tokens;

  [[nodiscard]] static constexpr ResolvedThinkingConfig disabled() {
    return ResolvedThinkingConfig{};
  }

  [[nodiscard]] static constexpr ResolvedThinkingConfig unsupported(ThinkingConfig requested_config) {
    return ResolvedThinkingConfig{
        requested_config,
        ThinkingConfig::disabled(),
        ThinkingBudgetSupport::None,
        ThinkingBudgetFallback::Unsupported,
        std::nullopt,
        std::nullopt,
    };
  }

  [[nodiscard]] static constexpr ResolvedThinkingConfig qualitative(
      ThinkingConfig requested_config,
      std::optional<ThinkingBudgetFallback> budget_fallback
  ) {
    return ResolvedThinkingConfig{
        requested_config,
        ThinkingConfig{requested_config.level, std::nullopt},
        ThinkingBudgetSupport::Qualitative,
        budget_fallback,
        std::nullopt,
        std::nullopt,
    };
  }

  [[nodiscard]] static constexpr ResolvedThinkingConfig quantitative(
      ThinkingConfig requested_config,
      ThinkingConfig applied_config,
      std::optional<ThinkingBudgetFallback> budget_fallback,
      std::optional<std::uint32_t> clamped_requested = std::nullopt,
      std::optional<std::uint32_t> clamped_applied = std::nullopt
  ) {
    return ResolvedThinkingConfig{
        requested_config,
        applied_config,
        ThinkingBudgetSupport::Quantitative,
        budget_fallback,
        clamped_requested,
        clamped_applied,
    };
  }
};

class Provider {
public:
  virtual ~Provider() = default;

  [[nodiscard]] virtual std::string model_name() const = 0;
  [[nodiscard]] virtual ProviderKind provider_kind() const { return ProviderKind::OpenAI; }
  [[nodiscard]] virtual ProviderCapabilities capabilities() const { return {}; }

  [[nodiscard]] virtual std::size_t estimate_tokens(std::string_view input) const = 0;
  [[nodiscard]] virtual double estimate_cost(std::size_t input_tokens, std::size_t output_tokens) const = 0;

  [[nodiscard]] virtual bool supports_tools() const { return false; }
  [[nodiscard]] virtual bool supports_thinking() const { return false; }
  [[nodiscard]] virtual std::vector<types::ThinkingLevel> thinking_levels() const { return {}; }

  [[nodiscard]] virtual ResolvedThinkingConfig resolve_thinking_config(ThinkingConfig config) const {
    if(!config.is_enabled()) {
      return ResolvedThinkingConfig::disabled();
    }

    if(!supports_thinking()) {
      return ResolvedThinkingConfig::unsupported(config);
    }

    const auto fallback = config.budget_tokens.has_value()
                              ? std::optional<ThinkingBudgetFallback>{ThinkingBudgetFallback::Ignored}
                              : std::nullopt;
    return ResolvedThinkingConfig::qualitative(config, fallback);
  }

  [[nodiscard]] virtual LlmResponse generate(
      const std::vector<ChatMessage>& messages,
      const std::vector<types::Tool>& tools,
      ThinkingConfig thinking
  ) const = 0;

  [[nodiscard]] virtual std::vector<types::StreamChunk> generate_stream(
      const std::vector<ChatMessage>& messages,
      const std::vector<types::Tool>& tools,
      ThinkingConfig thinking
  ) const = 0;
};

using ProviderPtr = std::shared_ptr<Provider>;

}  // namespace ava::llm
