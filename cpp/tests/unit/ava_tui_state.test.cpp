#include <catch2/catch_test_macros.hpp>
#include <CLI/CLI.hpp>
#include <nlohmann/json.hpp>

#include "options.hpp"
#include "state.hpp"

namespace {

[[nodiscard]] std::vector<char*> argv_for(std::vector<std::string>& args) {
  std::vector<char*> argv;
  argv.reserve(args.size());
  for(auto& arg : args) {
    argv.push_back(arg.data());
  }
  return argv;
}

}  // namespace

TEST_CASE("tui state submission appends user message and enters running", "[ava_tui]") {
  ava::tui::AppState state;

  state.insert_text("  write a summary  ");
  const auto submission = state.take_submission();

  REQUIRE(submission.has_value());
  REQUIRE(*submission == "write a summary");
  REQUIRE(state.running());
  REQUIRE(state.messages().size() == 1);
  REQUIRE(state.messages().front().kind == ava::tui::MessageKind::User);
  REQUIRE(state.messages().front().text == "write a summary");
}

TEST_CASE("tui state empty or whitespace submission is rejected", "[ava_tui]") {
  ava::tui::AppState state;

  state.insert_text("   \n\t ");
  const auto submission = state.take_submission();

  REQUIRE_FALSE(submission.has_value());
  REQUIRE_FALSE(state.running());
  REQUIRE(state.input_buffer().empty());
  REQUIRE(state.messages().empty());
  REQUIRE(state.status_line() == "Input is empty.");
}

TEST_CASE("tui state backspace is safe on empty buffer", "[ava_tui]") {
  ava::tui::AppState state;

  state.backspace();
  REQUIRE(state.input_buffer().empty());

  state.insert_text("x");
  state.backspace();
  state.backspace();
  REQUIRE(state.input_buffer().empty());
}

TEST_CASE("tui state submission keeps multiline body but trims trailing newline", "[ava_tui]") {
  ava::tui::AppState state;

  state.insert_text("  first line\nsecond line\n");
  const auto submission = state.take_submission();

  REQUIRE(submission.has_value());
  REQUIRE(*submission == "first line\nsecond line");
  REQUIRE(state.messages().size() == 2);
  REQUIRE(state.messages().at(0).kind == ava::tui::MessageKind::User);
  REQUIRE(state.messages().at(0).text == "first line");
  REQUIRE(state.messages().at(1).kind == ava::tui::MessageKind::User);
  REQUIRE(state.messages().at(1).text == "second line");
}

TEST_CASE("tui state scroll is clamped to message bounds", "[ava_tui]") {
  ava::tui::AppState state;
  state.set_viewport_rows(2);

  for(int index = 0; index < 5; ++index) {
    state.insert_text("msg");
    (void) state.take_submission();
    state.apply_agent_event(ava::agent::AgentEvent{
        .kind = ava::agent::AgentEventKind::Completion,
        .turn = 1,
        .message = "done",
        .completion_reason = ava::agent::AgentCompletionReason::Completed,
    });
  }

  REQUIRE(state.scroll_offset() == 3);
  state.scroll_down(10);
  REQUIRE(state.scroll_offset() == 3);
  state.scroll_up(100);
  REQUIRE(state.scroll_offset() == 0);
}

TEST_CASE("tui state page scroll uses viewport and clamps", "[ava_tui]") {
  ava::tui::AppState state;
  state.set_viewport_rows(2);

  for(int index = 0; index < 6; ++index) {
    state.insert_text("msg");
    (void) state.take_submission();
    state.apply_agent_event(ava::agent::AgentEvent{
        .kind = ava::agent::AgentEventKind::Completion,
        .turn = 1,
        .message = "done",
        .completion_reason = ava::agent::AgentCompletionReason::Completed,
    });
  }

  REQUIRE(state.scroll_offset() == 4);
  state.page_up();
  REQUIRE(state.scroll_offset() == 2);
  state.page_up();
  REQUIRE(state.scroll_offset() == 0);
  state.page_up();
  REQUIRE(state.scroll_offset() == 0);

  state.page_down();
  REQUIRE(state.scroll_offset() == 2);
  state.page_down();
  REQUIRE(state.scroll_offset() == 4);
  state.page_down();
  REQUIRE(state.scroll_offset() == 4);
}

