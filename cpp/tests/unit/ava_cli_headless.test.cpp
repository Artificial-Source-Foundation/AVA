#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>
#include <iterator>

#include <nlohmann/json.hpp>

#include "ava/config/model_spec.hpp"
#include "ava/orchestration/composition.hpp"
#include "cli.hpp"
#include "events.hpp"

namespace {

std::filesystem::path temp_root_for_test() {
  const auto unique = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
  return std::filesystem::temp_directory_path() / ("ava_cpp_cli_test_" + unique);
}

ava::types::SessionRecord empty_session(std::string id) {
  return ava::types::SessionRecord{
      .id = std::move(id),
      .created_at = "2026-01-01T00:00:00Z",
      .updated_at = "2026-01-01T00:00:00Z",
      .metadata = nlohmann::json::object(),
      .messages = {},
      .branch_head = std::nullopt,
  };
}

}  // namespace

TEST_CASE("cli parses headless milestone 9 flags", "[ava_app]") {
  const char* argv[] = {
      "ava",
      "fix tests",
      "--provider",
      "openai",
      "--model",
      "gpt-5-mini",
      "--continue",
      "--json",
      "--max-turns",
      "5",
      "--auto-approve",
  };

  const auto cli = ava::app::parse_cli_or_throw(static_cast<int>(std::size(argv)), const_cast<char**>(argv));
  REQUIRE(cli.goal == std::optional<std::string>{"fix tests"});
  REQUIRE(cli.provider == std::optional<std::string>{"openai"});
  REQUIRE(cli.model == std::optional<std::string>{"gpt-5-mini"});
  REQUIRE(cli.resume);
  REQUIRE(cli.json);
  REQUIRE(cli.max_turns == 5);
  REQUIRE(cli.max_turns_explicit);
  REQUIRE(cli.auto_approve);
}

TEST_CASE("cli rejects conflicting resume flags", "[ava_app]") {
  const char* argv[] = {
      "ava",
      "goal",
      "--continue",
      "--session",
      "sess_123",
  };

  REQUIRE_THROWS(ava::app::parse_cli_or_throw(static_cast<int>(std::size(argv)), const_cast<char**>(argv)));
}

TEST_CASE("model parsing lives in config-owned seam", "[ava_app]") {
  const auto direct = ava::config::parse_model_spec("openai/gpt-5-mini");
  REQUIRE(direct.provider == "openai");
  REQUIRE(direct.model == "gpt-5-mini");

  const auto cli_prefix = ava::config::parse_model_spec("cli:/");
  REQUIRE(cli_prefix.provider == "openrouter");
  REQUIRE(cli_prefix.model == "cli:/");
}

TEST_CASE("session startup resolves new latest and specific", "[ava_app]") {
  const auto root = temp_root_for_test();
  std::filesystem::create_directories(root);

  ava::session::SessionManager manager(root / "sessions.db");

  auto first = manager.create();
  first.updated_at = "2026-01-01T00:00:00Z";
  manager.save(first);

  auto second = manager.create();
  second.updated_at = "2026-01-01T00:00:01Z";
  manager.save(second);

  const auto latest = ava::orchestration::resolve_startup_session(manager, true, std::nullopt);
  REQUIRE(latest.kind == ava::orchestration::SessionStartupKind::ContinueLatest);
  REQUIRE(latest.session.id == second.id);

  const auto specific = ava::orchestration::resolve_startup_session(manager, false, std::optional<std::string>{first.id});
  REQUIRE(specific.kind == ava::orchestration::SessionStartupKind::ContinueById);
  REQUIRE(specific.session.id == first.id);

  const auto created = ava::orchestration::resolve_startup_session(manager, false, std::nullopt);
  REQUIRE(created.kind == ava::orchestration::SessionStartupKind::New);
  REQUIRE(!created.session.id.empty());

  std::filesystem::remove_all(root);
}

TEST_CASE("agent selection applies cli precedence over persisted metadata", "[ava_app]") {
  auto session = empty_session("sess_1");
  session.metadata["headless"] = nlohmann::json{
      {"provider", "openai"},
      {"model", "gpt-5-mini"},
      {"max_turns", 3},
  };

  const auto selection = ava::orchestration::resolve_runtime_selection(
      ava::orchestration::RuntimeSelectionOptions{
          .provider = "openai",
          .model = "gpt-5.3-codex",
          .max_turns = 9,
          .max_turns_explicit = true,
      },
      session
  );
  REQUIRE(selection.provider == "openai");
  REQUIRE(selection.model == "gpt-5.3-codex");
  REQUIRE(selection.max_turns == 9);
}

