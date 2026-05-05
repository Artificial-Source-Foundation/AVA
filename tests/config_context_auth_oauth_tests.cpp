#include "ava/app/commands.h"
#include "ava/app/events.h"
#include "ava/app/headless_policy.h"
#include "ava/app/print_mode.h"
#include "ava/app/rpc_mode.h"
#include "ava/app/runtime.h"

#include "ava/agent/agent_loop.h"
#include "ava/agent/mode.h"
#include "ava/agent/tool_dispatcher.h"

#include "ava/tools/bash_tool.h"
#include "ava/tools/file_tools.h"
#include "ava/tools/search_tools.h"

#include "ava/tui/composer.h"
#include "ava/tui/terminal.h"

#include "ava/config/auth.h"
#include "ava/config/model_config.h"
#include "ava/config/model_profiles.h"
#include "ava/config/openai_oauth.h"
#include "ava/config/prompt_config.h"
#include "ava/config/provider_profiles.h"
#include "ava/config/reasoning_profiles.h"
#include "ava/config/xdg_paths.h"

#include "ava/session/compaction.h"
#include "ava/session/export.h"
#include "ava/session/session_store.h"

#include "ava/permissions/permission.h"

#include "ava/provider/openai_provider.h"

#include "ava/context/context_loader.h"

#include "ava/core/ids.h"
#include "ava/core/json.h"

#include "tests/support/fake_transport.h"
#include "tests/support/test_harness.h"

#include <algorithm>
#include <chrono>
#include <climits>
#include <cstdlib>
#include <cwchar>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include <sys/stat.h>
#include <unistd.h>

