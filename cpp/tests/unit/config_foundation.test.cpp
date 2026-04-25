#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <optional>
#include <set>
#include <string>

#if !defined(_WIN32)
#include <sys/stat.h>
#endif

#include "ava/config/config.hpp"

namespace {

void set_env_var(const std::string& key, const std::string& value) {
#if defined(_WIN32)
  _putenv_s(key.c_str(), value.c_str());
#else
  setenv(key.c_str(), value.c_str(), 1);
#endif
}

void unset_env_var(const std::string& key) {
#if defined(_WIN32)
  _putenv_s(key.c_str(), "");
#else
  unsetenv(key.c_str());
#endif
}

struct ScopedEnvVar {
  std::string key;
  std::optional<std::string> old_value;

  ScopedEnvVar(std::string k, std::string value)
      : key(std::move(k)) {
    if(const char* current = std::getenv(key.c_str()); current != nullptr) {
      old_value = std::string(current);
    }
    set_env_var(key, value);
  }

  ~ScopedEnvVar() {
    if(old_value.has_value()) {
      set_env_var(key, *old_value);
    } else {
      unset_env_var(key);
    }
  }
};

struct ScopedUnsetEnvVar {
  std::string key;
  std::optional<std::string> old_value;

  explicit ScopedUnsetEnvVar(std::string k)
      : key(std::move(k)) {
    if(const char* current = std::getenv(key.c_str()); current != nullptr) {
      old_value = std::string(current);
    }
    unset_env_var(key);
  }

  ~ScopedUnsetEnvVar() {
    if(old_value.has_value()) {
      set_env_var(key, *old_value);
    } else {
      unset_env_var(key);
    }
  }
};

std::filesystem::path temp_root_for_test() {
  const auto unique = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
  return std::filesystem::temp_directory_path() / ("ava_cpp_config_test_" + unique);
}

}  // namespace

TEST_CASE("config paths/trust/credentials foundations persist correctly", "[ava_config]") {
  const auto root = temp_root_for_test();
  std::filesystem::create_directories(root);

  ScopedEnvVar xdg_config("XDG_CONFIG_HOME", (root / "config").string());
  ScopedEnvVar xdg_data("XDG_DATA_HOME", (root / "data").string());
  ScopedEnvVar xdg_state("XDG_STATE_HOME", (root / "state").string());
  ScopedEnvVar xdg_cache("XDG_CACHE_HOME", (root / "cache").string());

  const auto paths = ava::config::resolve_app_paths();
  REQUIRE(paths.config_dir.filename() == "ava");
  REQUIRE(paths.data_dir.filename() == "ava");

  ava::config::clear_trust_cache_for_tests();
  const auto project = root / "project";
  std::filesystem::create_directories(project);
  REQUIRE_FALSE(ava::config::is_project_trusted(project));
  ava::config::trust_project(project);
  REQUIRE(ava::config::is_project_trusted(project));

  ava::config::CredentialStore store;
  store.set("openai", ava::config::ProviderCredential{.api_key = "test-key"});
  const auto creds_path = ava::config::credentials_path();
  store.save(creds_path);
  REQUIRE(std::filesystem::exists(creds_path));

  const auto loaded = ava::config::CredentialStore::load(creds_path);
  REQUIRE(loaded.get("openai").has_value());
  REQUIRE(loaded.get("openai")->api_key == "test-key");

  const auto summary = ava::config::summary();
  REQUIRE(summary.xdg_paths);
  REQUIRE(summary.trust_store);
  REQUIRE(summary.credential_store);
  REQUIRE(summary.embedded_model_registry);

  std::filesystem::remove_all(root);
}

TEST_CASE("credential env overrides take precedence", "[ava_config]") {
  ScopedEnvVar openai_key("OPENAI_API_KEY", "env-openai-key");
  ScopedEnvVar ava_openai_key("AVA_OPENAI_API_KEY", "ava-env-openai-key");

  ava::config::CredentialStore store;
  store.set("openai", ava::config::ProviderCredential{.api_key = "file-openai-key"});

  const auto credential = store.get("openai");
  REQUIRE(credential.has_value());
  REQUIRE(credential->api_key == "ava-env-openai-key");
}

TEST_CASE("credential env overrides apply even without persisted provider entry", "[ava_config]") {
  ScopedEnvVar openai_key("OPENAI_API_KEY", "env-openai-key");

  ava::config::CredentialStore store;
  const auto credential = store.get("openai");
  REQUIRE(credential.has_value());
  REQUIRE(credential->api_key == "env-openai-key");
}

