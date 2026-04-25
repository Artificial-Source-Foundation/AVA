#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <optional>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include "ava/config/paths.hpp"
#include "ava/config/trust.hpp"
#include "ava/llm/factory.hpp"
#include "ava/session/session.hpp"
#include "cli.hpp"
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

struct ScopedStreamRedirect {
  std::ostream& stream;
  std::streambuf* previous;

  ScopedStreamRedirect(std::ostream& target, std::streambuf* replacement)
      : stream(target), previous(target.rdbuf(replacement)) {}

  ~ScopedStreamRedirect() {
    stream.rdbuf(previous);
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

[[nodiscard]] std::vector<nlohmann::json> parse_ndjson_lines(const std::string& text) {
  std::vector<nlohmann::json> lines;
  std::istringstream input(text);
  std::string line;
  while(std::getline(input, line)) {
    if(line.find_first_not_of(" \t\r\n") == std::string::npos) {
      continue;
    }
    try {
      lines.push_back(nlohmann::json::parse(line));
    } catch(const nlohmann::json::exception& error) {
      FAIL("invalid NDJSON line: " << line << " (" << error.what() << ")");
    }
  }
  return lines;
}

[[nodiscard]] std::vector<std::string> event_types(const std::vector<nlohmann::json>& events) {
  std::vector<std::string> types;
  types.reserve(events.size());
  for(const auto& event : events) {
    types.push_back(event.value("type", ""));
  }
  return types;
}

[[nodiscard]] bool contains_type(const std::vector<nlohmann::json>& events, const std::string& type) {
  return std::any_of(events.begin(), events.end(), [&](const auto& event) {
    return event.value("type", "") == type;
  });
}

[[nodiscard]] const nlohmann::json* first_event_of_type(
    const std::vector<nlohmann::json>& events,
    const std::string& type
) {
  const auto it = std::find_if(events.begin(), events.end(), [&](const auto& event) {
    return event.value("type", "") == type;
  });
  return it == events.end() ? nullptr : &*it;
}

void prepare_isolated_app_db(const std::filesystem::path& root) {
  const auto db_path = root / "data" / "ava" / "data.db";
  std::filesystem::create_directories(db_path.parent_path());
  std::ofstream touch(db_path, std::ios::app);
}

[[nodiscard]] ava::types::SessionMessage test_message(
    std::string id,
    std::string role,
    std::string content,
    std::optional<std::string> parent_id = std::nullopt
) {
  return ava::types::SessionMessage{
      .id = std::move(id),
      .role = std::move(role),
      .content = std::move(content),
      .timestamp = "2000-01-01T00:00:00Z",
      .parent_id = std::move(parent_id),
  };
}

}  // namespace

TEST_CASE("headless scripted tool loop executes tool and persists transcript", "[ava_app][integration]") {
  const auto root = temp_root_for_test("ava_cpp_m10_scripted");
  ScopedTempDir temp_root(root);
  const auto workspace = root / "workspace";
  std::filesystem::create_directories(workspace);
  std::ofstream(workspace / "artifact.txt") << "from scripted tool";

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
              .content = "I'll read the artifact now.",
              .tool_calls = {ava::types::ToolCall{
                  .id = "call_read_1",
                  .name = "read",
                  .arguments = nlohmann::json{{"path", "artifact.txt"}},
              }},
              .usage = ava::types::TokenUsage{.input_tokens = 10, .output_tokens = 5},
              .thinking = std::nullopt,
          },
          ava::llm::LlmResponse{
              .content = "Done. artifact.txt has been read.",
              .tool_calls = {},
              .usage = ava::types::TokenUsage{.input_tokens = 8, .output_tokens = 6},
              .thinking = std::nullopt,
          },
      }
  );

  ava::app::CliOptions cli;
  cli.goal = "read artifact";
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
  REQUIRE(session.messages.at(2).tool_call_id == std::optional<std::string>{"call_read_1"});

  const auto tool_payload = nlohmann::json::parse(session.messages.at(2).content);
  REQUIRE(tool_payload.at("call_id") == "call_read_1");
  REQUIRE(tool_payload.at("is_error") == false);
  REQUIRE(tool_payload.at("content").get<std::string>().find("from scripted tool") != std::string::npos);

  REQUIRE(session.metadata.at("headless").at("last_run").at("reason") == "completed");
  REQUIRE(session.metadata.at("headless").at("last_run").at("run_id").get<std::string>().find(session.id + ":run:") == 0);
  REQUIRE(session.metadata.at("headless").at("last_run").at("auto_approve") == true);
}

