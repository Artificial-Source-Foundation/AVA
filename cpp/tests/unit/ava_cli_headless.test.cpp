#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <csignal>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <iterator>
#include <optional>
#include <sstream>
#include <utility>

#include <nlohmann/json.hpp>

#include "ava/config/model_spec.hpp"
#include "ava/orchestration/composition.hpp"
#include "cli.hpp"
#include "events.hpp"
#include "signal_cancel.hpp"

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

class EnvGuard {
 public:
  explicit EnvGuard(std::vector<std::string> names) : names_(std::move(names)) {
    for(const auto& name : names_) {
      if(const char* value = std::getenv(name.c_str()); value != nullptr) {
        previous_.push_back({name, std::string(value)});
      } else {
        previous_.push_back({name, std::nullopt});
      }
      unsetenv(name.c_str());
    }
  }

  ~EnvGuard() {
    for(const auto& [name, value] : previous_) {
      if(value.has_value()) {
        setenv(name.c_str(), value->c_str(), 1);
      } else {
        unsetenv(name.c_str());
      }
    }
  }

  EnvGuard(const EnvGuard&) = delete;
  EnvGuard& operator=(const EnvGuard&) = delete;

 private:
  std::vector<std::string> names_;
  std::vector<std::pair<std::string, std::optional<std::string>>> previous_;
};

}  // namespace

TEST_CASE("cli parses full headless flag surface", "[ava_app]") {
  const char* argv[] = {
      "ava",
      "fix tests",
      "--provider",
      "openai",
      "--model",
      "gpt-5-mini",
      "--cwd",
      ".",
      "--agent",
      "review",
      "--trust",
      "--continue",
      "--json",
      "--max-turns",
      "5",
      "--max-budget",
      "1.25",
      "--follow-up",
      "run checks",
      "--later",
      "summarize",
      "--later-group",
      "2:notify",
      "--auto-approve",
  };

  const auto cli = ava::app::parse_cli_or_throw(static_cast<int>(std::size(argv)), const_cast<char**>(argv));
  REQUIRE(cli.goal == std::optional<std::string>{"fix tests"});
  REQUIRE(cli.provider == std::optional<std::string>{"openai"});
  REQUIRE(cli.model == std::optional<std::string>{"gpt-5-mini"});
  REQUIRE(cli.cwd == std::optional<std::filesystem::path>{std::filesystem::path(".")});
  REQUIRE(cli.agent == std::optional<std::string>{"review"});
  REQUIRE(cli.trust);
  REQUIRE(cli.resume);
  REQUIRE(cli.json);
  REQUIRE(cli.max_turns == 5);
  REQUIRE(cli.max_turns_explicit);
  REQUIRE(cli.max_budget_usd == 1.25);
  REQUIRE(cli.follow_up_messages == std::vector<std::string>{"run checks"});
  REQUIRE(cli.post_complete_messages == std::vector<std::string>{"summarize"});
  REQUIRE(cli.post_complete_group_messages.size() == 1);
  REQUIRE(cli.post_complete_group_messages.front().group == 2);
  REQUIRE(cli.post_complete_group_messages.front().text == "notify");
  REQUIRE(cli.auto_approve);
}

TEST_CASE("cli applies environment defaults when flags are unset", "[ava_app]") {
  EnvGuard env({"AVA_PROVIDER", "AVA_MODEL", "AVA_WORKING_DIRECTORY", "AVA_AGENT"});
  setenv("AVA_PROVIDER", "anthropic", 1);
  setenv("AVA_MODEL", "claude-sonnet-4", 1);
  setenv("AVA_WORKING_DIRECTORY", "/tmp", 1);
  setenv("AVA_AGENT", "explore", 1);

  const char* argv[] = {"ava", "inspect repo"};
  const auto cli = ava::app::parse_cli_or_throw(static_cast<int>(std::size(argv)), const_cast<char**>(argv));

  REQUIRE(cli.provider == std::optional<std::string>{"anthropic"});
  REQUIRE(cli.model == std::optional<std::string>{"claude-sonnet-4"});
  REQUIRE(cli.cwd == std::optional<std::filesystem::path>{std::filesystem::path("/tmp")});
  REQUIRE(cli.agent == std::optional<std::string>{"explore"});
}