TEST_CASE("credential store handles removal malformed json and configured providers", "[ava_config]") {
  ScopedUnsetEnvVar unset_openai("OPENAI_API_KEY");
  ScopedUnsetEnvVar unset_ava_openai("AVA_OPENAI_API_KEY");
  ScopedUnsetEnvVar unset_ollama("AVA_OLLAMA_API_KEY");
  const auto root = temp_root_for_test();
  std::filesystem::create_directories(root);
  const auto creds_path = root / "credentials.json";

  ava::config::CredentialStore store;
  store.set("openai", ava::config::ProviderCredential{.api_key = "test-key"});
  REQUIRE(store.get("openai").has_value());
  REQUIRE(store.remove("openai"));
  REQUIRE_FALSE(store.remove("openai"));
  REQUIRE_FALSE(store.get("openai").has_value());

  {
    std::ofstream malformed(creds_path);
    malformed << "{not json";
  }
  REQUIRE(ava::config::CredentialStore::load(creds_path).providers.empty());

  {
    ava::config::CredentialStore placeholder_store;
    placeholder_store.set("openai", ava::config::ProviderCredential{.api_key = "your-api-key"});
    placeholder_store.save(creds_path);
  }
  REQUIRE_FALSE(ava::config::CredentialStore::load(creds_path).get("openai").has_value());

  store.set("openai", ava::config::ProviderCredential{.api_key = "your-api-key"});
  store.set("ollama", ava::config::ProviderCredential{.base_url = "http://localhost:11434"});
  store.set("custom", ava::config::ProviderCredential{.api_key = "custom-key"});
  const auto configured = store.configured_providers();
  const std::set<std::string> configured_set(configured.begin(), configured.end());
  REQUIRE_FALSE(configured_set.contains("openai"));
  REQUIRE(configured_set.contains("ollama"));
  REQUIRE(configured_set.contains("custom"));

  std::filesystem::remove_all(root);
}

TEST_CASE("provider credentials choose effective oauth and api keys", "[ava_config]") {
  ava::config::ProviderCredential credential{.api_key = "api-key"};
  REQUIRE_FALSE(credential.is_oauth_configured());
  REQUIRE(credential.effective_api_key() == std::optional<std::string>{"api-key"});

  credential.oauth_token = "oauth-token";
  credential.oauth_expires_at = 4'102'444'800ULL;
  REQUIRE(credential.is_oauth_configured());
  REQUIRE_FALSE(credential.is_oauth_expired());
  REQUIRE(credential.effective_api_key() == std::optional<std::string>{"oauth-token"});

  credential.oauth_expires_at = 1ULL;
  REQUIRE(credential.is_oauth_expired());
  REQUIRE(credential.effective_api_key() == std::optional<std::string>{"api-key"});

  credential.api_key.clear();
  REQUIRE_FALSE(credential.effective_api_key().has_value());
}

TEST_CASE("credential env and base url helpers normalize provider names", "[ava_config]") {
  ScopedEnvVar openai_key("OPENAI_API_KEY", "env-openai-key");

  ava::config::CredentialStore store;
  const auto credential = store.get("chatgpt");
  REQUIRE(credential.has_value());
  REQUIRE(credential->api_key == "env-openai-key");

  REQUIRE(ava::config::normalize_provider_name("Google") == "gemini");
  REQUIRE(ava::config::standard_env_var("chatgpt") == std::optional<std::string>{"OPENAI_API_KEY"});
  REQUIRE(ava::config::default_base_url_for_provider("chatgpt") == std::optional<std::string>{"https://api.openai.com"});
  REQUIRE(ava::config::default_base_url_for_provider("unknown") == std::nullopt);
}

TEST_CASE("config path resolution prefers existing new paths over legacy and falls back when missing", "[ava_config]") {
  const auto root = temp_root_for_test();
  const auto home = root / "home";
  const auto xdg_config_home = root / "xdg-config";
  const auto xdg_data_home = root / "xdg-data";

  std::filesystem::create_directories(home / ".ava");
  std::filesystem::create_directories(xdg_config_home / "ava");
  std::filesystem::create_directories(xdg_data_home / "ava");

  ScopedEnvVar home_var("HOME", home.string());
  ScopedEnvVar xdg_config("XDG_CONFIG_HOME", xdg_config_home.string());
  ScopedEnvVar xdg_data("XDG_DATA_HOME", xdg_data_home.string());

  {
    std::ofstream legacy_config(home / ".ava" / "config.yaml");
    legacy_config << "legacy";
  }
  {
    std::ofstream legacy_creds(home / ".ava" / "credentials.json");
    legacy_creds << "{}";
  }

  REQUIRE(ava::config::config_file_path() == (home / ".ava" / "config.yaml"));
  REQUIRE(ava::config::credentials_path() == (home / ".ava" / "credentials.json"));

  {
    std::ofstream new_config(xdg_config_home / "ava" / "config.yaml");
    new_config << "new";
  }
  {
    std::ofstream new_creds(xdg_data_home / "ava" / "credentials.json");
    new_creds << "{}";
  }

  REQUIRE(ava::config::config_file_path() == (xdg_config_home / "ava" / "config.yaml"));
  REQUIRE(ava::config::credentials_path() == (xdg_data_home / "ava" / "credentials.json"));

  std::filesystem::remove_all(root);
}