TEST_CASE("headless cwd selection persists workspace metadata", "[ava_app][integration]") {
  const auto root = temp_root_for_test("ava_cpp_m33_workspace");
  ScopedTempDir temp_root(root);
  const auto cwd_workspace = root / "cwd-workspace";
  std::filesystem::create_directories(cwd_workspace);

  ScopedEnvVar xdg_config("XDG_CONFIG_HOME", (root / "config").string());
  ScopedEnvVar xdg_data("XDG_DATA_HOME", (root / "data").string());
  ScopedEnvVar xdg_state("XDG_STATE_HOME", (root / "state").string());
  ScopedEnvVar xdg_cache("XDG_CACHE_HOME", (root / "cache").string());
  prepare_isolated_app_db(root);

  const auto cwd_provider = ava::llm::create_mock_provider("mock-cwd", std::vector<std::string>{"cwd ok"});
  ava::app::CliOptions cwd_cli;
  cwd_cli.goal = "prove cwd";
  cwd_cli.provider = "mock";
  cwd_cli.model = "mock-cwd";
  cwd_cli.cwd = cwd_workspace;
  cwd_cli.max_turns = 2;
  cwd_cli.max_turns_explicit = true;
  cwd_cli.json = true;

  std::ostringstream cwd_stdout;
  {
    ScopedStreamRedirect capture(std::cout, cwd_stdout.rdbuf());
    REQUIRE(ava::app::run_headless_blocking(cwd_cli, cwd_provider) == 0);
  }
  const auto cwd_events = parse_ndjson_lines(cwd_stdout.str());
  REQUIRE(cwd_events.front().at("workspace_root") == std::filesystem::weakly_canonical(cwd_workspace).string());

  ava::session::SessionManager sessions(ava::config::app_db_path());
  const auto after_cwd = sessions.list_recent(1);
  REQUIRE(!after_cwd.empty());
  REQUIRE(after_cwd.front().metadata.at("headless").at("workspace_root") == std::filesystem::weakly_canonical(cwd_workspace).string());
}

TEST_CASE("headless env workspace default flows through app boundary", "[ava_app][integration]") {
  const auto root = temp_root_for_test("ava_cpp_m33_env_workspace");
  ScopedTempDir temp_root(root);
  const auto env_workspace = root / "env-workspace";
  std::filesystem::create_directories(env_workspace);

  ScopedEnvVar xdg_config("XDG_CONFIG_HOME", (root / "config").string());
  ScopedEnvVar xdg_data("XDG_DATA_HOME", (root / "data").string());
  ScopedEnvVar xdg_state("XDG_STATE_HOME", (root / "state").string());
  ScopedEnvVar xdg_cache("XDG_CACHE_HOME", (root / "cache").string());
  prepare_isolated_app_db(root);

  ScopedEnvVar ava_cwd("AVA_WORKING_DIRECTORY", env_workspace.string());
  ScopedEnvVar ava_provider("AVA_PROVIDER", "mock");
  ScopedEnvVar ava_model("AVA_MODEL", "mock-env-workspace");
  std::array<char*, 2> argv{const_cast<char*>("ava"), const_cast<char*>("prove env cwd")};
  auto env_cli = ava::app::parse_cli_or_throw(static_cast<int>(argv.size()), argv.data());
  env_cli.max_turns = 2;
  env_cli.max_turns_explicit = true;
  env_cli.json = true;

  const auto env_provider = ava::llm::create_mock_provider("mock-env-workspace", std::vector<std::string>{"env cwd ok"});
  std::ostringstream env_stdout;
  {
    ScopedStreamRedirect capture(std::cout, env_stdout.rdbuf());
    REQUIRE(ava::app::run_headless_blocking(env_cli, env_provider) == 0);
  }
  const auto env_events = parse_ndjson_lines(env_stdout.str());
  REQUIRE(env_events.front().at("workspace_root") == std::filesystem::weakly_canonical(env_workspace).string());
}

TEST_CASE("headless trust flag persists trusted project through app boundary", "[ava_app][integration]") {
  const auto root = temp_root_for_test("ava_cpp_m33_trust");
  ScopedTempDir temp_root(root);
  const auto workspace = root / "workspace";
  std::filesystem::create_directories(workspace);

  ScopedEnvVar xdg_config("XDG_CONFIG_HOME", (root / "config").string());
  ScopedEnvVar xdg_data("XDG_DATA_HOME", (root / "data").string());
  ScopedEnvVar xdg_state("XDG_STATE_HOME", (root / "state").string());
  ScopedEnvVar xdg_cache("XDG_CACHE_HOME", (root / "cache").string());
  prepare_isolated_app_db(root);
  ava::config::clear_trust_cache_for_tests();
  REQUIRE_FALSE(ava::config::is_project_trusted(workspace));

  const auto provider = ava::llm::create_mock_provider("mock-trust", std::vector<std::string>{"trusted"});
  ava::app::CliOptions cli;
  cli.goal = "trust workspace";
  cli.provider = "mock";
  cli.model = "mock-trust";
  cli.cwd = workspace;
  cli.trust = true;
  cli.max_turns = 2;
  cli.max_turns_explicit = true;

  REQUIRE(ava::app::run_headless_blocking(cli, provider) == 0);
  ava::config::clear_trust_cache_for_tests();
  REQUIRE(ava::config::is_project_trusted(workspace));
}

