#include <cstdlib>
#include <string>

#include "ava/config/auth.h"
#include "ava/config/xdg_paths.h"
#include "ava/provider/anthropic_provider.h"
#include "ava/provider/registry.h"
#include "tests/support/fake_transport.h"
#include "tests/support/test_harness.h"

namespace {

void test_anthropic_provider_contract() {
  const ava::provider::AnthropicProvider provider("https://anthropic.example.test/");
  const auto request = provider.build_request(
      ava::provider::ProviderRequest{.provider_id = "anthropic",
                                     .model_id = "claude-sonnet-4-5",
                                     .system_prompt = "system",
                                      .messages = {ava::provider::ChatMessage{.role = "user", .content = "hello"},
                                                   ava::provider::ChatMessage{.role = "assistant", .content = "hi"}},
                                      .tools_json = {R"({"type":"function","name":"read_file","description":"Read","parameters":{"type":"object","properties":{"path":{"type":"string"}},"required":["path"]}})"},
                                      .stream = true,
                                      .max_output_tokens = 64'000},
      "anthropic-key");
  expect(request.has_value(), "Anthropic request builds with API key");
  if (request) {
    expect(request->method == "POST" && request->url == "https://anthropic.example.test/v1/messages",
           "Anthropic request targets messages endpoint with trimmed base URL");
    expect(request->headers.at("x-api-key") == "anthropic-key", "Anthropic API-key request uses x-api-key header");
    expect(request->headers.at("anthropic-version") == "2023-06-01", "Anthropic request carries API version");
    expect(request->headers.at("Accept") == "text/event-stream" && !request->follow_redirects,
           "Anthropic streaming request asks for SSE and does not follow authenticated redirects");
    expect(request->body.find("\"model\":\"claude-sonnet-4-5\"") != std::string::npos,
           "Anthropic request includes model id");
    expect(request->body.find("\"max_tokens\":64000") != std::string::npos,
           "Anthropic request uses model max output token metadata when supplied");
    expect(request->body.find("\"system\":\"system\"") != std::string::npos,
           "Anthropic request includes top-level system prompt");
    expect(request->body.find("\"stream\":true") != std::string::npos, "Anthropic request preserves stream flag");
    expect(request->body.find("\"messages\":[{\"role\":\"user\",\"content\":\"hello\"}") !=
               std::string::npos,
           "Anthropic request includes messages");
    expect(request->body.find("\"input_schema\":{\"type\":\"object\"") != std::string::npos,
           "Anthropic request maps OpenAI-style parameters to input_schema");
    expect(request->body.find("\"parameters\"") == std::string::npos,
           "Anthropic request does not send OpenAI parameters key");
  }

  auto oauth_request = provider.build_request(
      ava::provider::ProviderRequest{.provider_id = "anthropic",
                                     .model_id = "claude-sonnet-4-5",
                                     .system_prompt = "system",
                                     .messages = {},
                                     .tools_json = {},
                                     .stream = false},
      ava::provider::ProviderAuthContext{.access_token = "oauth-token", .credential_type = "oauth", .account_id = ""});
  expect(oauth_request.has_value(), "Anthropic request builds with OAuth token auth context");
  if (oauth_request) {
    expect(oauth_request->headers.find("x-api-key") == oauth_request->headers.end() &&
                oauth_request->headers.at("Authorization") == "Bearer oauth-token",
            "Anthropic OAuth request uses bearer header instead of x-api-key");
    expect(oauth_request->headers.at("Accept") == "application/json" && !oauth_request->follow_redirects,
           "Anthropic non-stream request asks for JSON and does not follow authenticated redirects");
  }

  const auto collapsed = provider.build_request(
      ava::provider::ProviderRequest{.provider_id = "anthropic",
                                     .model_id = "claude-sonnet-4-5",
                                     .system_prompt = "system",
                                     .messages = {ava::provider::ChatMessage{.role = "assistant", .content = "first"},
                                                  ava::provider::ChatMessage{.role = "assistant", .content = "second"},
                                                  ava::provider::ChatMessage{.role = "user", .content = "third"},
                                                  ava::provider::ChatMessage{.role = "user", .content = "fourth"}},
                                     .tools_json = {},
                                     .stream = false},
      "anthropic-key");
  expect(collapsed && collapsed->body.find("first\\n\\nsecond") != std::string::npos &&
             collapsed->body.find("third\\n\\nfourth") != std::string::npos,
         "Anthropic request collapses consecutive same-role messages for role alternation");

  const auto missing_token = provider.build_request(
      ava::provider::ProviderRequest{.provider_id = "anthropic",
                                     .model_id = "claude-sonnet-4-5",
                                     .system_prompt = "system",
                                     .messages = {},
                                     .tools_json = {}},
      "");
  expect(!missing_token && missing_token.error().category() == ava::core::ErrorCategory::PermissionDenied,
         "Anthropic request rejects empty token");
}

void test_anthropic_parsing() {
  const std::string sse =
      "event: message_start\n"
      "data: {\"type\":\"message_start\",\"message\":{\"usage\":{\"input_tokens\":10,"
      "\"cache_read_input_tokens\":2}}}\n\n"
      "event: content_block_start\n"
      "data: {\"type\":\"content_block_start\",\"index\":0,\"content_block\":{\"type\":\"text\",\"text\":\"\"}}\n\n"
      "event: content_block_delta\n"
      "data: {\"type\":\"content_block_delta\",\"index\":0,\"delta\":{\"type\":\"text_delta\",\"text\":\"hi\"}}\n\n"
      "event: content_block_start\n"
      "data: {\"type\":\"content_block_start\",\"index\":1,\"content_block\":{\"type\":\"tool_use\",\"id\":\"toolu_1\",\"name\":\"read_file\",\"input\":{}}}\n\n"
      "event: content_block_delta\n"
      "data: {\"type\":\"content_block_delta\",\"index\":1,\"delta\":{\"type\":\"input_json_delta\",\"partial_json\":\"{\\\"path\\\":\"}}\n\n"
      "event: content_block_delta\n"
      "data: {\"type\":\"content_block_delta\",\"index\":1,\"delta\":{\"type\":\"input_json_delta\",\"partial_json\":\"\\\"README.md\\\"}\"}}\n\n"
      "event: content_block_stop\n"
      "data: {\"type\":\"content_block_stop\",\"index\":1}\n\n"
      "event: message_delta\n"
      "data: {\"type\":\"message_delta\",\"usage\":{\"output_tokens\":7}}\n\n"
      "event: message_stop\n"
      "data: {\"type\":\"message_stop\"}\n\n";
  auto events = ava::provider::parse_anthropic_sse(sse);
  expect(events.has_value(), "Anthropic SSE parses");
  if (events) {
    expect(events->size() == 6, "Anthropic SSE produces text, tool, and done events");
    expect((*events)[0].type == ava::provider::StreamEventType::TextDelta && (*events)[0].text == "hi",
           "Anthropic SSE text delta parses");
    expect((*events)[1].type == ava::provider::StreamEventType::ToolCallStart &&
               (*events)[1].tool_call_id == "toolu_1" && (*events)[1].tool_name == "read_file",
           "Anthropic SSE tool start parses");
    expect((*events)[2].type == ava::provider::StreamEventType::ToolCallDelta &&
                (*events)[2].tool_call_id == "toolu_1" && (*events)[2].text.find("path") != std::string::npos &&
                (*events)[3].type == ava::provider::StreamEventType::ToolCallDelta &&
                (*events)[3].tool_call_id == "toolu_1" && (*events)[3].text.find("README.md") != std::string::npos,
            "Anthropic SSE tool input deltas parse");
    expect((*events)[4].type == ava::provider::StreamEventType::ToolCallEnd &&
                (*events)[4].tool_call_id == "toolu_1",
            "Anthropic SSE tool stop parses");
    expect((*events)[5].type == ava::provider::StreamEventType::Done && (*events)[5].usage &&
                (*events)[5].usage->input_tokens == 12 && (*events)[5].usage->output_tokens == 7 &&
                (*events)[5].usage->cache_read_tokens == 2,
            "Anthropic SSE usage accumulates message_start and message_delta usage");
  }

  ava::provider::AnthropicStreamParser parser;
  auto first_chunk = parser.append("event: content_block_delta\n");
  auto second_chunk = parser.append(
      "data: {\"type\":\"content_block_delta\",\"index\":0,\"delta\":{\"type\":\"text_delta\",\"text\":\"split\"}}\n");
  auto third_chunk = parser.append("\n");
  expect(first_chunk && first_chunk->empty() && second_chunk && second_chunk->empty() && third_chunk &&
             third_chunk->size() == 1 && (*third_chunk)[0].text == "split",
         "Anthropic incremental SSE parser buffers split frames");

  auto malformed = ava::provider::parse_anthropic_sse("data: not-json\n\n");
  expect(malformed && malformed->size() == 1 && (*malformed)[0].type == ava::provider::StreamEventType::Error,
         "Anthropic malformed SSE data produces a provider error event");

  auto non_stream = ava::provider::parse_anthropic_response(ava::provider::HttpResponse{
      .status_code = 200,
      .headers = {},
      .body = R"({"content":[{"type":"text","text":"hello"},{"type":"tool_use","id":"toolu_2","name":"grep","input":{"pattern":"needle"}}],"usage":{"input_tokens":3,"output_tokens":4,"cache_creation_input_tokens":1}})"});
  expect(non_stream && non_stream->size() == 5 && (*non_stream)[0].text == "hello" && (*non_stream)[1].tool_name == "grep" &&
              (*non_stream)[2].text.find("needle") != std::string::npos && (*non_stream)[4].usage &&
              (*non_stream)[4].usage->input_tokens == 4 && (*non_stream)[4].usage->total_tokens == 8 &&
              (*non_stream)[4].usage->cache_write_tokens == 1,
          "Anthropic non-stream response parses text, tools, and usage");

  auto malformed_success = ava::provider::parse_anthropic_response(
      ava::provider::HttpResponse{.status_code = 200, .headers = {}, .body = R"({"content":[]})"});
  expect(!malformed_success, "Anthropic non-stream success without content is rejected");

  auto http_error = ava::provider::parse_anthropic_sse_response(ava::provider::HttpResponse{
      .status_code = 529,
      .headers = {},
      .body = R"({"error":{"type":"overloaded_error","message":"Overloaded","api_key":"secret-key"}})"});
  const auto error_text = http_error ? std::string{} : http_error.error().format();
  expect(!http_error && error_text.find("provider_error_kind: transient") != std::string::npos &&
             error_text.find("[redacted]") != std::string::npos && error_text.find("secret-key") == std::string::npos,
          "Anthropic HTTP errors carry normalized provider error kind");
}

void test_anthropic_registry_and_env_auth() {
  auto registry = ava::provider::builtin_provider_registry();
  expect(registry.contains("anthropic"), "builtin provider registry contains Anthropic");
  auto provider = registry.create("anthropic");
  expect(provider.has_value() && *provider, "builtin provider registry creates Anthropic provider");

  const auto root = temp_root() / "anthropic-auth";
  const auto config_home = root / "config";
  const auto state_home = root / "state";
  const auto data_home = root / "data";
  const auto ava_config = config_home / "ava";
  const auto ava_state = state_home / "ava";
  const auto paths = ava::config::XdgPaths{.config_home = config_home,
                                           .state_home = state_home,
                                           .data_home = data_home,
                                           .ava_config_dir = ava_config,
                                           .ava_state_dir = ava_state,
                                           .auth_file = ava_config / "auth.json",
                                           .compaction_file = ava_config / "compaction.json",
                                           .global_agents_file = ava_config / "AGENTS.md",
                                           .models_file = ava_config / "models.json",
                                           .prompts_dir = ava_config / "prompts",
                                           .sessions_dir = ava_state / "sessions"};
  ava::tests::FakeTransport transport({});
  ScopedEnvVar api_key("ANTHROPIC_API_KEY", "api-key-value");
  auto api_credential = ava::config::provider_credential_for_request(paths, "anthropic", transport);
  expect(api_credential && *api_credential && (*api_credential)->access_token == "api-key-value" &&
             (*api_credential)->credential_type == "api_key" && (*api_credential)->source == "env:ANTHROPIC_API_KEY",
         "Anthropic API key is discovered from environment");

  ScopedEnvVar oauth_token("ANTHROPIC_OAUTH_TOKEN", "oauth-token-value");
  auto oauth_credential = ava::config::provider_credential_for_request(paths, "anthropic", transport);
  expect(oauth_credential && *oauth_credential && (*oauth_credential)->access_token == "oauth-token-value" &&
             (*oauth_credential)->credential_type == "oauth" &&
             (*oauth_credential)->source == "env:ANTHROPIC_OAUTH_TOKEN",
         "Anthropic OAuth token environment variable takes precedence over API key");
}

}  // namespace

void run_provider_anthropic_tests() {
  test_anthropic_provider_contract();
  test_anthropic_parsing();
  test_anthropic_registry_and_env_auth();
}
