#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <chrono>
#include <cstdlib>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#if AVA_WITH_CPR
#include <cpr/cpr.h>
#endif

#include "ava/core/string_utils.hpp"
#include "ava/llm/llm.hpp"
#include "ava/llm/providers/anthropic_protocol.hpp"
#include "ava/llm/providers/anthropic_provider.hpp"
#include "ava/llm/providers/openai_protocol.hpp"
#include "ava/llm/providers/openai_provider.hpp"
#include "sse_utils.hpp"

namespace {

class ThinkingDummyProvider final : public ava::llm::Provider {
public:
  explicit ThinkingDummyProvider(bool supports_thinking) : supports_thinking_(supports_thinking) {}

  [[nodiscard]] std::string model_name() const override { return "dummy"; }
  [[nodiscard]] std::size_t estimate_tokens(std::string_view input) const override { return input.size(); }
  [[nodiscard]] double estimate_cost(std::size_t, std::size_t) const override { return 0.0; }
  [[nodiscard]] bool supports_thinking() const override { return supports_thinking_; }

  [[nodiscard]] ava::llm::LlmResponse generate(
      const std::vector<ava::llm::ChatMessage>&,
      const std::vector<ava::types::Tool>&,
      ava::llm::ThinkingConfig
  ) const override {
    return {};
  }

  [[nodiscard]] std::vector<ava::types::StreamChunk> generate_stream(
      const std::vector<ava::llm::ChatMessage>&,
      const std::vector<ava::types::Tool>&,
      ava::llm::ThinkingConfig
  ) const override {
    return {};
  }

private:
  bool supports_thinking_;
};

class EnvGuard {
public:
  explicit EnvGuard(std::vector<std::string> names) : names_(std::move(names)) {
    for(const auto& name : names_) {
      if(const char* value = std::getenv(name.c_str()); value != nullptr) {
        previous_.push_back({name, std::string(value)});
      } else {
        previous_.push_back({name, std::nullopt});
      }
      unsetenv(name.c_str());
    }
  }

  ~EnvGuard() {
    for(const auto& [name, value] : previous_) {
      if(value.has_value()) {
        setenv(name.c_str(), value->c_str(), 1);
      } else {
        unsetenv(name.c_str());
      }
    }
  }

  EnvGuard(const EnvGuard&) = delete;
  EnvGuard& operator=(const EnvGuard&) = delete;

private:
  std::vector<std::string> names_;
  std::vector<std::pair<std::string, std::optional<std::string>>> previous_;
};

TEST_CASE("trim_ascii_view removes surrounding ASCII whitespace", "[ava_llm]") {
  REQUIRE(ava::core::trim_ascii_view("hello") == "hello");
  REQUIRE(ava::core::trim_ascii_view(" \t hello world \r\n") == "hello world");
  REQUIRE(ava::core::trim_ascii_view("\n\t\r ").empty());
}

#if AVA_WITH_CPR
TEST_CASE("provider retry-after parser handles ms, seconds, and reset timestamp hints", "[ava_llm]") {
  const cpr::Header ms_priority_headers{
      {"retry-after", "15"},
      {"retry-after-ms", "1500"},
  };
  const auto parsed_ms_priority = ava::llm::providers::parse_retry_after_secs(ms_priority_headers);
  REQUIRE(parsed_ms_priority.has_value());
  REQUIRE(*parsed_ms_priority == 2);

  const cpr::Header valid_seconds_headers{
      {"Date", "Wed, 26 Jun 2024 12:00:00 GMT"},
      {"ReTrY-AfTeR", " 15 "},
  };
  const auto parsed_seconds = ava::llm::providers::parse_retry_after_secs(valid_seconds_headers);
  REQUIRE(parsed_seconds.has_value());
  REQUIRE(*parsed_seconds == 15);

  const auto now_epoch =
      std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch());
  const auto now_secs = now_epoch.count() > 0 ? static_cast<std::uint64_t>(now_epoch.count()) : 0U;

  const auto parsed_reset = ava::llm::providers::parse_retry_after_secs(
      cpr::Header{{"x-ratelimit-reset", std::to_string(now_secs + 3)}}
  );
  REQUIRE(parsed_reset.has_value());
  REQUIRE(*parsed_reset <= 3);
  REQUIRE(*parsed_reset >= 1);

  const auto parsed_past_reset = ava::llm::providers::parse_retry_after_secs(
      cpr::Header{{"x-ratelimit-reset", std::to_string(now_secs > 0 ? now_secs - 1 : 0U)}}
  );
  REQUIRE(parsed_past_reset.has_value());
  REQUIRE(*parsed_past_reset == 0);

  const auto parsed_seconds_fallback = ava::llm::providers::parse_retry_after_secs(
      cpr::Header{{"retry-after-ms", "NaN"}, {"retry-after", "7"}}
  );
  REQUIRE(parsed_seconds_fallback.has_value());
  REQUIRE(*parsed_seconds_fallback == 7);

  const auto parsed_reset_fallback = ava::llm::providers::parse_retry_after_secs(
      cpr::Header{{"retry-after", "NaN"}, {"x-ratelimit-reset", std::to_string(now_secs + 2)}}
  );
  REQUIRE(parsed_reset_fallback.has_value());
  REQUIRE(*parsed_reset_fallback <= 2);
  REQUIRE(*parsed_reset_fallback >= 1);

  REQUIRE_FALSE(ava::llm::providers::parse_retry_after_secs(cpr::Header{{"retry-after", "2.5"}}).has_value());
  REQUIRE_FALSE(ava::llm::providers::parse_retry_after_secs(cpr::Header{{"retry-after", " "}}).has_value());
  REQUIRE_FALSE(ava::llm::providers::parse_retry_after_secs(cpr::Header{{"retry-after-ms", " "}}).has_value());
  REQUIRE_FALSE(ava::llm::providers::parse_retry_after_secs(cpr::Header{{"x-request-id", "req_123"}}).has_value());
}
#endif

TEST_CASE("SSE event buffer handles split CRLF and multiline payloads", "[ava_llm]") {
  ava::llm::providers::SseEventBuffer buffer;
  std::vector<std::string> payloads;

  REQUIRE(buffer.append("event: message\r", [&](std::string_view payload) {
    payloads.emplace_back(payload);
    return true;
  }));
  REQUIRE(payloads.empty());

  REQUIRE(buffer.append("\ndata: {\"a\":1}\r\ndata: {\"b\":2}\r\n\r\n", [&](std::string_view payload) {
    payloads.emplace_back(payload);
    return true;
  }));

  REQUIRE(payloads == std::vector<std::string>{"{\"a\":1}\n{\"b\":2}"});
}

TEST_CASE("SSE event buffer stops when payload handler returns false", "[ava_llm]") {
  ava::llm::providers::SseEventBuffer buffer;
  std::size_t calls = 0;

  REQUIRE_FALSE(buffer.append("data: one\n\ndata: two\n\n", [&](std::string_view) {
    ++calls;
    return false;
  }));
  REQUIRE(calls == 1);
}

TEST_CASE("SSE event buffer flushes partial payloads and bounds oversized events", "[ava_llm]") {
  ava::llm::providers::SseEventBuffer buffer;
  std::vector<std::string> payloads;

  REQUIRE(buffer.append("data: partial", [&](std::string_view payload) {
    payloads.emplace_back(payload);
    return true;
  }));
  REQUIRE(payloads.empty());
  REQUIRE(buffer.flush([&](std::string_view payload) {
    payloads.emplace_back(payload);
    return true;
  }));
  REQUIRE(payloads == std::vector<std::string>{"partial"});

  ava::llm::providers::SseEventBuffer oversized;
  REQUIRE_THROWS_AS(oversized.append(std::string(4 * 1024 * 1024 + 1, 'x'), nullptr), std::runtime_error);
}

}  // namespace