TEST_CASE("tui state maps runtime events into visible assistant output and status", "[ava_tui]") {
  ava::tui::AppState state;
  state.set_running(true);

  state.apply_agent_event(ava::agent::AgentEvent{
      .kind = ava::agent::AgentEventKind::AssistantResponse,
      .turn = 1,
      .message = "hello from assistant",
  });
  state.apply_agent_event(ava::agent::AgentEvent{
      .kind = ava::agent::AgentEventKind::Completion,
      .turn = 1,
      .message = "hello from assistant",
      .completion_reason = ava::agent::AgentCompletionReason::Completed,
  });

  REQUIRE_FALSE(state.running());
  REQUIRE(state.status_line() == "Run complete: completed");
  REQUIRE(state.messages().size() == 1);
  REQUIRE(state.messages().front().kind == ava::tui::MessageKind::Assistant);
  REQUIRE(state.messages().front().text == "hello from assistant");
}

TEST_CASE("tui state maps streaming assistant deltas and cancelled completion", "[ava_tui]") {
  ava::tui::AppState state;
  state.set_running(true);

  state.apply_agent_event(ava::agent::AgentEvent{
      .kind = ava::agent::AgentEventKind::AssistantResponseDelta,
      .turn = 1,
      .message = "par",
  });
  state.apply_agent_event(ava::agent::AgentEvent{
      .kind = ava::agent::AgentEventKind::AssistantResponseDelta,
      .turn = 1,
      .message = "tial",
  });
  state.apply_agent_event(ava::agent::AgentEvent{
      .kind = ava::agent::AgentEventKind::Completion,
      .turn = 1,
      .message = "agent run cancelled",
      .completion_reason = ava::agent::AgentCompletionReason::Cancelled,
  });

  REQUIRE_FALSE(state.running());
  REQUIRE(state.status_line() == "Run complete: cancelled");
  REQUIRE(state.messages().size() == 1);
  REQUIRE(state.messages().front().text == "partial");
}

TEST_CASE("tui state starts a new assistant message for new delta sequence", "[ava_tui]") {
  ava::tui::AppState state;

  state.apply_agent_event(ava::agent::AgentEvent{.kind = ava::agent::AgentEventKind::TurnStarted, .turn = 1});
  state.apply_agent_event(ava::agent::AgentEvent{.kind = ava::agent::AgentEventKind::AssistantResponseDelta, .turn = 1, .message = "one"});
  state.apply_agent_event(ava::agent::AgentEvent{
      .kind = ava::agent::AgentEventKind::Completion,
      .turn = 1,
      .message = "done",
      .completion_reason = ava::agent::AgentCompletionReason::Completed,
  });

  state.apply_agent_event(ava::agent::AgentEvent{.kind = ava::agent::AgentEventKind::TurnStarted, .turn = 2});
  state.apply_agent_event(ava::agent::AgentEvent{.kind = ava::agent::AgentEventKind::AssistantResponseDelta, .turn = 2, .message = "two"});

  REQUIRE(state.messages().size() == 2);
  REQUIRE(state.messages().at(0).text == "one");
  REQUIRE(state.messages().at(1).text == "two");
}

