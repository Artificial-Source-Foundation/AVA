#include <string>

#include "ava/provider/openai_compatible_tool_schema.h"
#include "tests/support/test_harness.h"

namespace {

void test_openai_compatible_tool_schema_converts_bare_schema()
{
  auto const converted = ava::provider::chat_completion_tool_json(
      "{\"type\":\"function\",\"name\":\"strict_tool\",\"description\":\"Strict tool\","
      "\"strict\":true,\"parameters\":{\"type\":\"object\",\"properties\":{\"path\":{\"type\":\"string\"}}}}");
  expect(converted &&
             converted->find("\"type\":\"function\",\"function\":{\"name\":\"strict_tool\"") != std::string::npos &&
             converted->find("},\"strict\":true}") != std::string::npos &&
             converted->find("\"properties\":{\"path\":{\"type\":\"string\"}}") != std::string::npos,
         "OpenAI-compatible tool schema converts bare function schemas and preserves strict mode");

  auto const nested_strict = ava::provider::chat_completion_tool_json(
      "{\"type\":\"function\",\"name\":\"nested_strict\",\"parameters\":{\"type\":\"object\",\"strict\":true}}");
  expect(nested_strict && nested_strict->find("},\"strict\":true}") == std::string::npos,
         "OpenAI-compatible tool schema only preserves top-level strict mode");
}

void test_openai_compatible_tool_schema_preserves_wrapped_schema()
{
  auto const wrapped = ava::provider::chat_completion_tool_json(
      "{\"type\":\"function\",\"function\":{\"name\":\"wrapped_tool\",\"parameters\":{\"type\":\"object\"}}}");
  expect(wrapped && *wrapped ==
                        "{\"type\":\"function\",\"function\":{\"name\":\"wrapped_tool\","
                        "\"parameters\":{\"type\":\"object\"}}}",
         "OpenAI-compatible tool schema preserves already wrapped chat tools");
}

void test_openai_compatible_tool_schema_rejects_invalid_schema()
{
  auto const malformed_parameters = ava::provider::chat_completion_tool_json(
      "{\"type\":\"function\",\"name\":\"bad_params\",\"parameters\":{\"type\":\"object\"");
  expect(!malformed_parameters && malformed_parameters.error().category() == ava::core::ErrorCategory::InvalidArgument,
         "OpenAI-compatible tool schema rejects malformed parameters");

  auto const missing_name = ava::provider::chat_completion_tool_json("{\"type\":\"function\"}");
  expect(!missing_name && missing_name.error().category() == ava::core::ErrorCategory::InvalidArgument,
         "OpenAI-compatible tool schema rejects tools without names");

  auto const wrapped_missing_name =
      ava::provider::chat_completion_tool_json("{\"type\":\"function\",\"function\":{\"parameters\":{}}}");
  expect(!wrapped_missing_name && wrapped_missing_name.error().category() == ava::core::ErrorCategory::InvalidArgument,
         "OpenAI-compatible tool schema rejects wrapped function tools without names");
}

void test_openai_compatible_tool_schema_validates_request_tools()
{
  auto const valid = ava::provider::validate_openai_compatible_tools_json(ava::provider::ProviderRequest{
      .provider_id = "", .model_id = "", .system_prompt = "", .messages = {}, .tools_json = {"{\"name\":\"ok\"}"}});
  expect(valid.has_value(), valid ? "OpenAI-compatible tool schema accepts valid tool JSON"
                                  : "OpenAI-compatible tool schema accepts valid tool JSON: " + valid.error().format());

  auto const invalid = ava::provider::validate_openai_compatible_tools_json(
      ava::provider::ProviderRequest{.provider_id = "",
                                     .model_id = "",
                                     .system_prompt = "",
                                     .messages = {},
                                     .tools_json = {"{\"name\":\"ok\"}", "["}});
  expect(!invalid && invalid.error().category() == ava::core::ErrorCategory::InvalidArgument &&
             invalid.error().format().find("tool_index: 1") != std::string::npos,
         "OpenAI-compatible tool schema reports invalid tool JSON with index context");
}

}  // namespace

void run_provider_openai_compatible_tool_schema_tests()
{
  test_openai_compatible_tool_schema_converts_bare_schema();
  test_openai_compatible_tool_schema_preserves_wrapped_schema();
  test_openai_compatible_tool_schema_rejects_invalid_schema();
  test_openai_compatible_tool_schema_validates_request_tools();
}