TEST_CASE("provider error classification and retryability", "[ava_llm]") {
  const auto rate_limited = ava::llm::classify_provider_error("openai", 429, "rate limit hit", 12);
  REQUIRE(rate_limited.kind == ava::llm::ProviderErrorKind::RateLimit);
  REQUIRE(rate_limited.retry_after_secs == 12);
  REQUIRE(ava::llm::is_retryable(rate_limited));

  const auto auth = ava::llm::classify_provider_error("openai", 401, "unauthorized");
  REQUIRE(auth.kind == ava::llm::ProviderErrorKind::AuthFailure);
  REQUIRE_FALSE(ava::llm::is_retryable(auth));

  const auto context = ava::llm::classify_provider_error("openai", std::nullopt, "context_length_exceeded");
  REQUIRE(context.kind == ava::llm::ProviderErrorKind::ContextWindowExceeded);
}

TEST_CASE("retry budget respects retryability", "[ava_llm]") {
  ava::llm::RetryBudget budget(2);

  ava::llm::ProviderError retryable{
      .kind = ava::llm::ProviderErrorKind::Timeout,
      .provider = "openai",
      .message = "timed out",
  };
  REQUIRE(budget.should_retry(retryable).has_value());
  REQUIRE(budget.should_retry(retryable).has_value());
  REQUIRE_FALSE(budget.should_retry(retryable).has_value());

  budget.reset();
  ava::llm::ProviderError non_retryable{
      .kind = ava::llm::ProviderErrorKind::AuthFailure,
      .provider = "openai",
      .message = "bad key",
  };
  REQUIRE_FALSE(budget.should_retry(non_retryable).has_value());
}

TEST_CASE("retry budget honors server retry hints", "[ava_llm]") {
  ava::llm::RetryBudget budget(2);
  budget.with_delays(std::chrono::milliseconds(10), std::chrono::seconds(5));

  ava::llm::ProviderError rate_limited{
      .kind = ava::llm::ProviderErrorKind::RateLimit,
      .provider = "openai",
      .message = "too many requests",
      .retry_after_secs = 3,
  };

  const auto retry_after_delay = budget.should_retry(rate_limited);
  REQUIRE(retry_after_delay.has_value());
  REQUIRE(*retry_after_delay >= std::chrono::seconds(3));
  REQUIRE(*retry_after_delay <= std::chrono::seconds(5));

  budget.reset();
  ava::llm::ProviderError timeout{
      .kind = ava::llm::ProviderErrorKind::Timeout,
      .provider = "openai",
      .message = "timed out",
  };

  const auto hinted_delay = budget.should_retry_with_hint(timeout, std::chrono::milliseconds(2500));
  REQUIRE(hinted_delay.has_value());
  REQUIRE(*hinted_delay >= std::chrono::milliseconds(2500));
  REQUIRE(*hinted_delay <= std::chrono::seconds(5));
}

TEST_CASE("retry budget caps oversized server retry hints", "[ava_llm]") {
  ava::llm::RetryBudget budget(2);
  budget.with_delays(std::chrono::milliseconds(10), std::chrono::milliseconds(20));

  ava::llm::ProviderError rate_limited{
      .kind = ava::llm::ProviderErrorKind::RateLimit,
      .provider = "openai",
      .message = "too many requests",
      .retry_after_secs = 3,
  };

  const auto retry_after_delay = budget.should_retry(rate_limited);
  REQUIRE(retry_after_delay.has_value());
  REQUIRE(*retry_after_delay <= std::chrono::milliseconds(20));

  budget.reset();
  ava::llm::ProviderError timeout{
      .kind = ava::llm::ProviderErrorKind::Timeout,
      .provider = "openai",
      .message = "timed out",
  };

  const auto hinted_delay = budget.should_retry_with_hint(timeout, std::chrono::seconds(3));
  REQUIRE(hinted_delay.has_value());
  REQUIRE(*hinted_delay <= std::chrono::milliseconds(20));
}

TEST_CASE("retry mode and overload tracking expose persistent retry primitives", "[ava_llm]") {
  REQUIRE(ava::llm::retry_mode_from_config(false, false) == ava::llm::RetryMode::Interactive);
  REQUIRE(ava::llm::retry_mode_from_config(true, false) == ava::llm::RetryMode::Persistent);
  REQUIRE(ava::llm::retry_mode_from_config(false, true) == ava::llm::RetryMode::Persistent);

  ava::llm::RetryBudget persistent_budget(2);
  persistent_budget.with_delays(std::chrono::milliseconds(10), std::chrono::seconds(30));
  persistent_budget.with_mode(ava::llm::RetryMode::Persistent);

  ava::llm::ProviderError rate_limited{
      .kind = ava::llm::ProviderErrorKind::RateLimit,
      .provider = "openai",
      .message = "too many requests",
      .retry_after_secs = 3600,
  };

  const auto capped_delay = persistent_budget.should_retry(rate_limited);
  REQUIRE(capped_delay.has_value());
  REQUIRE(*capped_delay <= std::chrono::minutes(5));

  ava::llm::OverloadTracker tracker;
  REQUIRE(ava::llm::OverloadTracker::is_overload_status(503));
  REQUIRE(ava::llm::OverloadTracker::is_overload_status(529));
  REQUIRE_FALSE(ava::llm::OverloadTracker::is_overload_status(500));
  REQUIRE_FALSE(tracker.record_overload());
  REQUIRE_FALSE(tracker.record_overload());
  REQUIRE(tracker.record_overload());
  REQUIRE(tracker.should_fallback());
  tracker.record_success();
  REQUIRE_FALSE(tracker.should_fallback());
}

TEST_CASE("circuit breaker opens and transitions to half-open", "[ava_llm]") {
  ava::llm::CircuitBreaker breaker(2, std::chrono::milliseconds(10));

  REQUIRE(breaker.allow_request());
  breaker.record_failure();
  breaker.record_failure();
  REQUIRE(breaker.is_open());
  REQUIRE_FALSE(breaker.allow_request());

  std::this_thread::sleep_for(std::chrono::milliseconds(15));
  REQUIRE(breaker.allow_request());
  REQUIRE(breaker.state_name() == "half-open");

  breaker.record_success();
  REQUIRE_FALSE(breaker.is_open());
  REQUIRE(breaker.state_name() == "closed");
}

TEST_CASE("circuit breaker reopens after failed half-open probe", "[ava_llm]") {
  ava::llm::CircuitBreaker breaker(1, std::chrono::milliseconds(10));

  breaker.record_failure();
  REQUIRE(breaker.is_open());

  std::this_thread::sleep_for(std::chrono::milliseconds(15));
  REQUIRE(breaker.allow_request());
  REQUIRE(breaker.state_name() == "half-open");

  breaker.record_failure();
  REQUIRE(breaker.is_open());
  REQUIRE_FALSE(breaker.allow_request());
}

TEST_CASE("thinking config resolution reports unsupported and qualitative fallback", "[ava_llm]") {
  const ThinkingDummyProvider unsupported(false);
  const auto unsupported_result = unsupported.resolve_thinking_config(
      ava::llm::ThinkingConfig{.level = ava::types::ThinkingLevel::High, .budget_tokens = 1024}
  );
  REQUIRE(unsupported_result.budget_support == ava::llm::ThinkingBudgetSupport::None);
  REQUIRE(unsupported_result.fallback == ava::llm::ThinkingBudgetFallback::Unsupported);
  REQUIRE_FALSE(unsupported_result.applied.is_enabled());

  const ThinkingDummyProvider supported(true);
  const auto qualitative_result = supported.resolve_thinking_config(
      ava::llm::ThinkingConfig{.level = ava::types::ThinkingLevel::Medium, .budget_tokens = 256}
  );
  REQUIRE(qualitative_result.budget_support == ava::llm::ThinkingBudgetSupport::Qualitative);
  REQUIRE(qualitative_result.fallback == ava::llm::ThinkingBudgetFallback::Ignored);
}

