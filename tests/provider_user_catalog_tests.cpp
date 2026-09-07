#include "sys.h"
#include "tests/support/test_harness.h"
#include "ava/http/transport.h"
#include "ava/app/connect_openai.h"
#include "ava/app/reasoning_controls.h"
#include "ava/app/runtime/Session.h"
#include "ava/app/runtime_credentials.h"
#include "ava/app/runtime_reasoning.h"
#include "ava/config/auth.h"
#include "ava/config/provider_config.h"
#include "ava/config/reasoning_profiles.h"
#include "ava/config/xdg_paths.h"
#include "ava/provider/catalog.h"
#include "ava/provider/openai_provider.h"
#include "ava/provider/provider.h"
#include "ava/core/json.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
#include <sys/stat.h>
#include <unistd.h>

namespace {

ava::config::XdgPaths test_paths(std::filesystem::path const& root)
{
  auto const config_home = root / "config";
  auto const state_home = root / "state";
  auto const data_home = root / "data";
  auto const ava_config = config_home / "ava";
  auto const ava_state = state_home / "ava";
  for (auto const& directory : {config_home, state_home, data_home, ava_config, ava_state, ava_state / "sessions"})
  {
    std::filesystem::create_directories(directory);
    ::chmod(directory.c_str(), 0700);
  }
  return ava::config::XdgPaths{.config_home = config_home,
                               .state_home = state_home,
                               .data_home = data_home,
                               .ava_config_dir = ava_config,
                               .ava_state_dir = ava_state,
                               .auth_file = ava_config / "auth.json",
                               .compaction_file = ava_config / "compaction.json",
                               .global_agents_file = ava_config / "AGENTS.md",
                               .models_file = ava_config / "models.json",
                               .providers_file = ava_config / "providers.json",
                               .prompts_dir = ava_config / "prompts",
                               .sessions_dir = ava_state / "sessions"};
}

void write_file(std::filesystem::path const& path, std::string_view body, mode_t mode = 0600)
{
  std::filesystem::create_directories(path.parent_path());
  {
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file.write(body.data(), static_cast<std::streamsize>(body.size()));
  }
  ::chmod(path.c_str(), mode);
}

std::string providers_json_three_protocols()
{
  return R"JSON({
  "version": 1,
  "providers": [
    {
      "id": "local-chat",
      "display_name": "Local Chat",
      "protocol": "openai_chat_completions",
      "base_url": "http://127.0.0.1:11434",
      "auth": "api_key",
      "api_key_env": "LOCAL_CHAT_API_KEY",
      "compatibility": {"include_stream_usage": true}
    },
    {
      "id": "local-responses",
      "display_name": "Local Responses",
      "protocol": "openai_responses",
      "base_url": "http://127.0.0.1:8080",
      "auth": "api_key",
      "api_key_env": "LOCAL_RESPONSES_API_KEY"
    },
    {
      "id": "local-anthropic",
      "display_name": "Local Anthropic",
      "protocol": "anthropic_messages",
      "base_url": "http://127.0.0.1:9000",
      "auth": "none"
    }
  ]
})JSON";
}

ava::provider::ProviderRequest sample_request(std::string_view provider_id, std::string_view model_id)
{
  ava::provider::ProviderRequest request;
  request.provider_id = std::string(provider_id);
  request.model_id = std::string(model_id);
  request.system_prompt = "sys";
  request.messages = {ava::provider::ChatMessage{.role = "user", .content = "hi"}};
  request.stream = true;
  request.max_output_tokens = 64;
  return request;
}

bool header_has(ava::http::HttpRequest const& request, std::string_view key)
{
  return request.headers.find(std::string(key)) != request.headers.end();
}

std::string header_get(ava::http::HttpRequest const& request, std::string_view key)
{
  auto it = request.headers.find(std::string(key));
  return it == request.headers.end() ? std::string{} : it->second;
}

class NullTransport final : public ava::http::Transport
{
 public:
  ava::core::Result<ava::http::HttpResponse> send(ava::http::HttpRequest const&) override
  {
    return ava::http::HttpResponse{.status_code = 500, .headers = {}, .body = "{}"};
  }
};

ava::provider::ProviderAuthContext make_auth(std::string_view token, std::string_view type, std::string_view account = {})
{
  ava::provider::ProviderAuthContext auth;
  auth.access_token = std::string(token);
  auth.credential_type = std::string(type);
  auth.account_id = std::string(account);
  return auth;
}

