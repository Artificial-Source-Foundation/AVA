#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <chrono>
#include <thread>

#include "ava/llm/llm.hpp"
#include "ava/llm/providers/openai_protocol.hpp"

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

TEST_CASE("factory selects openai and stubs deferred providers", "[ava_llm]") {
  ava::config::CredentialStore store;
  store.set("openai", ava::config::ProviderCredential{.api_key = "test-key"});

  const auto openai = ava::llm::create_provider("openai", "gpt-4.1-mini", store);
  REQUIRE(openai->provider_kind() == ava::llm::ProviderKind::OpenAI);

  const auto alias = ava::llm::create_provider("chatgpt", "gpt-4.1-mini", store);
  REQUIRE(alias->provider_kind() == ava::llm::ProviderKind::OpenAI);

  REQUIRE_THROWS_AS(ava::llm::create_provider("anthropic", "claude-sonnet-4", store), ava::llm::ProviderException);
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

TEST_CASE("provider normalization and pricing use canonical config metadata", "[ava_llm]") {
  REQUIRE(ava::llm::normalize_provider_alias("chatgpt") == "openai");
  REQUIRE(ava::llm::normalize_provider_alias("google") == "gemini");
  REQUIRE(ava::llm::base_url_for_provider("chatgpt") == std::optional<std::string>{"https://api.openai.com"});

  const auto exact_cost = ava::llm::estimate_cost_usd("openai", "gpt-5-mini", 1'000'000, 500'000);
  REQUIRE(exact_cost == Catch::Approx(0.9));

  const auto alias_cost = ava::llm::estimate_cost_usd("openai", "mini", 1'000'000, 500'000);
  REQUIRE(alias_cost == Catch::Approx(0.9));
}
