#include "ava/provider/openai_usage_parser.h"
#include "tests/support/test_harness.h"

namespace {

void test_openai_usage_parser_top_level_usage()
{
  auto usage =
      ava::provider::parse_openai_usage(R"({"usage":{"input_tokens":11,"output_tokens":7,"total_tokens":18,)"
                                        R"("output_tokens_details":{"reasoning_tokens":3},)"
                                        R"("input_tokens_details":{"cached_tokens":5,"cache_creation_tokens":2}}})");
  expect(usage && usage->input_tokens == 11 && usage->output_tokens == 7 && usage->total_tokens == 18 &&
             usage->reasoning_tokens == 3 && usage->cache_read_tokens == 5 && usage->cache_write_tokens == 2,
         "OpenAI usage parser reads response usage and nested token details");
}

void test_openai_usage_parser_compatible_aliases()
{
  auto usage = ava::provider::parse_openai_usage(
      R"({"prompt_tokens":10,"completion_tokens":4,"completion_tokens_details":{"reasoning_tokens":1},)"
      R"("prompt_tokens_details":{"cache_read_tokens":6,"cache_creation_tokens":2}})");
  expect(usage && usage->input_tokens == 10 && usage->output_tokens == 4 && usage->reasoning_tokens == 1 &&
             usage->cache_read_tokens == 6 && usage->cache_write_tokens == 2,
         "OpenAI usage parser reads chat-completion token aliases");
}

void test_openai_usage_parser_response_nested_usage()
{
  auto usage = ava::provider::parse_openai_usage(
      R"({"response":{"usage":{"input_tokens":1,"output_tokens":2,"cache_read_input_tokens":3,)"
      R"("cache_creation_input_tokens":4}}})");
  expect(usage && usage->input_tokens == 1 && usage->output_tokens == 2 && usage->cache_read_tokens == 3 &&
             usage->cache_write_tokens == 4,
         "OpenAI usage parser reads nested response usage");
}

void test_openai_usage_parser_ignores_invalid_usage()
{
  auto negative = ava::provider::parse_openai_usage(
      R"({"usage":{"input_tokens":-1,"output_tokens":-2,"total_tokens":-3,"reasoning_tokens":-4}})");
  expect(!negative, "OpenAI usage parser ignores negative token counts");

  auto missing = ava::provider::parse_openai_usage(R"({"id":"resp_1"})");
  expect(!missing, "OpenAI usage parser returns empty when no token fields are present");
}

}  // namespace

void run_provider_openai_usage_parser_tests()
{
  test_openai_usage_parser_top_level_usage();
  test_openai_usage_parser_compatible_aliases();
  test_openai_usage_parser_response_nested_usage();
  test_openai_usage_parser_ignores_invalid_usage();
}