TEST_CASE("headless json output emits session context and correlated tool events", "[ava_app][integration]") {
  const auto root = temp_root_for_test("ava_cpp_m10_json_events");
  ScopedTempDir temp_root(root);
  const auto workspace = root / "workspace";
  std::filesystem::create_directories(workspace);
  std::ofstream(workspace / "artifact.txt") << "json event validation";

  ScopedEnvVar xdg_config("XDG_CONFIG_HOME", (root / "config").string());
  ScopedEnvVar xdg_data("XDG_DATA_HOME", (root / "data").string());
  ScopedEnvVar xdg_state("XDG_STATE_HOME", (root / "state").string());
  ScopedEnvVar xdg_cache("XDG_CACHE_HOME", (root / "cache").string());
  ScopedCurrentPath cwd(workspace);
  prepare_isolated_app_db(root);

  const auto provider = ava::llm::create_mock_provider(
      "mock-json",
      std::vector<ava::llm::LlmResponse>{
          ava::llm::LlmResponse{
              .content = "Reading artifact in JSON mode.",
              .tool_calls = {ava::types::ToolCall{
                  .id = "call_read_json",
                  .name = "read",
                  .arguments = nlohmann::json{{"path", "artifact.txt"}},
              }},
              .usage = std::nullopt,
              .thinking = std::nullopt,
          },
          ava::llm::LlmResponse{
              .content = "Done.",
              .tool_calls = {},
              .usage = std::nullopt,
              .thinking = std::nullopt,
          },
      }
  );

  ava::app::CliOptions cli;
  cli.goal = "validate json output";
  cli.provider = "mock";
  cli.model = "mock-json";
  cli.max_turns = 4;
  cli.max_turns_explicit = true;
  cli.auto_approve = true;
  cli.json = true;

  std::ostringstream captured_stdout;
  {
    ScopedStreamRedirect capture(std::cout, captured_stdout.rdbuf());
    const auto exit_code = ava::app::run_headless_blocking(cli, provider);
    REQUIRE(exit_code == 0);
  }

  const auto events = parse_ndjson_lines(captured_stdout.str());
  REQUIRE(events.size() >= 5);

  const auto& session_context = events.front();
  REQUIRE(session_context.at("type") == "session_context");
  REQUIRE(session_context.at("provider") == "mock");
  REQUIRE(session_context.at("model") == "mock-json");
  REQUIRE(session_context.contains("session_id"));
  REQUIRE_FALSE(session_context.at("session_id").get<std::string>().empty());

  std::optional<nlohmann::json> tool_call;
  std::optional<nlohmann::json> tool_result;
  std::optional<nlohmann::json> completion;
  for(const auto& event : events) {
    const auto type = event.value("type", "");
    if(type == "tool_call" && !tool_call.has_value()) {
      tool_call = event;
      continue;
    }
    if(type == "tool_result" && !tool_result.has_value()) {
      tool_result = event;
      continue;
    }
    if(type == "complete") {
      completion = event;
    }
  }

  REQUIRE(tool_call.has_value());
  REQUIRE(tool_result.has_value());
  REQUIRE(completion.has_value());

  REQUIRE(tool_call->at("call_id") == "call_read_json");
  REQUIRE(tool_call->at("tool") == "read");
  REQUIRE(tool_result->at("call_id") == tool_call->at("call_id"));
  REQUIRE(tool_result->at("is_error") == false);
  REQUIRE(completion->at("reason") == "completed");

  const auto types = event_types(events);
  const auto session_context_it = std::find(types.begin(), types.end(), "session_context");
  const auto run_started_it = std::find(types.begin(), types.end(), "run_started");
  const auto turn_started_it = std::find(types.begin(), types.end(), "turn_started");
  const auto delta_it = std::find(types.begin(), types.end(), "assistant_response_delta");
  const auto checkpoint_it = std::find(types.begin(), types.end(), "checkpoint");
  const auto tool_call_it = std::find(types.begin(), types.end(), "tool_call");
  const auto tool_result_it = std::find(types.begin(), types.end(), "tool_result");
  const auto complete_it = std::find(types.begin(), types.end(), "complete");
  const auto checkpoint_after_delta_it = delta_it == types.end() ? types.end() : std::find(delta_it, types.end(), "checkpoint");
  REQUIRE(session_context_it != types.end());
  REQUIRE(run_started_it != types.end());
  REQUIRE(turn_started_it != types.end());
  REQUIRE(delta_it != types.end());
  REQUIRE(checkpoint_it != types.end());
  REQUIRE(checkpoint_after_delta_it != types.end());
  REQUIRE(tool_call_it != types.end());
  REQUIRE(tool_result_it != types.end());
  REQUIRE(complete_it != types.end());
  REQUIRE(session_context_it < run_started_it);
  REQUIRE(run_started_it < turn_started_it);
  REQUIRE(turn_started_it < delta_it);
  REQUIRE(delta_it < checkpoint_after_delta_it);
  REQUIRE(checkpoint_after_delta_it < tool_call_it);
  REQUIRE(tool_call_it < tool_result_it);
  REQUIRE(tool_result_it < complete_it);

  REQUIRE(events.at(static_cast<std::size_t>(run_started_it - types.begin())).contains("run_id"));
  REQUIRE(events.at(static_cast<std::size_t>(checkpoint_after_delta_it - types.begin())).contains("message"));
}

