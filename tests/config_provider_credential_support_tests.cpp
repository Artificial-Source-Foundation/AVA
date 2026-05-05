#include <sys/stat.h>

#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <vector>

#include "ava/config/provider_credential_support.h"
#include "tests/support/test_harness.h"

namespace {

void test_provider_environment_key_helpers()
{
  expect(ava::config::detail::provider_env_key_from_id("moon-shot_2!") == "MOON_SHOT_2_API_KEY",
         "provider credential support derives generic API-key environment names");

  auto openai_keys = ava::config::detail::provider_env_keys("openai");
  auto anthropic_keys = ava::config::detail::provider_env_keys("anthropic");
  auto custom_keys = ava::config::detail::provider_env_keys("moonshot");
  expect(openai_keys == std::vector<std::string>{"OPENAI_API_KEY"},
         "provider credential support avoids duplicate OpenAI generic environment names");
  expect(anthropic_keys == std::vector<std::string>{"ANTHROPIC_OAUTH_TOKEN", "ANTHROPIC_API_KEY"},
         "provider credential support checks Anthropic OAuth tokens before API keys");
  expect(custom_keys == std::vector<std::string>{"MOONSHOT_API_KEY"},
         "provider credential support checks generic provider API keys");
}

void test_provider_environment_credentials()
{
  ScopedEnvVar anthropic_api("ANTHROPIC_API_KEY", "api-key-value");
  ScopedEnvVar anthropic_oauth("ANTHROPIC_OAUTH_TOKEN", "oauth-token-value");
  auto anthropic = ava::config::detail::provider_credential_from_env("anthropic");
  expect(anthropic && anthropic->provider_id == "anthropic" && anthropic->access_token == "oauth-token-value" &&
             anthropic->credential_type == "oauth" && anthropic->source == "env:ANTHROPIC_OAUTH_TOKEN",
         "provider credential support prefers provider-specific OAuth environment credentials");

  ScopedEnvVar moonshot("MOONSHOT_API_KEY", "moonshot-key");
  auto moonshot_credential = ava::config::detail::provider_credential_from_env("moonshot");
  expect(moonshot_credential && moonshot_credential->access_token == "moonshot-key" &&
             moonshot_credential->credential_type == "api_key" && moonshot_credential->source == "env:MOONSHOT_API_KEY",
         "provider credential support reads generic provider API-key environment credentials");
}

void test_token_and_key_aliases()
{
  expect(ava::config::detail::oauth_token_from(R"({"access_token":"access"})") == std::string("access") &&
             ava::config::detail::oauth_token_from(R"({"access":"short"})") == std::string("short") &&
             ava::config::detail::oauth_token_from(R"({"token":"token"})") == std::string("token"),
         "provider credential support accepts OAuth token aliases");
  expect(ava::config::detail::openai_api_key_from(R"({"OPENAI_API_KEY":"env-style"})") == std::string("env-style") &&
             ava::config::detail::openai_api_key_from(R"({"openai_api_key":"snake"})") == std::string("snake"),
         "provider credential support accepts OpenAI API-key compatibility aliases");
  expect(ava::config::detail::generic_api_key_from(R"({"OPENAI_API_KEY":"env-style"})") == std::nullopt,
         "provider credential support keeps generic provider API-key parsing narrow");
}

void test_parse_provider_credentials()
{
  auto oauth = ava::config::detail::parse_provider_credential(
      R"({"anthropic":{"type":"oauth","access":"oauth-token","account_id":"acct_1"}})", "anthropic", "/tmp/auth.json");
  expect(oauth && oauth->provider_id == "anthropic" && oauth->credential_type == "oauth" &&
             oauth->access_token == "oauth-token" && oauth->account_id == "acct_1" && oauth->source == "/tmp/auth.json",
         "provider credential support parses stored OAuth credentials");

  auto api = ava::config::detail::parse_provider_credential(
      R"({"moonshot":{"type":"api_key","api_key":"api-key","account_id":"ignored-account"}})", "moonshot",
      "/tmp/auth.json");
  expect(api && api->provider_id == "moonshot" && api->credential_type == "api_key" && api->access_token == "api-key" &&
             api->account_id == "ignored-account",
         "provider credential support parses stored API-key credentials");

  auto default_api =
      ava::config::detail::parse_provider_credential(R"({"kimi":{"key":"fallback-key"}})", "kimi", "/tmp/auth.json");
  expect(default_api && default_api->credential_type == "api_key" && default_api->access_token == "fallback-key",
         "provider credential support defaults generic credentials to API keys when type is omitted");

  auto invalid_type = ava::config::detail::parse_provider_credential(
      R"({"kimi":{"type":"session","key":"fallback-key"}})", "kimi", "/tmp/auth.json");
  auto missing_provider =
      ava::config::detail::parse_provider_credential(R"({"other":{"key":"fallback-key"}})", "kimi", "/tmp/auth.json");
  expect(!invalid_type && !missing_provider,
         "provider credential support rejects invalid or missing credential scopes");
}

void test_load_provider_credential_from_auth_file()
{
  auto const root = temp_root() / "provider-credential-support";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  std::filesystem::create_directories(root);

  ava::config::XdgPaths paths;
  paths.auth_file = root / "auth.json";
  auto missing = ava::config::detail::load_provider_credential_from_auth_file(paths, "moonshot");
  expect(missing && !*missing, "provider credential support treats missing auth files as absent credentials");

  {
    std::ofstream file(paths.auth_file, std::ios::binary | std::ios::trunc);
    file << R"({"moonshot":{"type":"api","api_key":"stored-key"}})";
  }
  ::chmod(paths.auth_file.c_str(), S_IRUSR | S_IWUSR);
  auto loaded = ava::config::detail::load_provider_credential_from_auth_file(paths, "moonshot");
  expect(loaded && *loaded && (*loaded)->access_token == "stored-key" && (*loaded)->source == paths.auth_file.string(),
         "provider credential support loads stored provider credentials from private auth files");
}

}  // namespace

void run_config_provider_credential_support_tests()
{
  test_provider_environment_key_helpers();
  test_provider_environment_credentials();
  test_token_and_key_aliases();
  test_parse_provider_credentials();
  test_load_provider_credential_from_auth_file();
}