TEST_CASE("tui state maps run/tool/error events into status and message log", "[ava_tui]") {
  ava::tui::AppState state;

  state.apply_agent_event(ava::agent::AgentEvent{
      .kind = ava::agent::AgentEventKind::RunStarted,
      .turn = 0,
      .message = "agent run started",
  });
  REQUIRE(state.running());
  REQUIRE(state.status_line() == "agent run started");

  state.apply_agent_event(ava::agent::AgentEvent{
      .kind = ava::agent::AgentEventKind::TurnStarted,
      .turn = 3,
  });
  REQUIRE(state.status_line() == "Turn 3 started.");

  state.apply_agent_event(ava::agent::AgentEvent{
      .kind = ava::agent::AgentEventKind::ToolCall,
      .turn = 3,
      .tool_call = ava::types::ToolCall{.id = "call-1", .name = "grep", .arguments = nlohmann::json::object()},
  });
  REQUIRE(state.messages().size() == 1);
  REQUIRE(state.messages().at(0).kind == ava::tui::MessageKind::System);
  REQUIRE(state.messages().at(0).text == "tool_call: grep");
  REQUIRE(state.status_line() == "Running tool: grep");

  state.apply_agent_event(ava::agent::AgentEvent{
      .kind = ava::agent::AgentEventKind::ToolResult,
      .turn = 3,
      .tool_result = ava::types::ToolResult{.call_id = "call-1", .content = "ok", .is_error = false},
  });
  REQUIRE(state.messages().size() == 2);
  REQUIRE(state.messages().at(1).kind == ava::tui::MessageKind::System);
  REQUIRE(state.messages().at(1).text == "tool_result[call-1]: ok");
  REQUIRE(state.status_line() == "Tool completed: ok.");

  state.apply_agent_event(ava::agent::AgentEvent{
      .kind = ava::agent::AgentEventKind::ToolResult,
      .turn = 3,
      .tool_result = ava::types::ToolResult{.call_id = "", .content = "permission denied", .is_error = true},
  });
  REQUIRE(state.messages().size() == 3);
  REQUIRE(state.messages().at(2).kind == ava::tui::MessageKind::Error);
  REQUIRE(state.messages().at(2).text == "tool_result: error");
  REQUIRE(state.status_line() == "Tool completed: error.");

  state.apply_agent_event(ava::agent::AgentEvent{
      .kind = ava::agent::AgentEventKind::Error,
      .turn = 3,
      .message = "provider failed",
  });
  REQUIRE_FALSE(state.running());
  REQUIRE(state.status_line() == "Run error: provider failed");
  REQUIRE(state.messages().size() == 4);
  REQUIRE(state.messages().at(3).kind == ava::tui::MessageKind::Error);
  REQUIRE(state.messages().at(3).text == "provider failed");
}

TEST_CASE("tui state slash commands provide help/clear/model and unsupported compact handling", "[ava_tui]") {
  ava::tui::AppState state;
  state.set_model_identity("openai", "gpt-5.4");

  state.insert_text("/help");
  REQUIRE_FALSE(state.take_submission().has_value());
  REQUIRE_FALSE(state.messages().empty());
  REQUIRE(state.messages().at(0).text == "Slash commands:");

  state.insert_text("/model");
  REQUIRE_FALSE(state.take_submission().has_value());
  REQUIRE(state.status_line() == "Current model: openai/gpt-5.4");

  state.insert_text("/compact");
  REQUIRE_FALSE(state.take_submission().has_value());
  REQUIRE(state.status_line() == "Unsupported command: /compact");

  state.insert_text("/clear");
  REQUIRE_FALSE(state.take_submission().has_value());
  REQUIRE(state.messages().empty());
  REQUIRE(state.status_line() == "Cleared message log.");
}

TEST_CASE("tui state keeps input history and restores draft with up/down", "[ava_tui]") {
  ava::tui::AppState state;

  state.insert_text("first");
  REQUIRE(state.take_submission().has_value());
  state.apply_agent_event(ava::agent::AgentEvent{
      .kind = ava::agent::AgentEventKind::Completion,
      .turn = 1,
      .completion_reason = ava::agent::AgentCompletionReason::Completed,
  });

  state.insert_text("second");
  REQUIRE(state.take_submission().has_value());
  state.apply_agent_event(ava::agent::AgentEvent{
      .kind = ava::agent::AgentEventKind::Completion,
      .turn = 2,
      .completion_reason = ava::agent::AgentCompletionReason::Completed,
  });

  state.insert_text("draft");
  REQUIRE(state.history_previous());
  REQUIRE(state.input_buffer() == "second");
  REQUIRE(state.history_previous());
  REQUIRE(state.input_buffer() == "first");
  REQUIRE(state.history_next());
  REQUIRE(state.input_buffer() == "second");
  REQUIRE(state.history_next());
  REQUIRE(state.input_buffer() == "draft");
}

