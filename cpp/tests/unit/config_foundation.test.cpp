#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>

#if !defined(_WIN32)
#include <sys/stat.h>
#endif

#include "ava/config/config.hpp"

namespace {

struct ScopedEnvVar {
  std::string key;
  std::optional<std::string> old_value;

  ScopedEnvVar(std::string k, std::string value)
      : key(std::move(k)) {
    if(const char* current = std::getenv(key.c_str()); current != nullptr) {
      old_value = std::string(current);
    }
    setenv(key.c_str(), value.c_str(), 1);
  }

  ~ScopedEnvVar() {
    if(old_value.has_value()) {
      setenv(key.c_str(), old_value->c_str(), 1);
    } else {
      unsetenv(key.c_str());
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

TEST_CASE("embedded model registry supports lookup and normalize", "[ava_config]") {
  const auto& registry = ava::config::registry();

  REQUIRE_FALSE(registry.models().empty());
  REQUIRE(registry.find("opus") != nullptr);
  REQUIRE(registry.find_for_provider("openai", "gpt-5.3-codex") != nullptr);
  REQUIRE(registry.normalize("claude-opus-4-6") == std::optional<std::string>{"claude-opus-4.6"});
  REQUIRE(registry.pricing("claude-opus-4.6").has_value());
  REQUIRE(registry.is_loop_prone("glm-4.7"));
  REQUIRE_FALSE(registry.is_loop_prone("claude-opus-4.6"));
}