TEST_CASE("headless scripted loop executes multiple tool calls from one turn", "[ava_app][integration]") {
  const auto root = temp_root_for_test("ava_cpp_m10_multi_tool");
  ScopedTempDir temp_root(root);
  const auto workspace = root / "workspace";
  std::filesystem::create_directories(workspace);
  std::ofstream(workspace / "one.txt") << "first artifact";
  std::ofstream(workspace / "two.txt") << "second artifact";

  ScopedEnvVar xdg_config("XDG_CONFIG_HOME", (root / "config").string());
  ScopedEnvVar xdg_data("XDG_DATA_HOME", (root / "data").string());
  ScopedEnvVar xdg_state("XDG_STATE_HOME", (root / "state").string());
  ScopedEnvVar xdg_cache("XDG_CACHE_HOME", (root / "cache").string());
  ScopedCurrentPath cwd(workspace);
  prepare_isolated_app_db(root);

  const auto provider = ava::llm::create_mock_provider(
      "mock-multi-tool",
      std::vector<ava::llm::LlmResponse>{
          ava::llm::LlmResponse{
              .content = "Reading both artifacts.",
              .tool_calls = {ava::types::ToolCall{
                                 .id = "call_read_one",
                                 .name = "read",
                                 .arguments = nlohmann::json{{"path", "one.txt"}},
                             },
                             ava::types::ToolCall{
                                 .id = "call_read_two",
                                 .name = "read",
                                 .arguments = nlohmann::json{{"path", "two.txt"}},
                             }},
              .usage = std::nullopt,
              .thinking = std::nullopt,
          },
          ava::llm::LlmResponse{
              .content = "Both artifacts were read.",
              .tool_calls = {},
              .usage = std::nullopt,
              .thinking = std::nullopt,
          },
      }
  );

  ava::app::CliOptions cli;
  cli.goal = "read both files";
  cli.provider = "mock";
  cli.model = "mock-multi-tool";
  cli.max_turns = 4;
  cli.max_turns_explicit = true;
  cli.auto_approve = true;
  cli.json = true;

  std::ostringstream captured_stdout;
  {
    ScopedStreamRedirect capture(std::cout, captured_stdout.rdbuf());
    const auto exit_code = ava::app::run_headless_blocking(cli, provider);
    REQUIRE(exit_code == 0);
  }

  const auto events = parse_ndjson_lines(captured_stdout.str());
  bool saw_call_one = false;
  bool saw_call_two = false;
  bool saw_result_one = false;
  bool saw_result_two = false;
  for(const auto& event : events) {
    const auto type = event.value("type", "");
    const auto call_id = event.value("call_id", "");
    if(type == "tool_call" && call_id == "call_read_one") {
      saw_call_one = true;
    } else if(type == "tool_call" && call_id == "call_read_two") {
      saw_call_two = true;
    } else if(type == "tool_result" && call_id == "call_read_one") {
      saw_result_one = event.value("is_error", true) == false && event.value("content", "").find("first artifact") != std::string::npos;
    } else if(type == "tool_result" && call_id == "call_read_two") {
      saw_result_two = event.value("is_error", true) == false && event.value("content", "").find("second artifact") != std::string::npos;
    }
  }

  REQUIRE(saw_call_one);
  REQUIRE(saw_call_two);
  REQUIRE(saw_result_one);
  REQUIRE(saw_result_two);

  ava::session::SessionManager sessions(ava::config::app_db_path());
  const auto recent = sessions.list_recent(1);
  REQUIRE(!recent.empty());
  const auto& session = recent.front();
  REQUIRE(session.messages.size() == 5);
  REQUIRE(session.messages.at(0).role == "user");
  REQUIRE(session.messages.at(1).role == "assistant");
  REQUIRE(session.messages.at(2).role == "tool");
  REQUIRE(session.messages.at(3).role == "tool");
  REQUIRE(session.messages.at(4).role == "assistant");
}