TEST_CASE("cli flags override environment defaults", "[ava_app]") {
  EnvGuard env({"AVA_PROVIDER", "AVA_MODEL", "AVA_WORKING_DIRECTORY", "AVA_AGENT"});
  setenv("AVA_PROVIDER", "anthropic", 1);
  setenv("AVA_MODEL", "claude-sonnet-4", 1);
  setenv("AVA_WORKING_DIRECTORY", "/tmp", 1);
  setenv("AVA_AGENT", "explore", 1);

  const char* argv[] = {
      "ava", "inspect repo", "--provider", "openai", "--model", "gpt-5-mini", "--cwd", ".", "--agent", "review",
  };
  const auto cli = ava::app::parse_cli_or_throw(static_cast<int>(std::size(argv)), const_cast<char**>(argv));

  REQUIRE(cli.provider == std::optional<std::string>{"openai"});
  REQUIRE(cli.model == std::optional<std::string>{"gpt-5-mini"});
  REQUIRE(cli.cwd == std::optional<std::filesystem::path>{std::filesystem::path(".")});
  REQUIRE(cli.agent == std::optional<std::string>{"review"});
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

TEST_CASE("headless signal cancellation bridge records cancellation requests", "[ava_app]") {
  ava::app::reset_headless_signal_cancel();
  REQUIRE_FALSE(ava::app::headless_signal_cancel_requested());

  ava::app::request_headless_cancel_for_testing();
  REQUIRE(ava::app::headless_signal_cancel_requested());

  ava::app::reset_headless_signal_cancel();
  REQUIRE_FALSE(ava::app::headless_signal_cancel_requested());

  ava::app::install_headless_signal_cancel_handlers();
  std::raise(SIGINT);
  REQUIRE(ava::app::headless_signal_cancel_requested());

  ava::app::reset_headless_signal_cancel();
  REQUIRE_FALSE(ava::app::headless_signal_cancel_requested());

  std::raise(SIGTERM);
  REQUIRE(ava::app::headless_signal_cancel_requested());
  ava::app::restore_headless_signal_cancel_handlers();
  ava::app::reset_headless_signal_cancel();
}

TEST_CASE("headless signal cancellation bridge supports nested installs", "[ava_app]") {
  ava::app::reset_headless_signal_cancel();
  ava::app::install_headless_signal_cancel_handlers();
  ava::app::install_headless_signal_cancel_handlers();

  std::raise(SIGINT);
  REQUIRE(ava::app::headless_signal_cancel_requested());

  ava::app::reset_headless_signal_cancel();
  ava::app::restore_headless_signal_cancel_handlers();
  std::raise(SIGTERM);
  REQUIRE(ava::app::headless_signal_cancel_requested());

  ava::app::restore_headless_signal_cancel_handlers();
  ava::app::reset_headless_signal_cancel();
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

TEST_CASE("resume by id preserves tool heavy message metadata", "[ava_app]") {
  const auto root = temp_root_for_test();
  std::filesystem::create_directories(root);

  ava::session::SessionManager manager(root / "sessions.db");
  auto session = manager.create();
  session.messages.push_back(ava::types::SessionMessage{
      .id = "m1",
      .role = "assistant",
      .content = "",
      .tool_calls = nlohmann::json::array({nlohmann::json{{"id", "call-1"}, {"name", "read"}, {"arguments", nlohmann::json{{"path", "README.md"}}}}}),
      .tool_results = nlohmann::json::array(),
      .timestamp = "2026-01-01T00:00:00Z",
      .parent_id = std::nullopt,
  });
  session.messages.push_back(ava::types::SessionMessage{
      .id = "m2",
      .role = "tool",
      .content = R"({"call_id":"call-1","content":"ok","is_error":false})",
      .tool_calls = nlohmann::json::array(),
      .tool_results = nlohmann::json::array({nlohmann::json{{"call_id", "call-1"}, {"content", "ok"}, {"is_error", false}}}),
      .tool_call_id = std::optional<std::string>{"call-1"},
      .timestamp = "2026-01-01T00:00:01Z",
      .parent_id = std::optional<std::string>{"m1"},
  });
  session.branch_head = "m2";
  manager.save(session);

  const auto resumed = ava::orchestration::resolve_startup_session(manager, false, std::optional<std::string>{session.id});
  REQUIRE(resumed.kind == ava::orchestration::SessionStartupKind::ContinueById);
  REQUIRE(resumed.session.messages.size() == 2);
  REQUIRE(resumed.session.messages.at(0).tool_calls == session.messages.at(0).tool_calls);
  REQUIRE(resumed.session.messages.at(1).tool_results == session.messages.at(1).tool_results);
  REQUIRE(resumed.session.messages.at(1).tool_call_id == std::optional<std::string>{"call-1"});
  REQUIRE(resumed.session.branch_head == std::optional<std::string>{"m2"});

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
  REQUIRE(selection.credential_provider == "openai");
  REQUIRE(selection.model == "gpt-5.3-codex");
  REQUIRE(selection.max_turns == 9);
}

TEST_CASE("agent selection preserves raw provider alias for credential lookup", "[ava_app]") {
  const auto selection = ava::orchestration::resolve_runtime_selection(
      ava::orchestration::RuntimeSelectionOptions{
          .provider = "chatgpt",
          .model = "gpt-5-mini",
          .max_turns = 16,
          .max_turns_explicit = false,
      },
      empty_session("sess_alias")
  );

  REQUIRE(selection.provider == "openai");
  REQUIRE(selection.credential_provider == "chatgpt");
  REQUIRE(selection.model == "gpt-5-mini");

  const auto model_spec_selection = ava::orchestration::resolve_runtime_selection(
      ava::orchestration::RuntimeSelectionOptions{
          .provider = std::nullopt,
          .model = "chatgpt/gpt-5-mini",
          .max_turns = 16,
          .max_turns_explicit = false,
      },
      empty_session("sess_alias_model")
  );

  REQUIRE(model_spec_selection.provider == "openai");
  REQUIRE(model_spec_selection.credential_provider == "chatgpt");
  REQUIRE(model_spec_selection.model == "gpt-5-mini");

  const auto mixed_case_selection = ava::orchestration::resolve_runtime_selection(
      ava::orchestration::RuntimeSelectionOptions{
          .provider = "ChatGPT",
          .model = "gpt-5-mini",
          .max_turns = 16,
          .max_turns_explicit = false,
      },
      empty_session("sess_alias_mixed_case")
  );

  REQUIRE(mixed_case_selection.provider == "openai");
  REQUIRE(mixed_case_selection.credential_provider == "ChatGPT");
  REQUIRE(mixed_case_selection.model == "gpt-5-mini");
}

TEST_CASE("agent selection restores persisted provider model when cli unset", "[ava_app]") {
  auto session = empty_session("sess_2");
  session.metadata["headless"] = nlohmann::json{
      {"provider", "openai"},
      {"credential_provider", "chatgpt"},
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
  REQUIRE(selection.credential_provider == "chatgpt");
  REQUIRE(selection.model == "gpt-5-mini");
  REQUIRE(selection.max_turns == 4);
}

TEST_CASE("agent selection applies builtin agent defaults", "[ava_app]") {
  auto session = empty_session("sess_agent");

  const auto selection = ava::orchestration::resolve_runtime_selection(
      ava::orchestration::RuntimeSelectionOptions{
          .provider = "openai",
          .model = "gpt-5-mini",
          .agent = "explore",
          .max_turns = 16,
          .max_turns_explicit = false,
      },
      session
  );

  REQUIRE(selection.agent == std::optional<std::string>{"explore"});
  REQUIRE(selection.max_turns == 5);
}

TEST_CASE("agent selection rejects unknown agent profiles", "[ava_app]") {
  auto session = empty_session("sess_unknown_agent");

  REQUIRE_THROWS(ava::orchestration::resolve_runtime_selection(
      ava::orchestration::RuntimeSelectionOptions{
          .provider = "openai",
          .model = "gpt-5-mini",
          .agent = "missing-agent",
          .max_turns = 16,
          .max_turns_explicit = false,
      },
      session
  ));
}

TEST_CASE("runtime selection preserves unknown persisted agent without applying defaults", "[ava_app]") {
  auto session = empty_session("sess_persisted_agent");
  session.metadata["runtime"] = nlohmann::json{
      {"agent", "custom-agent"},
      {"max_turns", 11},
  };

  const auto selection = ava::orchestration::resolve_runtime_selection(
      ava::orchestration::RuntimeSelectionOptions{
          .provider = "openai",
          .model = "gpt-5-mini",
          .max_turns = 16,
          .max_turns_explicit = false,
      },
      session
  );

  REQUIRE(selection.agent == std::optional<std::string>{"custom-agent"});
  REQUIRE(selection.max_turns == 11);
}

TEST_CASE("runtime selection prefers persisted turns over builtin agent defaults", "[ava_app]") {
  auto session = empty_session("sess_agent_turns");
  session.metadata["runtime"] = nlohmann::json{
      {"agent", "explore"},
      {"max_turns", 12},
  };

  const auto selection = ava::orchestration::resolve_runtime_selection(
      ava::orchestration::RuntimeSelectionOptions{
          .provider = "openai",
          .model = "gpt-5-mini",
          .max_turns = 16,
          .max_turns_explicit = false,
      },
      session
  );

  REQUIRE(selection.agent == std::optional<std::string>{"explore"});
  REQUIRE(selection.max_turns == 12);
}

TEST_CASE("runtime selection applies explicit agent defaults over persisted turns", "[ava_app]") {
  auto session = empty_session("sess_agent_override_turns");
  session.metadata["runtime"] = nlohmann::json{
      {"max_turns", 12},
  };

  const auto selection = ava::orchestration::resolve_runtime_selection(
      ava::orchestration::RuntimeSelectionOptions{
          .provider = "openai",
          .model = "gpt-5-mini",
          .agent = "explore",
          .max_turns = 16,
          .max_turns_explicit = false,
      },
      session
  );

  REQUIRE(selection.agent == std::optional<std::string>{"explore"});
  REQUIRE(selection.max_turns == 5);
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

TEST_CASE("ndjson event projects token usage budget and compaction fields", "[ava_app]") {
  const auto usage = ava::app::headless_event_to_ndjson(ava::agent::AgentEvent{
      .kind = ava::agent::AgentEventKind::TokenUsage,
      .run_id = "run-usage",
      .turn = 2,
      .token_usage = ava::types::TokenUsage{.input_tokens = 10, .output_tokens = 7, .cache_read_tokens = 3, .cache_creation_tokens = 2},
      .token_cost_usd = 0.0042,
  });
  REQUIRE(usage.at("type") == "token_usage");
  REQUIRE(usage.at("run_id") == "run-usage");
  REQUIRE(usage.at("turn") == 2);
  REQUIRE(usage.at("input_tokens") == 10);
  REQUIRE(usage.at("output_tokens") == 7);
  REQUIRE(usage.at("cache_read_tokens") == 3);
  REQUIRE(usage.at("cache_creation_tokens") == 2);
  REQUIRE(usage.at("cost_usd") == 0.0042);

  const auto budget = ava::app::headless_event_to_ndjson(ava::agent::AgentEvent{
      .kind = ava::agent::AgentEventKind::BudgetWarning,
      .run_id = "run-budget",
      .turn = 3,
      .budget_warning = ava::agent::BudgetWarning{.threshold_percent = 100, .spent_usd = 1.25, .budget_usd = 1.0},
  });
  REQUIRE(budget.at("type") == "budget_warning");
  REQUIRE(budget.at("run_id") == "run-budget");
  REQUIRE(budget.at("threshold_percent") == 100);
  REQUIRE(budget.at("spent_usd") == 1.25);
  REQUIRE(budget.at("current_cost_usd") == 1.25);
  REQUIRE(budget.at("budget_usd") == 1.0);
  REQUIRE(budget.at("max_budget_usd") == 1.0);

  const auto compacted = ava::app::headless_event_to_ndjson(ava::agent::AgentEvent{
      .kind = ava::agent::AgentEventKind::ContextCompacted,
      .run_id = "run-compact",
      .turn = 4,
      .compacted_message_count = 7,
      .compacted_token_estimate = 1234,
  });
  REQUIRE(compacted.at("type") == "context_compacted");
  REQUIRE(compacted.at("run_id") == "run-compact");
  REQUIRE(compacted.at("message_count") == 7);
  REQUIRE(compacted.at("estimated_tokens") == 1234);

  const auto complete = ava::app::headless_event_to_ndjson(ava::agent::AgentEvent{
      .kind = ava::agent::AgentEventKind::Completion,
      .run_id = "run-budget",
      .turn = 5,
      .message = "budget exhausted",
      .completion_reason = ava::agent::AgentCompletionReason::BudgetExceeded,
  });
  REQUIRE(complete.at("type") == "complete");
  REQUIRE(complete.at("reason") == "budget_exceeded");

  const auto checkpoint = ava::app::headless_event_to_ndjson(ava::agent::AgentEvent{
      .kind = ava::agent::AgentEventKind::Checkpoint,
      .run_id = "run-checkpoint",
      .turn = 6,
      .message = "checkpoint after tool result",
  });
  REQUIRE(checkpoint.at("type") == "checkpoint");
  REQUIRE(checkpoint.at("run_id") == "run-checkpoint");
  REQUIRE(checkpoint.at("turn") == 6);
  REQUIRE(checkpoint.at("message") == "checkpoint after tool result");
}

TEST_CASE("ndjson tool call and result correlate call_id", "[ava_app]") {
  const auto tool_call = ava::app::headless_event_to_ndjson(ava::agent::AgentEvent{
      .kind = ava::agent::AgentEventKind::ToolCall,
      .run_id = "run-2",
      .turn = 3,
      .tool_call = ava::types::ToolCall{.id = "call-correlated", .name = "read", .arguments = nlohmann::json{{"path", "README.md"}}},
  });
  const auto tool_result = ava::app::headless_event_to_ndjson(ava::agent::AgentEvent{
      .kind = ava::agent::AgentEventKind::ToolResult,
      .run_id = "run-2",
      .turn = 3,
      .tool_result = ava::types::ToolResult{.call_id = "call-correlated", .content = "ok", .is_error = false},
  });

  REQUIRE(tool_call.at("type") == "tool_call");
  REQUIRE(tool_result.at("type") == "tool_result");
  REQUIRE(tool_call.at("run_id") == tool_result.at("run_id"));
  REQUIRE(tool_call.at("call_id") == tool_result.at("call_id"));
  REQUIRE(tool_result.at("is_error") == false);
}

TEST_CASE("ndjson subagent complete event emits canonical fields", "[ava_app]") {
  const auto event = ava::app::headless_event_to_ndjson(ava::agent::AgentEvent{
      .kind = ava::agent::AgentEventKind::SubagentComplete,
      .run_id = "parent-run-1",
      .subagent_call_id = "call-subagent-1",
      .subagent_session_id = "child-session-1",
      .subagent_description = "Review the parser changes",
      .subagent_message_count = 4,
  });

  REQUIRE(event.at("type") == "subagent_complete");
  REQUIRE(event.at("run_id") == "parent-run-1");
  REQUIRE(event.at("call_id") == "call-subagent-1");
  REQUIRE(event.at("session_id") == "child-session-1");
  REQUIRE(event.at("description") == "Review the parser changes");
  REQUIRE(event.at("message_count") == 4);
}

TEST_CASE("ndjson malformed subagent complete event emits canonical error", "[ava_app]") {
  const auto event = ava::app::headless_event_to_ndjson(ava::agent::AgentEvent{
      .kind = ava::agent::AgentEventKind::SubagentComplete,
      .run_id = "parent-run-1",
      .subagent_session_id = "child-session-1",
      .subagent_description = "Review the parser changes",
  });

  REQUIRE(event.at("type") == "error");
  REQUIRE(event.at("run_id") == "parent-run-1");
  REQUIRE(event.at("message") == "malformed subagent_complete event: missing required canonical field: call_id");
  REQUIRE_FALSE(event.contains("call_id"));
}

TEST_CASE("ndjson malformed subagent complete event preserves canonical error run id", "[ava_app]") {
  const auto event = ava::app::headless_event_to_ndjson(ava::agent::AgentEvent{
      .kind = ava::agent::AgentEventKind::SubagentComplete,
      .subagent_call_id = "call-subagent-1",
      .subagent_session_id = "child-session-1",
      .subagent_description = "Review the parser changes",
  });

  REQUIRE(event.at("type") == "error");
  REQUIRE(event.at("run_id") == "unknown");
  REQUIRE(event.at("message") == "malformed subagent_complete event: missing required canonical field: run_id");
  REQUIRE_FALSE(event.contains("call_id"));
}

TEST_CASE("ndjson blank subagent complete fields are malformed", "[ava_app]") {
  const auto event = ava::app::headless_event_to_ndjson(ava::agent::AgentEvent{
      .kind = ava::agent::AgentEventKind::SubagentComplete,
      .run_id = "parent-run-1",
      .subagent_call_id = "   ",
      .subagent_session_id = "child-session-1",
      .subagent_description = "Review the parser changes",
  });

  REQUIRE(event.at("type") == "error");
  REQUIRE(event.at("run_id") == "parent-run-1");
  REQUIRE(event.at("message") == "malformed subagent_complete event: missing required canonical field: call_id");
  REQUIRE_FALSE(event.contains("call_id"));
}

TEST_CASE("ndjson subagent complete omits optional message count when absent", "[ava_app]") {
  const auto event = ava::app::headless_event_to_ndjson(ava::agent::AgentEvent{
      .kind = ava::agent::AgentEventKind::SubagentComplete,
      .run_id = "parent-run-1",
      .subagent_call_id = "call-subagent-1",
      .subagent_session_id = "child-session-1",
      .subagent_description = "Review the parser changes",
  });

  REQUIRE(event.at("type") == "subagent_complete");
  REQUIRE_FALSE(event.contains("message_count"));
}

TEST_CASE("text subagent complete event prints stable label", "[ava_app]") {
  std::ostringstream output;
  auto* previous = std::cout.rdbuf(output.rdbuf());

  ava::app::print_headless_event_text(ava::agent::AgentEvent{
      .kind = ava::agent::AgentEventKind::SubagentComplete,
      .subagent_description = "Review the parser changes",
  });
  ava::app::print_headless_event_text(ava::agent::AgentEvent{.kind = ava::agent::AgentEventKind::SubagentComplete});

  std::cout.rdbuf(previous);
  REQUIRE(output.str() == "[subagent_complete] Review the parser changes\n[subagent_complete]\n");
}

TEST_CASE("text budget and checkpoint events include actionable details", "[ava_app]") {
  std::ostringstream output;
  std::ostringstream error;
  auto* previous_out = std::cout.rdbuf(output.rdbuf());
  auto* previous_err = std::cerr.rdbuf(error.rdbuf());

  ava::app::print_headless_event_text(ava::agent::AgentEvent{
      .kind = ava::agent::AgentEventKind::ContextCompacted,
      .compacted_message_count = 3,
      .compacted_token_estimate = 99,
  });
  ava::app::print_headless_event_text(ava::agent::AgentEvent{
      .kind = ava::agent::AgentEventKind::Completion,
      .run_id = "run-text",
      .completion_reason = ava::agent::AgentCompletionReason::BudgetExceeded,
  });
  ava::app::print_headless_event_text(ava::agent::AgentEvent{
      .kind = ava::agent::AgentEventKind::Error,
      .run_id = "run-text",
      .message = "boom",
  });
  ava::app::print_headless_event_text(ava::agent::AgentEvent{
      .kind = ava::agent::AgentEventKind::Checkpoint,
      .message = "checkpoint after tool result",
  });

  std::cout.rdbuf(previous_out);
  std::cerr.rdbuf(previous_err);
  REQUIRE(output.str().find("[context] compacted messages=3 tokens~=99") != std::string::npos);
  REQUIRE(output.str().find("[completion] reason=budget_exceeded run=run-text") != std::string::npos);
  REQUIRE(output.str().find("[checkpoint] checkpoint after tool result") != std::string::npos);
  REQUIRE(error.str().find("[error] run=run-text boom") != std::string::npos);
}

TEST_CASE("ndjson error tool result preserves call_id", "[ava_app]") {
  const auto tool_result = ava::app::headless_event_to_ndjson(ava::agent::AgentEvent{
      .kind = ava::agent::AgentEventKind::ToolResult,
      .run_id = "run-error",
      .turn = 4,
      .tool_result = ava::types::ToolResult{.call_id = "call-error", .content = "permission denied", .is_error = true},
  });

  REQUIRE(tool_result.at("type") == "tool_result");
  REQUIRE(tool_result.at("run_id") == "run-error");
  REQUIRE(tool_result.at("call_id") == "call-error");
  REQUIRE(tool_result.at("is_error") == true);
}
