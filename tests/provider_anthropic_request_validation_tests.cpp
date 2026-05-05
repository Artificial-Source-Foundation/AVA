#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "ava/provider/anthropic_request_validation.h"
#include "tests/support/test_harness.h"

namespace {

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

ava::provider::ContentPart text_part(std::string text, std::string cache_ttl = {})
{
  return ava::provider::ContentPart{
      .type = ava::provider::ContentPartType::Text, .text = std::move(text), .cache_control_ttl = std::move(cache_ttl)};
}

ava::provider::ContentPart tool_use(std::string id)
{
  return ava::provider::ContentPart{.type = ava::provider::ContentPartType::ToolUse,
                                    .tool_call_id = std::move(id),
                                    .tool_name = "read_file",
                                    .input_json = "{\"path\":\"note.txt\"}"};
}

ava::provider::ContentPart tool_result(std::string id)
{
  return ava::provider::ContentPart{
      .type = ava::provider::ContentPartType::ToolResult, .text = "ok", .tool_call_id = std::move(id)};
}

void test_allow_lists()
{
  expect(ava::provider::detail::valid_anthropic_cache_control_ttl("5m") &&
             ava::provider::detail::valid_anthropic_cache_control_ttl("1h") &&
             !ava::provider::detail::valid_anthropic_cache_control_ttl("30m"),
         "Anthropic request validation keeps cache-control ttl allow-list narrow");
  expect(ava::provider::detail::valid_anthropic_reasoning_type("enabled") &&
             ava::provider::detail::valid_anthropic_reasoning_type("adaptive") &&
             !ava::provider::detail::valid_anthropic_reasoning_type("off"),
         "Anthropic request validation keeps reasoning type allow-list narrow");
  expect(ava::provider::detail::valid_anthropic_reasoning_display("summarized") &&
             ava::provider::detail::valid_anthropic_reasoning_display("omitted") &&
             !ava::provider::detail::valid_anthropic_reasoning_display("verbose"),
         "Anthropic request validation keeps reasoning display allow-list narrow");
}

void test_request_option_validation()
{
  auto invalid_ttl = provider_request();
  invalid_ttl.system_prompt = "system";
  invalid_ttl.system_prompt_cache_ttl = "30m";
  auto ttl_result = ava::provider::detail::validate_anthropic_request_options(invalid_ttl);
  expect(!ttl_result && ttl_result.error().message().find("ttl") != std::string::npos,
         "Anthropic request validation rejects invalid system cache ttl");

  auto adaptive_with_budget = provider_request();
  adaptive_with_budget.reasoning = ava::provider::ProviderReasoningOptions{.type = "adaptive", .budget_tokens = 2048};
  auto adaptive_result = ava::provider::detail::validate_anthropic_request_options(adaptive_with_budget);
  expect(!adaptive_result && adaptive_result.error().message().find("fixed budget") != std::string::npos,
         "Anthropic request validation rejects fixed budgets for adaptive reasoning");
}

void test_content_part_validation()
{
  auto duplicate_tool_use = ava::provider::detail::validate_anthropic_content_parts(
      {ava::provider::ChatMessage{.role = "assistant", .content_parts = {tool_use("toolu_1")}},
       ava::provider::ChatMessage{.role = "user", .content_parts = {tool_result("toolu_1")}},
       ava::provider::ChatMessage{.role = "assistant", .content_parts = {tool_use("toolu_1")}}});
  expect(!duplicate_tool_use && duplicate_tool_use.error().message().find("unique") != std::string::npos,
         "Anthropic request validation rejects duplicate tool-use ids");

  auto ordinary_before_result = ava::provider::detail::validate_anthropic_content_parts(
      {ava::provider::ChatMessage{.role = "assistant", .content_parts = {tool_use("toolu_2")}},
       ava::provider::ChatMessage{.role = "user", .content_parts = {text_part("ordinary"), tool_result("toolu_2")}}});
  expect(
      !ordinary_before_result && ordinary_before_result.error().message().find("precede ordinary") != std::string::npos,
      "Anthropic request validation requires tool results before ordinary user content");

  auto redacted_without_data = ava::provider::detail::validate_anthropic_content_parts(
      {ava::provider::ChatMessage{.role = "assistant",
                                  .content_parts = {ava::provider::ContentPart{
                                      .type = ava::provider::ContentPartType::Reasoning, .redacted = true}}}});
  expect(!redacted_without_data && redacted_without_data.error().message().find("redacted data") != std::string::npos,
         "Anthropic request validation rejects redacted reasoning without provider data");
}

void test_cache_control_order_validation()
{
  auto request = provider_request();
  request.system_prompt = "system";
  request.system_prompt_cache_ttl = "1h";
  auto valid = ava::provider::detail::validate_anthropic_cache_control_order(
      request,
      {ava::provider::ChatMessage{.role = "user", .content_parts = {text_part("one", "1h"), text_part("two", "5m")}}});
  expect(valid.has_value(),
         valid ? "Anthropic request validation accepts 1h before 5m cache order"
               : "Anthropic request validation accepts 1h before 5m cache order: " + valid.error().format());

  request.system_prompt_cache_ttl = "5m";
  auto invalid = ava::provider::detail::validate_anthropic_cache_control_order(
      request, {ava::provider::ChatMessage{.role = "user", .content_parts = {text_part("one", "1h")}}});
  expect(!invalid && invalid.error().message().find("cannot follow") != std::string::npos,
         "Anthropic request validation rejects 1h cache ttl after 5m");
}

}  // namespace

void run_provider_anthropic_request_validation_tests()
{
  test_allow_lists();
  test_request_option_validation();
  test_content_part_validation();
  test_cache_control_order_validation();
}