TEST_CASE("headless auto approve rejects dangerous mutating tool", "[ava_app][integration]") {
  const auto root = temp_root_for_test("ava_cpp_m10_auto_reject");
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
                   .id = "call_write_auto_reject",
                   .name = "write",
                  .arguments = nlohmann::json{{"path", "blocked-auto.txt"}, {"content", "should not write"}},
              }},
               .usage = std::nullopt,
               .thinking = std::nullopt,
           },
           ava::llm::LlmResponse{
               .content = "blocked safely",
               .tool_calls = {},
               .usage = std::nullopt,
               .thinking = std::nullopt,
           },
       }
   );

  ava::app::CliOptions cli;
  cli.goal = "attempt mutating auto approve";
  cli.provider = "mock";
  cli.model = "mock-scripted";
  cli.max_turns = 2;
  cli.max_turns_explicit = true;
  cli.auto_approve = true;

  const auto exit_code = ava::app::run_headless_blocking(cli, provider);
  REQUIRE(exit_code == 0);
  REQUIRE_FALSE(std::filesystem::exists(workspace / "blocked-auto.txt"));

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
  REQUIRE(tool_payload.at("call_id") == "call_write_auto_reject");
  REQUIRE(tool_payload.at("is_error") == true);
  REQUIRE(tool_payload.at("content").get<std::string>().find("auto-approve rejects high-risk") != std::string::npos);
  REQUIRE(session.metadata.at("headless").at("last_run").at("reason") == "completed");
  REQUIRE(session.metadata.at("headless").at("last_run").at("auto_approve") == true);
  REQUIRE_FALSE(session.metadata.at("headless").at("last_run").contains("error"));
}

