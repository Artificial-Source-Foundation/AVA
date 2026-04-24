#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include "ava/config/paths.hpp"
#include "ava/llm/factory.hpp"
#include "ava/session/session.hpp"
#include "headless_run.hpp"

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

struct ScopedCurrentPath {
  std::filesystem::path previous;

  explicit ScopedCurrentPath(const std::filesystem::path& next)
      : previous(std::filesystem::current_path()) {
    std::filesystem::current_path(next);
  }

  ~ScopedCurrentPath() {
    std::error_code ec;
    std::filesystem::current_path(previous, ec);
  }
};

struct ScopedTempDir {
  std::filesystem::path root;

  explicit ScopedTempDir(std::filesystem::path path)
      : root(std::move(path)) {
    std::filesystem::create_directories(root);
  }

  ~ScopedTempDir() {
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
  }
};

[[nodiscard]] std::filesystem::path temp_root_for_test(std::string prefix) {
  const auto unique = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
  return std::filesystem::temp_directory_path() / (std::move(prefix) + "_" + unique);
}

[[nodiscard]] std::string read_file_or_empty(const std::filesystem::path& path) {
  std::ifstream in(path);
  if(!in.is_open()) {
    return "";
  }
  return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

void prepare_isolated_app_db(const std::filesystem::path& root) {
  const auto db_path = root / "data" / "ava" / "data.db";
  std::filesystem::create_directories(db_path.parent_path());
  std::ofstream touch(db_path, std::ios::app);
}

}  // namespace

TEST_CASE("headless scripted tool loop executes tool and persists transcript", "[ava_app][integration]") {
  const auto root = temp_root_for_test("ava_cpp_m10_scripted");
  ScopedTempDir temp_root(root);
  const auto workspace = root / "workspace";
  std::filesystem::create_directories(workspace);

  ScopedEnvVar xdg_config("XDG_CONFIG_HOME", (root / "config").string());
  ScopedEnvVar xdg_data("XDG_DATA_HOME", (root / "data").string());
  ScopedEnvVar xdg_state("XDG_STATE_HOME", (root / "state").string());
  ScopedEnvVar xdg_cache("XDG_CACHE_HOME", (root / "cache").string());
  ScopedCurrentPath cwd(workspace);
  prepare_isolated_app_db(root);

  const auto provider = ava::llm::create_mock_provider(
      "mock-scripted",
      std::vector<ava::llm::LlmResponse>{
          ava::llm::LlmResponse{
              .content = "I'll write the artifact now.",
              .tool_calls = {ava::types::ToolCall{
                  .id = "call_write_1",
                  .name = "write",
                  .arguments = nlohmann::json{{"path", "artifact.txt"}, {"content", "from scripted tool"}},
              }},
              .usage = ava::types::TokenUsage{.input_tokens = 10, .output_tokens = 5},
              .thinking = std::nullopt,
          },
          ava::llm::LlmResponse{
              .content = "Done. artifact.txt has been written.",
              .tool_calls = {},
              .usage = ava::types::TokenUsage{.input_tokens = 8, .output_tokens = 6},
              .thinking = std::nullopt,
          },
      }
  );

  ava::app::CliOptions cli;
  cli.goal = "create artifact";
  cli.provider = "mock";
  cli.model = "mock-scripted";
  cli.max_turns = 4;
  cli.max_turns_explicit = true;
  cli.auto_approve = true;

  const auto exit_code = ava::app::run_headless_blocking(cli, provider);
  REQUIRE(exit_code == 0);

  const auto artifact = workspace / "artifact.txt";
  REQUIRE(std::filesystem::exists(artifact));
  REQUIRE(read_file_or_empty(artifact) == "from scripted tool");

  ava::session::SessionManager sessions(ava::config::app_db_path());
  const auto recent = sessions.list_recent(1);
  REQUIRE(!recent.empty());

  const auto& session = recent.front();
  REQUIRE(session.messages.size() == 4);
  REQUIRE(session.messages.at(0).role == "user");
  REQUIRE(session.messages.at(1).role == "assistant");
  REQUIRE(session.messages.at(2).role == "tool");
  REQUIRE(session.messages.at(3).role == "assistant");

  const auto tool_payload = nlohmann::json::parse(session.messages.at(2).content);
  REQUIRE(tool_payload.at("call_id") == "call_write_1");
  REQUIRE(tool_payload.at("is_error") == false);
  REQUIRE(tool_payload.at("content").get<std::string>().find("Wrote") != std::string::npos);

  REQUIRE(session.metadata.at("headless").at("last_run").at("reason") == "completed");
  REQUIRE(session.metadata.at("headless").at("last_run").at("auto_approve") == true);
}

