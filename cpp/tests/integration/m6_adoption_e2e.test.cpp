#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
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

[[nodiscard]] std::optional<std::size_t> first_event_index_of_type(
    const std::vector<nlohmann::json>& events,
    const std::string& type
) {
  for(std::size_t index = 0; index < events.size(); ++index) {
    if(events.at(index).value("type", "") == type) {
      return index;
    }
  }
  return std::nullopt;
}

}  // namespace

TEST_CASE("m6 adoption loop e2e evidence lane runs compiled cli deterministically", "[m6_e2e][ava_app]") {
  REQUIRE(std::string{AVA_CLI_BINARY_PATH}.size() > 0);
  REQUIRE(std::filesystem::exists(AVA_CLI_BINARY_PATH));

  const auto root = temp_root_for_test("ava_cpp_m6_e2e");
  ScopedTempDir temp_root(root);
  const auto home = root / "home";
  const auto workspace = root / "workspace";
  std::filesystem::create_directories(home);
  std::filesystem::create_directories(workspace);
  {
    std::ofstream artifact(workspace / "artifact.txt");
    artifact << "m6 adoption artifact payload";
    REQUIRE(artifact.good());
  }

  const auto responses_path = root / "mock_responses.json";
  const nlohmann::json responses = nlohmann::json::array({
      nlohmann::json{
          {"content", "I will inspect the adoption artifact."},
          {"tool_calls", nlohmann::json::array({nlohmann::json{
                             {"id", "call_read_m6"},
                             {"name", "read"},
                             {"arguments", nlohmann::json{{"path", "artifact.txt"}}},
                         }})},
          {"usage", nlohmann::json{{"input_tokens", 15}, {"output_tokens", 8}}},
      },
      nlohmann::json{
          {"content", "M6 adoption evidence run completed after reading artifact.txt."},
          {"usage", nlohmann::json{{"input_tokens", 11}, {"output_tokens", 10}}},
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
      " " + shell_quote_text("run m6 adoption evidence lane") +
      " --provider mock --model mock-m6-e2e --cwd " + shell_quote(workspace) +
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
  REQUIRE(context->at("model") == "mock-m6-e2e");
  REQUIRE(context->at("workspace_root") == std::filesystem::weakly_canonical(workspace).string());
  const auto session_id = context->at("session_id").get<std::string>();

  const auto* tool_call = first_event_of_type(events, "tool_call");
  REQUIRE(tool_call != nullptr);
  const auto run_id = tool_call->at("run_id").get<std::string>();
  REQUIRE_FALSE(run_id.empty());
  REQUIRE(tool_call->at("run_id") == run_id);
  REQUIRE(tool_call->at("call_id") == "call_read_m6");
  REQUIRE(tool_call->at("tool") == "read");
  REQUIRE(tool_call->at("args").at("path") == "artifact.txt");

  const auto* tool_result = first_event_of_type(events, "tool_result");
  REQUIRE(tool_result != nullptr);
  REQUIRE(tool_result->at("run_id") == run_id);
  REQUIRE(tool_result->at("call_id") == "call_read_m6");
  REQUIRE_FALSE(tool_result->at("is_error").get<bool>());
  REQUIRE(tool_result->at("content").get<std::string>().find("m6 adoption artifact payload") != std::string::npos);

  bool saw_first_usage = false;
  bool saw_second_usage = false;
  bool saw_assistant_delta = false;
  std::optional<std::size_t> first_usage_idx;
  std::optional<std::size_t> second_usage_idx;
  std::optional<std::size_t> assistant_delta_idx;
  for(std::size_t index = 0; index < events.size(); ++index) {
    const auto& event = events.at(index);
    if(event.value("type", "") == "token_usage") {
      REQUIRE(event.at("run_id") == run_id);
      REQUIRE(event.at("input_tokens").is_number_integer());
      REQUIRE(event.at("output_tokens").is_number_integer());
      if(event.at("input_tokens") == 15 && event.at("output_tokens") == 8) {
        saw_first_usage = true;
        first_usage_idx = index;
      }
      if(event.at("input_tokens") == 11 && event.at("output_tokens") == 10) {
        saw_second_usage = true;
        second_usage_idx = index;
      }
    }
    if(event.value("type", "") == "assistant_response_delta") {
      REQUIRE(event.at("run_id") == run_id);
      if(event.at("delta").get<std::string>().find("inspect the adoption artifact") != std::string::npos) {
        saw_assistant_delta = true;
        assistant_delta_idx = index;
      }
    }
  }
  REQUIRE(saw_first_usage);
  REQUIRE(saw_second_usage);
  REQUIRE(saw_assistant_delta);
  REQUIRE(first_usage_idx.has_value());
  REQUIRE(second_usage_idx.has_value());
  REQUIRE(assistant_delta_idx.has_value());

  const auto* complete = first_event_of_type(events, "complete");
  REQUIRE(complete != nullptr);
  REQUIRE(complete->at("run_id") == run_id);
  REQUIRE(complete->at("reason") == "completed");

  const auto session_context_idx = first_event_index_of_type(events, "session_context");
  const auto tool_call_idx = first_event_index_of_type(events, "tool_call");
  const auto tool_result_idx = first_event_index_of_type(events, "tool_result");
  const auto token_usage_idx = first_event_index_of_type(events, "token_usage");
  const auto complete_idx = first_event_index_of_type(events, "complete");
  REQUIRE(session_context_idx.has_value());
  REQUIRE(tool_call_idx.has_value());
  REQUIRE(tool_result_idx.has_value());
  REQUIRE(token_usage_idx.has_value());
  REQUIRE(complete_idx.has_value());
  REQUIRE(*session_context_idx < *tool_call_idx);
  REQUIRE(*assistant_delta_idx < *first_usage_idx);
  REQUIRE(*first_usage_idx < *tool_call_idx);
  REQUIRE(*tool_call_idx < *tool_result_idx);
  REQUIRE(*tool_result_idx < *second_usage_idx);
  REQUIRE(*second_usage_idx < *complete_idx);
  REQUIRE(*tool_result_idx < *complete_idx);
  REQUIRE(*token_usage_idx < *complete_idx);

  ava::session::SessionManager sessions(root / "data" / "ava" / "data.db");
  const auto session = sessions.get(session_id);
  REQUIRE(session.has_value());
  REQUIRE(session->metadata.at("headless").at("last_run").at("reason") == "completed");
  REQUIRE(session->metadata.at("headless").at("credential_provider") == "mock");
  REQUIRE(session->token_usage.at("input_tokens") == 26);
  REQUIRE(session->token_usage.at("output_tokens") == 18);
  REQUIRE(session->messages.size() >= 4);

  bool saw_tool_result = false;
  bool saw_final_assistant = false;
  for(const auto& message : session->messages) {
    if(message.role == "tool" && message.tool_call_id == "call_read_m6"
       && message.content.find("m6 adoption artifact payload") != std::string::npos) {
      saw_tool_result = true;
    }
    if(message.role == "assistant"
       && message.content.find("M6 adoption evidence run completed") != std::string::npos) {
      saw_final_assistant = true;
    }
  }
  REQUIRE(saw_tool_result);
  REQUIRE(saw_final_assistant);
}