namespace {

void test_xdg_paths()
{
  auto const root = temp_root() / "xdg";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  std::filesystem::create_directories(root);

  setenv("HOME", (root / "home").c_str(), 1);
  unsetenv("XDG_CONFIG_HOME");
  unsetenv("XDG_STATE_HOME");
  unsetenv("XDG_DATA_HOME");
  auto fallback = ava::config::xdg_paths();
  expect(fallback.ava_config_dir == root / "home" / ".config" / "ava", "XDG config falls back to ~/.config/ava");
  expect(fallback.ava_state_dir == root / "home" / ".local" / "state" / "ava",
         "XDG state falls back to ~/.local/state/ava");
  expect(fallback.auth_file == fallback.ava_config_dir / "auth.json", "auth file is in XDG config dir");
  expect(fallback.compaction_file == fallback.ava_config_dir / "compaction.json",
         "compaction file is in XDG config dir");
  expect(fallback.global_agents_file == fallback.ava_config_dir / "AGENTS.md",
         "global AGENTS.md file is in XDG config dir");
  expect(fallback.sessions_dir == fallback.ava_state_dir / "sessions", "sessions are in XDG state dir");

  setenv("XDG_CONFIG_HOME", (root / "config").c_str(), 1);
  setenv("XDG_STATE_HOME", (root / "state").c_str(), 1);
  setenv("XDG_DATA_HOME", (root / "data").c_str(), 1);
  auto overridden = ava::config::xdg_paths();
  expect(overridden.ava_config_dir == root / "config" / "ava", "XDG config override is honored");
  expect(overridden.ava_state_dir == root / "state" / "ava", "XDG state override is honored");
  expect(ava::config::opencode_auth_path() == root / "data" / "opencode" / "auth.json",
         "opencode auth path follows XDG data home");

  setenv("XDG_CONFIG_HOME", "relative-config", 1);
  auto relative_ignored = ava::config::xdg_paths();
  expect(relative_ignored.config_home == root / "home" / ".config", "relative XDG config path is ignored safely");

  unsetenv("HOME");
  unsetenv("XDG_CONFIG_HOME");
  unsetenv("XDG_STATE_HOME");
  unsetenv("XDG_DATA_HOME");
  auto no_home = ava::config::xdg_paths();
  expect(no_home.config_home.is_absolute(), "XDG config fallback remains absolute when HOME is unset");
  expect(no_home.config_home != std::filesystem::current_path() / ".config",
         "XDG fallback does not use current directory");

  setenv("HOME", "", 1);
  auto empty_home = ava::config::xdg_paths();
  expect(empty_home.state_home.is_absolute(), "XDG state fallback remains absolute when HOME is empty");
}

void test_context_loader()
{
  auto const root = temp_root() / "context";
  auto const workspace = root / "workspace";
  auto const nested = workspace / "src" / "feature";
  auto const config = root / "config" / "ava";
  auto const outside = root / "outside";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  std::filesystem::create_directories(nested);
  std::filesystem::create_directories(config);
  std::filesystem::create_directories(outside);

  auto const root_agents = workspace / "AGENTS.md";
  auto const src_agents = workspace / "src" / "AGENTS.md";
  auto const nested_agents = nested / "AGENTS.md";
  auto const global_agents = config / "AGENTS.md";
  {
    std::ofstream file(root_agents, std::ios::binary | std::ios::trunc);
    file << "root instructions\n";
  }
  {
    std::ofstream file(src_agents, std::ios::binary | std::ios::trunc);
    file << "src instructions\n";
  }
  {
    std::ofstream file(nested_agents, std::ios::binary | std::ios::trunc);
    file << "nested instructions";
  }
  {
    std::ofstream file(global_agents, std::ios::binary | std::ios::trunc);
    file << "global instructions\n";
  }

  auto loaded = ava::context::load_context_files(ava::context::ContextLoadOptions{
      .workspace_root = workspace,
      .current_dir = nested,
      .global_agents_file = {},
      .max_file_bytes = 1024,
  });
  expect(loaded.has_value(), "context loader loads workspace AGENTS.md files");
  if (loaded) {
    expect(loaded->size() == 3, "context loader finds root-to-current workspace contexts");
    if (loaded->size() == 3) {
      expect((*loaded)[0].path == std::filesystem::weakly_canonical(root_agents), "root AGENTS.md loads first");
      expect((*loaded)[1].path == std::filesystem::weakly_canonical(src_agents), "intermediate AGENTS.md loads second");
      expect((*loaded)[2].path == std::filesystem::weakly_canonical(nested_agents), "nested AGENTS.md loads third");
      expect((*loaded)[0].source_type == ava::context::ContextSourceType::Workspace,
             "workspace context records workspace source type");
    }
  }

  auto outside_loaded = ava::context::load_context_files(ava::context::ContextLoadOptions{
      .workspace_root = workspace,
      .current_dir = outside,
      .global_agents_file = {},
      .max_file_bytes = 1024,
  });
  expect(outside_loaded.has_value(), "context loader handles current_dir outside workspace");
  if (outside_loaded) {
    expect(outside_loaded->size() == 1 && (*outside_loaded)[0].path == std::filesystem::weakly_canonical(root_agents),
           "outside current_dir only loads workspace root AGENTS.md");
  }

  auto with_global = ava::context::load_context_files(ava::context::ContextLoadOptions{
      .workspace_root = workspace,
      .current_dir = nested,
      .global_agents_file = global_agents,
      .max_file_bytes = 1024,
  });
  expect(with_global.has_value(), "context loader includes global AGENTS.md");
  if (with_global) {
    expect(with_global->size() == 4, "global AGENTS.md is appended to workspace contexts");
    if (with_global->size() == 4) {
      expect((*with_global)[3].path == std::filesystem::weakly_canonical(global_agents),
             "global AGENTS.md path is recorded");
      expect((*with_global)[3].source_type == ava::context::ContextSourceType::Global,
             "global AGENTS.md records global source type");

      auto const formatted = ava::context::format_context_for_prompt(*with_global);
      expect(formatted.find("# Loaded Project Instructions") != std::string::npos,
             "formatted context includes a section title");
      expect(formatted.find("## workspace: " + std::filesystem::weakly_canonical(root_agents).string()) !=
                 std::string::npos,
             "formatted context includes workspace source/path header");
      expect(formatted.find("## global: " + std::filesystem::weakly_canonical(global_agents).string()) !=
                 std::string::npos,
             "formatted context includes global source/path header");
      expect(formatted.find("root instructions") != std::string::npos &&
                 formatted.find("global instructions") != std::string::npos,
             "formatted context includes loaded content");
    }
  }

  auto deduped_global = ava::context::load_context_files(ava::context::ContextLoadOptions{
      .workspace_root = workspace,
      .current_dir = nested,
      .global_agents_file = root_agents,
      .max_file_bytes = 1024,
  });
  expect(deduped_global.has_value(), "context loader handles duplicate global path");
  if (deduped_global) {
    expect(deduped_global->size() == 3, "duplicate global AGENTS.md is not loaded twice");
  }

  auto const oversized_workspace = root / "oversized";
  std::filesystem::create_directories(oversized_workspace);
  {
    std::ofstream file(oversized_workspace / "AGENTS.md", std::ios::binary | std::ios::trunc);
    file << std::string(6, 'x');
  }
  auto oversized = ava::context::load_context_files(ava::context::ContextLoadOptions{
      .workspace_root = oversized_workspace,
      .current_dir = oversized_workspace,
      .global_agents_file = {},
      .max_file_bytes = 5,
  });
  expect(!oversized && oversized.error().category() == ava::core::ErrorCategory::Io,
         "oversized context files fail safely");

  auto const symlink_workspace = root / "symlink-workspace";
  auto const symlink_target = root / "outside-secret.md";
  std::filesystem::create_directories(symlink_workspace);
  {
    std::ofstream file(symlink_target, std::ios::binary | std::ios::trunc);
    file << "secret instructions\n";
  }
  std::error_code symlink_error;
  std::filesystem::create_symlink(symlink_target, symlink_workspace / "AGENTS.md", symlink_error);
  expect(!symlink_error, "test creates symlinked AGENTS.md");
  if (!symlink_error) {
    auto symlinked = ava::context::load_context_files(ava::context::ContextLoadOptions{
        .workspace_root = symlink_workspace,
        .current_dir = symlink_workspace,
        .global_agents_file = {},
        .max_file_bytes = 1024,
    });
    expect(!symlinked && symlinked.error().category() == ava::core::ErrorCategory::Io,
           "context loader rejects symlinked AGENTS.md files");
  }
}

void test_auth_load_and_store()
{
  auto const root = temp_root() / "auth";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  std::filesystem::create_directories(root / "config" / "ava");
  setenv("HOME", (root / "home").c_str(), 1);
  setenv("XDG_CONFIG_HOME", (root / "config").c_str(), 1);
  setenv("XDG_STATE_HOME", (root / "state").c_str(), 1);
  setenv("XDG_DATA_HOME", (root / "data").c_str(), 1);

  auto const paths = ava::config::xdg_paths();
  {
    std::ofstream file(paths.auth_file, std::ios::binary | std::ios::trunc);
    file << "{\"openai\":{\"type\":\"oauth\",\"access_token\":\"secret-token\",\"refresh_token\":\"refresh\",\"expires_"
            "at\":42}}";
  }
  ::chmod(paths.auth_file.c_str(), S_IRUSR | S_IWUSR);
  auto loaded = ava::config::load_openai_credential(paths);
  expect(loaded && loaded->has_value(), "OpenAI OAuth credential loads from AVA XDG auth file");
  if (loaded && *loaded) {
    expect((*loaded)->access_token == "secret-token", "OpenAI access token parses");
    expect((*loaded)->source_path == paths.auth_file, "credential source path records location only");
  }

  std::filesystem::remove(paths.auth_file, remove_error);
  std::filesystem::create_directories(root / "data" / "opencode");
  {
    std::ofstream file(root / "data" / "opencode" / "auth.json", std::ios::binary | std::ios::trunc);
    file << "{\"openai\":{\"type\":\"oauth\",\"access\":\"opencode-token\",\"refresh\":\"r\",\"expires\":7}}";
  }
  ::chmod((root / "data" / "opencode" / "auth.json").c_str(), S_IRUSR | S_IWUSR | S_IRGRP);
  auto insecure_import = ava::config::load_openai_credential(paths);
  expect(insecure_import && !insecure_import->has_value(),
         "OpenAI credential load skips group-readable fallback auth file");
  ::chmod((root / "data" / "opencode" / "auth.json").c_str(), S_IRUSR | S_IWUSR);
  auto imported = ava::config::load_openai_credential(paths);
  expect(imported && imported->has_value() && (*imported)->access_token == "opencode-token",
         "OpenAI OAuth credential is recognized from opencode auth path");

  {
    std::ofstream file(paths.auth_file, std::ios::binary | std::ios::trunc);
    file << "{\"openai\":{\"type\":\"api\",\"key\":\"ava-api-key\"}}";
  }
  ::chmod(paths.auth_file.c_str(), S_IRUSR | S_IWUSR);
  auto explicit_ava_preferred = ava::config::load_openai_credential(paths);
  expect(explicit_ava_preferred && explicit_ava_preferred->has_value() &&
             (*explicit_ava_preferred)->access_token == "ava-api-key" &&
             (*explicit_ava_preferred)->type == ava::config::OpenAICredentialType::ApiKey,
         "explicit AVA OpenAI auth is preferred over legacy fallback auth");

  std::filesystem::remove(paths.auth_file, remove_error);
  std::filesystem::remove_all(root / "home" / ".ava", remove_error);
  std::filesystem::create_directories(root / "home" / ".ava" / "credentials.json");
  {
    std::ofstream file(root / "data" / "opencode" / "auth.json", std::ios::binary | std::ios::trunc);
    file << "{\"openai\":{\"type\":\"api\",\"key\":\"opencode-api-key\"}}";
  }
  ::chmod((root / "data" / "opencode" / "auth.json").c_str(), S_IRUSR | S_IWUSR);
  auto api_key = ava::config::load_openai_credential(paths);
  expect(api_key && api_key->has_value() && (*api_key)->access_token == "opencode-api-key" &&
             (*api_key)->type == ava::config::OpenAICredentialType::ApiKey,
         "OpenAI API key auth shape loads after skipping non-regular legacy candidate");

  auto stored = ava::config::store_openai_credential(
      paths, ava::config::OpenAICredential{.type = ava::config::OpenAICredentialType::OAuth,
                                           .access_token = "stored-token",
                                           .refresh_token = "stored-refresh",
                                           .expires_at = 99,
                                           .account_id = "acct_stored",
                                           .source_path = {}});
  expect(stored.has_value(), "OpenAI OAuth credential stores to AVA XDG auth file");
  auto stored_oauth = ava::config::load_openai_credential(paths);
  expect(stored_oauth && stored_oauth->has_value() &&
             (*stored_oauth)->type == ava::config::OpenAICredentialType::OAuth &&
             (*stored_oauth)->access_token == "stored-token" && (*stored_oauth)->refresh_token == "stored-refresh" &&
             (*stored_oauth)->expires_at == 99 && (*stored_oauth)->account_id == "acct_stored",
         "OpenAI OAuth credential store/load round trips type, token, and account fields");

  auto stored_api = ava::config::store_openai_credential(
      paths, ava::config::OpenAICredential{.type = ava::config::OpenAICredentialType::ApiKey,
                                           .access_token = "stored-api-key",
                                           .refresh_token = "ignored-refresh",
                                           .expires_at = 1,
                                           .account_id = "",
                                           .source_path = {}});
  expect(stored_api.has_value(), "OpenAI API key credential stores to AVA XDG auth file");
  auto loaded_api = ava::config::load_openai_credential(paths);
  expect(loaded_api && loaded_api->has_value() && (*loaded_api)->type == ava::config::OpenAICredentialType::ApiKey &&
             (*loaded_api)->access_token == "stored-api-key" && (*loaded_api)->refresh_token.empty() &&
             (*loaded_api)->expires_at == 0,
         "OpenAI API key credential store/load round trips without OAuth expiry");

  auto stored_anthropic_api = ava::config::store_provider_credential(
      paths, ava::config::ProviderCredential{.provider_id = "anthropic",
                                             .access_token = "stored-anthropic-api-key",
                                             .credential_type = "api_key",
                                             .account_id = "",
                                             .source = "test"});
  expect(stored_anthropic_api.has_value(), "generic provider API key credential stores to auth file");
  loaded_api = ava::config::load_openai_credential(paths);
  expect(loaded_api && loaded_api->has_value() && (*loaded_api)->access_token == "stored-api-key",
         "generic provider credential store preserves existing OpenAI credential");
  ava::tests::FakeTransport generic_store_transport({});
  auto stored_anthropic_api_credential =
      ava::config::provider_credential_for_request(paths, "anthropic", generic_store_transport);
  expect(stored_anthropic_api_credential && stored_anthropic_api_credential->has_value() &&
             (*stored_anthropic_api_credential)->access_token == "stored-anthropic-api-key" &&
             (*stored_anthropic_api_credential)->credential_type == "api_key",
         "generic provider API key credential loads after storing");

  auto stored_anthropic_oauth = ava::config::store_provider_credential(
      paths, ava::config::ProviderCredential{.provider_id = "anthropic",
                                             .access_token = "stored-anthropic-oauth-token",
                                             .credential_type = "oauth",
                                             .account_id = "",
                                             .source = "test"});
  expect(stored_anthropic_oauth.has_value(), "generic provider OAuth bearer credential stores to auth file");
  auto stored_anthropic_oauth_credential =
      ava::config::provider_credential_for_request(paths, "anthropic", generic_store_transport);
  expect(stored_anthropic_oauth_credential && stored_anthropic_oauth_credential->has_value() &&
             (*stored_anthropic_oauth_credential)->access_token == "stored-anthropic-oauth-token" &&
             (*stored_anthropic_oauth_credential)->credential_type == "oauth",
         "generic provider OAuth bearer credential replaces the prior provider API key");

  auto stored_moonshot_api = ava::config::store_provider_credential(
      paths, ava::config::ProviderCredential{.provider_id = "moonshot",
                                             .access_token = "stored-moonshot-api-key",
                                             .credential_type = "api_key",
                                             .account_id = "",
                                             .source = "test"});
  expect(stored_moonshot_api.has_value(), "Moonshot API key credential stores through generic provider auth");
  auto stored_moonshot_credential =
      ava::config::provider_credential_for_request(paths, "moonshot", generic_store_transport);
  expect(stored_moonshot_credential && stored_moonshot_credential->has_value() &&
             (*stored_moonshot_credential)->access_token == "stored-moonshot-api-key" &&
             (*stored_moonshot_credential)->credential_type == "api_key",
         "Moonshot API key credential loads through generic provider auth");

  auto rotated_openai_api = ava::config::store_openai_credential(
      paths, ava::config::OpenAICredential{.type = ava::config::OpenAICredentialType::ApiKey,
                                           .access_token = "rotated-openai-api-key",
                                           .refresh_token = "",
                                           .expires_at = 0,
                                           .account_id = "",
                                           .source_path = {}});
  expect(rotated_openai_api.has_value(), "OpenAI credential update stores after generic provider credentials");
  loaded_api = ava::config::load_openai_credential(paths);
  expect(loaded_api && loaded_api->has_value() && (*loaded_api)->access_token == "rotated-openai-api-key",
         "OpenAI credential update loads after provider credential merge");
  stored_anthropic_oauth_credential =
      ava::config::provider_credential_for_request(paths, "anthropic", generic_store_transport);
  expect(stored_anthropic_oauth_credential && stored_anthropic_oauth_credential->has_value() &&
             (*stored_anthropic_oauth_credential)->access_token == "stored-anthropic-oauth-token",
         "OpenAI credential update preserves Anthropic provider credential");
  stored_moonshot_credential = ava::config::provider_credential_for_request(paths, "moonshot", generic_store_transport);
  expect(stored_moonshot_credential && stored_moonshot_credential->has_value() &&
             (*stored_moonshot_credential)->access_token == "stored-moonshot-api-key",
         "OpenAI credential update preserves Moonshot provider credential");

  ::chmod(paths.auth_file.c_str(), S_IRUSR | S_IWUSR | S_IRGRP);
  auto repaired_broad_permissions = ava::config::store_provider_credential(
      paths, ava::config::ProviderCredential{.provider_id = "kimi",
                                             .access_token = "stored-kimi-api-key",
                                             .credential_type = "api_key",
                                             .account_id = "",
                                             .source = "test"});
  expect(repaired_broad_permissions.has_value(), "provider credential store repairs user-owned broad auth permissions");
  struct stat auth_stat{};
  expect(::stat(paths.auth_file.c_str(), &auth_stat) == 0 && (auth_stat.st_mode & (S_IRWXG | S_IRWXO)) == 0,
         "provider credential store rewrites auth file with owner-only permissions");
  std::ifstream repaired_auth_file(paths.auth_file, std::ios::binary);
  std::stringstream repaired_auth_stream;
  repaired_auth_stream << repaired_auth_file.rdbuf();
  auto const repaired_auth_json = repaired_auth_stream.str();
  expect(repaired_auth_json.find("openai") == std::string::npos &&
             repaired_auth_json.find("anthropic") == std::string::npos &&
             repaired_auth_json.find("moonshot") == std::string::npos,
         "provider credential store discards broad-permission auth contents instead of blessing them");
  auto stored_kimi_credential = ava::config::provider_credential_for_request(paths, "kimi", generic_store_transport);
  expect(stored_kimi_credential && stored_kimi_credential->has_value() &&
             (*stored_kimi_credential)->access_token == "stored-kimi-api-key",
         "provider credential store writes only the requested credential while repairing permissions");

  {
    std::ofstream file(paths.auth_file, std::ios::binary | std::ios::trunc);
    file << "{\"openai\":{\"type\":\"api_key\",\"api_key\":\"bad\"}}trailing";
  }
  ::chmod(paths.auth_file.c_str(), S_IRUSR | S_IWUSR);
  auto malformed_auth_store = ava::config::store_provider_credential(
      paths, ava::config::ProviderCredential{.provider_id = "anthropic",
                                             .access_token = "should-not-overwrite",
                                             .credential_type = "api_key",
                                             .account_id = "",
                                             .source = "test"});
  expect(!malformed_auth_store && malformed_auth_store.error().format().find("trailing content") != std::string::npos,
         "provider credential store rejects malformed auth JSON with actionable error");
  {
    std::ofstream file(paths.auth_file, std::ios::binary | std::ios::trunc);
    file << "{\"openai\": }";
  }
  ::chmod(paths.auth_file.c_str(), S_IRUSR | S_IWUSR);
  auto missing_value_auth_store = ava::config::store_provider_credential(
      paths, ava::config::ProviderCredential{.provider_id = "anthropic",
                                             .access_token = "should-not-overwrite",
                                             .credential_type = "api_key",
                                             .account_id = "",
                                             .source = "test"});
  expect(
      !missing_value_auth_store && missing_value_auth_store.error().format().find("invalid value") != std::string::npos,
      "provider credential store rejects auth JSON members with missing values");
  std::filesystem::remove(paths.auth_file, remove_error);
  auto restored_after_malformed_auth = ava::config::store_openai_credential(
      paths, ava::config::OpenAICredential{.type = ava::config::OpenAICredentialType::ApiKey,
                                           .access_token = "rotated-openai-api-key",
                                           .refresh_token = "",
                                           .expires_at = 0,
                                           .account_id = "",
                                           .source_path = {}});
  expect(restored_after_malformed_auth.has_value(), "OpenAI credential stores after removing malformed auth file");

  expect(!ava::config::parse_openai_credential("{\"openai\":{\"type\":\"oauth\",\"api_key\":\"wrong\"}}"),
         "OpenAI credential parser rejects typed OAuth without OAuth token");
  expect(!ava::config::parse_openai_credential("{\"openai\":{\"type\":\"api_key\",\"access_token\":\"wrong\"}}"),
         "OpenAI credential parser rejects typed API key without key field");
  expect(!ava::config::parse_openai_credential("{\"openai\":{\"type\":\"unknown\",\"key\":\"wrong\"}}"),
         "OpenAI credential parser rejects unknown auth type");
  auto expired_oauth = ava::config::openai_access_token_for_request(
      ava::config::OpenAICredential{.type = ava::config::OpenAICredentialType::OAuth,
                                    .access_token = "expired-token",
                                    .refresh_token = "",
                                    .expires_at = 10,
                                    .account_id = "",
                                    .source_path = paths.auth_file},
      11);
  expect(!expired_oauth && expired_oauth.error().category() == ava::core::ErrorCategory::PermissionDenied,
         "OpenAI expired OAuth credential is not usable for requests");
  auto api_key_not_expired = ava::config::openai_access_token_for_request(
      ava::config::OpenAICredential{.type = ava::config::OpenAICredentialType::ApiKey,
                                    .access_token = "api-token",
                                    .refresh_token = "",
                                    .expires_at = 10,
                                    .account_id = "",
                                    .source_path = {}},
      11);
  expect(api_key_not_expired && *api_key_not_expired == "api-token", "OpenAI API key ignores OAuth expiry field");

  setenv("OPENAI_API_KEY", "env-openai-key", 1);
  ava::tests::FakeTransport env_transport({});
  auto stored_provider_credential = ava::config::provider_credential_for_request(paths, "openai", env_transport);
  expect(stored_provider_credential && stored_provider_credential->has_value() &&
             (*stored_provider_credential)->access_token == "rotated-openai-api-key" &&
             (*stored_provider_credential)->credential_type == "api_key",
         "provider credential discovery prefers stored OpenAI auth before env fallback");
  std::filesystem::remove(paths.auth_file, remove_error);
  std::filesystem::remove(root / "data" / "opencode" / "auth.json", remove_error);
  auto env_openai_credential = ava::config::provider_credential_for_request(paths, "openai", env_transport);
  expect(env_openai_credential && env_openai_credential->has_value() &&
             (*env_openai_credential)->access_token == "env-openai-key" &&
             (*env_openai_credential)->source == "env:OPENAI_API_KEY",
         "provider credential discovery falls back to OPENAI_API_KEY without storing it");
  unsetenv("OPENAI_API_KEY");
  {
    std::ofstream file(paths.auth_file, std::ios::binary | std::ios::trunc);
    file << "{\"anthropic\":{\"type\":\"api_key\",\"api_key\":\"stored-anthropic-key\"}}";
  }
  ::chmod(paths.auth_file.c_str(), S_IRUSR | S_IWUSR);
  auto stored_anthropic_credential = ava::config::provider_credential_for_request(paths, "anthropic", env_transport);
  expect(stored_anthropic_credential && stored_anthropic_credential->has_value() &&
             (*stored_anthropic_credential)->provider_id == "anthropic" &&
             (*stored_anthropic_credential)->access_token == "stored-anthropic-key" &&
             (*stored_anthropic_credential)->credential_type == "api_key" &&
             (*stored_anthropic_credential)->source == paths.auth_file.string(),
         "provider credential discovery loads non-OpenAI API keys from auth file");
  std::filesystem::remove(paths.auth_file, remove_error);
  setenv("ANTHROPIC_API_KEY", "env-anthropic-key", 1);
  auto anthropic_credential = ava::config::provider_credential_for_request(paths, "anthropic", env_transport);
  expect(anthropic_credential && anthropic_credential->has_value() &&
             (*anthropic_credential)->provider_id == "anthropic" &&
             (*anthropic_credential)->access_token == "env-anthropic-key",
         "provider credential discovery supports non-OpenAI API key environment variables");
  unsetenv("ANTHROPIC_API_KEY");
  auto restored_api = ava::config::store_openai_credential(
      paths, ava::config::OpenAICredential{.type = ava::config::OpenAICredentialType::ApiKey,
                                           .access_token = "stored-api-key",
                                           .refresh_token = "",
                                           .expires_at = 0,
                                           .account_id = "",
                                           .source_path = {}});
  expect(restored_api.has_value(), "OpenAI API key credential restores after env discovery checks");
  struct stat st{};
  if (::stat(paths.auth_file.c_str(), &st) == 0) {
    expect((st.st_mode & 0777) == 0600, "auth file is owner-only");
  } else {
    expect(false, "auth file stat succeeds");
  }
  struct stat dir_st{};
  if (::stat(paths.auth_file.parent_path().c_str(), &dir_st) == 0) {
    expect((dir_st.st_mode & 0777) == 0700, "auth directory is owner-only");
  } else {
    expect(false, "auth directory stat succeeds");
  }

  std::filesystem::remove(paths.auth_file, remove_error);
  {
    std::ofstream file(paths.auth_file, std::ios::binary | std::ios::trunc);
    file << "{\"openai\":{\"type\":\"api_key\",\"api_key\":\"too-readable\"}}";
  }
  ::chmod(paths.auth_file.c_str(), S_IRUSR | S_IWUSR | S_IRGRP);
  auto broad_auth_file = ava::config::load_openai_credential(paths);
  expect(!broad_auth_file && broad_auth_file.error().category() == ava::core::ErrorCategory::PermissionDenied,
         "OpenAI credential load rejects group-readable auth file");

  auto const symlink_target = root / "symlink-target.json";
  {
    std::ofstream file(symlink_target, std::ios::binary | std::ios::trunc);
    file << "unchanged";
  }
  std::filesystem::remove(paths.auth_file, remove_error);
  std::error_code symlink_error;
  std::filesystem::create_symlink(symlink_target, paths.auth_file, symlink_error);
  if (!symlink_error) {
    auto symlink_store = ava::config::store_openai_credential(
        paths, ava::config::OpenAICredential{.type = ava::config::OpenAICredentialType::ApiKey,
                                             .access_token = "must-not-write-through-symlink",
                                             .refresh_token = "",
                                             .expires_at = 0,
                                             .account_id = "",
                                             .source_path = {}});
    expect(!symlink_store, "OpenAI credential store rejects symlink auth file");
    std::ifstream target_read(symlink_target, std::ios::binary);
    std::string target_content;
    target_read >> target_content;
    expect(target_content == "unchanged", "OpenAI credential store does not write through symlink");
  }
}

void test_openai_oauth_helpers()
{
  std::string const verifier = "dBjftJeZ4CVP-mB92K27uhbUJU1p1r_wW1gFWFOEjXk";
  expect(ava::config::openai_oauth_code_challenge(verifier) == "E9Melhoa2OwvFrEMTJguCHaoeK1t8URWbuGJSstw-cM",
         "OpenAI OAuth PKCE challenge matches RFC 7636 test vector");
  expect(ava::config::openai_oauth_account_id_from_token(
             "header.eyJodHRwczovL2FwaS5vcGVuYWkuY29tL2F1dGgiOnsiY2hhdGdwdF9hY2NvdW50X2lkIjoiYWNjdF8xMjMifX0.sig") ==
             "acct_123",
         "OpenAI OAuth account id extracts from Codex JWT claim");

  auto session = ava::config::make_openai_oauth_session(verifier, "state value");
  expect(session.has_value(), "OpenAI OAuth deterministic session builds");
  if (session) {
    expect(session->authorization_url.find("https://auth.openai.com/oauth/authorize?") == 0,
           "OpenAI OAuth authorization URL uses auth.openai.com");
    expect(session->authorization_url.find("client_id=app_EMoamEEZ73f0CkXaXp7hrann") != std::string::npos,
           "OpenAI OAuth authorization URL includes Codex client id");
    expect(session->authorization_url.find("redirect_uri=http%3A%2F%2Flocalhost%3A1455%2Fauth%2Fcallback") !=
               std::string::npos,
           "OpenAI OAuth authorization URL includes local callback redirect");
    expect(session->authorization_url.find("code_challenge=E9Melhoa2OwvFrEMTJguCHaoeK1t8URWbuGJSstw-cM") !=
               std::string::npos,
           "OpenAI OAuth authorization URL includes S256 code challenge");
    expect(session->authorization_url.find("state=state%20value") != std::string::npos,
           "OpenAI OAuth authorization URL percent-encodes state");
    expect(session->authorization_url.find("originator=ava") != std::string::npos,
           "OpenAI OAuth authorization URL identifies AVA originator");
  }

  ava::tests::FakeTransport transport({ava::provider::HttpResponse{
      .status_code = 200,
      .headers = {},
      .body = "{\"access_token\":\"access\",\"refresh_token\":\"refresh\",\"expires_in\":120,"
              "\"id_token\":\"header."
              "eyJodHRwczovL2FwaS5vcGVuYWkuY29tL2F1dGgiOnsiY2hhdGdwdF9hY2NvdW50X2lkIjoiYWNjdF8xMjMifX0.sig\"}",
  }});
  auto credential = ava::config::exchange_openai_oauth_code("code value", verifier, transport, 1000);
  expect(credential.has_value(), "OpenAI OAuth code exchange parses token response");
  if (credential) {
    expect(credential->type == ava::config::OpenAICredentialType::OAuth && credential->access_token == "access" &&
               credential->refresh_token == "refresh" && credential->expires_at == 1120 &&
               credential->account_id == "acct_123",
           "OpenAI OAuth code exchange returns OAuth credential with absolute expiry and account id");
  }
  auto const& requests = transport.requests();
  expect(requests.size() == 1 && requests.front().url == "https://auth.openai.com/oauth/token" &&
             requests.front().method == "POST",
         "OpenAI OAuth code exchange posts to token endpoint");
  if (!requests.empty()) {
    expect(requests.front().body.find("grant_type=authorization_code") != std::string::npos &&
               requests.front().body.find("code=code%20value") != std::string::npos &&
               requests.front().body.find("code_verifier=" + verifier) != std::string::npos,
           "OpenAI OAuth code exchange form-encodes authorization code and verifier");
  }
}

void test_openai_oauth_refresh()
{
  ava::tests::FakeTransport refresh_transport({ava::provider::HttpResponse{
      .status_code = 200,
      .headers = {},
      .body = "{\"access_token\":\"refreshed-access\",\"refresh_token\":\"rotated-refresh\","
              "\"expires_in\":120,\"account_id\":\"acct_rotated\"}",
  }});
  auto const refreshed = ava::config::refresh_openai_oauth_credential(
      ava::config::OpenAICredential{.type = ava::config::OpenAICredentialType::OAuth,
                                    .access_token = "old-access",
                                    .refresh_token = "old refresh/token",
                                    .expires_at = 900,
                                    .account_id = "acct_old",
                                    .source_path = {}},
      refresh_transport, 1000);
  expect(refreshed && refreshed->access_token == "refreshed-access" && refreshed->refresh_token == "rotated-refresh" &&
             refreshed->expires_at == 1120 && refreshed->account_id == "acct_rotated",
         "OpenAI OAuth refresh parses rotated token response");
  auto const& refresh_requests = refresh_transport.requests();
  expect(refresh_requests.size() == 1 && refresh_requests.front().url == "https://auth.openai.com/oauth/token" &&
             refresh_requests.front().method == "POST",
         "OpenAI OAuth refresh posts to token endpoint");
  if (!refresh_requests.empty()) {
    expect(refresh_requests.front().body.find("grant_type=refresh_token") != std::string::npos &&
               refresh_requests.front().body.find("refresh_token=old%20refresh%2Ftoken") != std::string::npos &&
               refresh_requests.front().body.find("client_id=app_EMoamEEZ73f0CkXaXp7hrann") != std::string::npos,
           "OpenAI OAuth refresh form-encodes refresh token and client id");
  }

  ava::tests::FakeTransport preserved_transport({ava::provider::HttpResponse{
      .status_code = 200,
      .headers = {},
      .body = "{\"access_token\":\"preserved-access\",\"expires_at\":2222}",
  }});
  auto const preserved = ava::config::refresh_openai_oauth_credential(
      ava::config::OpenAICredential{.type = ava::config::OpenAICredentialType::OAuth,
                                    .access_token = "old-access",
                                    .refresh_token = "stable-refresh",
                                    .expires_at = 900,
                                    .account_id = "acct_stable",
                                    .source_path = {}},
      preserved_transport, 1000);
  expect(preserved && preserved->access_token == "preserved-access" && preserved->refresh_token == "stable-refresh" &&
             preserved->expires_at == 2222 && preserved->account_id == "acct_stable",
         "OpenAI OAuth refresh preserves existing refresh token when response omits rotation");

  ava::tests::FakeTransport id_token_transport({ava::provider::HttpResponse{
      .status_code = 200,
      .headers = {},
      .body = "{\"access_token\":\"id-token-access\",\"refresh_token\":\"id-token-refresh\","
              "\"expires_in\":120,\"id_token\":\"header."
              "eyJodHRwczovL2FwaS5vcGVuYWkuY29tL2F1dGgiOnsiY2hhdGdwdF9hY2NvdW50X2lkIjoiYWNjdF8xMjMifX0.sig\"}",
  }});
  auto const id_token_refreshed = ava::config::refresh_openai_oauth_credential(
      ava::config::OpenAICredential{.type = ava::config::OpenAICredentialType::OAuth,
                                    .access_token = "old-access",
                                    .refresh_token = "id-token-refresh-input",
                                    .expires_at = 900,
                                    .account_id = "",
                                    .source_path = {}},
      id_token_transport, 1000);
  expect(id_token_refreshed && id_token_refreshed->account_id == "acct_123",
         "OpenAI OAuth refresh falls back to id_token account id when account_id is absent");

  ava::tests::FakeTransport malformed_transport({ava::provider::HttpResponse{
      .status_code = 200,
      .headers = {},
      .body = "not json",
  }});
  auto const malformed = ava::config::refresh_openai_oauth_credential(
      ava::config::OpenAICredential{.type = ava::config::OpenAICredentialType::OAuth,
                                    .access_token = "old-access",
                                    .refresh_token = "refresh",
                                    .expires_at = 900,
                                    .account_id = "",
                                    .source_path = {}},
      malformed_transport, 1000);
  expect(!malformed && malformed.error().message().find("malformed JSON") != std::string::npos,
         "OpenAI OAuth refresh reports malformed JSON responses clearly");

  auto const root = temp_root() / "oauth-refresh";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  setenv("HOME", (root / "home").c_str(), 1);
  setenv("XDG_CONFIG_HOME", (root / "config").c_str(), 1);
  setenv("XDG_STATE_HOME", (root / "state").c_str(), 1);
  setenv("XDG_DATA_HOME", (root / "data").c_str(), 1);
  auto const paths = ava::config::xdg_paths();
  auto stored = ava::config::store_openai_credential(
      paths, ava::config::OpenAICredential{.type = ava::config::OpenAICredentialType::OAuth,
                                           .access_token = "expired-access",
                                           .refresh_token = "persist-refresh",
                                           .expires_at = 100,
                                           .account_id = "acct_old",
                                           .source_path = {}});
  expect(stored.has_value(), "OpenAI OAuth refresh persistence test stores expired credential");
  auto loaded = ava::config::load_openai_credential(paths);
  expect(loaded && loaded->has_value(), "OpenAI OAuth refresh persistence test loads expired credential");
  if (loaded && *loaded) {
    ava::tests::FakeTransport persist_transport({ava::provider::HttpResponse{
        .status_code = 200,
        .headers = {},
        .body = "{\"access_token\":\"persisted-access\",\"refresh_token\":\"persisted-refresh\","
                "\"expires_in\":60,\"account_id\":\"acct_persisted\"}",
    }});
    auto usable = ava::config::openai_credential_for_request(paths, **loaded, persist_transport, 1000);
    expect(usable && usable->access_token == "persisted-access" && usable->refresh_token == "persisted-refresh" &&
               usable->expires_at == 1060 && usable->account_id == "acct_persisted",
           "OpenAI OAuth credential preflight refreshes expired credential before use");
    auto persisted = ava::config::load_openai_credential(paths);
    expect(persisted && persisted->has_value() && (*persisted)->access_token == "persisted-access" &&
               (*persisted)->refresh_token == "persisted-refresh",
           "OpenAI OAuth credential preflight persists refreshed token rotation");
  }

  auto failure_store = ava::config::store_openai_credential(
      paths, ava::config::OpenAICredential{.type = ava::config::OpenAICredentialType::OAuth,
                                           .access_token = "failure-access",
                                           .refresh_token = "failure-refresh",
                                           .expires_at = 100,
                                           .account_id = "acct_failure",
                                           .source_path = {}});
  expect(failure_store.has_value(), "OpenAI OAuth refresh failure test stores expired credential");
  loaded = ava::config::load_openai_credential(paths);
  if (loaded && *loaded) {
    ava::tests::FakeTransport failure_transport({ava::provider::HttpResponse{
        .status_code = 400,
        .headers = {},
        .body = "{\"error\":\"invalid_grant\"}",
    }});
    auto failed = ava::config::openai_credential_for_request(paths, **loaded, failure_transport, 1000);
    expect(!failed && failed.error().message().find("ava connect openai") != std::string::npos,
           "OpenAI OAuth refresh failure suggests reconnecting");
    auto unchanged = ava::config::load_openai_credential(paths);
    expect(unchanged && unchanged->has_value() && (*unchanged)->access_token == "failure-access" &&
               (*unchanged)->refresh_token == "failure-refresh" && (*unchanged)->expires_at == 100,
           "OpenAI OAuth refresh failure does not overwrite existing credential");
  }
}

void test_model_and_prompt_config()
{
  auto const root = temp_root() / "model";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  setenv("HOME", (root / "home").c_str(), 1);
  setenv("XDG_CONFIG_HOME", (root / "config").c_str(), 1);
  setenv("XDG_STATE_HOME", (root / "state").c_str(), 1);
  setenv("XDG_DATA_HOME", (root / "data").c_str(), 1);
  auto const paths = ava::config::xdg_paths();

  auto const builtin = ava::config::builtin_model_registry();
  auto selected = ava::config::select_default_model(builtin);
  auto const openai_profile = ava::config::find_provider_profile("openai");
  expect(openai_profile && openai_profile->display_name == "OpenAI" &&
             openai_profile->api_family == selected.api_family && openai_profile->supports_oauth,
         "provider profile centralizes OpenAI display, API family, and OAuth capability");
  expect(ava::config::model_display_label("gpt-5.5") == "GPT-5.5", "model profile centralizes GPT display labels");
  auto const vercel_profile = ava::config::find_provider_profile("vercel");
  expect(vercel_profile && vercel_profile->display_name == "Vercel AI Gateway" &&
             vercel_profile->connect_detail == "API key",
         "provider profile centralizes non-runtime gateway connect metadata");
  expect(selected.provider_id == "openai" && selected.model_id == "gpt-5.5", "default model is OpenAI GPT-5.5");
  expect(selected.context_window_tokens && *selected.context_window_tokens == 200'000,
         "default model carries context window metadata");
  expect(selected.supports_reasoning.value_or(false) && selected.reasoning_format == "openai_responses" &&
             std::find(selected.reasoning_levels.begin(), selected.reasoning_levels.end(), "xhigh") !=
                 selected.reasoning_levels.end(),
         "default GPT-5.5 model declares OpenAI reasoning effort levels including xhigh");
  bool saw_priced_builtin = false;
  bool saw_anthropic_runtime_reasoning_disabled = false;
  bool saw_kimi_builtin = false;
  bool saw_moonshot_builtin = false;
  bool all_builtins_have_context_windows = !builtin.models.empty();
  bool all_builtins_have_text_output = !builtin.models.empty();
  for (auto const& model : builtin.models) {
    all_builtins_have_context_windows = all_builtins_have_context_windows && model.context_window_tokens.has_value();
    all_builtins_have_text_output = all_builtins_have_text_output &&
                                    std::find(model.output_modalities.begin(), model.output_modalities.end(), "text") !=
                                        model.output_modalities.end();
    saw_priced_builtin =
        saw_priced_builtin || (model.model_id == "gpt-4.1-mini" && model.context_window_tokens && model.pricing &&
                               *model.context_window_tokens == 1'048'576 && model.pricing->input_per_million &&
                               model.pricing->output_per_million && model.api_family == "openai_responses" &&
                               model.supports_tools.value_or(false) && model.supports_streaming.value_or(false) &&
                               model.reports_usage.value_or(false));
    expect(ava::config::find_provider_profile(model.provider_id).has_value(),
           "each builtin model references a centralized provider profile");
    saw_anthropic_runtime_reasoning_disabled =
        saw_anthropic_runtime_reasoning_disabled ||
        (model.provider_id == "anthropic" && model.model_id == "claude-sonnet-4-5" &&
         !model.supports_reasoning.value_or(false) && model.reasoning_format.empty() && model.reasoning_levels.empty());
    saw_kimi_builtin =
        saw_kimi_builtin ||
        (model.provider_id == "kimi" && model.model_id == "kimi-k2-thinking" &&
         model.api_family == "openai_chat_completions" && model.supports_tools.value_or(false) &&
         model.supports_reasoning.value_or(false) && model.reasoning_format == "reasoning_content" && !model.pricing &&
         std::find(model.compatibility_quirks.begin(), model.compatibility_quirks.end(), "kimi") !=
             model.compatibility_quirks.end() &&
         std::find(model.compatibility_quirks.begin(), model.compatibility_quirks.end(),
                   "preserve_reasoning_content") != model.compatibility_quirks.end());
    saw_moonshot_builtin = saw_moonshot_builtin || (model.provider_id == "moonshot" && model.model_id == "kimi-k2.6" &&
                                                    model.api_family == "openai_chat_completions" &&
                                                    model.reasoning_format == "reasoning_content");
  }
  expect(all_builtins_have_context_windows, "builtin model registry always provides context windows");
  expect(all_builtins_have_text_output, "builtin model registry always declares text output support");
  expect(saw_priced_builtin, "builtin model registry carries static pricing, context, and capability metadata");
  expect(saw_anthropic_runtime_reasoning_disabled,
         "Anthropic builtin keeps reasoning disabled until runtime controls are wired");
  expect(saw_kimi_builtin && saw_moonshot_builtin,
         "builtin model registry includes Kimi and Moonshot OpenAI-compatible coding profiles");
  expect(ava::config::reasoning_parameter_text(selected) == openai_profile->reasoning_request_parameters,
         "model reasoning parameter text comes from centralized provider/reasoning profiles");

  std::filesystem::create_directories(paths.ava_config_dir);
  {
    std::ofstream file(paths.models_file, std::ios::binary | std::ios::trunc);
    file << "{\"default_provider\":\"openai\",\"default_model\":\"gpt-5.5-mini\","
            "\"models\":[{\"provider\":\"openai\",\"id\":\"gpt-5.5-mini\",\"name\":\"Mini\","
            "\"family\":\"gpt-5\",\"context_window_tokens\":12345,\"max_output_tokens\":678,"
            "\"api_family\":\"openai_responses\",\"input_modalities\":[\"text\",\"image\"],"
            "\"output_modalities\":[\"text\"],\"reasoning_format\":\"reasoning_content\","
            "\"supports_tools\":false,\"supports_streaming\":true,\"supports_reasoning\":true,"
            "\"reports_usage\":true,\"reasoning_levels\":[\"low\",\"high\"],"
            "\"compatibility_quirks\":[\"requires_strict_tools\"],"
            "\"pricing\":{\"input_per_million\":1.5,\"output_per_million\":2.5}}]}";
  }
  auto registry = ava::config::load_model_registry(paths);
  expect(registry.has_value(), "model registry loads XDG override");
  if (registry) {
    selected = ava::config::select_default_model(*registry);
    expect(selected.model_id == "gpt-5.5-mini" && selected.display_name == "Mini",
           "default model override selects user model");
    ava::provider::TokenUsage const usage{.input_tokens = 1000,
                                          .output_tokens = 2000,
                                          .reasoning_tokens = std::nullopt,
                                          .cache_read_tokens = std::nullopt,
                                          .cache_write_tokens = std::nullopt,
                                          .total_tokens = 3000,
                                          .estimated_input_bytes = std::nullopt,
                                          .estimated_output_bytes = std::nullopt,
                                          .estimated_total_bytes = std::nullopt,
                                          .estimated = false};
    auto cost = selected.pricing ? ava::config::usage_cost_usd(*selected.pricing, usage) : std::nullopt;
    expect(selected.context_window_tokens == 12345 && selected.max_output_tokens == 678 &&
               selected.api_family == "openai_responses" && selected.input_modalities.size() == 2 &&
               selected.output_modalities.size() == 1 && selected.output_modalities[0] == "text" &&
               selected.reasoning_format == "reasoning_content" && selected.supports_tools == false &&
               selected.supports_streaming == true && selected.supports_reasoning == true &&
               selected.reports_usage == true && selected.reasoning_levels.size() == 2 &&
               selected.compatibility_quirks.size() == 1 && cost && *cost > 0.0064L && *cost < 0.0066L,
           "model registry parses local capability and pricing metadata and calculates cost");
    expect(!ava::config::usage_cost_usd(ava::config::ModelPricing{}, usage),
           "usage cost remains unknown when pricing rates are absent");
    ava::provider::TokenUsage const cached_usage{.input_tokens = 1000,
                                                 .output_tokens = 0,
                                                 .reasoning_tokens = std::nullopt,
                                                 .cache_read_tokens = 100,
                                                 .cache_write_tokens = std::nullopt,
                                                 .total_tokens = 1000,
                                                 .estimated_input_bytes = std::nullopt,
                                                 .estimated_output_bytes = std::nullopt,
                                                 .estimated_total_bytes = std::nullopt,
                                                 .estimated = false};
    expect(!ava::config::usage_cost_usd(*selected.pricing, cached_usage),
           "usage cost remains unknown when present cache usage has no cache pricing");
  }

  {
    std::ofstream file(paths.models_file, std::ios::binary | std::ios::trunc);
    file << "{\"default_provider\":\"openai\",\"default_model\":\"gpt-5.5\","
            "\"models\":[{\"provider\":\"openai\",\"id\":\"gpt-5.5\",\"name\":\"Custom GPT\"}]}";
  }
  registry = ava::config::load_model_registry(paths);
  expect(registry.has_value(), "model registry loads partial builtin override");
  if (registry) {
    selected = ava::config::select_default_model(*registry);
    expect(selected.display_name == "Custom GPT" && selected.context_window_tokens == 200'000 &&
               selected.supports_reasoning == true && selected.reports_usage == true &&
               selected.reasoning_levels.size() == 4 && selected.output_modalities.size() == 1 &&
               selected.output_modalities[0] == "text" && selected.reasoning_format == "openai_responses",
           "builtin model overrides preserve missing capability metadata");
  }

  auto inferred_family = ava::config::select_default_model(
      ava::config::ModelRegistry{.default_provider_id = "openai", .default_model_id = "gpt-5.5", .models = {}});
  expect(inferred_family.family == "gpt-5", "GPT-5.5 model id infers GPT-5 prompt family");

  auto prompt = ava::config::select_prompt(paths, selected, ava::agent::Mode::Build);
  expect(prompt && !prompt->from_override && prompt->text.find("Provider=openai") != std::string::npos,
         "builtin prompt selects by provider and family");
  std::filesystem::create_directories(paths.prompts_dir / "openai" / "gpt-5");
  {
    std::ofstream file(paths.prompts_dir / "openai" / "gpt-5" / "plan.txt", std::ios::binary | std::ios::trunc);
    file << "custom plan prompt";
  }
  auto override = ava::config::select_prompt(paths, selected, ava::agent::Mode::Plan);
  expect(override && override->from_override && override->text == "custom plan prompt",
         "prompt override loads from XDG config");

  {
    std::ofstream file(paths.models_file, std::ios::binary | std::ios::trunc);
    file << std::string((1024 * 1024) + 1, 'x');
  }
  auto oversized_registry = ava::config::load_model_registry(paths);
  expect(!oversized_registry, "oversized model config is rejected");

  {
    std::ofstream file(paths.prompts_dir / "openai" / "gpt-5" / "plan.txt", std::ios::binary | std::ios::trunc);
    file << std::string((256 * 1024) + 1, 'x');
  }
  auto oversized_prompt = ava::config::select_prompt(paths, selected, ava::agent::Mode::Plan);
  expect(!oversized_prompt, "oversized prompt override is rejected");
}

}  // namespace

void run_config_context_auth_oauth_tests()
{
  test_xdg_paths();
  test_context_loader();
  test_auth_load_and_store();
  test_openai_oauth_helpers();
  test_openai_oauth_refresh();
  test_model_and_prompt_config();
}