TEST_CASE("headless rejects mutating tool call without auto approve", "[ava_app][integration]") {
  const auto root = temp_root_for_test("ava_cpp_m10_reject");
  ScopedTempDir temp_root(root);
  const auto workspace = root / "workspace";
  std::filesystem::create_directories(workspace);

  ScopedEnvVar xdg_config("XDG_CONFIG_HOME", (root / "config").string());
  ScopedEnvVar xdg_data("XDG_DATA_HOME", (root / "data").string());
  ScopedEnvVar xdg_state("XDG_STATE_HOME", (root / "state").string());
  ScopedEnvVar xdg_cache("XDG_CACHE_HOME", (root / "cache").string());
  ScopedCurrentPath cwd(workspace);
  prepare_isolated_app_db(root);

  const auto provider = ava::llm::create_mock_provider(
      "mock-scripted",
      std::vector<ava::llm::LlmResponse>{
          ava::llm::LlmResponse{
              .content = "I'll write a file.",
              .tool_calls = {ava::types::ToolCall{
                  .id = "call_write_1",
                  .name = "write",
                  .arguments = nlohmann::json{{"path", "blocked.txt"}, {"content", "should not write"}},
              }},
              .usage = std::nullopt,
              .thinking = std::nullopt,
          },
      }
  );

  ava::app::CliOptions cli;
  cli.goal = "attempt mutating tool";
  cli.provider = "mock";
  cli.model = "mock-scripted";
  cli.max_turns = 2;
  cli.max_turns_explicit = true;
  cli.auto_approve = false;

  const auto exit_code = ava::app::run_headless_blocking(cli, provider);
  REQUIRE(exit_code == 2);
  REQUIRE_FALSE(std::filesystem::exists(workspace / "blocked.txt"));

  ava::session::SessionManager sessions(ava::config::app_db_path());
  const auto recent = sessions.list_recent(1);
  REQUIRE(!recent.empty());

  const auto& session = recent.front();
  REQUIRE(session.messages.size() == 2);
  REQUIRE(session.messages.at(0).role == "user");
  REQUIRE(session.messages.at(1).role == "assistant");
  REQUIRE(session.metadata.at("headless").at("last_run").at("reason") == "error");
  REQUIRE(session.metadata.at("headless").at("last_run").at("error").get<std::string>().find("requires approval") != std::string::npos);
}

TEST_CASE("optional live OpenAI smoke runs through headless path", "[ava_app][integration][live]") {
  const char* live_flag = std::getenv("AVA_LIVE_PROVIDER_TESTS");
  if(live_flag == nullptr || std::string(live_flag).empty() || std::string(live_flag) == "0") {
    SKIP("set AVA_LIVE_PROVIDER_TESTS=1 to enable live-provider smoke test");
  }

  const char* openai_key = std::getenv("OPENAI_API_KEY");
  if(openai_key == nullptr || std::string(openai_key).empty()) {
    SKIP("OPENAI_API_KEY is required when AVA_LIVE_PROVIDER_TESTS is enabled");
  }

#if !AVA_WITH_CPR
  SKIP("live OpenAI smoke requires AVA_WITH_CPR=ON");
#endif

  const auto root = temp_root_for_test("ava_cpp_m10_live");
  ScopedTempDir temp_root(root);
  const auto workspace = root / "workspace";
  std::filesystem::create_directories(workspace);

  ScopedEnvVar xdg_config("XDG_CONFIG_HOME", (root / "config").string());
  ScopedEnvVar xdg_data("XDG_DATA_HOME", (root / "data").string());
  ScopedEnvVar xdg_state("XDG_STATE_HOME", (root / "state").string());
  ScopedEnvVar xdg_cache("XDG_CACHE_HOME", (root / "cache").string());
  ScopedCurrentPath cwd(workspace);
  prepare_isolated_app_db(root);

  ava::config::CredentialStore credentials;
  credentials.set("openai", ava::config::ProviderCredential{.api_key = openai_key});

  const std::string model = [&] {
    if(const char* configured = std::getenv("AVA_LIVE_OPENAI_MODEL"); configured != nullptr && configured[0] != '\0') {
      return std::string(configured);
    }
    return std::string{"gpt-4.1-mini"};
  }();

  const auto provider = ava::llm::create_provider("openai", model, credentials);

  ava::app::CliOptions cli;
  cli.goal = "Reply with a short plain-text greeting and no tool calls.";
  cli.provider = "openai";
  cli.model = model;
  cli.max_turns = 3;
  cli.max_turns_explicit = true;
  cli.auto_approve = true;

  const auto exit_code = ava::app::run_headless_blocking(cli, provider);
  REQUIRE(exit_code == 0);

  ava::session::SessionManager sessions(ava::config::app_db_path());
  const auto recent = sessions.list_recent(1);
  REQUIRE(!recent.empty());

  const auto& session = recent.front();
  REQUIRE(!session.messages.empty());

  bool has_assistant_text = false;
  for(const auto& message : session.messages) {
    if(message.role == "assistant" && !message.content.empty()) {
      has_assistant_text = true;
      break;
    }
  }
  REQUIRE(has_assistant_text);
}
