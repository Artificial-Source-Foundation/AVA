#include "tests/support/test_harness.h"
#include "ava/provider/curl_transport.h"
#include "ava/provider/registry.h"

#include <cstdlib>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace {

bool env_enabled(std::string_view name)
{
  auto const* value = std::getenv(std::string(name).c_str());
  if (value == nullptr)
    return false;
  std::string_view const text(value);
  return text == "1" || text == "true" || text == "TRUE" || text == "yes" || text == "on";
}

std::optional<std::string> env_value(std::string_view name)
{
  auto const* value = std::getenv(std::string(name).c_str());
  if (value == nullptr || std::string_view(value).empty())
    return std::nullopt;
  return std::string(value);
}

std::string env_or(std::string_view name, std::string fallback)
{
  if (auto value = env_value(name))
    return *value;
  return fallback;
}

struct LiveSmokeCase
{
  std::string provider_id;
  std::string model_id;
  std::string access_token;
  std::string credential_type = "api_key";
  std::string label;
};

std::vector<LiveSmokeCase> configured_live_smokes()
{
  std::vector<LiveSmokeCase> cases;
  if (auto token = env_value("OPENAI_API_KEY"))
  {
    cases.push_back(LiveSmokeCase{.provider_id = "openai",
                                  .model_id = env_or("AVA_LIVE_OPENAI_MODEL", "gpt-4.1-mini"),
                                  .access_token = *token,
                                  .credential_type = "api_key",
                                  .label = "OpenAI"});
  }
  if (auto token = env_value("ANTHROPIC_OAUTH_TOKEN"))
  {
    cases.push_back(LiveSmokeCase{.provider_id = "anthropic",
                                  .model_id = env_or("AVA_LIVE_ANTHROPIC_MODEL", "claude-sonnet-4-5"),
                                  .access_token = *token,
                                  .credential_type = "oauth",
                                  .label = "Anthropic OAuth"});
  }
  else if (auto token = env_value("ANTHROPIC_AUTH_TOKEN"))
  {
    cases.push_back(LiveSmokeCase{.provider_id = "anthropic",
                                  .model_id = env_or("AVA_LIVE_ANTHROPIC_MODEL", "claude-sonnet-4-5"),
                                  .access_token = *token,
                                  .credential_type = "oauth",
                                  .label = "Anthropic Auth token"});
  }
  else if (auto token = env_value("ANTHROPIC_API_KEY"))
  {
    cases.push_back(LiveSmokeCase{.provider_id = "anthropic",
                                  .model_id = env_or("AVA_LIVE_ANTHROPIC_MODEL", "claude-sonnet-4-5"),
                                  .access_token = *token,
                                  .credential_type = "api_key",
                                  .label = "Anthropic API key"});
  }
  if (auto token = env_value("DEEPSEEK_API_KEY"))
  {
    cases.push_back(LiveSmokeCase{.provider_id = "deepseek",
                                  .model_id = env_or("AVA_LIVE_DEEPSEEK_MODEL", "deepseek-v4-flash"),
                                  .access_token = *token,
                                  .credential_type = "api_key",
                                  .label = "DeepSeek"});
  }
  if (auto token = env_value("KIMI_API_KEY"))
  {
    cases.push_back(LiveSmokeCase{.provider_id = "kimi",
                                  .model_id = env_or("AVA_LIVE_KIMI_MODEL", "kimi-k2-thinking"),
                                  .access_token = *token,
                                  .credential_type = "api_key",
                                  .label = "Kimi"});
  }
  if (auto token = env_value("MOONSHOT_API_KEY"))
  {
    cases.push_back(LiveSmokeCase{.provider_id = "moonshot",
                                  .model_id = env_or("AVA_LIVE_MOONSHOT_MODEL", "kimi-k2.6"),
                                  .access_token = *token,
                                  .credential_type = "api_key",
                                  .label = "Moonshot"});
  }
  if (auto token = env_value("OPENROUTER_API_KEY"))
  {
    cases.push_back(LiveSmokeCase{.provider_id = "openrouter",
                                  .model_id = env_or("AVA_LIVE_OPENROUTER_MODEL", "moonshotai/kimi-k2.6"),
                                  .access_token = *token,
                                  .credential_type = "api_key",
                                  .label = "OpenRouter"});
  }
  return cases;
}

void run_live_smoke_case(ava::provider::ProviderRegistry& registry, ava::provider::Transport& transport, LiveSmokeCase const& smoke)
{
  auto provider = registry.create(smoke.provider_id);
  expect(provider.has_value() && *provider, smoke.label + " live smoke provider is registered");
  if (!provider || !*provider)
    return;

  auto request = (*provider)->build_request(
      ava::provider::ProviderRequest{.provider_id = smoke.provider_id,
                                     .model_id = smoke.model_id,
                                     .system_prompt = "You are a live smoke test endpoint verifier.",
                                     .messages = {ava::provider::ChatMessage{.role = "user", .content = "Reply with exactly: ava-live-smoke"}},
                                     .tools_json = {},
                                     .stream = false,
                                     .max_output_tokens = 512},
      ava::provider::ProviderAuthContext{.access_token = smoke.access_token, .credential_type = smoke.credential_type, .account_id = {}});
  expect(request.has_value(), smoke.label + " live smoke request builds");
  if (!request)
    return;

  auto response = transport.send(*request, [] { return false; });
  expect(response.has_value(), smoke.label + " live smoke transport returns a response");
  if (!response)
    return;

  auto events = (*provider)->parse_response(*response, false);
  expect(events.has_value(), smoke.label + " live smoke response parses");
  if (!events)
    return;

  bool saw_text = false;
  bool saw_done = false;
  std::string stop_reason;
  std::size_t reasoning_events = 0;
  for (auto const& event : *events)
  {
    saw_text = saw_text || (event.type == ava::provider::StreamEventType::TextDelta && !event.text.empty());
    reasoning_events += event.type == ava::provider::StreamEventType::ReasoningDelta ? 1U : 0U;
    if (event.type == ava::provider::StreamEventType::Done)
    {
      saw_done = true;
      stop_reason = event.stop_reason;
    }
  }
  if (!saw_text || !saw_done)
  {
    std::cerr << smoke.label << " live smoke diagnostic: saw_text=" << (saw_text ? "true" : "false") << " saw_done=" << (saw_done ? "true" : "false")
              << " stop_reason=" << stop_reason << " reasoning_events=" << reasoning_events << '\n';
  }
  expect(saw_text && saw_done, smoke.label + " live smoke produces text and a terminal done event");
}

}  // namespace

void run_provider_live_smoke_tests()
{
  if (!env_enabled("AVA_LIVE_PROVIDER_SMOKE"))
  {
    ava::test::request_skip("set AVA_LIVE_PROVIDER_SMOKE=1 and provider credentials to run");
    return;
  }

  auto cases = configured_live_smokes();
  if (cases.empty())
  {
    ava::test::request_skip("no supported provider credential environment variables are set");
    return;
  }

  auto registry = ava::provider::builtin_provider_registry();
  ava::provider::CurlCliTransport transport;
  for (auto const& smoke : cases)
  {
    run_live_smoke_case(registry, transport, smoke);
  }
}
