#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include "state.hpp"

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
