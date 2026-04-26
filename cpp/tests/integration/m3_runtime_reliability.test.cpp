#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <random>
#include <sstream>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "ava/session/session.hpp"

#ifndef AVA_CLI_BINARY_PATH
#define AVA_CLI_BINARY_PATH ""
#endif

namespace {

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
  std::random_device entropy;
  const auto unique = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
  return std::filesystem::temp_directory_path()
         / (std::move(prefix) + "_" + unique + "_" + std::to_string(entropy()));
}

[[nodiscard]] std::string shell_quote(const std::filesystem::path& path) {
  std::string value = path.string();
  std::string quoted = "'";
  for(const char ch : value) {
    if(ch == '\'') {
      quoted += "'\\''";
    } else {
      quoted += ch;
    }
  }
  quoted += "'";
  return quoted;
}

[[nodiscard]] std::string shell_quote_text(const std::string& value) {
  std::string quoted = "'";
  for(const char ch : value) {
    if(ch == '\'') {
      quoted += "'\\''";
    } else {
      quoted += ch;
    }
  }
  quoted += "'";
  return quoted;
}

[[nodiscard]] std::string read_text_file(const std::filesystem::path& path) {
  std::ifstream input(path);
  return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
}

[[nodiscard]] std::vector<nlohmann::json> parse_ndjson_lines(const std::string& text) {
  std::vector<nlohmann::json> lines;
  std::istringstream input(text);
  std::string line;
  while(std::getline(input, line)) {
    const auto first_non_ws = line.find_first_not_of(" \t\r\n");
    if(first_non_ws == std::string::npos) {
      continue;
    }
    if(line.at(first_non_ws) != '{') {
      INFO("non-NDJSON stdout line: " << line);
      FAIL("--json stdout must contain only NDJSON object lines");
    }
    lines.push_back(nlohmann::json::parse(line));
  }
  return lines;
}

[[nodiscard]] const nlohmann::json* first_event_of_type(
    const std::vector<nlohmann::json>& events,
    const std::string& type
) {
  for(const auto& event : events) {
    if(event.value("type", "") == type) {
      return &event;
    }
  }
  return nullptr;
}

[[nodiscard]] std::size_t count_events_of_type(const std::vector<nlohmann::json>& events, const std::string& type) {
  std::size_t count = 0;
  for(const auto& event : events) {
    if(event.value("type", "") == type) {
      ++count;
    }
  }
  return count;
}

}  // namespace

TEST_CASE("m3 dogfoods compiled headless CLI with mock provider and tool execution", "[m3_runtime][ava_app]") {
  REQUIRE(std::string{AVA_CLI_BINARY_PATH}.size() > 0);
  REQUIRE(std::filesystem::exists(AVA_CLI_BINARY_PATH));

  const auto root = temp_root_for_test("ava_cpp_m3_dogfood");
  ScopedTempDir temp_root(root);
  const auto home = root / "home";
  const auto workspace = root / "workspace";
  std::filesystem::create_directories(home);
  std::filesystem::create_directories(workspace);
  {
    std::ofstream artifact(workspace / "artifact.txt");
    artifact << "dogfood artifact contents";
    REQUIRE(artifact.good());
  }

  const auto responses_path = root / "mock_responses.json";
  const nlohmann::json responses = nlohmann::json::array({
      nlohmann::json{
          {"content", "I will inspect the artifact."},
          {"tool_calls", nlohmann::json::array({nlohmann::json{
                             {"id", "call_read_dogfood"},
                             {"name", "read"},
                             {"arguments", nlohmann::json{{"path", "artifact.txt"}}},
                         }})},
          {"usage", nlohmann::json{{"input_tokens", 12}, {"output_tokens", 6}}},
      },
      nlohmann::json{
          {"content", "Dogfood run completed after reading artifact.txt."},
          {"usage", nlohmann::json{{"input_tokens", 10}, {"output_tokens", 8}}},
      },
  });
  {
    std::ofstream response_file(responses_path);
    response_file << responses.dump(2);
    REQUIRE(response_file.good());
  }

  const auto stdout_path = root / "stdout.ndjson";
  const auto stderr_path = root / "stderr.txt";
  const std::string command =
      "/usr/bin/env -i PATH=/usr/bin:/bin LANG=C.UTF-8 HOME=" + shell_quote(home) +
      " XDG_CONFIG_HOME=" + shell_quote(root / "config") +
      " XDG_DATA_HOME=" + shell_quote(root / "data") +
      " XDG_STATE_HOME=" + shell_quote(root / "state") +
      " XDG_CACHE_HOME=" + shell_quote(root / "cache") +
      " AVA_MOCK_PROVIDER_RESPONSES_FILE=" + shell_quote(responses_path) +
      " " + shell_quote(AVA_CLI_BINARY_PATH) +
      " " + shell_quote_text("read artifact via real cli") +
      " --provider mock --model mock-m3-dogfood --cwd " + shell_quote(workspace) +
      " --auto-approve --json --max-turns 4 > " + shell_quote(stdout_path) +
      " 2> " + shell_quote(stderr_path);

  const int exit_code = std::system(command.c_str());
  INFO("command: " << command);
  INFO("stderr: " << read_text_file(stderr_path));
  INFO("stdout: " << read_text_file(stdout_path));
  REQUIRE(exit_code == 0);
  REQUIRE(read_text_file(stderr_path).empty());

  const auto events = parse_ndjson_lines(read_text_file(stdout_path));
  REQUIRE(!events.empty());

  const auto* context = first_event_of_type(events, "session_context");
  REQUIRE(context != nullptr);
  REQUIRE(context->at("provider") == "mock");
  REQUIRE(context->at("model") == "mock-m3-dogfood");
  REQUIRE(context->at("workspace_root") == std::filesystem::weakly_canonical(workspace).string());
  const auto session_id = context->at("session_id").get<std::string>();

  const auto* tool_call = first_event_of_type(events, "tool_call");
  REQUIRE(tool_call != nullptr);
  REQUIRE(tool_call->at("call_id") == "call_read_dogfood");
  REQUIRE(tool_call->at("tool") == "read");

  const auto* tool_result = first_event_of_type(events, "tool_result");
  REQUIRE(tool_result != nullptr);
  REQUIRE(tool_result->at("call_id") == "call_read_dogfood");
  REQUIRE_FALSE(tool_result->at("is_error").get<bool>());
  REQUIRE(tool_result->at("content").get<std::string>().find("dogfood artifact contents") != std::string::npos);

  REQUIRE(count_events_of_type(events, "token_usage") >= 2);
  const auto* complete = first_event_of_type(events, "complete");
  REQUIRE(complete != nullptr);
  REQUIRE(complete->at("reason") == "completed");

  ava::session::SessionManager sessions(root / "data" / "ava" / "data.db");
  const auto session = sessions.get(session_id);
  REQUIRE(session.has_value());
  REQUIRE(session->metadata.at("headless").at("last_run").at("reason") == "completed");
  REQUIRE(session->messages.size() >= 4);

  bool saw_tool_result = false;
  bool saw_final_assistant = false;
  for(const auto& message : session->messages) {
    if(message.role == "tool" && message.tool_call_id == "call_read_dogfood" &&
       message.content.find("dogfood artifact contents") != std::string::npos) {
      saw_tool_result = true;
    }
    if(message.role == "assistant" && message.content.find("Dogfood run completed") != std::string::npos) {
      saw_final_assistant = true;
    }
  }
  REQUIRE(saw_tool_result);
  REQUIRE(saw_final_assistant);
}