void test_catalog_activates_three_protocols_and_auth_none()
{
  auto const root = create_empty_root("user-catalog-protocols");
  auto paths = test_paths(root);
  write_file(paths.providers_file, providers_json_three_protocols());

  auto catalog = ava::provider::ProviderCatalog::build(paths);
  expect(catalog.has_value(), catalog ? "catalog builds with three user providers" : catalog.error().format());
  if (!catalog)
    return;
  expect((*catalog)->contains("local-chat") && (*catalog)->contains("local-responses") && (*catalog)->contains("local-anthropic"),
         "user providers are registered as runtime factories");
  expect((*catalog)->provider_auth_is_none("local-anthropic") && !(*catalog)->provider_auth_is_none("local-chat"),
         "auth:none is exposed on the catalog descriptor");
  expect((*catalog)->provider_api_key_env("local-chat") == "LOCAL_CHAT_API_KEY", "api_key_env is retained on the descriptor");

  auto chat = (*catalog)->create("local-chat");
  expect(chat.has_value(), "chat factory creates");
  if (chat)
  {
    auto req = (*chat)->build_request(sample_request("local-chat", "m"), "secret-key");
    expect(req && req->url == "http://127.0.0.1:11434/v1/chat/completions", "chat uses exact canonical endpoint");
    expect(req && !req->follow_redirects, "chat disables redirects");
    expect(req && header_get(*req, "Authorization") == "Bearer secret-key", "chat sends Bearer for api_key");
    expect(req && !header_has(*req, "x-api-key"), "chat does not send x-api-key");
    expect(req && req->body.find("stream_options") != std::string::npos, "chat include_stream_usage enables stream_options");
  }

  auto responses = (*catalog)->create("local-responses");
  expect(responses.has_value(), "responses factory creates");
  if (responses)
  {
    auto req = (*responses)->build_request(sample_request("local-responses", "m"), make_auth("secret-key", "api_key"));
    expect(req && req->url == "http://127.0.0.1:8080/v1/responses", "responses uses exact canonical endpoint");
    expect(req && !req->follow_redirects, "responses disables redirects");
    expect(req && header_get(*req, "Authorization") == "Bearer secret-key", "responses sends Bearer for api_key");
    expect(req && req->body.find("max_output_tokens") != std::string::npos, "responses includes max_output_tokens");
    expect(req && !header_has(*req, "ChatGPT-Account-Id") && !header_has(*req, "chatgpt-account-id"), "generic responses never apply Codex OAuth headers");
    auto oauth_req = (*responses)->build_request(sample_request("local-responses", "m"), make_auth("oauth-token", "oauth", "acct"));
    expect(oauth_req && oauth_req->url == "http://127.0.0.1:8080/v1/responses", "generic responses keeps endpoint under oauth credential type");
    expect(oauth_req && !header_has(*oauth_req, "ChatGPT-Account-Id"), "generic responses ignores oauth account headers");
    expect(oauth_req && oauth_req->body.find("max_output_tokens") != std::string::npos, "generic responses still emits max_output_tokens");
  }

  auto anthropic = (*catalog)->create("local-anthropic");
  expect(anthropic.has_value(), "anthropic factory creates");
  if (anthropic)
  {
    auto req = (*anthropic)->build_request(sample_request("local-anthropic", "m"), make_auth("", "none"));
    expect(req && req->url == "http://127.0.0.1:9000/v1/messages", "anthropic uses exact canonical endpoint");
    expect(req && !req->follow_redirects, "anthropic disables redirects");
    expect(req && !header_has(*req, "Authorization") && !header_has(*req, "x-api-key"), "auth:none omits Authorization and x-api-key");
    expect(req && header_has(*req, "anthropic-version"), "anthropic still sends anthropic-version");
    auto oauth_req = (*anthropic)->build_request(sample_request("local-anthropic", "m"), make_auth("oauth", "oauth"));
    expect(oauth_req && !header_has(*oauth_req, "Authorization"), "generic anthropic never swaps to OAuth Authorization");
    expect(oauth_req && !header_has(*oauth_req, "anthropic-beta"), "generic anthropic never adds OAuth beta header");
  }
}

