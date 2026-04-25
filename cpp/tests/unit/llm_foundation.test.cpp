#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <chrono>
#include <thread>

#include "ava/llm/llm.hpp"
#include "ava/llm/providers/anthropic_protocol.hpp"
#include "ava/llm/providers/anthropic_provider.hpp"
#include "ava/llm/providers/openai_protocol.hpp"
#include "ava/llm/providers/openai_provider.hpp"

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

TEST_CASE("factory selects openai + anthropic and stubs deferred providers", "[ava_llm]") {
  ava::config::CredentialStore store;
  store.set("openai", ava::config::ProviderCredential{.api_key = "test-key"});
  store.set(
      "anthropic",
      ava::config::ProviderCredential{.api_key = "anthropic-key", .base_url = "https://anthropic.example"}
  );

  const auto openai = ava::llm::create_provider("openai", "gpt-4.1-mini", store);
  REQUIRE(openai->provider_kind() == ava::llm::ProviderKind::OpenAI);

  const auto alias = ava::llm::create_provider("chatgpt", "gpt-4.1-mini", store);
  REQUIRE(alias->provider_kind() == ava::llm::ProviderKind::OpenAI);

  const auto anthropic = ava::llm::create_provider("anthropic", "claude-sonnet-4-6", store);
  REQUIRE(anthropic->provider_kind() == ava::llm::ProviderKind::Anthropic);
  REQUIRE(anthropic->model_name() == "claude-sonnet-4-6");

  REQUIRE_THROWS_AS(ava::llm::create_provider("gemini", "gemini-2.5-pro", store), ava::llm::ProviderException);
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
#if AVA_WITH_CPR
  SUCCEED("CPR-enabled build compiles OpenAI transport path");
#else
  const ava::llm::OpenAiProvider provider{"gpt-4.1-mini", "test-key", "https://api.openai.com", std::nullopt};

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

TEST_CASE("anthropic provider reports CPR transport gating and deferred streaming", "[ava_llm]") {
  const ava::llm::AnthropicProvider provider{
      "claude-sonnet-4-6",
      "test-key",
      "https://api.anthropic.com",
      "2023-06-01",
  };

  REQUIRE(provider.provider_kind() == ava::llm::ProviderKind::Anthropic);
  REQUIRE(provider.supports_tools());
  REQUIRE_FALSE(provider.capabilities().supports_streaming);

  const auto stream_result = provider.stream_generate(
      {ava::llm::ChatMessage::user("hello")},
      {},
      ava::llm::ThinkingConfig::disabled(),
      [](const ava::types::StreamChunk&) { return true; }
  );
  REQUIRE(stream_result == ava::llm::Provider::StreamDispatchResult::Unsupported);
  REQUIRE_THROWS_AS(
      provider.generate_stream(
          {ava::llm::ChatMessage::user("hello")},
          {},
          ava::llm::ThinkingConfig::disabled()
      ),
      ava::llm::ProviderException
  );

#if AVA_WITH_CPR
  SUCCEED("CPR-enabled build compiles Anthropic transport path");
#else
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
      ava::llm::ThinkingConfig::disabled()
  );

  REQUIRE(request.at("model") == "claude-sonnet-4-6");
  REQUIRE(request.at("max_tokens") == 2048);
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