TEST_CASE("tui state restores draft after editing recalled history", "[ava_tui]") {
  ava::tui::AppState state;

  state.insert_text("first");
  REQUIRE(state.take_submission().has_value());
  state.apply_agent_event(ava::agent::AgentEvent{
      .kind = ava::agent::AgentEventKind::Completion,
      .turn = 1,
      .completion_reason = ava::agent::AgentCompletionReason::Completed,
  });

  state.insert_text("draft");
  REQUIRE(state.history_previous());
  REQUIRE(state.input_buffer() == "first");

  state.insert_text(" edited");
  REQUIRE(state.input_buffer() == "first edited");
  REQUIRE(state.history_next());
  REQUIRE(state.input_buffer() == "draft");
}

TEST_CASE("tui state reports message navigation status and supports top/bottom jumps", "[ava_tui]") {
  ava::tui::AppState state;
  state.set_viewport_rows(2);

  for(int index = 0; index < 5; ++index) {
    state.insert_text("msg " + std::to_string(index));
    REQUIRE(state.take_submission().has_value());
    state.apply_agent_event(ava::agent::AgentEvent{
        .kind = ava::agent::AgentEventKind::Completion,
        .turn = static_cast<std::size_t>(index + 1),
        .completion_reason = ava::agent::AgentCompletionReason::Completed,
    });
  }

  REQUIRE(state.message_navigation_line() == "Messages: 4-5/5");
  state.scroll_to_top();
  REQUIRE(state.message_navigation_line() == "Messages: 1-2/5");
  state.scroll_to_bottom();
  REQUIRE(state.message_navigation_line() == "Messages: 4-5/5");
}

TEST_CASE("tui state tracks adapter-facing interactive request visibility and clearing", "[ava_tui]") {
  ava::tui::AppState state;

  state.set_interactive_request(
      ava::control_plane::InteractiveRequestKind::Approval,
      ava::control_plane::InteractiveRequestHandle{
          .request_id = "approval-1",
          .kind = ava::control_plane::InteractiveRequestKind::Approval,
          .state = ava::control_plane::InteractiveRequestState::Pending,
          .run_id = "run-1",
      }
  );
  state.set_interactive_request(
      ava::control_plane::InteractiveRequestKind::Question,
      ava::control_plane::InteractiveRequestHandle{
          .request_id = "question-1",
          .kind = ava::control_plane::InteractiveRequestKind::Question,
          .state = ava::control_plane::InteractiveRequestState::Pending,
          .run_id = "run-1",
      }
  );

  REQUIRE(state.interactive_requests().pending_count() == 2);
  REQUIRE(state.interactive_requests().approval.has_value());
  REQUIRE(state.interactive_requests().question.has_value());
  REQUIRE_FALSE(state.interactive_requests().plan.has_value());

  state.clear_interactive_requests();
  REQUIRE(state.interactive_requests().pending_count() == 0);
}

TEST_CASE("tui options parse provider model and runtime flags", "[ava_tui]") {
  std::vector<std::string> args{
      "ava_tui",
      "--provider",
      "openai",
      "--model",
      "gpt-5-mini",
      "--max-turns",
      "3",
      "--auto-approve",
  };
  auto argv = argv_for(args);

  const auto options = ava::tui::parse_tui_options_or_throw(static_cast<int>(argv.size()), argv.data());

  REQUIRE(options.provider == "openai");
  REQUIRE(options.model == "gpt-5-mini");
  REQUIRE(options.max_turns == 3);
  REQUIRE(options.max_turns_explicit);
  REQUIRE(options.auto_approve);
}

TEST_CASE("tui options reject continue and session conflict", "[ava_tui]") {
  std::vector<std::string> args{"ava_tui", "--continue", "--session", "sess_1"};
  auto argv = argv_for(args);

  REQUIRE_THROWS_AS(
      ava::tui::parse_tui_options_or_throw(static_cast<int>(argv.size()), argv.data()),
      std::invalid_argument
  );
}

TEST_CASE("tui options help exits through CLI help path", "[ava_tui]") {
  std::vector<std::string> args{"ava_tui", "--help"};
  auto argv = argv_for(args);

  REQUIRE_THROWS_AS(ava::tui::parse_tui_options_or_throw(static_cast<int>(argv.size()), argv.data()), CLI::CallForHelp);
}