void test_user_api_key_precedence_and_isolation()
{
  auto const root = create_empty_root("user-catalog-auth-prec");
  auto paths = test_paths(root);
  write_file(paths.providers_file, providers_json_three_protocols());
  auto catalog = ava::provider::ProviderCatalog::build(paths);
  expect(catalog.has_value(), "catalog builds for auth precedence");
  if (!catalog)
    return;

  auto stored = ava::config::store_provider_credential(
      paths, ava::config::ProviderCredential{
                 .provider_id = "local-chat", .access_token = "stored-chat-key", .credential_type = "api_key", .account_id = "", .source = "test"});
  expect(stored.has_value(), "stored user provider API key writes auth.json");
  ::setenv("LOCAL_CHAT_API_KEY", "env-chat-key", 1);
  ::setenv("LOCAL_OPENAI_API_KEY", "wrong-generic-env", 1);
  NullTransport transport;
  auto prepared = ava::app::prepare_runtime_credentials(paths, "local-chat", {}, transport, "test", *catalog);
  expect(prepared && prepared->access_token == "stored-chat-key" && prepared->credential_type == "api_key",
         "stored provider-scoped API key wins over configured env");

  std::error_code ec;
  std::filesystem::remove(paths.auth_file, ec);
  prepared = ava::app::prepare_runtime_credentials(paths, "local-chat", {}, transport, "test", *catalog);
  expect(prepared && prepared->access_token == "env-chat-key", "configured api_key_env is used when stored key is absent");

  ::unsetenv("LOCAL_CHAT_API_KEY");
  prepared = ava::app::prepare_runtime_credentials(paths, "local-chat", {}, transport, "test", *catalog);
  expect(!prepared, "missing stored key and configured env fails closed without falling back to generic env names");

  prepared = ava::app::prepare_runtime_credentials(paths, "local-anthropic", {}, transport, "test", *catalog);
  expect(prepared && prepared->credential_type == "none" && prepared->access_token.empty(),
         "auth:none is a successful credential state without manufacturing tokens");

  ::unsetenv("LOCAL_OPENAI_API_KEY");
}

void test_connect_auth_none_does_not_write_auth_file()
{
  auto const root = create_empty_root("user-catalog-connect-none");
  auto paths = test_paths(root);
  write_file(paths.providers_file, providers_json_three_protocols());

  std::istringstream in;
  std::ostringstream out;
  std::ostringstream err;
  auto const code = ava::app::run_connect_provider_credential(
      paths, ava::app::ConnectProviderCredentialOptions{.provider_id = "local-anthropic", .credential_type = ava::app::ConnectCredentialType::ApiKey}, in, out,
      err);
  expect(code == 0 && err.str().empty(), "connect auth:none succeeds without error");
  expect(out.str().find("auth:none") != std::string::npos || out.str().find("no credential") != std::string::npos,
         "connect auth:none reports no credential required");
  expect(!std::filesystem::exists(paths.auth_file), "connect auth:none does not write auth.json");
}

void test_builtin_openai_behavior_unchanged_under_options_defaults()
{
  ava::provider::OpenAIProvider builtin;
  auto req = builtin.build_request(sample_request("openai", "gpt-test"), make_auth("tok", "api_key"));
  expect(req && req->url == "https://api.openai.com/v1/responses", "built-in OpenAI still uses default Responses path join");
  expect(req && req->follow_redirects, "built-in OpenAI still follows redirects");
  expect(req && header_get(*req, "Authorization") == "Bearer tok", "built-in OpenAI still sends Bearer");

  auto oauth = builtin.build_request(sample_request("openai", "gpt-test"), make_auth("tok", "oauth", "acct"));
  expect(oauth && header_has(*oauth, "ChatGPT-Account-Id"), "built-in OpenAI OAuth still applies account headers");
}