TEST_CASE("openai thinking config clamps oversized quantitative budgets", "[ava_llm]") {
  const ava::llm::OpenAiProvider provider{"gpt-5-mini", "test-key", "https://api.openai.com", std::nullopt};

  const auto result = provider.resolve_thinking_config(
      ava::llm::ThinkingConfig{.level = ava::types::ThinkingLevel::High, .budget_tokens = 9000}
  );

  REQUIRE(result.budget_support == ava::llm::ThinkingBudgetSupport::Quantitative);
  REQUIRE(result.fallback == ava::llm::ThinkingBudgetFallback::Clamped);
  REQUIRE(result.clamped_requested_tokens == 9000);
  REQUIRE(result.clamped_applied_tokens == 8192);
  REQUIRE(result.applied.budget_tokens == 8192);
}

TEST_CASE("openai reasoning models expose thinking support", "[ava_llm]") {
  const ava::llm::OpenAiProvider provider{"o3-mini", "test-key", "https://api.openai.com", std::nullopt};

  REQUIRE(provider.supports_thinking());
  REQUIRE_FALSE(provider.thinking_levels().empty());
}

TEST_CASE("openai non-xhigh reasoning models exclude max thinking level", "[ava_llm]") {
  const ava::llm::OpenAiProvider provider{"gpt-5-mini", "test-key", "https://api.openai.com", std::nullopt};

  REQUIRE(
      provider.thinking_levels()
      == std::vector<ava::types::ThinkingLevel>{
          ava::types::ThinkingLevel::Low,
          ava::types::ThinkingLevel::Medium,
          ava::types::ThinkingLevel::High,
      }
  );
}

TEST_CASE("openai xhigh-capable reasoning models include max thinking level", "[ava_llm]") {
  const ava::llm::OpenAiProvider provider{"gpt-5.3-codex", "test-key", "https://api.openai.com", std::nullopt};

  REQUIRE(
      provider.thinking_levels()
      == std::vector<ava::types::ThinkingLevel>{
          ava::types::ThinkingLevel::Low,
          ava::types::ThinkingLevel::Medium,
          ava::types::ThinkingLevel::High,
          ava::types::ThinkingLevel::Max,
      }
  );
}

TEST_CASE("openai capabilities mirror model-gated thinking support", "[ava_llm]") {
  const ava::llm::OpenAiProvider provider{"gpt-4.1-mini", "test-key", "https://api.openai.com", std::nullopt};

  const auto caps = provider.capabilities();
  const auto supports_thinking = provider.supports_thinking();
  const auto supports_thinking_levels = !provider.thinking_levels().empty();

  REQUIRE_FALSE(supports_thinking);
  REQUIRE_FALSE(supports_thinking_levels);
  REQUIRE(caps.supports_thinking == supports_thinking);
  REQUIRE(caps.supports_thinking_levels == supports_thinking_levels);
}

TEST_CASE("factory selects implemented API provider families and stubs deferred providers", "[ava_llm]") {
  ava::config::CredentialStore store;
  store.set("openai", ava::config::ProviderCredential{.api_key = "test-key"});
  store.set(
      "anthropic",
      ava::config::ProviderCredential{.api_key = "anthropic-key", .base_url = "https://anthropic.example"}
  );
  store.set("openrouter", ava::config::ProviderCredential{.api_key = "openrouter-key"});
  store.set("inception", ava::config::ProviderCredential{.api_key = "inception-key"});
  store.set("zai", ava::config::ProviderCredential{.api_key = "zai-key"});
  store.set("alibaba", ava::config::ProviderCredential{.api_key = "alibaba-key"});
  store.set("kimi", ava::config::ProviderCredential{.api_key = "kimi-key"});
  store.set("minimax", ava::config::ProviderCredential{.api_key = "minimax-key"});

  const auto openai = ava::llm::create_provider("openai", "gpt-4.1-mini", store);
  REQUIRE(openai->provider_kind() == ava::llm::ProviderKind::OpenAI);

  const auto alias = ava::llm::create_provider("chatgpt", "gpt-4.1-mini", store);
  REQUIRE(alias->provider_kind() == ava::llm::ProviderKind::OpenAI);

  const auto anthropic = ava::llm::create_provider("anthropic", "claude-sonnet-4-6", store);
  REQUIRE(anthropic->provider_kind() == ava::llm::ProviderKind::Anthropic);
  REQUIRE(anthropic->model_name() == "claude-sonnet-4-6");

  const auto openrouter = ava::llm::create_provider("openrouter", "openai/gpt-4.1-mini", store);
  REQUIRE(openrouter->provider_kind() == ava::llm::ProviderKind::OpenRouter);

  const auto inception = ava::llm::create_provider("inception", "mercury-coder-small", store);
  REQUIRE(inception->provider_kind() == ava::llm::ProviderKind::Inception);

  const auto zai = ava::llm::create_provider("zai", "glm-4.5", store);
  REQUIRE(zai->provider_kind() == ava::llm::ProviderKind::OpenAI);

  const auto alibaba = ava::llm::create_provider("alibaba", "qwen3-coder-plus", store);
  REQUIRE(alibaba->provider_kind() == ava::llm::ProviderKind::Anthropic);

  const auto kimi = ava::llm::create_provider("kimi", "kimi-k2", store);
  REQUIRE(kimi->provider_kind() == ava::llm::ProviderKind::Anthropic);

  const auto minimax = ava::llm::create_provider("minimax", "minimax-m1", store);
  REQUIRE(minimax->provider_kind() == ava::llm::ProviderKind::Anthropic);

  REQUIRE_THROWS_AS(ava::llm::create_provider("gemini", "gemini-2.5-pro", store), ava::llm::ProviderException);
}

TEST_CASE("factory resolves mixed-case aliases and rejects placeholder alias env keys", "[ava_llm]") {
  EnvGuard env({"OPENAI_API_KEY", "AVA_OPENAI_API_KEY", "AVA_CHATGPT_API_KEY"});

  ava::config::CredentialStore alias_store;
  alias_store.set("chatgpt", ava::config::ProviderCredential{.api_key = "alias-key"});

  const auto alias = ava::llm::create_provider("ChatGPT", "gpt-4.1-mini", alias_store);
  REQUIRE(alias->provider_kind() == ava::llm::ProviderKind::OpenAI);

  ava::config::CredentialStore raw_alias_store;
  raw_alias_store.set("ChatGPT", ava::config::ProviderCredential{.api_key = "raw-alias-key"});
  const auto raw_alias = ava::llm::create_provider("ChatGPT", "gpt-4.1-mini", raw_alias_store);
  REQUIRE(raw_alias->provider_kind() == ava::llm::ProviderKind::OpenAI);

  setenv("AVA_CHATGPT_API_KEY", "replace-me", 1);
  ava::config::CredentialStore empty_store;
  REQUIRE_THROWS_AS(ava::llm::create_provider("ChatGPT", "gpt-4.1-mini", empty_store), ava::llm::ProviderException);

  setenv("AVA_CHATGPT_API_KEY", "sk-...", 1);
  REQUIRE_THROWS_AS(ava::llm::create_provider("ChatGPT", "gpt-4.1-mini", empty_store), ava::llm::ProviderException);
}