TEST_CASE("headless auto approve rejects edit and bash mutating tools", "[ava_app][integration]") {
  struct Scenario {
    std::string tool_name;
    nlohmann::json arguments;
    std::string blocked_path;
  };

  const std::vector<Scenario> scenarios{
      Scenario{
          .tool_name = "edit",
          .arguments = nlohmann::json{{"path", "edit-target.txt"}, {"old_text", "original"}, {"new_text", "mutated"}},
          .blocked_path = "edit-target.txt",
      },
      Scenario{
          .tool_name = "bash",
          .arguments = nlohmann::json{{"command", "touch bash-should-not-run.txt"}},
          .blocked_path = "bash-should-not-run.txt",
      },
  };

  for(const auto& scenario : scenarios) {
    const auto root = temp_root_for_test("ava_cpp_m20_auto_reject_" + scenario.tool_name);
    ScopedTempDir temp_root(root);
    const auto workspace = root / "workspace";
    std::filesystem::create_directories(workspace);
    if(scenario.tool_name == "edit") {
      std::ofstream(workspace / scenario.blocked_path) << "original";
    }

    ScopedEnvVar xdg_config("XDG_CONFIG_HOME", (root / "config").string());
    ScopedEnvVar xdg_data("XDG_DATA_HOME", (root / "data").string());
    ScopedEnvVar xdg_state("XDG_STATE_HOME", (root / "state").string());
    ScopedEnvVar xdg_cache("XDG_CACHE_HOME", (root / "cache").string());
    ScopedCurrentPath cwd(workspace);
    prepare_isolated_app_db(root);

    const auto provider = ava::llm::create_mock_provider(
        "mock-auto-reject-" + scenario.tool_name,
        std::vector<ava::llm::LlmResponse>{
            ava::llm::LlmResponse{
                .content = "I'll use a mutating tool.",
                .tool_calls = {ava::types::ToolCall{
                    .id = "call_" + scenario.tool_name + "_auto_reject",
                    .name = scenario.tool_name,
                    .arguments = scenario.arguments,
                }},
                .usage = std::nullopt,
                .thinking = std::nullopt,
            },
            ava::llm::LlmResponse{
                .content = "blocked safely",
                .tool_calls = {},
                .usage = std::nullopt,
                .thinking = std::nullopt,
            },
        }
    );

    ava::app::CliOptions cli;
    cli.goal = "attempt mutating auto approve";
    cli.provider = "mock";
    cli.model = "mock-auto-reject-" + scenario.tool_name;
    cli.max_turns = 2;
    cli.max_turns_explicit = true;
    cli.auto_approve = true;

    const auto exit_code = ava::app::run_headless_blocking(cli, provider);
    REQUIRE(exit_code == 0);
    if(scenario.tool_name == "edit") {
      REQUIRE(read_file_or_empty(workspace / scenario.blocked_path) == "original");
    } else {
      REQUIRE_FALSE(std::filesystem::exists(workspace / scenario.blocked_path));
    }

    ava::session::SessionManager sessions(ava::config::app_db_path());
    const auto recent = sessions.list_recent(1);
    REQUIRE(!recent.empty());
    const auto& session = recent.front();
    REQUIRE(session.messages.size() == 4);
    REQUIRE(session.messages.at(2).tool_call_id == std::optional<std::string>{"call_" + scenario.tool_name + "_auto_reject"});
    const auto tool_payload = nlohmann::json::parse(session.messages.at(2).content);
    REQUIRE(tool_payload.at("is_error") == true);
    REQUIRE(tool_payload.at("content").get<std::string>().find("auto-approve rejects high-risk") != std::string::npos);
  }
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
           ava::llm::LlmResponse{
               .content = "blocked safely",
               .tool_calls = {},
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
  REQUIRE(exit_code == 0);
  REQUIRE_FALSE(std::filesystem::exists(workspace / "blocked.txt"));

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
  REQUIRE(tool_payload.at("is_error") == true);
  REQUIRE(tool_payload.at("content").get<std::string>().find("requires approval") != std::string::npos);
  REQUIRE(session.metadata.at("headless").at("last_run").at("reason") == "completed");
  REQUIRE_FALSE(session.metadata.at("headless").at("last_run").contains("error"));
}

TEST_CASE("headless persists max turns terminal state", "[ava_app][integration]") {
  const auto root = temp_root_for_test("ava_cpp_m10_max_turns");
  ScopedTempDir temp_root(root);
  const auto workspace = root / "workspace";
  std::filesystem::create_directories(workspace);
  std::ofstream(workspace / "loop.txt") << "keep looping";

  ScopedEnvVar xdg_config("XDG_CONFIG_HOME", (root / "config").string());
  ScopedEnvVar xdg_data("XDG_DATA_HOME", (root / "data").string());
  ScopedEnvVar xdg_state("XDG_STATE_HOME", (root / "state").string());
  ScopedEnvVar xdg_cache("XDG_CACHE_HOME", (root / "cache").string());
  ScopedCurrentPath cwd(workspace);
  prepare_isolated_app_db(root);

  const auto provider = ava::llm::create_mock_provider(
      "mock-max-turns",
      std::vector<ava::llm::LlmResponse>{ava::llm::LlmResponse{
          .content = "I need another turn after this read.",
          .tool_calls = {ava::types::ToolCall{
              .id = "call_read_loop",
              .name = "read",
              .arguments = nlohmann::json{{"path", "loop.txt"}},
          }},
          .usage = std::nullopt,
          .thinking = std::nullopt,
      }}
  );

  ava::app::CliOptions cli;
  cli.goal = "force max turns";
  cli.provider = "mock";
  cli.model = "mock-max-turns";
  cli.max_turns = 1;
  cli.max_turns_explicit = true;
  cli.auto_approve = true;
  cli.json = true;

  std::ostringstream captured_stdout;
  {
    ScopedStreamRedirect capture(std::cout, captured_stdout.rdbuf());
    const auto exit_code = ava::app::run_headless_blocking(cli, provider);
    REQUIRE(exit_code == 2);
  }

  const auto events = parse_ndjson_lines(captured_stdout.str());
  std::optional<nlohmann::json> completion;
  for(const auto& event : events) {
    if(event.value("type", "") == "complete") {
      completion = event;
    }
  }
  REQUIRE(completion.has_value());
  REQUIRE(completion->at("reason") == "max_turns");

  ava::session::SessionManager sessions(ava::config::app_db_path());
  const auto recent = sessions.list_recent(1);
  REQUIRE(!recent.empty());
  const auto& last_run = recent.front().metadata.at("headless").at("last_run");
  REQUIRE(last_run.at("reason") == "max_turns");
  REQUIRE_FALSE(last_run.contains("error"));
}

TEST_CASE("headless metadata clears stale error after successful rerun", "[ava_app][integration]") {
  const auto root = temp_root_for_test("ava_cpp_m10_metadata_clear_error");
  ScopedTempDir temp_root(root);
  const auto workspace = root / "workspace";
  std::filesystem::create_directories(workspace);

  ScopedEnvVar xdg_config("XDG_CONFIG_HOME", (root / "config").string());
  ScopedEnvVar xdg_data("XDG_DATA_HOME", (root / "data").string());
  ScopedEnvVar xdg_state("XDG_STATE_HOME", (root / "state").string());
  ScopedEnvVar xdg_cache("XDG_CACHE_HOME", (root / "cache").string());
  ScopedCurrentPath cwd(workspace);
  prepare_isolated_app_db(root);

  const auto failing_provider = ava::llm::create_mock_provider(
      "mock-scripted",
      std::vector<ava::llm::LlmResponse>{ava::llm::LlmResponse{
          .content = "I'll write a file.",
          .tool_calls = {ava::types::ToolCall{
              .id = "call_write_retry",
              .name = "write",
              .arguments = nlohmann::json{{"path", "blocked.txt"}, {"content", "should not write"}},
          }},
          .usage = std::nullopt,
          .thinking = std::nullopt,
      }}
  );

  ava::app::CliOptions failing_cli;
  failing_cli.goal = "attempt mutating tool";
  failing_cli.provider = "mock";
  failing_cli.model = "mock-scripted";
  failing_cli.max_turns = 2;
  failing_cli.max_turns_explicit = true;
  failing_cli.auto_approve = false;

  const auto failing_exit = ava::app::run_headless_blocking(failing_cli, failing_provider);
  REQUIRE(failing_exit == 2);

  ava::session::SessionManager sessions(ava::config::app_db_path());
  const auto recent_after_fail = sessions.list_recent(1);
  REQUIRE(!recent_after_fail.empty());
  const auto failed_session_id = recent_after_fail.front().id;
  REQUIRE(recent_after_fail.front().messages.size() == 3);
  const auto failed_tool_payload = nlohmann::json::parse(recent_after_fail.front().messages.at(2).content);
  REQUIRE(failed_tool_payload.at("is_error") == true);
  REQUIRE(failed_tool_payload.at("content").get<std::string>().find("requires approval") != std::string::npos);
  REQUIRE(
      recent_after_fail.front().metadata.at("headless").at("last_run").at("error").get<std::string>().find("mock provider has no queued responses")
      != std::string::npos
  );

  const auto success_provider = ava::llm::create_mock_provider(
      "mock-scripted",
      std::vector<ava::llm::LlmResponse>{ava::llm::LlmResponse{
          .content = "done",
          .tool_calls = {},
          .usage = std::nullopt,
          .thinking = std::nullopt,
      }}
  );

  ava::app::CliOptions success_cli;
  success_cli.goal = "continue after failure";
  success_cli.provider = "mock";
  success_cli.model = "mock-scripted";
  success_cli.session_id = failed_session_id;
  success_cli.max_turns = 2;
  success_cli.max_turns_explicit = true;
  success_cli.auto_approve = false;

  const auto success_exit = ava::app::run_headless_blocking(success_cli, success_provider);
  REQUIRE(success_exit == 0);

  const auto resumed = sessions.get(failed_session_id);
  REQUIRE(resumed.has_value());
  const auto& last_run = resumed->metadata.at("headless").at("last_run");
  REQUIRE(last_run.at("reason") == "completed");
  REQUIRE_FALSE(last_run.contains("error"));
}

TEST_CASE("headless session resume applies recovery before new work", "[ava_app][integration]") {
  const auto root = temp_root_for_test("ava_cpp_m33_recovery");
  ScopedTempDir temp_root(root);
  const auto workspace = root / "workspace";
  std::filesystem::create_directories(workspace);

  ScopedEnvVar xdg_config("XDG_CONFIG_HOME", (root / "config").string());
  ScopedEnvVar xdg_data("XDG_DATA_HOME", (root / "data").string());
  ScopedEnvVar xdg_state("XDG_STATE_HOME", (root / "state").string());
  ScopedEnvVar xdg_cache("XDG_CACHE_HOME", (root / "cache").string());
  ScopedCurrentPath cwd(workspace);
  prepare_isolated_app_db(root);

  ava::session::SessionManager sessions(ava::config::app_db_path());
  auto session = sessions.create();
  session.metadata["runtime"]["provider"] = "mock";
  session.metadata["runtime"]["model"] = "mock-recovery";
  session.metadata["runtime"]["max_turns"] = 4;
  session.messages.push_back(test_message("m1", "user", "please read a file"));
  auto assistant = test_message("m2", "assistant", "I will call a tool", "m1");
  assistant.tool_calls = nlohmann::json::array({nlohmann::json{
      {"id", "orphaned_read"},
      {"name", "read"},
      {"arguments", nlohmann::json{{"path", "missing.txt"}}},
  }});
  session.messages.push_back(std::move(assistant));
  session.branch_head = "m2";
  sessions.save(session);

  const auto provider = ava::llm::create_mock_provider("mock-recovery", std::vector<std::string>{"recovered and done"});
  ava::app::CliOptions cli;
  cli.goal = "continue after interrupted tool";
  cli.session_id = session.id;
  cli.max_turns = 4;
  cli.max_turns_explicit = true;
  cli.json = true;

  std::ostringstream captured_stdout;
  {
    ScopedStreamRedirect capture(std::cout, captured_stdout.rdbuf());
    REQUIRE(ava::app::run_headless_blocking(cli, provider) == 0);
  }
  const auto events = parse_ndjson_lines(captured_stdout.str());
  REQUIRE(contains_type(events, "checkpoint"));

  const auto recovered = sessions.get(session.id);
  REQUIRE(recovered.has_value());
  REQUIRE(recovered->metadata.at("agent").at("last_recovery").at("interrupted_tools_added") == 1);
  REQUIRE(recovered->metadata.at("headless").at("last_startup_kind") == "continue_by_id");
  REQUIRE(recovered->metadata.at("headless").at("provider") == "mock");
  REQUIRE(recovered->metadata.at("headless").at("model") == "mock-recovery");

  const auto recovered_tool = std::find_if(recovered->messages.begin(), recovered->messages.end(), [](const auto& message) {
    return message.role == "tool" && message.tool_call_id == std::optional<std::string>{"orphaned_read"};
  });
  REQUIRE(recovered_tool != recovered->messages.end());
  REQUIRE(recovered_tool->content == "[Tool execution was interrupted]");
}

TEST_CASE("headless queued CLI messages drive follow-up and post-complete turns", "[ava_app][integration]") {
  const auto root = temp_root_for_test("ava_cpp_m33_queue");
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
      "mock-queue",
      std::vector<std::string>{"initial done", "follow-up done", "post-complete done"}
  );

  ava::app::CliOptions cli;
  cli.goal = "run queued work";
  cli.provider = "mock";
  cli.model = "mock-queue";
  cli.max_turns = 5;
  cli.max_turns_explicit = true;
  cli.follow_up_messages = {"follow-up item"};
  cli.post_complete_group_messages = {ava::app::PostCompleteMessageOption{.group = 2, .text = "post-complete item"}};

  REQUIRE(ava::app::run_headless_blocking(cli, provider) == 0);

  ava::session::SessionManager sessions(ava::config::app_db_path());
  const auto recent = sessions.list_recent(1);
  REQUIRE(!recent.empty());
  bool saw_follow_up = false;
  bool saw_post_complete = false;
  for(const auto& message : recent.front().messages) {
    if(message.role == "user" && message.content.find("[User follow-up]\nfollow-up item") != std::string::npos) {
      saw_follow_up = true;
    }
    if(message.role == "user" && message.content.find("[User post-complete group 2]\npost-complete item") != std::string::npos) {
      saw_post_complete = true;
    }
  }
  REQUIRE(saw_follow_up);
  REQUIRE(saw_post_complete);
  REQUIRE(recent.front().metadata.at("headless").at("last_run").at("turns_used") == 3);
}