void test_custom_chat_reasoning_effort()
{
  auto const root = create_empty_root("user-catalog-reasoning-effort");
  auto const paths = test_paths(root);
  write_file(paths.providers_file, R"({"version":1,"providers":[{
    "id":"airouter","display_name":"Airouter","protocol":"openai_chat_completions",
    "base_url":"http://127.0.0.1:11434","auth":"none"}]})");
  auto const catalog = ava::provider::ProviderCatalog::build(paths);
  expect(catalog.has_value(), catalog ? "custom reasoning catalog builds" : catalog.error().format());
  if (!catalog)
  {
    return;
  }
  auto const provider = (*catalog)->create("airouter");
  expect(provider.has_value(), "custom reasoning provider creates");
  if (!provider)
  {
    return;
  }

  auto const models = ava::config::parse_model_registry(R"({"models":[{
    "provider":"airouter","id":"DeepSeek-V4-Flash","api_family":"openai_chat_completions",
    "supports_reasoning":true,"reasoning_levels":["high","xhigh"],
    "reasoning_format":"reasoning_content","compatibility_quirks":["reasoning_effort"]}]})");
  auto const model = ava::config::find_model(models, "airouter", "DeepSeek-V4-Flash");
  expect(model.has_value(), "custom reasoning model parses");
  if (!model)
  {
    return;
  }
  auto const selector = ava::app::reasoning_selector_view(*model, std::nullopt);
  expect(selector && selector->items.size() == 3 && selector->items.at(0).label == "Default" && selector->items.at(1).value == "high" &&
             selector->items.at(2).value == "xhigh",
         "Airouter DeepSeek exposes Default, High and Extra high without a false Off choice");
  expect(ava::config::reasoning_parameter_text(*model) == "request.reasoning_effort=<level>", "custom effort metadata describes the actual wire parameter");

  auto const workspace = root / "workspace";
  std::filesystem::create_directories(workspace);
  ava::app::runtime::OpenContext open_context;
  open_context.workspace_dir = workspace;
  open_context.current_dir = workspace;
  open_context.paths = paths;
  auto opened = ava::app::runtime::Session::open(open_context);
  expect(opened.has_value(), "reasoning cycle fixture opens a session");
  if (opened)
  {
    ava::app::runtime::session_ts session(std::move(*opened));
    auto switched = ava::app::runtime::Session::switch_model_and_refresh(session, *model);
    expect(switched.has_value(), "reasoning cycle fixture selects custom DeepSeek");
    for (auto const* expected : {"high", "xhigh", "default", "high", "xhigh", "default"})
    {
      auto cycled = ava::app::cycle_runtime_reasoning(session);
      expect(cycled.has_value(), "custom DeepSeek reasoning cycle succeeds");
      auto const view = ava::app::reasoning_selector_view(session);
      expect(view && view->items.at(view->selected_item_index).value == expected,
             "DeepSeek cycles High, Extra high, Default repeatedly and selector agrees with runtime state");
    }
  }

  for (auto const* level : {"high", "xhigh"})
  {
    auto const selection = ava::app::runtime::resolve_runtime_reasoning_selection(
        *model, {.level = level, .provider_level = std::nullopt, .budget_tokens = std::nullopt, .display = {}});
    expect(selection.has_value(), "advertised Airouter effort resolves");
    if (!selection)
    {
      continue;
    }
    auto request = sample_request("airouter", "DeepSeek-V4-Flash");
    request.compatibility_quirks = model->compatibility_quirks;
    request.reasoning = ava::app::runtime::provider_reasoning_options(*selection);
    auto const built = (*provider)->build_request(request, "");
    expect(built && ava::core::json::string_field(built->body, "reasoning_effort") == level && !built->body.contains("\"thinking\""),
           "custom effort uses the exact selected wire value, including xhigh without DeepSeek-direct remapping");
    request.reasoning = std::nullopt;
    auto const automatic = (*provider)->build_request(request, "");
    expect(automatic && !automatic->body.contains("reasoning_effort") && !automatic->body.contains("\"thinking\""),
           "Default omits the override rather than disabling thinking");
  }
  for (auto const* unsupported : {"none", "low", "medium", "max", "enabled"})
  {
    expect(!ava::app::runtime::resolve_runtime_reasoning_selection(
               *model, {.level = unsupported, .provider_level = std::nullopt, .budget_tokens = std::nullopt, .display = {}}),
           "Airouter DeepSeek rejects efforts it does not advertise");
  }
  auto undeclared = *model;
  undeclared.supports_reasoning = false;
  undeclared.reasoning_levels.clear();
  expect(!ava::app::reasoning_selector_view(undeclared, std::nullopt), "unknown or disabled capabilities remain unavailable");

  // Other custom models can opt into the same wire format with different levels.
  auto qwen = *model;
  qwen.model_id = "Qwen3.8";
  qwen.reasoning_levels = {"none", "low", "medium", "xhigh"};
  for (auto const& level : qwen.reasoning_levels)
  {
    auto const selection = ava::app::runtime::resolve_runtime_reasoning_selection(
        qwen, {.level = level, .provider_level = std::nullopt, .budget_tokens = std::nullopt, .display = {}});
    expect(selection.has_value(), "Qwen effort resolves independently from DeepSeek");
    if (!selection)
    {
      continue;
    }
    auto request = sample_request("airouter", qwen.model_id);
    request.compatibility_quirks = qwen.compatibility_quirks;
    request.reasoning = ava::app::runtime::provider_reasoning_options(*selection);
    auto const built = (*provider)->build_request(request, "");
    expect(built && ava::core::json::string_field(built->body, "reasoning_effort") == level, "Qwen none, low, medium and xhigh are preserved on the wire");
  }
}

}  // namespace

void run_provider_user_catalog_tests()
{
  test_catalog_activates_three_protocols_and_auth_none();
  test_user_api_key_precedence_and_isolation();
  test_connect_auth_none_does_not_write_auth_file();
  test_builtin_openai_behavior_unchanged_under_options_defaults();
  test_custom_chat_reasoning_effort();
}