TEST_CASE("factory alias api keys clear oauth fields from the same alias credential", "[ava_llm]") {
  EnvGuard env({"OPENAI_API_KEY", "AVA_OPENAI_API_KEY", "AVA_CHATGPT_API_KEY"});

  ava::config::CredentialStore store;
  store.set(
      "openai",
      ava::config::ProviderCredential{
          .oauth_token = "canonical-oauth-token",
          .oauth_refresh_token = "canonical-refresh-token",
          .oauth_expires_at = 4'102'444'800ULL,
          .oauth_account_id = "canonical-account",
      }
  );
  store.set(
      "chatgpt",
      ava::config::ProviderCredential{
          .api_key = "alias-key",
          .oauth_token = "alias-oauth-token",
          .oauth_refresh_token = "alias-refresh-token",
          .oauth_expires_at = 4'102'444'801ULL,
          .oauth_account_id = "alias-account",
      }
  );

  const auto merged = ava::llm::resolve_credential_for_provider_for_testing(store, "chatgpt");
  REQUIRE(merged.has_value());
  REQUIRE(merged->api_key == "alias-key");
  REQUIRE_FALSE(merged->oauth_token.has_value());
  REQUIRE_FALSE(merged->oauth_refresh_token.has_value());
  REQUIRE_FALSE(merged->oauth_expires_at.has_value());
  REQUIRE_FALSE(merged->oauth_account_id.has_value());
}

