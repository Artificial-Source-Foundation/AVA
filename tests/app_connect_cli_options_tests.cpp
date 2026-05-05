#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "ava/app/connect_cli_options.h"
#include "tests/support/test_harness.h"

namespace {

ava::core::Result<ava::app::detail::ConnectCliInvocation> parse(std::vector<std::string_view> args,
                                                                std::size_t first_arg_index)
{
  return ava::app::detail::parse_connect_cli_invocation(args, first_arg_index);
}

void test_provider_and_stdin_source_parse()
{
  auto parsed = parse({"ava", "connect", "anthropic", "--api-key-stdin"}, 2);
  expect(parsed && parsed->provider_id == std::string("anthropic") &&
             parsed->credential_source == ava::app::detail::ConnectCliCredentialSource::Stdin &&
             parsed->credential_type && *parsed->credential_type == ava::app::ConnectCredentialType::ApiKey &&
             !parsed->env_var,
         "connect CLI parser captures provider and stdin API-key source");
}

void test_env_oauth_source_parse()
{
  auto parsed = parse({"ava", "login", "moonshot", "--oauth-token-env", "AVA_TEST_TOKEN"}, 2);
  expect(parsed && parsed->provider_id == std::string("moonshot") &&
             parsed->credential_source == ava::app::detail::ConnectCliCredentialSource::Env &&
             parsed->credential_type && *parsed->credential_type == ava::app::ConnectCredentialType::OAuthToken &&
             parsed->env_var == std::string("AVA_TEST_TOKEN"),
         "connect CLI parser captures env OAuth-token source");
}

void test_prompt_source_without_provider_parse()
{
  auto parsed = parse({"ava", "auth", "login", "--oauth-token"}, 3);
  expect(parsed && !parsed->provider_id &&
             parsed->credential_source == ava::app::detail::ConnectCliCredentialSource::Prompt &&
             parsed->credential_type && *parsed->credential_type == ava::app::ConnectCredentialType::OAuthToken,
         "connect CLI parser allows prompt credential source without provider");
}

void test_no_source_parse()
{
  auto parsed = parse({"ava", "connect", "openai"}, 2);
  expect(parsed && parsed->provider_id == std::string("openai") &&
             parsed->credential_source == ava::app::detail::ConnectCliCredentialSource::None &&
             !parsed->credential_type && !parsed->env_var,
         "connect CLI parser captures provider with no explicit credential source");
}

void test_duplicate_source_rejected()
{
  auto parsed = parse({"ava", "connect", "anthropic", "--api-key-stdin", "--oauth-token-stdin"}, 2);
  expect(!parsed && parsed.error().message() == "connect accepts only one credential source",
         "connect CLI parser rejects duplicate credential sources");
}

void test_missing_env_var_rejected()
{
  auto parsed = parse({"ava", "connect", "anthropic", "--api-key-env"}, 2);
  expect(!parsed && parsed.error().message() == "--api-key-env requires an environment variable name",
         "connect CLI parser rejects missing env var names");
}

void test_unknown_option_rejected()
{
  auto parsed = parse({"ava", "connect", "anthropic", "extra"}, 2);
  expect(!parsed && parsed.error().message() == "unknown connect option",
         "connect CLI parser rejects unknown connect options after provider");
}

void test_headless_source_requires_provider()
{
  auto parsed = parse({"ava", "connect", "--api-key-stdin"}, 2);
  std::stringstream in;
  std::stringstream out;
  std::stringstream err;
  auto const status =
      ava::app::detail::run_connect_cli_invocation(ava::config::XdgPaths{}, *parsed, false, false, in, out, err);
  expect(status == 2 && out.str().empty() &&
             err.str().find("connect requires a provider with headless credential sources") != std::string::npos,
         "connect CLI dispatch rejects headless credential sources without provider");
}

}  // namespace

void run_app_connect_cli_options_tests()
{
  test_provider_and_stdin_source_parse();
  test_env_oauth_source_parse();
  test_prompt_source_without_provider_parse();
  test_no_source_parse();
  test_duplicate_source_rejected();
  test_missing_env_var_rejected();
  test_unknown_option_rejected();
  test_headless_source_requires_provider();
}
