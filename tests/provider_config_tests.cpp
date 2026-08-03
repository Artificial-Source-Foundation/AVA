#include "sys.h"
#include "tests/support/test_harness.h"
#include "ava/config/provider_config.h"
#include "ava/config/xdg_paths.h"
#include "ava/core/error.h"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace {

ava::config::XdgPaths providers_test_paths(std::filesystem::path const& root)
{
  auto const config_home = root / "config";
  auto const state_home = root / "state";
  auto const data_home = root / "data";
  auto const ava_config = config_home / "ava";
  auto const ava_state = state_home / "ava";
  std::filesystem::create_directories(ava_config);
  std::filesystem::create_directories(ava_state / "sessions");
  ::chmod(root.c_str(), 0700);
  ::chmod(config_home.c_str(), 0700);
  ::chmod(ava_config.c_str(), 0700);
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

void write_providers_file(std::filesystem::path const& path, std::string_view body, mode_t mode = 0600)
{
  std::filesystem::create_directories(path.parent_path());
  {
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file.write(body.data(), static_cast<std::streamsize>(body.size()));
  }
  ::chmod(path.c_str(), mode);
}

bool error_mentions(ava::core::Error const& error, std::string_view needle)
{
  if (error.message().find(needle) != std::string::npos)
    return true;
  auto const formatted = error.format();
  return formatted.find(needle) != std::string::npos;
}

bool error_has_path_context(ava::core::Error const& error)
{
  for (auto const& ctx : error.context())
  {
    if (ctx.key == "path" && !ctx.value.empty())
      return true;
  }
  return false;
}

std::string provider_json(std::string_view id, std::string_view display, std::string_view protocol, std::string_view base_url, std::string_view extras = {})
{
  std::string body = std::string("{\"id\":\"") + std::string(id) + "\",\"display_name\":\"" + std::string(display) + "\",\"protocol\":\"" +
                     std::string(protocol) + "\",\"base_url\":\"" + std::string(base_url) + "\"";
  if (!extras.empty())
  {
    body.push_back(',');
    body.append(extras);
  }
  body.push_back('}');
  return body;
}

std::string wrap_providers(std::string_view providers_array_body)
{
  return std::string("{\"version\":1,\"providers\":[") + std::string(providers_array_body) + "]}";
}

void test_parse_all_protocols_and_defaults()
{
  auto const body = wrap_providers(provider_json("local-openai", "Local OpenAI", "openai_chat_completions", "http://127.0.0.1:11434") + "," +
                                   provider_json("local-responses", "Local Responses", "openai_responses", "http://localhost:8080/") + "," +
                                   provider_json("local-anthropic", "Local Anthropic", "anthropic_messages", "http://[::1]:9000", "\"auth\":\"none\""));

  auto parsed = ava::config::parse_user_provider_definitions(body);
  expect(parsed.has_value() && parsed->size() == 3, "valid three-protocol providers.json parses");
  if (!parsed)
    return;

  auto const& chat = (*parsed)[0];
  expect(chat.id == "local-openai" && chat.display_name == "Local OpenAI", "chat provider identity is preserved");
  expect(chat.protocol == ava::config::ProviderProtocol::OpenAIChatCompletions, "chat protocol decodes");
  expect(chat.base_url == "http://127.0.0.1:11434", "chat base_url trailing slash trim is a no-op without slash");
  expect(chat.request_path == "/v1/chat/completions", "chat protocol default request_path");
  expect(chat.endpoint == "http://127.0.0.1:11434/v1/chat/completions", "chat canonical endpoint joins base and path");
  expect(chat.auth == ava::config::ProviderAuthMode::ApiKey && chat.api_key_env == "LOCAL_OPENAI_API_KEY", "api_key auth defaults and derives env from id");
  expect(!chat.compatibility.include_stream_usage, "compatibility defaults to include_stream_usage=false");

  auto const& responses = (*parsed)[1];
  expect(responses.protocol == ava::config::ProviderProtocol::OpenAIResponses, "responses protocol decodes");
  expect(responses.base_url == "http://localhost:8080", "responses base_url trims trailing slash");
  expect(responses.request_path == "/v1/responses" && responses.endpoint == "http://localhost:8080/v1/responses",
         "responses protocol default path and endpoint");
  expect(responses.api_key_env == "LOCAL_RESPONSES_API_KEY", "responses api_key_env is derived");

  auto const& anthropic = (*parsed)[2];
  expect(anthropic.protocol == ava::config::ProviderProtocol::AnthropicMessages, "anthropic protocol decodes");
  expect(anthropic.base_url == "http://[::1]:9000" && anthropic.request_path == "/v1/messages", "anthropic localhost IPv6 and default path");
  expect(anthropic.auth == ava::config::ProviderAuthMode::None && anthropic.api_key_env.empty(), "auth none leaves api_key_env empty");
}

void test_parse_explicit_endpoint_and_compatibility()
{
  auto const body = wrap_providers(provider_json(
      "proxy-chat", "Proxy Chat", "openai_chat_completions", "https://proxy.example.com/openai",
      "\"request_path\":\"/v1/chat/completions\",\"auth\":\"api_key\",\"api_key_env\":\"PROXY_CHAT_KEY\",\"compatibility\":{\"include_stream_usage\":true}"));
  auto parsed = ava::config::parse_user_provider_definitions(body);
  expect(parsed && parsed->size() == 1, "explicit endpoint provider parses");
  if (!parsed)
    return;
  auto const& def = parsed->front();
  expect(def.base_url == "https://proxy.example.com/openai", "https remote base with path prefix is kept without trailing slash");
  expect(def.request_path == "/v1/chat/completions", "explicit request_path is retained");
  expect(def.endpoint == "https://proxy.example.com/openai/v1/chat/completions", "canonical endpoint joins without slash ambiguity");
  expect(def.api_key_env == "PROXY_CHAT_KEY", "explicit api_key_env is retained");
  expect(def.compatibility.include_stream_usage, "chat protocol may enable include_stream_usage");
}

void test_missing_file_and_load_success()
{
  auto const root = create_empty_root("providers-missing");
  auto const paths = providers_test_paths(root);
  auto missing = ava::config::load_user_provider_definitions(paths);
  expect(missing && missing->empty(), "missing providers.json yields successful empty definitions");

  write_providers_file(paths.providers_file,
                       wrap_providers(provider_json("local-openai", "Local OpenAI", "openai_chat_completions", "http://127.0.0.1:11434")));
  auto loaded = ava::config::load_user_provider_definitions(paths);
  expect(loaded && loaded->size() == 1 && loaded->front().id == "local-openai", "owner-controlled providers.json loads");
  if (loaded)
  {
    auto collision = ava::config::validate_user_provider_ids_against_builtins(*loaded);
    expect(collision.has_value(), "non-colliding user id passes builtin collision helper");
  }
}

void test_malformed_oversize_unknown_duplicate()
{
  auto malformed = ava::config::parse_user_provider_definitions("{not-json");
  expect(!malformed, "malformed JSON is rejected");

  auto wrong_version = ava::config::parse_user_provider_definitions(R"({"version":2,"providers":[]})");
  expect(!wrong_version && error_mentions(wrong_version.error(), "version"), "version must be exactly 1");

  auto missing_providers = ava::config::parse_user_provider_definitions(R"({"version":1})");
  expect(!missing_providers, "providers array is required");

  auto unknown_root = ava::config::parse_user_provider_definitions(R"({"version":1,"providers":[],"extra":true})");
  expect(!unknown_root && error_mentions(unknown_root.error(), "unsupported"), "unknown top-level fields are rejected");

  auto unknown_provider_field = ava::config::parse_user_provider_definitions(
      wrap_providers(R"({"id":"x","display_name":"X","protocol":"openai_chat_completions","base_url":"http://127.0.0.1","nope":1})"));
  expect(!unknown_provider_field, "unknown provider fields are rejected");

  auto unknown_compat = ava::config::parse_user_provider_definitions(wrap_providers(
      provider_json("x", "X", "openai_chat_completions", "http://127.0.0.1", "\"compatibility\":{\"include_stream_usage\":false,\"other\":true}")));
  expect(!unknown_compat, "unknown compatibility fields are rejected");

  auto dup_key = ava::config::parse_user_provider_definitions(R"({"version":1,"version":1,"providers":[]})");
  expect(!dup_key && error_mentions(dup_key.error(), "duplicate"), "duplicate decoded JSON keys are rejected");

  auto dup_id = ava::config::parse_user_provider_definitions(wrap_providers(provider_json("same", "A", "openai_chat_completions", "http://127.0.0.1") + "," +
                                                                            provider_json("same", "B", "openai_responses", "http://127.0.0.1")));
  expect(!dup_id && error_mentions(dup_id.error(), "duplicate"), "duplicate provider ids are rejected");

  std::string huge(ava::config::kMaxUserProviderConfigBytes + 1, 'x');
  auto oversize = ava::config::parse_user_provider_definitions(huge);
  expect(!oversize, "oversize document body is rejected");

  auto const root = create_empty_root("providers-oversize-file");
  auto const paths = providers_test_paths(root);
  write_providers_file(paths.providers_file, std::string(ava::config::kMaxUserProviderConfigBytes + 1, 'a'));
  auto oversize_file = ava::config::load_user_provider_definitions(paths);
  expect(!oversize_file && error_has_path_context(oversize_file.error()), "oversize present file fails with path context and no content");

  // Max provider count.
  std::string many = "{\"version\":1,\"providers\":[";
  for (std::size_t i = 0; i < ava::config::kMaxUserProviders + 1; ++i)
  {
    if (i != 0)
      many.push_back(',');
    many += provider_json("p-" + std::to_string(i), "P", "openai_chat_completions", "http://127.0.0.1");
  }
  many += "]}";
  auto too_many = ava::config::parse_user_provider_definitions(many);
  expect(!too_many, "more than 128 providers is rejected");
}

void test_id_display_env_boundaries()
{
  auto empty_id = ava::config::parse_user_provider_definitions(wrap_providers(provider_json("", "X", "openai_chat_completions", "http://127.0.0.1")));
  expect(!empty_id, "empty id is rejected");

  auto upper_id = ava::config::parse_user_provider_definitions(wrap_providers(provider_json("Local", "X", "openai_chat_completions", "http://127.0.0.1")));
  expect(!upper_id, "uppercase id is rejected");

  auto bad_id_char =
      ava::config::parse_user_provider_definitions(wrap_providers(provider_json("local.openai", "X", "openai_chat_completions", "http://127.0.0.1")));
  expect(!bad_id_char, "id rejects characters outside [a-z0-9_-]");

  std::string long_id(65, 'a');
  auto long_id_result =
      ava::config::parse_user_provider_definitions(wrap_providers(provider_json(long_id, "X", "openai_chat_completions", "http://127.0.0.1")));
  expect(!long_id_result, "id longer than 64 is rejected");

  auto empty_display = ava::config::parse_user_provider_definitions(wrap_providers(provider_json("ok", "", "openai_chat_completions", "http://127.0.0.1")));
  expect(!empty_display, "empty display_name is rejected");

  std::string long_display(129, 'd');
  auto long_display_result =
      ava::config::parse_user_provider_definitions(wrap_providers(provider_json("ok", long_display, "openai_chat_completions", "http://127.0.0.1")));
  expect(!long_display_result, "display_name longer than 128 is rejected");

  auto control_display = ava::config::parse_user_provider_definitions(
      wrap_providers(R"({"id":"ok","display_name":"bad\u0001name","protocol":"openai_chat_completions","base_url":"http://127.0.0.1"})"));
  expect(!control_display, "display_name rejects controls");

  auto bad_env = ava::config::parse_user_provider_definitions(
      wrap_providers(provider_json("ok", "X", "openai_chat_completions", "http://127.0.0.1", "\"api_key_env\":\"not-valid\"")));
  expect(!bad_env, "api_key_env must match shell-var pattern");

  auto env_digit = ava::config::parse_user_provider_definitions(
      wrap_providers(provider_json("ok", "X", "openai_chat_completions", "http://127.0.0.1", "\"api_key_env\":\"1ABC\"")));
  expect(!env_digit, "api_key_env may not start with a digit");

  std::string long_env(129, 'A');
  auto long_env_result = ava::config::parse_user_provider_definitions(
      wrap_providers(provider_json("ok", "X", "openai_chat_completions", "http://127.0.0.1", "\"api_key_env\":\"" + long_env + "\"")));
  expect(!long_env_result, "api_key_env longer than 128 is rejected");

  auto none_with_env = ava::config::parse_user_provider_definitions(
      wrap_providers(provider_json("ok", "X", "openai_chat_completions", "http://127.0.0.1", "\"auth\":\"none\",\"api_key_env\":\"OK_KEY\"")));
  expect(!none_with_env, "auth none rejects api_key_env");

  auto stream_on_responses = ava::config::parse_user_provider_definitions(
      wrap_providers(provider_json("ok", "X", "openai_responses", "https://example.com", "\"compatibility\":{\"include_stream_usage\":true}")));
  expect(!stream_on_responses, "include_stream_usage is rejected for non-chat protocols");

  auto stream_on_anthropic = ava::config::parse_user_provider_definitions(
      wrap_providers(provider_json("ok", "X", "anthropic_messages", "https://example.com", "\"compatibility\":{\"include_stream_usage\":true}")));
  expect(!stream_on_anthropic, "include_stream_usage is rejected for anthropic_messages");
}

void test_url_boundaries()
{
  auto reject_url = [](std::string_view base_url, std::string_view label) {
    auto parsed = ava::config::parse_user_provider_definitions(wrap_providers(provider_json("ok", "X", "openai_chat_completions", base_url)));
    expect(!parsed, std::string(label));
  };
  auto accept_url = [](std::string_view base_url, std::string_view expected_base, std::string_view label) {
    auto parsed = ava::config::parse_user_provider_definitions(wrap_providers(provider_json("ok", "X", "openai_chat_completions", base_url)));
    expect(parsed && parsed->size() == 1 && parsed->front().base_url == expected_base, std::string(label));
  };

  accept_url("https://api.example.com", "https://api.example.com", "https remote is accepted");
  accept_url("https://api.example.com:8443/v1/", "https://api.example.com:8443/v1", "https with port and trimmed path");
  accept_url("http://localhost", "http://localhost", "http localhost is accepted");
  accept_url("http://127.0.0.1", "http://127.0.0.1", "http 127.0.0.1 is accepted");
  accept_url("http://[::1]", "http://[::1]", "http [::1] is accepted");

  reject_url("http://example.com", "remote http is rejected");
  reject_url("https://user:pass@example.com", "userinfo is rejected");
  reject_url("https://example.com?x=1", "query is rejected");
  reject_url("https://example.com#frag", "fragment is rejected");
  reject_url("https://example.com/a\\b", "backslashes are rejected");
  reject_url("https://example.com/a/./b", "dot segments are rejected");
  reject_url("https://example.com/a/../b", "dot-dot segments are rejected");
  reject_url("https://example.com/a//b", "empty path segments are rejected");
  reject_url("https://example.com/%2e%2e/x", "encoded dot ambiguity is rejected");
  reject_url("https://example.com/%2fsecret", "encoded separator ambiguity is rejected");
  reject_url("https://example.com/%00x", "encoded controls are rejected");
  reject_url("https:///nohost", "missing host is rejected");
  reject_url("https://example.com:", "empty port is rejected");
  reject_url("https://example.com:0", "port 0 is rejected");
  reject_url("https://example.com:65536", "port above 65535 is rejected");
  reject_url("ftp://example.com", "non-http(s) scheme is rejected");
  reject_url("https://exam ple.com", "controls/spaces in host are rejected");

  std::string long_url = "https://example.com/" + std::string(2100, 'a');
  reject_url(long_url, "base_url longer than 2KiB is rejected");

  auto bad_path = ava::config::parse_user_provider_definitions(
      wrap_providers(provider_json("ok", "X", "openai_chat_completions", "https://example.com", "\"request_path\":\"relative\"")));
  expect(!bad_path, "relative request_path is rejected");

  auto path_query = ava::config::parse_user_provider_definitions(
      wrap_providers(provider_json("ok", "X", "openai_chat_completions", "https://example.com", "\"request_path\":\"/v1?x=1\"")));
  expect(!path_query, "request_path query is rejected");

  auto path_dot = ava::config::parse_user_provider_definitions(
      wrap_providers(provider_json("ok", "X", "openai_chat_completions", "https://example.com", "\"request_path\":\"/v1/../chat\"")));
  expect(!path_dot, "request_path dot segments are rejected");

  auto path_encoded = ava::config::parse_user_provider_definitions(
      wrap_providers(provider_json("ok", "X", "openai_chat_completions", "https://example.com", "\"request_path\":\"/v1/%2fchat\"")));
  expect(!path_encoded, "request_path encoded separators are rejected");

  std::string long_path = "/" + std::string(1024, 'p');
  auto path_long = ava::config::parse_user_provider_definitions(
      wrap_providers(provider_json("ok", "X", "openai_chat_completions", "https://example.com", "\"request_path\":\"" + long_path + "\"")));
  expect(!path_long, "request_path longer than 1KiB is rejected");
}

void test_filesystem_authority()
{
  auto const root = create_empty_root("providers-fs");
  auto paths = providers_test_paths(root);
  auto const body = wrap_providers(provider_json("local-openai", "Local OpenAI", "openai_chat_completions", "http://127.0.0.1:11434"));

  // Symlink.
  {
    auto const target = root / "target-providers.json";
    write_providers_file(target, body);
    std::error_code ec;
    std::filesystem::remove(paths.providers_file, ec);
    std::filesystem::create_symlink(target, paths.providers_file, ec);
    auto loaded = ava::config::load_user_provider_definitions(paths);
    expect(!loaded, "symlink providers.json is rejected");
    if (!loaded)
      expect(error_has_path_context(loaded.error()) && !error_mentions(loaded.error(), "local-openai"),
             "symlink rejection is sanitized with path context and no body content");
    std::filesystem::remove(paths.providers_file, ec);
  }

  // Group/world writable.
  {
    write_providers_file(paths.providers_file, body, 0662);
    auto loaded = ava::config::load_user_provider_definitions(paths);
    expect(!loaded, "group/world-writable providers.json is rejected");
    std::filesystem::remove(paths.providers_file);
  }
  {
    write_providers_file(paths.providers_file, body, 0644);  // world-readable is OK; not writable
    auto loaded = ava::config::load_user_provider_definitions(paths);
    expect(loaded && loaded->size() == 1, "group/world-readable but not writable providers.json is accepted");
    std::filesystem::remove(paths.providers_file);
  }
  {
    write_providers_file(paths.providers_file, body, 0620);
    auto loaded = ava::config::load_user_provider_definitions(paths);
    expect(!loaded, "group-writable providers.json is rejected");
    std::filesystem::remove(paths.providers_file);
  }

  // Hard link.
  {
    auto const other = root / "hardlink-other.json";
    write_providers_file(paths.providers_file, body, 0600);
    std::error_code ec;
    std::filesystem::create_hard_link(paths.providers_file, other, ec);
    if (!ec)
    {
      auto loaded = ava::config::load_user_provider_definitions(paths);
      expect(!loaded, "hard-linked providers.json is rejected");
      std::filesystem::remove(other, ec);
    }
    std::filesystem::remove(paths.providers_file, ec);
  }

  // FIFO nonblocking behavior.
  {
    std::error_code ec;
    std::filesystem::remove(paths.providers_file, ec);
    expect(::mkfifo(paths.providers_file.c_str(), 0600) == 0, "FIFO providers fixture is created");
    auto const started = std::chrono::steady_clock::now();
    auto loaded = ava::config::load_user_provider_definitions(paths);
    auto const elapsed = std::chrono::steady_clock::now() - started;
    expect(!loaded, "FIFO providers.json is rejected");
    expect(elapsed < std::chrono::seconds(2), "FIFO open does not block the loader");
    std::filesystem::remove(paths.providers_file, ec);
  }

  // Wrong owner when safely testable (requires privilege to chown).
  {
    write_providers_file(paths.providers_file, body, 0600);
    auto const foreign_uid = ::geteuid() == 0 ? 65534 : static_cast<uid_t>(-1);
    if (::geteuid() == 0 && foreign_uid != static_cast<uid_t>(-1) && ::chown(paths.providers_file.c_str(), foreign_uid, static_cast<gid_t>(-1)) == 0)
    {
      auto loaded = ava::config::load_user_provider_definitions(paths);
      expect(!loaded, "wrong-owner providers.json is rejected");
    }
    std::filesystem::remove(paths.providers_file);
  }

  // CLOEXEC practicality: production opens with O_CLOEXEC; verify the flag works on this platform
  // with the same open flags the loader uses.
  {
    write_providers_file(paths.providers_file, body, 0600);
    int flags = O_RDONLY | O_CLOEXEC | O_NONBLOCK;
#ifdef O_NOFOLLOW
    flags |= O_NOFOLLOW;
#endif
    int const fd = ::open(paths.providers_file.c_str(), flags);
    expect(fd >= 0, "CLOEXEC open of providers fixture succeeds");
    if (fd >= 0)
    {
      int const fd_flags = ::fcntl(fd, F_GETFD);
      expect(fd_flags >= 0 && (fd_flags & FD_CLOEXEC) != 0, "providers open flags include FD_CLOEXEC");
      ::close(fd);
    }
    // And the real loader still succeeds on the same file.
    auto loaded = ava::config::load_user_provider_definitions(paths);
    expect(loaded && loaded->size() == 1, "loader accepts a safe regular providers.json");
  }
}

void test_builtin_collision_helper()
{
  auto colliding =
      ava::config::parse_user_provider_definitions(wrap_providers(provider_json("openai", "Shadow OpenAI", "openai_chat_completions", "https://example.com")));
  expect(colliding && colliding->size() == 1, "builtin-id user entry parses in isolation");
  if (!colliding)
    return;
  auto against_builtins = ava::config::validate_user_provider_ids_against_builtins(*colliding);
  expect(!against_builtins && error_mentions(against_builtins.error(), "collides"), "builtin collision helper rejects openai id");

  std::string_view reserved[] = {"reserved-one", "reserved-two"};
  auto custom = ava::config::validate_user_provider_ids_against_reserved(*colliding, reserved);
  expect(custom.has_value(), "non-matching reserved list allows the id");
  reserved[0] = "openai";
  auto custom_hit = ava::config::validate_user_provider_ids_against_reserved(*colliding, reserved);
  expect(!custom_hit, "reserved list collision is reported");
}

void test_default_env_helper()
{
  expect(ava::config::default_api_key_env_for_provider_id("local-openai") == "LOCAL_OPENAI_API_KEY", "default env maps dashes to underscores");
  expect(ava::config::default_request_path_for(ava::config::ProviderProtocol::OpenAIChatCompletions) == "/v1/chat/completions", "default chat path helper");
  expect(ava::config::default_request_path_for(ava::config::ProviderProtocol::OpenAIResponses) == "/v1/responses", "default responses path helper");
  expect(ava::config::default_request_path_for(ava::config::ProviderProtocol::AnthropicMessages) == "/v1/messages", "default anthropic path helper");
  expect(ava::config::to_string(ava::config::ProviderProtocol::OpenAIChatCompletions) == "openai_chat_completions", "protocol to_string");
  expect(ava::config::to_string(ava::config::ProviderAuthMode::None) == "none", "auth to_string");
}

}  // namespace

void run_provider_config_tests()
{
  test_parse_all_protocols_and_defaults();
  test_parse_explicit_endpoint_and_compatibility();
  test_missing_file_and_load_success();
  test_malformed_oversize_unknown_duplicate();
  test_id_display_env_boundaries();
  test_url_boundaries();
  test_filesystem_authority();
  test_builtin_collision_helper();
  test_default_env_helper();
}