TEST_CASE("factory placeholder alias api keys preserve canonical oauth credentials", "[ava_llm]") {
  EnvGuard env({"OPENAI_API_KEY", "AVA_OPENAI_API_KEY", "AVA_CHATGPT_API_KEY"});

  ava::config::CredentialStore store;
  store.set(
      "openai",
      ava::config::ProviderCredential{
          .oauth_token = "canonical-oauth-token",
          .oauth_refresh_token = "canonical-refresh-token",
          .oauth_expires_at = 4'102'444'800ULL,
          .oauth_account_id = "canonical-account",
      }
  );
  store.set("chatgpt", ava::config::ProviderCredential{.api_key = "replace-me"});

  const auto merged = ava::llm::resolve_credential_for_provider_for_testing(store, "chatgpt");
  REQUIRE(merged.has_value());
  REQUIRE(merged->api_key.empty());
  REQUIRE(merged->oauth_token == std::optional<std::string>{"canonical-oauth-token"});
  REQUIRE(merged->oauth_refresh_token == std::optional<std::string>{"canonical-refresh-token"});
  REQUIRE(merged->oauth_expires_at == std::optional<std::uint64_t>{4'102'444'800ULL});
  REQUIRE(merged->oauth_account_id == std::optional<std::string>{"canonical-account"});

  const auto provider = ava::llm::create_provider("chatgpt", "gpt-4.1-mini", store);
  REQUIRE(provider->provider_kind() == ava::llm::ProviderKind::OpenAI);
}

TEST_CASE("factory alias placeholder api key can still provide oauth credentials", "[ava_llm]") {
  EnvGuard env({"OPENAI_API_KEY", "AVA_OPENAI_API_KEY", "AVA_CHATGPT_API_KEY"});

  ava::config::CredentialStore store;
  store.set(
      "chatgpt",
      ava::config::ProviderCredential{
          .api_key = "replace-me",
          .oauth_token = "alias-oauth-token",
          .oauth_refresh_token = "alias-refresh-token",
          .oauth_expires_at = 4'102'444'800ULL,
          .oauth_account_id = "alias-account",
      }
  );

  const auto merged = ava::llm::resolve_credential_for_provider_for_testing(store, "chatgpt");

  REQUIRE(merged.has_value());
  REQUIRE(merged->api_key.empty());
  REQUIRE(merged->oauth_token == std::optional<std::string>{"alias-oauth-token"});
  REQUIRE(merged->oauth_refresh_token == std::optional<std::string>{"alias-refresh-token"});
  REQUIRE(merged->oauth_expires_at == std::optional<std::uint64_t>{4'102'444'800ULL});
  REQUIRE(merged->oauth_account_id == std::optional<std::string>{"alias-account"});
}

TEST_CASE("factory env api key precedence is standard canonical then alias specific", "[ava_llm]") {
  EnvGuard env({"OPENAI_API_KEY", "AVA_OPENAI_API_KEY", "AVA_CHATGPT_API_KEY"});

  ava::config::CredentialStore empty_store;
  setenv("OPENAI_API_KEY", "standard-env-key", 1);
  setenv("AVA_OPENAI_API_KEY", "canonical-env-key", 1);
  setenv("AVA_CHATGPT_API_KEY", "alias-env-key", 1);

  const auto alias_wins = ava::llm::resolve_credential_for_provider_for_testing(empty_store, "chatgpt");
  REQUIRE(alias_wins.has_value());
  REQUIRE(alias_wins->api_key == "alias-env-key");
  REQUIRE_FALSE(alias_wins->oauth_token.has_value());

  unsetenv("AVA_CHATGPT_API_KEY");
  const auto canonical_wins = ava::llm::resolve_credential_for_provider_for_testing(empty_store, "chatgpt");
  REQUIRE(canonical_wins.has_value());
  REQUIRE(canonical_wins->api_key == "canonical-env-key");
  REQUIRE_FALSE(canonical_wins->oauth_token.has_value());

  unsetenv("AVA_OPENAI_API_KEY");
  const auto standard_used = ava::llm::resolve_credential_for_provider_for_testing(empty_store, "chatgpt");
  REQUIRE(standard_used.has_value());
  REQUIRE(standard_used->api_key == "standard-env-key");
  REQUIRE_FALSE(standard_used->oauth_token.has_value());
}

TEST_CASE("factory env api key overrides alias metadata while preserving non-auth fields", "[ava_llm]") {
  EnvGuard env({"OPENAI_API_KEY", "AVA_OPENAI_API_KEY", "AVA_CHATGPT_API_KEY"});
  setenv("OPENAI_API_KEY", "standard-env-key", 1);

  ava::config::CredentialStore store;
  store.set(
      "chatgpt",
      ava::config::ProviderCredential{
          .base_url = "https://proxy.example",
          .org_id = "org-123",
          .litellm_compatible = true,
      }
  );

  const auto merged = ava::llm::resolve_credential_for_provider_for_testing(store, "chatgpt");

  REQUIRE(merged.has_value());
  REQUIRE(merged->api_key == "standard-env-key");
  REQUIRE(merged->base_url == std::optional<std::string>{"https://proxy.example"});
  REQUIRE(merged->org_id == std::optional<std::string>{"org-123"});
  REQUIRE(merged->litellm_compatible == std::optional<bool>{true});
}

TEST_CASE("factory alias metadata overrides canonical provider metadata", "[ava_llm]") {
  EnvGuard env({"OPENAI_API_KEY", "AVA_OPENAI_API_KEY", "AVA_CHATGPT_API_KEY"});

  ava::config::CredentialStore store;
  store.set(
      "openai",
      ava::config::ProviderCredential{
          .api_key = "canonical-key",
          .base_url = "https://canonical.example",
          .org_id = "canonical-org",
      }
  );
  store.set(
      "chatgpt",
      ava::config::ProviderCredential{
          .base_url = "https://alias.example",
          .org_id = "alias-org",
      }
  );

  const auto merged = ava::llm::resolve_credential_for_provider_for_testing(store, "chatgpt");

  REQUIRE(merged.has_value());
  REQUIRE(merged->api_key == "canonical-key");
  REQUIRE(merged->base_url == std::optional<std::string>{"https://alias.example"});
  REQUIRE(merged->org_id == std::optional<std::string>{"alias-org"});
}

TEST_CASE("factory canonical api key can merge with alias oauth credentials", "[ava_llm]") {
  EnvGuard env({"OPENAI_API_KEY", "AVA_OPENAI_API_KEY", "AVA_CHATGPT_API_KEY"});

  ava::config::CredentialStore store;
  store.set("openai", ava::config::ProviderCredential{.api_key = "canonical-key"});
  store.set(
      "chatgpt",
      ava::config::ProviderCredential{
          .oauth_token = "alias-oauth-token",
          .oauth_refresh_token = "alias-refresh-token",
          .oauth_expires_at = 4'102'444'800ULL,
          .oauth_account_id = "alias-account",
      }
  );

  const auto merged = ava::llm::resolve_credential_for_provider_for_testing(store, "chatgpt");

  REQUIRE(merged.has_value());
  REQUIRE(merged->api_key == "canonical-key");
  REQUIRE(merged->oauth_token == std::optional<std::string>{"alias-oauth-token"});
  REQUIRE(merged->oauth_refresh_token == std::optional<std::string>{"alias-refresh-token"});
  REQUIRE(merged->oauth_expires_at == std::optional<std::uint64_t>{4'102'444'800ULL});
  REQUIRE(merged->oauth_account_id == std::optional<std::string>{"alias-account"});
  REQUIRE(merged->effective_api_key() == std::optional<std::string>{"alias-oauth-token"});
}

TEST_CASE("factory placeholder standard env does not block real alias env key", "[ava_llm]") {
  EnvGuard env({"OPENAI_API_KEY", "AVA_OPENAI_API_KEY", "AVA_CHATGPT_API_KEY"});
  setenv("OPENAI_API_KEY", "replace-me", 1);
  setenv("AVA_CHATGPT_API_KEY", "alias-env-key", 1);

  ava::config::CredentialStore empty_store;
  const auto merged = ava::llm::resolve_credential_for_provider_for_testing(empty_store, "chatgpt");

  REQUIRE(merged.has_value());
  REQUIRE(merged->api_key == "alias-env-key");
}

TEST_CASE("factory reports explicit deferred provider inventory", "[ava_llm]") {
  ava::config::CredentialStore store;
  store.set("openai", ava::config::ProviderCredential{.api_key = "test-key"});
  store.set("anthropic", ava::config::ProviderCredential{.api_key = "anthropic-key"});

  const auto deferred_providers = ava::llm::deferred_provider_names();
  REQUIRE_FALSE(deferred_providers.empty());

  for(const auto& provider : deferred_providers) {
    INFO(provider);
    REQUIRE(ava::llm::is_known_provider(provider));

    try {
      (void)ava::llm::create_provider(provider, "test-model", store);
      FAIL("expected deferred provider failure");
    } catch(const ava::llm::ProviderException& ex) {
      REQUIRE(ex.error().kind == ava::llm::ProviderErrorKind::Unknown);
      REQUIRE(ex.error().provider == provider);
      REQUIRE(ex.error().message.find("not implemented") != std::string::npos);
    }
  }
}

TEST_CASE("factory errors include provider context", "[ava_llm]") {
  ava::config::CredentialStore empty_store;

  try {
    (void)ava::llm::create_provider("chatgpt", "gpt-4.1-mini", empty_store);
    FAIL("expected missing credential failure");
  } catch(const ava::llm::ProviderException& ex) {
    REQUIRE(ex.error().kind == ava::llm::ProviderErrorKind::AuthFailure);
    REQUIRE(ex.error().message.find("openai") != std::string::npos);
    REQUIRE(ex.error().message.find("chatgpt") != std::string::npos);
  }

  try {
    (void)ava::llm::create_provider("anthropic", "claude-sonnet-4-6", empty_store);
    FAIL("expected missing anthropic credential failure");
  } catch(const ava::llm::ProviderException& ex) {
    REQUIRE(ex.error().kind == ava::llm::ProviderErrorKind::AuthFailure);
    REQUIRE(ex.error().message.find("anthropic") != std::string::npos);
  }

  try {
    (void)ava::llm::create_provider("unknown-provider", "model", empty_store);
    FAIL("expected unknown provider failure");
  } catch(const ava::llm::ProviderException& ex) {
    REQUIRE(ex.error().kind == ava::llm::ProviderErrorKind::Unknown);
    REQUIRE(ex.error().message.find("unknown-provider") != std::string::npos);
    REQUIRE(ex.error().message.find("openai") != std::string::npos);
  }
}

TEST_CASE("openai provider reports CPR transport gating when disabled", "[ava_llm]") {
  const ava::llm::OpenAiProvider provider{"gpt-4.1-mini", "test-key", "https://api.openai.com", std::nullopt};

#if AVA_WITH_CPR
  REQUIRE(provider.capabilities().supports_streaming);
  SUCCEED("CPR-enabled build compiles OpenAI transport path");
#else
  REQUIRE_FALSE(provider.capabilities().supports_streaming);

  try {
    (void)provider.generate(
        {ava::llm::ChatMessage::user("hello")},
        {},
        ava::llm::ThinkingConfig::disabled()
    );
    FAIL("expected CPR-disabled transport failure");
  } catch(const ava::llm::ProviderException& ex) {
    REQUIRE(ex.error().message.find("AVA_WITH_CPR=ON") != std::string::npos);
  }

  const auto result = provider.stream_generate(
      {ava::llm::ChatMessage::user("hello")},
      {},
      ava::llm::ThinkingConfig::disabled(),
      [](const ava::types::StreamChunk&) { return true; }
  );
  REQUIRE(result == ava::llm::Provider::StreamDispatchResult::Unsupported);
#endif
}

TEST_CASE("anthropic provider reports CPR transport gating and streaming capability", "[ava_llm]") {
  const ava::llm::AnthropicProvider provider{
      "claude-sonnet-4-6",
      "test-key",
      "https://api.anthropic.com",
      "2023-06-01",
  };

  REQUIRE(provider.provider_kind() == ava::llm::ProviderKind::Anthropic);
  REQUIRE(provider.supports_tools());

#if AVA_WITH_CPR
  REQUIRE(provider.capabilities().supports_streaming);
  SUCCEED("CPR-enabled build compiles Anthropic streaming transport path");
#else
  REQUIRE_FALSE(provider.capabilities().supports_streaming);

  const auto stream_result = provider.stream_generate(
      {ava::llm::ChatMessage::user("hello")},
      {},
      ava::llm::ThinkingConfig::disabled(),
      [](const ava::types::StreamChunk&) { return true; }
  );
  REQUIRE(stream_result == ava::llm::Provider::StreamDispatchResult::Unsupported);

  const auto stream_chunks = provider.generate_stream(
      {ava::llm::ChatMessage::user("hello")},
      {},
      ava::llm::ThinkingConfig::disabled()
  );
  REQUIRE(stream_chunks.empty());

  try {
    (void)provider.generate(
        {ava::llm::ChatMessage::user("hello")},
        {},
        ava::llm::ThinkingConfig::disabled()
    );
    FAIL("expected CPR-disabled anthropic transport failure");
  } catch(const ava::llm::ProviderException& ex) {
    REQUIRE(ex.error().message.find("AVA_WITH_CPR=ON") != std::string::npos);
  }
#endif
}

TEST_CASE("mock provider pops queued responses and streams", "[ava_llm]") {
  auto provider = ava::llm::create_mock_provider("mock-model", std::vector<std::string>{"first", "second"});

  const auto one = provider->generate({ava::llm::ChatMessage::user("hello")}, {}, ava::llm::ThinkingConfig::disabled());
  REQUIRE(one.content == "first");

  const auto stream_chunks = provider->generate_stream(
      {ava::llm::ChatMessage::user("hello")},
      {},
      ava::llm::ThinkingConfig::disabled()
  );
  REQUIRE(stream_chunks.size() == 2);
  REQUIRE(stream_chunks[0].content == "second");
  REQUIRE(stream_chunks[1].done);
}

TEST_CASE("mock provider supports scripted tool-call responses", "[ava_llm]") {
  auto provider = ava::llm::create_mock_provider(
      "mock-model",
      std::vector<ava::llm::LlmResponse>{
          ava::llm::LlmResponse{
              .content = "tool step",
              .tool_calls = {ava::types::ToolCall{
                  .id = "call_1",
                  .name = "read",
                  .arguments = nlohmann::json{{"path", "README.md"}},
              }},
              .usage = ava::types::TokenUsage{.input_tokens = 3, .output_tokens = 2},
              .thinking = std::nullopt,
          },
      }
  );

  const auto response = provider->generate({ava::llm::ChatMessage::user("hello")}, {}, ava::llm::ThinkingConfig::disabled());
  REQUIRE(response.content == "tool step");
  REQUIRE(response.tool_calls.size() == 1);
  REQUIRE(response.tool_calls.front().name == "read");
  REQUIRE(response.usage.has_value());
  REQUIRE(response.usage->input_tokens == 3);
  REQUIRE(response.usage->output_tokens == 2);
}

TEST_CASE("openai request building and response parsing", "[ava_llm]") {
  const auto request = ava::llm::openai::build_chat_completions_request(
      "gpt-4.1-mini",
      {ava::llm::ChatMessage::system("You are helpful"), ava::llm::ChatMessage::user("hi")},
      {},
      false,
      ava::llm::ThinkingConfig{.level = ava::types::ThinkingLevel::Low, .budget_tokens = std::nullopt}
  );

  REQUIRE(request.at("model") == "gpt-4.1-mini");
  REQUIRE(request.at("messages").size() == 2);
  REQUIRE(request.at("reasoning_effort") == "low");

  const nlohmann::json completion_payload = {
      {"choices", {{{"message", {{"content", "done"}}}}}},
      {"usage", {{"prompt_tokens", 7}, {"completion_tokens", 3}}},
  };

  const auto parsed = ava::llm::openai::parse_chat_completion_response(completion_payload);
  REQUIRE(parsed.content == "done");
  REQUIRE(parsed.usage.has_value());
  REQUIRE(parsed.usage->input_tokens == 7);
  REQUIRE(parsed.usage->output_tokens == 3);
}

TEST_CASE("openai request max thinking falls back to high for non-xhigh models", "[ava_llm]") {
  const auto request = ava::llm::openai::build_chat_completions_request(
      "gpt-5-mini",
      {ava::llm::ChatMessage::user("hi")},
      {},
      false,
      ava::llm::ThinkingConfig{.level = ava::types::ThinkingLevel::Max, .budget_tokens = std::nullopt}
  );

  REQUIRE(request.at("reasoning_effort") == "high");
}

TEST_CASE("openai request max thinking keeps xhigh for supported models", "[ava_llm]") {
  const auto request = ava::llm::openai::build_chat_completions_request(
      "gpt-5.4",
      {ava::llm::ChatMessage::user("hi")},
      {},
      false,
      ava::llm::ThinkingConfig{.level = ava::types::ThinkingLevel::Max, .budget_tokens = std::nullopt}
  );

  REQUIRE(request.at("reasoning_effort") == "xhigh");
}

TEST_CASE("openai completion parser tolerates tool-only payloads", "[ava_llm]") {
  const nlohmann::json payload = {
      {"choices",
       {{{"message",
          {{"content", nullptr},
           {"tool_calls",
            {{{"id", "call_1"},
              {"function", {{"name", "read"}, {"arguments", {{"path", "README.md"}}}}}}}}}}}}},
  };

  const auto parsed = ava::llm::openai::parse_chat_completion_response(payload);
  REQUIRE(parsed.content.empty());
  REQUIRE(parsed.tool_calls.size() == 1);
  REQUIRE(parsed.tool_calls.front().name == "read");
  REQUIRE(parsed.tool_calls.front().arguments.at("path") == "README.md");
}

TEST_CASE("anthropic request builder handles system prompts, tools, and tool results", "[ava_llm]") {
  auto assistant_with_tool = ava::llm::ChatMessage::assistant("Checking the workspace");
  assistant_with_tool.tool_calls.push_back(ava::types::ToolCall{
      .id = "toolu_1",
      .name = "read",
      .arguments = nlohmann::json{{"path", "README.md"}},
  });

  const auto request = ava::llm::anthropic::build_messages_request(
      "claude-sonnet-4-6",
      {
          ava::llm::ChatMessage::system("You are helpful"),
          ava::llm::ChatMessage::user("List project files"),
          assistant_with_tool,
          ava::llm::ChatMessage::tool(R"({"call_id":"toolu_1","content":"README content"})", "toolu_1"),
      },
      {
          ava::types::Tool{
              .name = "read",
              .description = "Read a file",
              .parameters = nlohmann::json{
                  {"type", "object"},
                  {"properties", nlohmann::json{{"path", nlohmann::json{{"type", "string"}}}}},
              },
          },
      },
      2048,
      ava::llm::ThinkingConfig::disabled(),
      true
  );

  REQUIRE(request.at("model") == "claude-sonnet-4-6");
  REQUIRE(request.at("max_tokens") == 2048);
  REQUIRE(request.at("stream") == true);
  REQUIRE(request.at("system") == "You are helpful");
  REQUIRE(request.at("messages").size() == 3);
  REQUIRE(request.at("messages").at(0).at("role") == "user");
  REQUIRE(request.at("messages").at(1).at("role") == "assistant");
  REQUIRE(request.at("messages").at(1).at("content").at(1).at("type") == "tool_use");
  REQUIRE(request.at("messages").at(2).at("content").at(0).at("type") == "tool_result");
  REQUIRE(request.at("messages").at(2).at("content").at(0).at("tool_use_id") == "toolu_1");
  REQUIRE(request.at("tools").size() == 1);
  REQUIRE(request.at("tools").at(0).at("name") == "read");
  REQUIRE(request.at("tools").at(0).at("input_schema").at("type") == "object");
}

TEST_CASE("anthropic request builder handles system and tool edge cases", "[ava_llm]") {
  auto assistant_with_tool = ava::llm::ChatMessage::assistant("");
  assistant_with_tool.tool_calls.push_back(ava::types::ToolCall{
      .id = "toolu_string",
      .name = "read",
      .arguments = R"({"path":"README.md"})",
  });

  const auto request = ava::llm::anthropic::build_messages_request(
      "claude-sonnet-4-6",
      {
          ava::llm::ChatMessage::system("First system"),
          ava::llm::ChatMessage::system("Second system"),
          assistant_with_tool,
          ava::llm::ChatMessage::tool("plain tool output", "toolu_plain"),
      },
      {
          ava::types::Tool{.name = "plain", .description = "Plain schema", .parameters = nlohmann::json::array()},
      },
      1024,
      ava::llm::ThinkingConfig{.level = ava::types::ThinkingLevel::High, .budget_tokens = 2000}
  );

  REQUIRE(request.at("system") == "First system\n\nSecond system");
  REQUIRE_FALSE(request.contains("thinking"));
  REQUIRE_FALSE(request.contains("reasoning_effort"));
  REQUIRE(request.at("messages").at(0).at("content").size() == 1);
  REQUIRE(request.at("messages").at(0).at("content").at(0).at("input").at("path") == "README.md");
  REQUIRE(request.at("messages").at(1).at("content").at(0).at("tool_use_id") == "toolu_plain");
  REQUIRE(request.at("messages").at(1).at("content").at(0).at("content") == "plain tool output");
  REQUIRE(request.at("tools").at(0).at("input_schema").at("type") == "object");
  REQUIRE_FALSE(request.contains("stream"));
}

TEST_CASE("anthropic request builder groups consecutive tool results", "[ava_llm]") {
  const auto request = ava::llm::anthropic::build_messages_request(
      "claude-sonnet-4-6",
      {
          ava::llm::ChatMessage::assistant("Tool calls done"),
          ava::llm::ChatMessage::tool(R"({"call_id":"toolu_1","content":"first"})", "toolu_1"),
          ava::llm::ChatMessage::tool(R"({"call_id":"toolu_2","content":"second"})", "toolu_2"),
      },
      {},
      1024,
      ava::llm::ThinkingConfig::disabled()
  );

  REQUIRE(request.at("messages").size() == 2);
  REQUIRE(request.at("messages").at(1).at("role") == "user");
  REQUIRE(request.at("messages").at(1).at("content").size() == 2);
  REQUIRE(request.at("messages").at(1).at("content").at(0).at("tool_use_id") == "toolu_1");
  REQUIRE(request.at("messages").at(1).at("content").at(1).at("tool_use_id") == "toolu_2");
}

TEST_CASE("anthropic request builder marks errored tool results", "[ava_llm]") {
  const auto request = ava::llm::anthropic::build_messages_request(
      "claude-sonnet-4-6",
      {
          ava::llm::ChatMessage::tool(R"({"call_id":"toolu_error","content":"failed","is_error":true})", "toolu_error"),
      },
      {},
      1024,
      ava::llm::ThinkingConfig::disabled()
  );

  const auto& result_block = request.at("messages").at(0).at("content").at(0);
  REQUIRE(result_block.at("tool_use_id") == "toolu_error");
  REQUIRE(result_block.at("content") == "failed");
  REQUIRE(result_block.at("is_error") == true);
}

TEST_CASE("anthropic response parser captures text, tool_use, and usage", "[ava_llm]") {
  const nlohmann::json payload = {
      {"content",
       {{{"type", "text"}, {"text", "Done."}},
        {{"type", "tool_use"},
         {"id", "toolu_1"},
         {"name", "read"},
         {"input", {{"path", "README.md"}}}}}},
      {"usage", {{"input_tokens", 11}, {"output_tokens", 5}, {"cache_read_input_tokens", 2}}},
  };

  const auto parsed = ava::llm::anthropic::parse_messages_response(payload);
  REQUIRE(parsed.content == "Done.");
  REQUIRE(parsed.tool_calls.size() == 1);
  REQUIRE(parsed.tool_calls.front().id == "toolu_1");
  REQUIRE(parsed.tool_calls.front().name == "read");
  REQUIRE(parsed.tool_calls.front().arguments.at("path") == "README.md");
  REQUIRE(parsed.usage.has_value());
  REQUIRE(parsed.usage->input_tokens == 11);
  REQUIRE(parsed.usage->output_tokens == 5);
  REQUIRE(parsed.usage->cache_read_tokens == 2);
  REQUIRE(parsed.usage->cache_creation_tokens == 0);
}

TEST_CASE("anthropic response parser handles thinking and defensive content shapes", "[ava_llm]") {
  const nlohmann::json payload = {
      {"content",
       nlohmann::json::array(
           {nlohmann::json{{"type", "thinking"}, {"thinking", "plan"}},
            "ignored",
            nlohmann::json{{"text", "missing type"}},
            nlohmann::json{{"type", "thinking"}, {"thinking", " done"}},
            nlohmann::json{{"type", "tool_use"}, {"id", "toolu_bad"}, {"name", "read"}, {"input", "not json"}}}
       )},
      {"usage", {{"input_tokens", 0}, {"output_tokens", 0}}},
  };

  const auto parsed = ava::llm::anthropic::parse_messages_response(payload);
  REQUIRE(parsed.content.empty());
  REQUIRE(parsed.thinking == std::optional<std::string>{"plan done"});
  REQUIRE(parsed.tool_calls.size() == 1);
  REQUIRE(parsed.tool_calls.front().arguments.is_object());
  REQUIRE(parsed.tool_calls.front().arguments.empty());
  REQUIRE_FALSE(parsed.usage.has_value());

  const auto string_payload = ava::llm::anthropic::parse_messages_response(nlohmann::json{{"content", "plain text"}});
  REQUIRE(string_payload.content == "plain text");
  REQUIRE_FALSE(string_payload.usage.has_value());
}

TEST_CASE("anthropic response parser rejects missing content", "[ava_llm]") {
  REQUIRE_THROWS_AS(
      ava::llm::anthropic::parse_messages_response(nlohmann::json{{"usage", nlohmann::json::object()}}),
      ava::llm::ProviderException
  );
}

TEST_CASE("anthropic response parser captures cache creation tokens", "[ava_llm]") {
  const auto parsed = ava::llm::anthropic::parse_messages_response(nlohmann::json{
      {"content", nlohmann::json::array()},
      {"usage", {{"input_tokens", 3}, {"output_tokens", 4}, {"cache_creation_input_tokens", 5}}},
  });

  REQUIRE(parsed.usage.has_value());
  REQUIRE(parsed.usage->input_tokens == 3);
  REQUIRE(parsed.usage->output_tokens == 4);
  REQUIRE(parsed.usage->cache_creation_tokens == 5);
}

TEST_CASE("anthropic stream parser captures text thinking tool and usage chunks", "[ava_llm]") {
  const auto text_chunk = ava::llm::anthropic::parse_stream_event(nlohmann::json{
      {"type", "content_block_delta"},
      {"delta", {{"type", "text_delta"}, {"text", "hello"}}},
  });
  REQUIRE(text_chunk.has_value());
  REQUIRE(text_chunk->content == "hello");

  const auto initial_text_chunk = ava::llm::anthropic::parse_stream_event(nlohmann::json{
      {"type", "content_block_start"},
      {"index", 0},
      {"content_block", {{"type", "text"}, {"text", "initial"}}},
  });
  REQUIRE(initial_text_chunk.has_value());
  REQUIRE(initial_text_chunk->content == "initial");

  const auto thinking_chunk = ava::llm::anthropic::parse_stream_event(nlohmann::json{
      {"type", "content_block_delta"},
      {"delta", {{"type", "thinking_delta"}, {"thinking", "plan"}}},
  });
  REQUIRE(thinking_chunk.has_value());
  REQUIRE(thinking_chunk->thinking == "plan");

  const auto initial_thinking_chunk = ava::llm::anthropic::parse_stream_event(nlohmann::json{
      {"type", "content_block_start"},
      {"index", 0},
      {"content_block", {{"type", "thinking"}, {"thinking", "initial plan"}}},
  });
  REQUIRE(initial_thinking_chunk.has_value());
  REQUIRE(initial_thinking_chunk->thinking == "initial plan");

  const auto tool_start_chunk = ava::llm::anthropic::parse_stream_event(nlohmann::json{
      {"type", "content_block_start"},
      {"index", 1},
      {"content_block", {{"type", "tool_use"}, {"id", "toolu_1"}, {"name", "read"}}},
  });
  REQUIRE(tool_start_chunk.has_value());
  REQUIRE(tool_start_chunk->tool_call.has_value());
  REQUIRE(tool_start_chunk->tool_call->index == 1);
  REQUIRE(tool_start_chunk->tool_call->id == "toolu_1");
  REQUIRE(tool_start_chunk->tool_call->name == "read");
  REQUIRE_FALSE(tool_start_chunk->tool_call->arguments_delta.has_value());

  const auto tool_args_chunk = ava::llm::anthropic::parse_stream_event(nlohmann::json{
      {"type", "content_block_delta"},
      {"index", 1},
      {"delta", {{"type", "input_json_delta"}, {"partial_json", "{\"path\":\"README.md\"}"}}},
  });
  REQUIRE(tool_args_chunk.has_value());
  REQUIRE(tool_args_chunk->tool_call.has_value());
  REQUIRE(tool_args_chunk->tool_call->index == 1);
  REQUIRE(tool_args_chunk->tool_call->arguments_delta == std::optional<std::string>{"{\"path\":\"README.md\"}"});

  const auto start_usage_chunk = ava::llm::anthropic::parse_stream_event(nlohmann::json{
      {"type", "message_start"},
      {"message", {{"usage", {{"input_tokens", 11}, {"cache_read_input_tokens", 3}}}}},
  });
  REQUIRE(start_usage_chunk.has_value());
  REQUIRE(start_usage_chunk->usage.has_value());
  REQUIRE(start_usage_chunk->usage->input_tokens == 11);
  REQUIRE(start_usage_chunk->usage->cache_read_tokens == 3);
  REQUIRE_FALSE(start_usage_chunk->done);

  const auto usage_chunk = ava::llm::anthropic::parse_stream_event(nlohmann::json{
      {"type", "message_delta"},
      {"usage", {{"output_tokens", 17}}},
  });
  REQUIRE(usage_chunk.has_value());
  REQUIRE(usage_chunk->usage.has_value());
  REQUIRE(usage_chunk->usage->output_tokens == 17);

  const auto nested_usage_chunk = ava::llm::anthropic::parse_stream_event(nlohmann::json{
      {"type", "message_delta"},
      {"delta", {{"usage", {{"output_tokens", 19}}}}},
  });
  REQUIRE(nested_usage_chunk.has_value());
  REQUIRE(nested_usage_chunk->usage.has_value());
  REQUIRE(nested_usage_chunk->usage->output_tokens == 19);

  const auto done_chunk = ava::llm::anthropic::parse_stream_event(nlohmann::json{{"type", "message_stop"}});
  REQUIRE(done_chunk.has_value());
  REQUIRE(done_chunk->done);
}

TEST_CASE("anthropic stream parser ignores forward-compatible non-content events", "[ava_llm]") {
  REQUIRE_FALSE(
      ava::llm::anthropic::parse_stream_event(nlohmann::json{{"type", "message_start"}, {"message", nlohmann::json::object()}})
          .has_value()
  );
  REQUIRE_FALSE(
      ava::llm::anthropic::parse_stream_event(nlohmann::json{{"type", "content_block_stop"}, {"index", 0}})
          .has_value()
  );
  REQUIRE_FALSE(ava::llm::anthropic::parse_stream_event(nlohmann::json{{"type", "ping"}}).has_value());
  REQUIRE_FALSE(ava::llm::anthropic::parse_stream_event(nlohmann::json{{"type", "future_event"}}).has_value());
}

TEST_CASE("anthropic stream parser raises provider exception for error events", "[ava_llm]") {
  try {
    (void)ava::llm::anthropic::parse_stream_event(nlohmann::json{
        {"type", "error"},
        {"error", {{"type", "overloaded_error"}, {"message", "stream exploded"}}},
    });
    FAIL("expected stream error event to throw provider exception");
  } catch(const ava::llm::ProviderException& ex) {
    REQUIRE(ex.error().provider == "anthropic");
    REQUIRE(ex.error().kind == ava::llm::ProviderErrorKind::ServerError);
    REQUIRE(ex.error().message == "stream exploded");
  }

  try {
    (void)ava::llm::anthropic::parse_stream_event(nlohmann::json{
        {"type", "error"},
        {"error", {{"type", "authentication_error"}, {"message", "bad key"}}},
    });
    FAIL("expected authentication stream error event to throw provider exception");
  } catch(const ava::llm::ProviderException& ex) {
    REQUIRE(ex.error().kind == ava::llm::ProviderErrorKind::AuthFailure);
    REQUIRE(ex.error().message == "bad key");
  }

  try {
    (void)ava::llm::anthropic::parse_stream_event(nlohmann::json{
        {"type", "error"},
        {"error", {{"type", "request_too_large"}, {"message", "too many tokens"}}},
    });
    FAIL("expected request-too-large stream error event to throw provider exception");
  } catch(const ava::llm::ProviderException& ex) {
    REQUIRE(ex.error().kind == ava::llm::ProviderErrorKind::ContextWindowExceeded);
    REQUIRE(ex.error().message == "too many tokens");
  }
}

TEST_CASE("openai stream parser captures content finish and usage chunks", "[ava_llm]") {
  const nlohmann::json content_payload = {
      {"choices", {{{"delta", {{"content", "hello"}}}}}},
  };

  const auto content = ava::llm::openai::parse_stream_event(content_payload);
  REQUIRE(content.has_value());
  REQUIRE(content->content == "hello");
  REQUIRE_FALSE(content->done);

  const nlohmann::json finish_payload = {
      {"choices", {{{"delta", nlohmann::json::object()}, {"finish_reason", "stop"}}}},
  };

  const auto finish = ava::llm::openai::parse_stream_event(finish_payload);
  REQUIRE(finish.has_value());
  REQUIRE(finish->done);

  const nlohmann::json usage_payload = {
      {"choices", nlohmann::json::array()},
      {"usage", {{"prompt_tokens", 11}, {"completion_tokens", 5}}},
  };

  const auto usage = ava::llm::openai::parse_stream_event(usage_payload);
  REQUIRE(usage.has_value());
  REQUIRE(usage->done);
  REQUIRE(usage->usage.has_value());
  REQUIRE(usage->usage->input_tokens == 11);
  REQUIRE(usage->usage->output_tokens == 5);
}

TEST_CASE("openai stream parser captures tool call deltas", "[ava_llm]") {
  const nlohmann::json tool_payload = {
      {"choices",
       {{{"delta",
          {{"tool_calls",
            {{{"index", 0},
              {"id", "call_1"},
              {"function", {{"name", "read"}, {"arguments", "{\"path\":"}}}}}}}}}}},
  };

  const auto tool = ava::llm::openai::parse_stream_event(tool_payload);
  REQUIRE(tool.has_value());
  REQUIRE(tool->tool_call.has_value());
  REQUIRE(tool->tool_call->index == 0);
  REQUIRE(tool->tool_call->id == "call_1");
  REQUIRE(tool->tool_call->name == "read");
  REQUIRE(tool->tool_call->arguments_delta == "{\"path\":");
}

TEST_CASE("openai stream parser emits all tool call entries in one chunk payload", "[ava_llm]") {
  const nlohmann::json tool_payload = {
      {"choices",
       {{{"delta",
          {{"tool_calls",
            {{{"index", 0}, {"id", "call_1"}, {"function", {{"name", "read"}, {"arguments", "{\"path\":\"a\"}"}}}},
             {{"index", 1}, {"id", "call_2"}, {"function", {{"name", "glob"}, {"arguments", "{\"pattern\":\"*.cpp\"}"}}}}}}}}}}},
  };

  const auto tools = ava::llm::openai::parse_stream_events(tool_payload);
  REQUIRE(tools.size() == 2);
  REQUIRE(tools.at(0).tool_call.has_value());
  REQUIRE(tools.at(0).tool_call->index == 0);
  REQUIRE(tools.at(0).tool_call->id == "call_1");
  REQUIRE(tools.at(1).tool_call.has_value());
  REQUIRE(tools.at(1).tool_call->index == 1);
  REQUIRE(tools.at(1).tool_call->id == "call_2");
}

TEST_CASE("openai stream parser handles array content and terminal finish reasons", "[ava_llm]") {
  const nlohmann::json payload = {
      {"choices",
       {{{"delta",
          {{"content",
            {{{"type", "output_text"}, {"text", "hello"}},
             {{"type", "output_text"}, {"text", " world"}}}}}},
         {"finish_reason", "length"}}}},
  };

  const auto chunks = ava::llm::openai::parse_stream_events(payload);
  REQUIRE(chunks.size() == 2);
  REQUIRE(chunks.at(0).content == "hello world");
  REQUIRE(chunks.at(1).done);
}

TEST_CASE("provider normalization and pricing use canonical config metadata", "[ava_llm]") {
  REQUIRE(ava::llm::normalize_provider_alias("chatgpt") == "openai");
  REQUIRE(ava::llm::normalize_provider_alias("google") == "gemini");
  REQUIRE(ava::llm::base_url_for_provider("chatgpt") == std::optional<std::string>{"https://api.openai.com"});
  REQUIRE(ava::llm::base_url_for_provider("anthropic") == std::optional<std::string>{"https://api.anthropic.com"});

  const auto exact_cost = ava::llm::estimate_cost_usd("openai", "gpt-5-mini", 1'000'000, 500'000);
  REQUIRE(exact_cost == Catch::Approx(0.9));

  const auto alias_cost = ava::llm::estimate_cost_usd("openai", "mini", 1'000'000, 500'000);
  REQUIRE(alias_cost == Catch::Approx(0.9));

  const auto anthropic_cost = ava::llm::estimate_cost_usd("anthropic", "claude-opus-4-6", 1'000'000, 500'000);
  REQUIRE(anthropic_cost == Catch::Approx(17.5));
}