TEST_CASE("credentials and trust stores use owner-only permissions on posix", "[ava_config]") {
  const auto root = temp_root_for_test();
  std::filesystem::create_directories(root);

  ScopedEnvVar xdg_config("XDG_CONFIG_HOME", (root / "config").string());
  ScopedEnvVar xdg_data("XDG_DATA_HOME", (root / "data").string());

  ava::config::CredentialStore store;
  store.set("openai", ava::config::ProviderCredential{.api_key = "test-key"});
  const auto creds_path = ava::config::credentials_path();
  store.save(creds_path);

  const auto project = root / "project";
  std::filesystem::create_directories(project);
  ava::config::trust_project(project);
  const auto trust_path = ava::config::trusted_projects_path();

#if !defined(_WIN32)
  struct stat creds_stat {
  };
  struct stat trust_stat {
  };
  REQUIRE(::stat(creds_path.c_str(), &creds_stat) == 0);
  REQUIRE(::stat(trust_path.c_str(), &trust_stat) == 0);
  REQUIRE((creds_stat.st_mode & 0777) == 0600);
  REQUIRE((trust_stat.st_mode & 0777) == 0600);
#else
  REQUIRE(std::filesystem::exists(creds_path));
  REQUIRE(std::filesystem::exists(trust_path));
#endif

  std::filesystem::remove_all(root);
}

TEST_CASE("trust cache invalidates after trust store writes", "[ava_config]") {
  const auto root = temp_root_for_test();
  std::filesystem::create_directories(root);

  ScopedEnvVar xdg_config("XDG_CONFIG_HOME", (root / "config").string());
  ava::config::clear_trust_cache_for_tests();
  const auto project = root / "project";
  std::filesystem::create_directories(project);

  REQUIRE_FALSE(ava::config::is_project_trusted(project));
  ava::config::trust_project(project);
  REQUIRE(ava::config::is_project_trusted(project));

  std::filesystem::remove_all(root);
  ava::config::clear_trust_cache_for_tests();
}

TEST_CASE("embedded model registry supports lookup and normalize", "[ava_config]") {
  const auto& registry = ava::config::registry();

  REQUIRE_FALSE(registry.models().empty());
  REQUIRE(registry.find("opus") != nullptr);
  REQUIRE(registry.find_for_provider("openai", "gpt-5.3-codex") != nullptr);
  REQUIRE(registry.normalize("claude-opus-4-6") == std::optional<std::string>{"claude-opus-4-6"});
  REQUIRE(registry.pricing("claude-opus-4-6").has_value());
  const auto* opus = registry.find("claude-opus-4-6");
  REQUIRE(opus != nullptr);
  REQUIRE(opus->capabilities.tool_call);
  REQUIRE_FALSE(opus->capabilities.streaming);
  REQUIRE_FALSE(opus->capabilities.reasoning);
  REQUIRE_FALSE(opus->capabilities.vision);
  REQUIRE(registry.is_loop_prone("glm-4.7"));
  REQUIRE_FALSE(registry.is_loop_prone("claude-opus-4-6"));
  REQUIRE(registry.find_for_provider("openai", "missing-model") == nullptr);
  REQUIRE(registry.normalize("missing-model") == std::nullopt);
}

TEST_CASE("known provider inventory includes deferred alias targets", "[ava_config]") {
  const auto& providers = ava::config::known_providers();
  REQUIRE(std::find(providers.begin(), providers.end(), "minimax") != providers.end());
  REQUIRE(ava::config::normalize_provider_name("minimax-coding-plan") == "minimax");
}

TEST_CASE("model specs parse provider model aliases and cli specs", "[ava_config]") {
  const auto explicit_spec = ava::config::parse_model_spec("openai/gpt-5.3-codex");
  REQUIRE(explicit_spec.provider == "openai");
  REQUIRE(explicit_spec.model == "gpt-5.3-codex");

  const auto alias_spec = ava::config::parse_model_spec("opus");
  REQUIRE(alias_spec.provider == "anthropic");
  REQUIRE(alias_spec.model == "claude-opus-4-6");

  const auto unknown_spec = ava::config::parse_model_spec("unknown-model");
  REQUIRE(unknown_spec.provider == "openrouter");
  REQUIRE(unknown_spec.model == "unknown-model");

  const auto cli_spec = ava::config::parse_model_spec("cli:local/test-model");
  REQUIRE(cli_spec.provider == "cli:local");
  REQUIRE(cli_spec.model == "test-model");
}

TEST_CASE("builtin agent templates expose stable defaults", "[ava_config]") {
  const auto& templates = ava::config::builtin_agent_templates();
  REQUIRE(templates.size() == 9);

  std::map<std::string_view, ava::config::BuiltinAgentTemplate> by_id;
  for(const auto& item : templates) {
    by_id.emplace(item.id, item);
  }

  REQUIRE(by_id.contains("general"));
  REQUIRE(by_id.contains("review"));
  REQUIRE(by_id.at("review").temperature == std::optional<float>{0.2F});
  REQUIRE(by_id.at("general").max_turns == std::optional<std::size_t>{12U});

  const ava::config::AgentDefaults defaults;
  REQUIRE(defaults.enabled);
  REQUIRE_FALSE(defaults.model.has_value());
  REQUIRE_FALSE(defaults.max_turns.has_value());
}
