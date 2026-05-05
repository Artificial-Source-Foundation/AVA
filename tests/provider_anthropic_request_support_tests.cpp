#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "ava/provider/anthropic_request_support.h"
#include "tests/support/test_harness.h"

namespace {

ava::provider::ContentPart text_part(std::string text, std::string cache_ttl = {})
{
  return ava::provider::ContentPart{.type = ava::provider::ContentPartType::Text,
                                    .text = std::move(text),
                                    .tool_call_id = "",
                                    .tool_name = "",
                                    .input_json = "",
                                    .is_error = false,
                                    .cache_control_ttl = std::move(cache_ttl)};
}

ava::provider::ProviderRequest provider_request()
{
  return ava::provider::ProviderRequest{.provider_id = "anthropic",
                                        .model_id = "claude-3",
                                        .system_prompt = "",
                                        .messages = {},
                                        .tools_json = {},
                                        .stream = true,
                                        .max_output_tokens = std::nullopt,
                                        .reasoning = std::nullopt,
                                        .system_prompt_cache_ttl = ""};
}

void test_role_and_content_fallback()
{
  expect(
      ava::provider::detail::anthropic_message_role(ava::provider::ChatMessage{.role = "assistant"}) == "assistant" &&
          ava::provider::detail::anthropic_message_role(ava::provider::ChatMessage{.role = "system"}) == "user",
      "Anthropic request support normalizes only assistant role as assistant");

  auto fallback = ava::provider::detail::anthropic_message_content_parts(
      ava::provider::ChatMessage{.role = "user", .content = "hello"});
  expect(
      fallback.size() == 1 && fallback[0].type == ava::provider::ContentPartType::Text && fallback[0].text == "hello",
      "Anthropic request support converts legacy text into content parts");

  auto empty =
      ava::provider::detail::anthropic_message_content_parts(ava::provider::ChatMessage{.role = "user", .content = ""});
  expect(empty.empty(), "Anthropic request support keeps empty fallback content empty");
}

void test_content_append_and_role_collapse()
{
  std::vector<ava::provider::ContentPart> plain{text_part("one")};
  ava::provider::detail::append_anthropic_content_parts(plain, {text_part("two")}, true);
  expect(plain.size() == 1 && plain[0].text == "one\n\ntwo",
         "Anthropic request support merges adjacent plain text across collapsed roles");

  std::vector<ava::provider::ContentPart> cached{text_part("cached", "1h")};
  ava::provider::detail::append_anthropic_content_parts(cached, {text_part("uncached")}, true);
  expect(cached.size() == 3 && cached[1].text == "\n\n" && cached[2].text == "uncached",
         "Anthropic request support separates cache-controlled text before appending plain text");

  auto collapsed = ava::provider::detail::collapse_consecutive_anthropic_roles(
      {ava::provider::ChatMessage{.role = "assistant", .content = "first"},
       ava::provider::ChatMessage{.role = "assistant", .content = "second"},
       ava::provider::ChatMessage{.role = "system", .content = "third"},
       ava::provider::ChatMessage{.role = "user", .content = "fourth"}});
  expect(collapsed.size() == 2 && collapsed[0].role == "assistant" && collapsed[0].content == "first\n\nsecond" &&
             collapsed[1].role == "user" && collapsed[1].content == "third\n\nfourth",
         "Anthropic request support collapses consecutive normalized roles");
}

void test_request_option_validation()
{
  auto missing_system_request = provider_request();
  missing_system_request.model_id = "claude-sonnet-4-5";
  missing_system_request.system_prompt_cache_ttl = "1h";
  auto missing_system = ava::provider::detail::validate_anthropic_request_options(missing_system_request);
  expect(!missing_system && missing_system.error().message().find("non-empty system prompt") != std::string::npos,
         "Anthropic request support rejects cached empty system prompts");

  auto valid_reasoning_request = provider_request();
  valid_reasoning_request.max_output_tokens = 2048;
  valid_reasoning_request.reasoning =
      ava::provider::ProviderReasoningOptions{.type = "enabled", .budget_tokens = 1024, .display = "summarized"};
  auto valid_reasoning = ava::provider::detail::validate_anthropic_request_options(valid_reasoning_request);
  expect(valid_reasoning.has_value(), valid_reasoning ? "Anthropic request support accepts valid enabled reasoning"
                                                      : "Anthropic request support accepts valid enabled reasoning: " +
                                                            valid_reasoning.error().format());

  auto invalid_display_request = provider_request();
  invalid_display_request.reasoning =
      ava::provider::ProviderReasoningOptions{.type = "enabled", .budget_tokens = 1024, .display = "verbose"};
  auto invalid_display = ava::provider::detail::validate_anthropic_request_options(invalid_display_request);
  expect(!invalid_display && invalid_display.error().message().find("display") != std::string::npos,
         "Anthropic request support rejects invalid reasoning display");

  auto oversized_budget_request = provider_request();
  oversized_budget_request.max_output_tokens = 1024;
  oversized_budget_request.reasoning =
      ava::provider::ProviderReasoningOptions{.type = "enabled", .budget_tokens = 1024};
  auto oversized_budget = ava::provider::detail::validate_anthropic_request_options(oversized_budget_request);
  expect(!oversized_budget && oversized_budget.error().message().find("below max output tokens") != std::string::npos,
         "Anthropic request support keeps reasoning budget below max output tokens");

  auto invalid_max_tokens_request = provider_request();
  invalid_max_tokens_request.max_output_tokens = -5;
  expect(ava::provider::detail::anthropic_max_tokens_for_request(invalid_max_tokens_request) ==
             ava::provider::detail::kDefaultAnthropicMaxTokens,
         "Anthropic request support falls back for invalid max output tokens");
}

void test_cache_order_validation()
{
  auto allowed_request = provider_request();
  allowed_request.system_prompt = "system";
  allowed_request.system_prompt_cache_ttl = "1h";
  auto allowed = ava::provider::detail::validate_anthropic_cache_control_order(
      allowed_request,
      {ava::provider::ChatMessage{.role = "user", .content_parts = {text_part("one", "1h"), text_part("two", "5m")}},
       ava::provider::ChatMessage{.role = "assistant", .content_parts = {text_part("three", "5m")}}});
  expect(allowed.has_value(),
         allowed ? "Anthropic request support accepts allowed cache breakpoint order"
                 : "Anthropic request support accepts allowed cache breakpoint order: " + allowed.error().format());

  auto reversed_request = provider_request();
  reversed_request.system_prompt = "system";
  reversed_request.system_prompt_cache_ttl = "5m";
  auto reversed = ava::provider::detail::validate_anthropic_cache_control_order(
      reversed_request, {ava::provider::ChatMessage{.role = "user", .content_parts = {text_part("one", "1h")}}});
  expect(!reversed && reversed.error().message().find("cannot follow") != std::string::npos,
         "Anthropic request support rejects 1h cache ttl after 5m");

  auto too_many_request = provider_request();
  too_many_request.system_prompt = "system";
  too_many_request.system_prompt_cache_ttl = "1h";
  auto too_many = ava::provider::detail::validate_anthropic_cache_control_order(
      too_many_request,
      {ava::provider::ChatMessage{.role = "user",
                                  .content_parts = {text_part("one", "1h"), text_part("two", "1h"),
                                                    text_part("three", "1h"), text_part("four", "5m")}}});
  expect(!too_many && too_many.error().message().find("at most four") != std::string::npos,
         "Anthropic request support rejects more than four cache breakpoints");
}

void test_content_part_validation_and_json()
{
  auto valid_parts = ava::provider::detail::validate_anthropic_content_parts(
      {ava::provider::ChatMessage{
           .role = "assistant",
           .content_parts = {ava::provider::ContentPart{.type = ava::provider::ContentPartType::ToolUse,
                                                        .tool_call_id = "toolu_1",
                                                        .tool_name = "read_file",
                                                        .input_json = "{\"path\":\"note.txt\"}"}}},
       ava::provider::ChatMessage{
           .role = "user",
           .content_parts = {ava::provider::ContentPart{
               .type = ava::provider::ContentPartType::ToolResult, .text = "ok", .tool_call_id = "toolu_1"}}}});
  expect(valid_parts.has_value(), valid_parts ? "Anthropic request support accepts matched tool use/result parts"
                                              : "Anthropic request support accepts matched tool use/result parts: " +
                                                    valid_parts.error().format());

  auto invalid_reasoning = ava::provider::detail::validate_anthropic_content_parts(
      {ava::provider::ChatMessage{.role = "user",
                                  .content_parts = {ava::provider::ContentPart{
                                      .type = ava::provider::ContentPartType::Reasoning, .text = "private"}}}});
  expect(!invalid_reasoning && invalid_reasoning.error().message().find("assistant role") != std::string::npos,
         "Anthropic request support rejects reasoning content outside assistant messages");

  auto text_json = ava::provider::detail::anthropic_content_part_json(text_part("cached", "1h"));
  expect(text_json == R"({"type":"text","text":"cached","cache_control":{"type":"ephemeral","ttl":"1h"}})",
         "Anthropic request support renders text content with cache control");

  auto invalid_tool_json = ava::provider::detail::anthropic_content_part_json(
      ava::provider::ContentPart{.type = ava::provider::ContentPartType::ToolUse,
                                 .tool_call_id = "toolu_2",
                                 .tool_name = "read_file",
                                 .input_json = "not-json"});
  expect(invalid_tool_json.find(R"("input":{})") != std::string::npos,
         "Anthropic request support falls back invalid tool input JSON to an empty object");

  auto tool_result_json = ava::provider::detail::anthropic_content_part_json(
      ava::provider::ContentPart{.type = ava::provider::ContentPartType::ToolResult,
                                 .text = "failed",
                                 .tool_call_id = "toolu_3",
                                 .is_error = true});
  expect(tool_result_json.find(R"("is_error":true)") != std::string::npos,
         "Anthropic request support renders tool result error flag");

  auto redacted_json = ava::provider::detail::anthropic_content_part_json(ava::provider::ContentPart{
      .type = ava::provider::ContentPartType::Reasoning, .reasoning_redacted_data = "opaque", .redacted = true});
  expect(redacted_json == R"({"type":"redacted_thinking","data":"opaque"})",
         "Anthropic request support renders redacted reasoning blocks");
}

void test_tool_and_body_json()
{
  auto tool = ava::provider::detail::anthropic_tool_json(R"({"name":"read_file","description":"Read"})");
  expect(tool == R"({"name":"read_file","description":"Read","input_schema":{"type":"object"}})",
         "Anthropic request support maps missing tool parameters to object schema");

  ava::provider::ProviderRequest request{
      .provider_id = "anthropic",
      .model_id = "claude-3",
      .system_prompt = "system",
      .messages = {ava::provider::ChatMessage{.role = "user", .content = "hello"}},
      .tools_json =
          {R"({"name":"read_file","description":"Read","parameters":{"type":"object","properties":{"path":{"type":"string"}}}})"},
      .stream = false,
      .max_output_tokens = 42,
      .reasoning =
          ava::provider::ProviderReasoningOptions{.type = "enabled", .budget_tokens = 12, .display = "omitted"}};
  auto body = ava::provider::detail::anthropic_request_body_json_unchecked(
      request, ava::provider::detail::collapse_consecutive_anthropic_roles(request.messages));
  expect(body.find(R"("model":"claude-3")") != std::string::npos &&
             body.find(R"("max_tokens":42)") != std::string::npos &&
             body.find(R"("stream":false)") != std::string::npos &&
             body.find(R"("system":"system")") != std::string::npos &&
             body.find(R"("messages":[{"role":"user","content":"hello"}])") != std::string::npos &&
             body.find(R"("input_schema":{"type":"object","properties":{"path":{"type":"string"}}})") !=
                 std::string::npos &&
             body.find(R"("thinking":{"type":"enabled","budget_tokens":12,"display":"omitted"})") != std::string::npos,
         "Anthropic request support assembles provider request JSON");
}

}  // namespace

void run_provider_anthropic_request_support_tests()
{
  test_role_and_content_fallback();
  test_content_append_and_role_collapse();
  test_request_option_validation();
  test_cache_order_validation();
  test_content_part_validation_and_json();
  test_tool_and_body_json();
}