TEST_CASE("agent selection restores persisted provider model when cli unset", "[ava_app]") {
  auto session = empty_session("sess_2");
  session.metadata["headless"] = nlohmann::json{
      {"provider", "openai"},
      {"model", "gpt-5-mini"},
      {"max_turns", 4},
  };

  const auto selection = ava::orchestration::resolve_runtime_selection(
      ava::orchestration::RuntimeSelectionOptions{
          .provider = std::nullopt,
          .model = std::nullopt,
          .max_turns = 16,
          .max_turns_explicit = false,
      },
      session
  );
  REQUIRE(selection.provider == "openai");
  REQUIRE(selection.model == "gpt-5-mini");
  REQUIRE(selection.max_turns == 4);
}

TEST_CASE("runtime selection prefers orchestration runtime metadata namespace", "[ava_app]") {
  auto session = empty_session("sess_runtime");
  session.metadata["runtime"] = nlohmann::json{
      {"provider", "openai"},
      {"model", "gpt-5.3-codex"},
      {"max_turns", 6},
  };
  session.metadata["headless"] = nlohmann::json{
      {"provider", "openrouter"},
      {"model", "fallback-model"},
      {"max_turns", 20},
  };

  const auto selection = ava::orchestration::resolve_runtime_selection(
      ava::orchestration::RuntimeSelectionOptions{
          .provider = std::nullopt,
          .model = std::nullopt,
          .max_turns = 16,
          .max_turns_explicit = false,
      },
      session
  );

  REQUIRE(selection.provider == "openai");
  REQUIRE(selection.model == "gpt-5.3-codex");
  REQUIRE(selection.max_turns == 6);
}

TEST_CASE("runtime selection keeps legacy headless metadata fallback", "[ava_app]") {
  auto session = empty_session("sess_headless_compat");
  session.metadata["headless"] = nlohmann::json{
      {"provider", "openai"},
      {"model", "gpt-5-mini"},
      {"max_turns", 5},
  };

  const auto selection = ava::orchestration::resolve_runtime_selection(
      ava::orchestration::RuntimeSelectionOptions{
          .provider = std::nullopt,
          .model = std::nullopt,
          .max_turns = 16,
          .max_turns_explicit = false,
      },
      session
  );

  REQUIRE(selection.provider == "openai");
  REQUIRE(selection.model == "gpt-5-mini");
  REQUIRE(selection.max_turns == 5);
}

TEST_CASE("ndjson event preserves canonical complete and error tags", "[ava_app]") {
  const auto complete = ava::app::headless_event_to_ndjson(ava::agent::AgentEvent{
      .kind = ava::agent::AgentEventKind::Completion,
      .turn = 2,
      .message = "done",
      .completion_reason = ava::agent::AgentCompletionReason::Completed,
  });
  REQUIRE(complete.at("type") == "complete");

  const auto error = ava::app::headless_event_to_ndjson(ava::agent::AgentEvent{
      .kind = ava::agent::AgentEventKind::Error,
      .turn = 2,
      .message = "boom",
  });
  REQUIRE(error.at("type") == "error");
}

TEST_CASE("ndjson event carries run_id and streaming delta payload", "[ava_app]") {
  const auto delta = ava::app::headless_event_to_ndjson(ava::agent::AgentEvent{
      .kind = ava::agent::AgentEventKind::AssistantResponseDelta,
      .run_id = "run-1",
      .turn = 1,
      .message = "hel",
  });
  REQUIRE(delta.at("type") == "assistant_response_delta");
  REQUIRE(delta.at("run_id") == "run-1");
  REQUIRE(delta.at("delta") == "hel");

  const auto complete = ava::app::headless_event_to_ndjson(ava::agent::AgentEvent{
      .kind = ava::agent::AgentEventKind::Completion,
      .run_id = "run-1",
      .turn = 1,
      .message = "done",
      .completion_reason = ava::agent::AgentCompletionReason::Cancelled,
  });
  REQUIRE(complete.at("run_id") == "run-1");
  REQUIRE(complete.at("reason") == "cancelled");

  const auto tool_call = ava::app::headless_event_to_ndjson(ava::agent::AgentEvent{
      .kind = ava::agent::AgentEventKind::ToolCall,
      .run_id = "run-1",
      .turn = 2,
      .tool_call = ava::types::ToolCall{.id = "call-1", .name = "read", .arguments = nlohmann::json::object()},
  });
  REQUIRE(tool_call.at("run_id") == "run-1");

  const auto tool_result = ava::app::headless_event_to_ndjson(ava::agent::AgentEvent{
      .kind = ava::agent::AgentEventKind::ToolResult,
      .run_id = "run-1",
      .turn = 2,
      .tool_result = ava::types::ToolResult{.call_id = "call-1", .content = "ok", .is_error = false},
  });
  REQUIRE(tool_result.at("run_id") == "run-1");
}