TEST_CASE("headless budget exhaustion emits NDJSON budget warning and completion", "[ava_app][integration]") {
  const auto root = temp_root_for_test("ava_cpp_m33_budget");
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
      "mock-budget",
      std::vector<ava::llm::LlmResponse>{ava::llm::LlmResponse{
          .content = "budget will be exhausted",
          .tool_calls = {},
          .usage = ava::types::TokenUsage{.input_tokens = 10, .output_tokens = 10},
          .thinking = std::nullopt,
      }}
  );

  ava::app::CliOptions cli;
  cli.goal = "spend budget";
  cli.provider = "mock";
  cli.model = "mock-budget";
  cli.max_turns = 3;
  cli.max_turns_explicit = true;
  cli.max_budget_usd = 0.000001;
  cli.json = true;

  std::ostringstream captured_stdout;
  {
    ScopedStreamRedirect capture(std::cout, captured_stdout.rdbuf());
    REQUIRE(ava::app::run_headless_blocking(cli, provider) == 2);
  }
  const auto events = parse_ndjson_lines(captured_stdout.str());
  const auto* warning = first_event_of_type(events, "budget_warning");
  const auto* completion = first_event_of_type(events, "complete");
  REQUIRE(warning != nullptr);
  REQUIRE(completion != nullptr);
  REQUIRE(warning->at("threshold_percent").get<int>() >= 50);
  REQUIRE(warning->at("spent_usd").get<double>() > warning->at("budget_usd").get<double>());
  REQUIRE(completion->at("reason") == "budget_exceeded");
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
