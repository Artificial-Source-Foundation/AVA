#include <catch2/catch_test_macros.hpp>
#include <CLI/CLI.hpp>
#include <algorithm>
#include <limits>
#include <nlohmann/json.hpp>
#include <stdexcept>

#include "ava/tools/permission_middleware.hpp"
#include "interactive_detail_projection.hpp"
#include "interactive_action_adapter.hpp"
#include "options.hpp"
#include "state.hpp"

namespace {

[[nodiscard]] std::vector<char*> argv_for(std::vector<std::string>& args) {
  std::vector<char*> argv;
  argv.reserve(args.size() + 1);
  for(auto& arg : args) {
    argv.push_back(arg.data());
  }
  argv.push_back(nullptr);
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

TEST_CASE("tui state backspace removes one utf8 codepoint", "[ava_tui]") {
  ava::tui::AppState state;

  state.insert_text("ok ☕");
  state.backspace();
  REQUIRE(state.input_buffer() == "ok ");

  state.insert_text("é");
  state.backspace();
  REQUIRE(state.input_buffer() == "ok ");

  state.insert_text("😀");
  state.backspace();
  REQUIRE(state.input_buffer() == "ok ");
}

TEST_CASE("tui state rejects submission while run is active", "[ava_tui]") {
  ava::tui::AppState state;

  state.set_running(true);
  state.insert_text("second prompt");

  const auto submission = state.take_submission();
  REQUIRE_FALSE(submission.has_value());
  REQUIRE(state.status_line() == "Run is already in progress.");
  REQUIRE(state.input_buffer() == "second prompt");

  ava::tui::AppState slash_state;
  slash_state.set_running(true);
  slash_state.insert_text("/help");
  const auto slash_submission = slash_state.take_submission();
  REQUIRE_FALSE(slash_submission.has_value());
  REQUIRE(slash_state.status_line() == "Run is already in progress.");
  REQUIRE(slash_state.messages().empty());
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

TEST_CASE("tui state scroll down saturates for very large amounts", "[ava_tui]") {
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

  state.scroll_to_top();
  REQUIRE(state.scroll_offset() == 0);

  state.scroll_down(1);
  REQUIRE(state.scroll_offset() == 1);

  state.scroll_down(std::numeric_limits<std::size_t>::max());
  REQUIRE(state.scroll_offset() == 3);

  state.scroll_down(std::numeric_limits<std::size_t>::max());
  REQUIRE(state.scroll_offset() == 3);
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

TEST_CASE("tui state projects child-run terminal metadata without owning lifecycle", "[ava_tui]") {
  ava::tui::AppState state;
  state.set_running(true);

  state.set_active_child_runs(std::vector<ava::orchestration::ChildRunInfo>{ava::orchestration::ChildRunInfo{
      .run_id = "child-run-1",
      .session_id = "child-session",
      .agent_type = "review",
      .parent_session_id = "parent-session",
      .depth = 2,
  }});

  REQUIRE(state.running());
  REQUIRE(state.child_runs().size() == 1);
  REQUIRE(state.child_runs().front().active);
  REQUIRE(state.child_runs().front().run_id == "child-run-1");

  state.record_child_run_terminal_summary(ava::orchestration::ChildRunTerminalSummary{
      .run_id = "child-run-1",
      .session_id = "child-session",
      .agent_type = "review",
      .parent_session_id = "parent-session",
      .depth = 2,
      .completion_reason = "cancelled",
      .cancelled = true,
      .watchdog_timed_out = false,
      .turns_used = 1,
      .error = "child run cancelled",
  });

  REQUIRE(state.running());
  REQUIRE(state.child_runs().size() == 1);
  REQUIRE_FALSE(state.child_runs().front().active);
  REQUIRE(state.child_runs().front().cancelled);
  REQUIRE_FALSE(state.child_runs().front().watchdog_timed_out);
  REQUIRE(state.child_runs().front().completion_reason == "cancelled");
  REQUIRE(state.child_runs().front().error == std::optional<std::string>{"child run cancelled"});
  REQUIRE(state.status_line() == "Child run review complete: cancelled");

  state.record_child_run_terminal_summary(ava::orchestration::ChildRunTerminalSummary{
      .run_id = "child-run-1",
      .session_id = "child-session",
      .agent_type = "review",
      .parent_session_id = "parent-session",
      .depth = 2,
      .completion_reason = "error",
      .cancelled = false,
      .watchdog_timed_out = true,
      .turns_used = 1,
      .error = "child run watchdog timeout",
  });
  REQUIRE(state.child_runs().front().watchdog_timed_out);
  REQUIRE(state.child_runs().front().completion_reason == "error");
  REQUIRE(state.child_runs().front().error == std::optional<std::string>{"child run watchdog timeout"});
  REQUIRE(state.status_line() == "Child run review complete: cancelled");

  state.set_active_child_runs(std::vector<ava::orchestration::ChildRunInfo>{ava::orchestration::ChildRunInfo{
      .run_id = "child-run-1",
      .session_id = "child-session",
      .agent_type = "review",
      .parent_session_id = "parent-session",
      .depth = 2,
  }});
  REQUIRE(state.child_runs().size() == 1);
  REQUIRE_FALSE(state.child_runs().front().active);

  state.record_child_run_terminal_summary(ava::orchestration::ChildRunTerminalSummary{
      .run_id = "child-run-unknown",
      .session_id = "child-session-2",
      .agent_type = "general",
      .parent_session_id = "parent-session",
      .depth = 2,
      .completion_reason = "completed",
      .cancelled = false,
      .watchdog_timed_out = false,
      .turns_used = 1,
      .error = std::nullopt,
  });
  REQUIRE(state.child_runs().size() == 2);
  REQUIRE(state.child_runs().back().run_id == "child-run-unknown");
}

TEST_CASE("tui state caps child-run observer list without dropping active runs first", "[ava_tui]") {
  ava::tui::AppState state;

  state.set_active_child_runs(std::vector<ava::orchestration::ChildRunInfo>{ava::orchestration::ChildRunInfo{
      .run_id = "active-child",
      .session_id = "child-session",
      .agent_type = "review",
      .parent_session_id = "parent-session",
      .depth = 2,
  }});

  constexpr auto kOverflowCount = 5;
  for(std::size_t index = 0; index < ava::tui::AppState::kMaxChildRuns + kOverflowCount; ++index) {
    state.record_child_run_terminal_summary(ava::orchestration::ChildRunTerminalSummary{
        .run_id = "terminal-" + std::to_string(index),
        .session_id = "child-session-" + std::to_string(index),
        .agent_type = "general",
        .parent_session_id = "parent-session",
        .depth = 2,
        .completion_reason = "completed",
        .cancelled = false,
        .watchdog_timed_out = false,
        .turns_used = 1,
        .error = std::nullopt,
    });
  }

  REQUIRE(state.child_runs().size() == ava::tui::AppState::kMaxChildRuns);
  const auto active = std::find_if(state.child_runs().begin(), state.child_runs().end(), [](const auto& child) {
    return child.run_id == "active-child";
  });
  REQUIRE(active != state.child_runs().end());
  REQUIRE(active->active);

  std::vector<ava::orchestration::ChildRunInfo> active_runs;
  for(std::size_t index = 0; index < ava::tui::AppState::kMaxChildRuns + kOverflowCount; ++index) {
    active_runs.push_back(ava::orchestration::ChildRunInfo{
        .run_id = "active-" + std::to_string(index),
        .session_id = "active-session-" + std::to_string(index),
        .agent_type = "review",
        .parent_session_id = "parent-session",
        .depth = 2,
    });
  }

  state.set_active_child_runs(active_runs);
  REQUIRE(state.child_runs().size() == ava::tui::AppState::kMaxChildRuns);
  REQUIRE(std::find_if(state.child_runs().begin(), state.child_runs().end(), [](const auto& child) {
            return child.run_id == "active-0";
          }) == state.child_runs().end());
  REQUIRE(std::find_if(state.child_runs().begin(), state.child_runs().end(), [](const auto& child) {
            return child.run_id == "active-" + std::to_string(ava::tui::AppState::kMaxChildRuns + kOverflowCount - 1);
          }) != state.child_runs().end());
}

TEST_CASE("tui state maps subagent complete into visible status", "[ava_tui]") {
  ava::tui::AppState state;

  state.apply_agent_event(ava::agent::AgentEvent{
      .kind = ava::agent::AgentEventKind::SubagentComplete,
      .subagent_description = "Review the parser changes",
  });

  REQUIRE(state.status_line() == "Subagent complete.");
  REQUIRE(state.messages().size() == 1);
  REQUIRE(state.messages().front().kind == ava::tui::MessageKind::System);
  REQUIRE(state.messages().front().text == "subagent_complete: Review the parser changes");

  state.apply_agent_event(ava::agent::AgentEvent{.kind = ava::agent::AgentEventKind::SubagentComplete});

  REQUIRE(state.status_line() == "Subagent complete.");
  REQUIRE(state.messages().size() == 1);
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

  state.insert_text("/model anthropic/claude-opus-4-6");
  REQUIRE_FALSE(state.take_submission().has_value());
  REQUIRE(state.status_line() == "Unsupported /model arguments: model switching is not implemented yet.");

  state.insert_text("/compact");
  REQUIRE_FALSE(state.take_submission().has_value());
  REQUIRE(state.status_line() == "Unsupported command: /compact");

  state.insert_text("/unknown");
  REQUIRE_FALSE(state.take_submission().has_value());
  REQUIRE_FALSE(state.messages().empty());
  REQUIRE(state.messages().back().kind == ava::tui::MessageKind::Error);
  REQUIRE(state.messages().back().text == "Unsupported slash command: /unknown");
  REQUIRE(state.status_line() == "Unsupported slash command: /unknown");

  state.set_interactive_request(
      ava::control_plane::InteractiveRequestKind::Approval,
      ava::control_plane::InteractiveRequestHandle{
          .request_id = "approval-clear",
          .kind = ava::control_plane::InteractiveRequestKind::Approval,
          .state = ava::control_plane::InteractiveRequestState::Pending,
      }
  );
  REQUIRE(state.active_interactive_dock().has_value());

  state.insert_text("/clear");
  REQUIRE_FALSE(state.take_submission().has_value());
  REQUIRE(state.messages().empty());
  REQUIRE(state.scroll_offset() == 0);
  REQUIRE(state.interactive_requests().pending_count() == 0);
  REQUIRE_FALSE(state.active_interactive_dock().has_value());
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
  REQUIRE_FALSE(state.history_previous());
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

TEST_CASE("tui state editing recalled history resets traversal", "[ava_tui]") {
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

  REQUIRE(state.history_previous());
  REQUIRE(state.input_buffer() == "second");
  state.backspace();
  REQUIRE(state.input_buffer() == "secon");
  REQUIRE(state.history_previous());
  REQUIRE(state.input_buffer() == "second");
}

TEST_CASE("tui state reports message navigation status and supports top/bottom jumps", "[ava_tui]") {
  ava::tui::AppState state;
  REQUIRE(state.message_navigation_line() == "Messages: 0/0");
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

TEST_CASE("tui state renders assistant deltas with newline splits", "[ava_tui]") {
  ava::tui::AppState state;

  state.apply_agent_event(ava::agent::AgentEvent{
      .kind = ava::agent::AgentEventKind::AssistantResponseDelta,
      .message = "first\nsecond",
  });

  REQUIRE(state.messages().size() == 2);
  REQUIRE(state.messages().at(0).kind == ava::tui::MessageKind::Assistant);
  REQUIRE(state.messages().at(0).text == "first");
  REQUIRE(state.messages().at(1).text == "second");
}

TEST_CASE("tui state visible messages follow viewport and scroll offset", "[ava_tui]") {
  ava::tui::AppState state;
  REQUIRE(state.visible_messages().empty());

  state.set_viewport_rows(2);
  for(int index = 0; index < 4; ++index) {
    state.insert_text("msg " + std::to_string(index));
    REQUIRE(state.take_submission().has_value());
    state.apply_agent_event(ava::agent::AgentEvent{
        .kind = ava::agent::AgentEventKind::Completion,
        .turn = static_cast<std::size_t>(index + 1),
        .completion_reason = ava::agent::AgentCompletionReason::Completed,
    });
  }

  auto visible = state.visible_messages();
  REQUIRE(visible.size() == 2);
  REQUIRE(visible.at(0).text == "msg 2");
  REQUIRE(visible.at(1).text == "msg 3");

  state.scroll_to_top();
  visible = state.visible_messages();
  REQUIRE(visible.size() == 2);
  REQUIRE(visible.at(0).text == "msg 0");
  REQUIRE(visible.at(1).text == "msg 1");

  state.set_viewport_rows(10);
  visible = state.visible_messages();
  REQUIRE(visible.size() == 4);
  REQUIRE(state.scroll_offset() == 0);
}

TEST_CASE("tui state quit request is tracked", "[ava_tui]") {
  ava::tui::AppState state;

  REQUIRE_FALSE(state.quit_requested());
  state.request_quit();
  REQUIRE(state.quit_requested());
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

TEST_CASE("tui state opens interactive dock for pending requests by priority", "[ava_tui]") {
  ava::tui::AppState state;

  state.set_interactive_request(
      ava::control_plane::InteractiveRequestKind::Question,
      ava::control_plane::InteractiveRequestHandle{
          .request_id = "question-1",
          .kind = ava::control_plane::InteractiveRequestKind::Question,
          .state = ava::control_plane::InteractiveRequestState::Pending,
          .run_id = "run-1",
      }
  );

  REQUIRE(state.active_interactive_dock().has_value());
  REQUIRE(state.active_interactive_dock()->kind == ava::tui::InteractiveDockKind::Question);
  REQUIRE(state.active_interactive_dock()->request.request_id == "question-1");

  state.set_interactive_request(
      ava::control_plane::InteractiveRequestKind::Approval,
      ava::control_plane::InteractiveRequestHandle{
          .request_id = "approval-1",
          .kind = ava::control_plane::InteractiveRequestKind::Approval,
          .state = ava::control_plane::InteractiveRequestState::Pending,
          .run_id = "run-1",
      }
  );

  REQUIRE(state.active_interactive_dock().has_value());
  REQUIRE(state.active_interactive_dock()->kind == ava::tui::InteractiveDockKind::Question);

  state.set_interactive_request(ava::control_plane::InteractiveRequestKind::Question, std::nullopt);
  REQUIRE(state.active_interactive_dock().has_value());
  REQUIRE(state.active_interactive_dock()->kind == ava::tui::InteractiveDockKind::Approval);
  REQUIRE(state.active_interactive_dock()->request.request_id == "approval-1");

  ava::tui::AppState approval_state;
  approval_state.set_interactive_request(
      ava::control_plane::InteractiveRequestKind::Approval,
      ava::control_plane::InteractiveRequestHandle{
          .request_id = "approval-current",
          .kind = ava::control_plane::InteractiveRequestKind::Approval,
          .state = ava::control_plane::InteractiveRequestState::Pending,
      }
  );
  approval_state.set_interactive_request(
      ava::control_plane::InteractiveRequestKind::Question,
      ava::control_plane::InteractiveRequestHandle{
          .request_id = "question-later",
          .kind = ava::control_plane::InteractiveRequestKind::Question,
          .state = ava::control_plane::InteractiveRequestState::Pending,
      }
  );

  REQUIRE(approval_state.active_interactive_dock().has_value());
  REQUIRE(approval_state.active_interactive_dock()->kind == ava::tui::InteractiveDockKind::Approval);
  REQUIRE(approval_state.active_interactive_dock()->request.request_id == "approval-current");
}

TEST_CASE("tui detail projection truncates safely and gates incomplete approvals", "[ava_tui]") {
  const auto complete = ava::tui::approval_detail_projection(ava::orchestration::ApprovalRequestPayload{
      .call = ava::types::ToolCall{.id = "call-1", .name = "read", .arguments = nlohmann::json{{"path", "README.md"}}},
      .inspection = ava::tools::PermissionInspection{.action = ava::tools::PermissionAction::Ask, .reason = "inspect", .risk_level = "low"},
  });
  REQUIRE(complete.complete);
  REQUIRE(complete.lines.back() == "approval detail complete");

  const auto truncated = ava::tui::approval_detail_projection(ava::orchestration::ApprovalRequestPayload{
      .call = ava::types::ToolCall{.id = "call-2", .name = std::string(121, 'x'), .arguments = nlohmann::json::object()},
      .inspection = ava::tools::PermissionInspection{.action = ava::tools::PermissionAction::Ask, .reason = "inspect", .risk_level = "low"},
  });
  REQUIRE_FALSE(truncated.complete);
  REQUIRE(truncated.lines.at(0).find("tool_preview(truncated): ") == 0);
  REQUIRE(truncated.lines.back().find("approval disabled") != std::string::npos);

  const std::string utf8 = "prefix 😀 suffix";
  const auto utf8_truncated = ava::tui::truncate_for_dock(utf8, 9);
  REQUIRE(utf8_truncated == "prefix ...");
}

TEST_CASE("tui state ignores terminal interactive handles and renders request details", "[ava_tui]") {
  ava::tui::AppState state;

  state.set_interactive_request(
      ava::control_plane::InteractiveRequestKind::Approval,
      ava::control_plane::InteractiveRequestHandle{
          .request_id = "approval-resolved",
          .kind = ava::control_plane::InteractiveRequestKind::Approval,
          .state = ava::control_plane::InteractiveRequestState::Resolved,
      }
  );
  REQUIRE_FALSE(state.active_interactive_dock().has_value());
  REQUIRE_FALSE(state.interactive_requests().approval.has_value());

  state.set_interactive_request(
      ava::control_plane::InteractiveRequestKind::Question,
      ava::control_plane::InteractiveRequestHandle{
          .request_id = "approval-as-question",
          .kind = ava::control_plane::InteractiveRequestKind::Approval,
          .state = ava::control_plane::InteractiveRequestState::Pending,
      }
  );
  REQUIRE_FALSE(state.active_interactive_dock().has_value());
  REQUIRE_FALSE(state.interactive_requests().question.has_value());

  state.set_interactive_request(
      ava::control_plane::InteractiveRequestKind::Approval,
      ava::control_plane::InteractiveRequestHandle{
          .request_id = "",
          .kind = ava::control_plane::InteractiveRequestKind::Approval,
          .state = ava::control_plane::InteractiveRequestState::Pending,
      }
  );
  REQUIRE_FALSE(state.active_interactive_dock().has_value());
  REQUIRE_FALSE(state.interactive_requests().approval.has_value());

  state.set_interactive_request(
      ava::control_plane::InteractiveRequestKind::Approval,
      ava::control_plane::InteractiveRequestHandle{
          .request_id = "approval-1",
          .kind = ava::control_plane::InteractiveRequestKind::Approval,
          .state = ava::control_plane::InteractiveRequestState::Pending,
      }
  );
  state.set_interactive_request_details("approval-1", {"tool: bash", "risk: high"});

  REQUIRE(state.active_interactive_dock().has_value());
  REQUIRE(state.active_interactive_dock()->detail_lines.size() == 2);
  REQUIRE(state.active_interactive_dock()->detail_lines.at(0) == "tool: bash");
  REQUIRE_FALSE(state.approve_interactive_dock_action().has_value());

  state.set_interactive_approval_can_approve("approval-1", true);
  REQUIRE(state.approve_interactive_dock_action().has_value());

  state.set_interactive_approval_can_approve("approval-1", false);
  REQUIRE_FALSE(state.approve_interactive_dock_action().has_value());
}

TEST_CASE("tui state clears stale interactive dock metadata when requests disappear", "[ava_tui]") {
  ava::tui::AppState state;

  state.set_interactive_request(
      ava::control_plane::InteractiveRequestKind::Approval,
      ava::control_plane::InteractiveRequestHandle{
          .request_id = "approval-1",
          .kind = ava::control_plane::InteractiveRequestKind::Approval,
          .state = ava::control_plane::InteractiveRequestState::Pending,
      }
  );
  state.set_interactive_request_details("approval-1", {"stale detail"});
  state.set_interactive_approval_can_approve("approval-1", true);
  REQUIRE(state.approve_interactive_dock_action().has_value());

  state.set_interactive_request(ava::control_plane::InteractiveRequestKind::Approval, std::nullopt);
  state.set_interactive_request(
      ava::control_plane::InteractiveRequestKind::Approval,
      ava::control_plane::InteractiveRequestHandle{
          .request_id = "approval-1",
          .kind = ava::control_plane::InteractiveRequestKind::Approval,
          .state = ava::control_plane::InteractiveRequestState::Pending,
      }
  );

  REQUIRE(state.active_interactive_dock().has_value());
  REQUIRE(state.active_interactive_dock()->detail_lines.empty());
  REQUIRE_FALSE(state.approve_interactive_dock_action().has_value());
}

TEST_CASE("tui state builds dock actions for approval question and plan", "[ava_tui]") {
  ava::tui::AppState state;

  state.set_interactive_request(
      ava::control_plane::InteractiveRequestKind::Approval,
      ava::control_plane::InteractiveRequestHandle{
          .request_id = "approval-1",
          .kind = ava::control_plane::InteractiveRequestKind::Approval,
          .state = ava::control_plane::InteractiveRequestState::Pending,
      }
  );
  state.set_interactive_approval_can_approve("approval-1", true);
  const auto approve = state.approve_interactive_dock_action();
  REQUIRE(approve.has_value());
  REQUIRE(approve->kind == ava::tui::InteractiveAdapterActionKind::Approve);
  REQUIRE(approve->request_id == "approval-1");

  const auto reject = state.reject_interactive_dock_action("no");
  REQUIRE(reject.has_value());
  REQUIRE(reject->kind == ava::tui::InteractiveAdapterActionKind::Reject);
  REQUIRE(reject->value == std::optional<std::string>{"no"});

  state.set_interactive_request(ava::control_plane::InteractiveRequestKind::Approval, std::nullopt);
  state.set_interactive_request(
      ava::control_plane::InteractiveRequestKind::Question,
      ava::control_plane::InteractiveRequestHandle{
          .request_id = "question-1",
          .kind = ava::control_plane::InteractiveRequestKind::Question,
          .state = ava::control_plane::InteractiveRequestState::Pending,
      }
  );
  state.insert_interactive_answer_text("y");
  state.insert_interactive_answer_text("es");
  state.insert_interactive_answer_text("☕");
  state.backspace_interactive_answer();
  const auto answer = state.answer_interactive_dock_action();
  REQUIRE(answer.has_value());
  REQUIRE(answer->kind == ava::tui::InteractiveAdapterActionKind::Answer);
  REQUIRE(answer->request_id == "question-1");
  REQUIRE(answer->value == std::optional<std::string>{"yes"});

  ava::tui::AppState empty_answer_state;
  empty_answer_state.set_interactive_request(
      ava::control_plane::InteractiveRequestKind::Question,
      ava::control_plane::InteractiveRequestHandle{
          .request_id = "question-empty",
          .kind = ava::control_plane::InteractiveRequestKind::Question,
          .state = ava::control_plane::InteractiveRequestState::Pending,
      }
  );
  const auto empty_answer = empty_answer_state.answer_interactive_dock_action();
  REQUIRE(empty_answer.has_value());
  REQUIRE(empty_answer->value == std::optional<std::string>{""});

  state.set_interactive_request(ava::control_plane::InteractiveRequestKind::Question, std::nullopt);
  state.set_interactive_request(
      ava::control_plane::InteractiveRequestKind::Plan,
      ava::control_plane::InteractiveRequestHandle{
          .request_id = "plan-1",
          .kind = ava::control_plane::InteractiveRequestKind::Plan,
          .state = ava::control_plane::InteractiveRequestState::Pending,
      }
  );
  const auto accept = state.accept_plan_interactive_dock_action();
  REQUIRE(accept.has_value());
  REQUIRE(accept->kind == ava::tui::InteractiveAdapterActionKind::AcceptPlan);
  REQUIRE(accept->request_id == "plan-1");

  const auto reject_plan = state.reject_interactive_dock_action("not now");
  REQUIRE(reject_plan.has_value());
  REQUIRE(reject_plan->kind == ava::tui::InteractiveAdapterActionKind::RejectPlan);
  REQUIRE(reject_plan->request_id == "plan-1");
  REQUIRE_FALSE(reject_plan->value.has_value());
}

TEST_CASE("tui state applies dock adapter result without clearing backend-owned visibility", "[ava_tui]") {
  ava::tui::AppState state;
  state.set_interactive_request(
      ava::control_plane::InteractiveRequestKind::Approval,
      ava::control_plane::InteractiveRequestHandle{
          .request_id = "approval-1",
          .kind = ava::control_plane::InteractiveRequestKind::Approval,
          .state = ava::control_plane::InteractiveRequestState::Pending,
      }
  );
  state.set_interactive_request(
      ava::control_plane::InteractiveRequestKind::Question,
      ava::control_plane::InteractiveRequestHandle{
          .request_id = "question-1",
          .kind = ava::control_plane::InteractiveRequestKind::Question,
          .state = ava::control_plane::InteractiveRequestState::Pending,
      }
  );

  state.apply_interactive_action_result(ava::tui::InteractiveAdapterActionResult{
      .accepted = true,
      .terminal_request = ava::control_plane::InteractiveRequestHandle{
          .request_id = "approval-1",
          .kind = ava::control_plane::InteractiveRequestKind::Approval,
          .state = ava::control_plane::InteractiveRequestState::Resolved,
      },
  });

  REQUIRE(state.interactive_requests().approval.has_value());
  REQUIRE(state.interactive_requests().question.has_value());
  REQUIRE(state.active_interactive_dock().has_value());
  REQUIRE(state.active_interactive_dock()->kind == ava::tui::InteractiveDockKind::Approval);
  REQUIRE(state.status_line() == "Interactive request resolved: approval-1");

  state.apply_interactive_action_result(ava::tui::InteractiveAdapterActionResult{
      .accepted = false,
      .error = "stale request",
  });
  REQUIRE(state.status_line() == "Interactive action failed: stale request");
}

TEST_CASE("tui state accepted dock result without terminal keeps backend-owned visibility", "[ava_tui]") {
  ava::tui::AppState state;
  state.set_interactive_request(
      ava::control_plane::InteractiveRequestKind::Plan,
      ava::control_plane::InteractiveRequestHandle{
          .request_id = "plan-1",
          .kind = ava::control_plane::InteractiveRequestKind::Plan,
          .state = ava::control_plane::InteractiveRequestState::Pending,
      }
  );

  state.apply_interactive_action_result(ava::tui::InteractiveAdapterActionResult{.accepted = true});

  REQUIRE(state.interactive_requests().pending_count() == 1);
  REQUIRE(state.active_interactive_dock().has_value());
  REQUIRE(state.active_interactive_dock()->request.request_id == "plan-1");
  REQUIRE(state.status_line() == "Interactive request resolved.");
}

TEST_CASE("tui state can dismiss interactive dock without resolving backend request", "[ava_tui]") {
  ava::tui::AppState state;
  state.set_interactive_request(
      ava::control_plane::InteractiveRequestKind::Plan,
      ava::control_plane::InteractiveRequestHandle{
          .request_id = "plan-1",
          .kind = ava::control_plane::InteractiveRequestKind::Plan,
          .state = ava::control_plane::InteractiveRequestState::Pending,
      }
  );

  REQUIRE(state.active_interactive_dock().has_value());
  state.dismiss_interactive_dock();
  REQUIRE_FALSE(state.active_interactive_dock().has_value());
  REQUIRE(state.interactive_requests().plan.has_value());
  REQUIRE(state.status_line() == "Interactive request dock dismissed.");

  state.set_interactive_request(ava::control_plane::InteractiveRequestKind::Plan, state.interactive_requests().plan);
  REQUIRE_FALSE(state.active_interactive_dock().has_value());

  state.set_interactive_request(
      ava::control_plane::InteractiveRequestKind::Question,
      ava::control_plane::InteractiveRequestHandle{
          .request_id = "question-1",
          .kind = ava::control_plane::InteractiveRequestKind::Question,
          .state = ava::control_plane::InteractiveRequestState::Pending,
      }
  );
  REQUIRE(state.active_interactive_dock().has_value());
  REQUIRE(state.active_interactive_dock()->kind == ava::tui::InteractiveDockKind::Question);

  ava::tui::AppState priority_state;
  priority_state.set_interactive_request(
      ava::control_plane::InteractiveRequestKind::Approval,
      ava::control_plane::InteractiveRequestHandle{
          .request_id = "approval-1",
          .kind = ava::control_plane::InteractiveRequestKind::Approval,
          .state = ava::control_plane::InteractiveRequestState::Pending,
      }
  );
  priority_state.set_interactive_request(
      ava::control_plane::InteractiveRequestKind::Question,
      ava::control_plane::InteractiveRequestHandle{
          .request_id = "question-1",
          .kind = ava::control_plane::InteractiveRequestKind::Question,
          .state = ava::control_plane::InteractiveRequestState::Pending,
      }
  );
  priority_state.dismiss_interactive_dock();
  priority_state.set_interactive_request(
      ava::control_plane::InteractiveRequestKind::Question,
      priority_state.interactive_requests().question
  );
  REQUIRE(priority_state.active_interactive_dock().has_value());
  REQUIRE(priority_state.active_interactive_dock()->kind == ava::tui::InteractiveDockKind::Question);

  priority_state.set_interactive_request(ava::control_plane::InteractiveRequestKind::Approval, std::nullopt);
  priority_state.set_interactive_request(ava::control_plane::InteractiveRequestKind::Question, std::nullopt);
  priority_state.set_interactive_request(
      ava::control_plane::InteractiveRequestKind::Approval,
      ava::control_plane::InteractiveRequestHandle{
          .request_id = "approval-1",
          .kind = ava::control_plane::InteractiveRequestKind::Approval,
          .state = ava::control_plane::InteractiveRequestState::Pending,
      }
  );
  REQUIRE(priority_state.active_interactive_dock().has_value());
  REQUIRE(priority_state.active_interactive_dock()->kind == ava::tui::InteractiveDockKind::Approval);
}

TEST_CASE("tui adapter action approve resolves pending approval via bridge", "[ava_tui]") {
  auto bridge = std::make_shared<ava::orchestration::InteractiveBridge>("run-approve");
  const auto pending = bridge->register_approval_for_adapter();
  ava::tui::InteractiveActionAdapter adapter(bridge);

  const auto result = adapter.apply(ava::tui::InteractiveAdapterAction{
      .kind = ava::tui::InteractiveAdapterActionKind::Approve,
      .request_id = pending.request_id,
  });

  REQUIRE(result.accepted);
  REQUIRE(result.terminal_request.has_value());
  REQUIRE(result.terminal_request->request_id == pending.request_id);
  REQUIRE(result.terminal_request->state == ava::control_plane::InteractiveRequestState::Resolved);
  REQUIRE(result.terminal_request->run_id == std::optional<std::string>{"run-approve"});
  const auto record = bridge->adapter_resolution_for(pending.request_id);
  REQUIRE(record.has_value());
  REQUIRE(record->kind == ava::control_plane::InteractiveRequestKind::Approval);
  REQUIRE(record->state == ava::control_plane::InteractiveRequestState::Resolved);
  REQUIRE(record->approval.has_value());
  REQUIRE(record->approval->kind == ava::tools::ToolApprovalKind::Allowed);
  REQUIRE_FALSE(bridge->approval_requests().current_pending().has_value());
}

TEST_CASE("tui adapter action reject cancels pending approval via bridge", "[ava_tui]") {
  auto bridge = std::make_shared<ava::orchestration::InteractiveBridge>("run-reject");
  const auto pending = bridge->register_approval_for_adapter();
  ava::tui::InteractiveActionAdapter adapter(bridge);

  const auto result = adapter.apply(ava::tui::InteractiveAdapterAction{
      .kind = ava::tui::InteractiveAdapterActionKind::Reject,
      .request_id = pending.request_id,
      .value = "operator rejected",
  });

  REQUIRE(result.accepted);
  REQUIRE(result.terminal_request.has_value());
  REQUIRE(result.terminal_request->request_id == pending.request_id);
  REQUIRE(result.terminal_request->state == ava::control_plane::InteractiveRequestState::Cancelled);
  REQUIRE(result.terminal_request->run_id == std::optional<std::string>{"run-reject"});
  const auto record = bridge->adapter_resolution_for(pending.request_id);
  REQUIRE(record.has_value());
  REQUIRE(record->kind == ava::control_plane::InteractiveRequestKind::Approval);
  REQUIRE(record->state == ava::control_plane::InteractiveRequestState::Cancelled);
  REQUIRE(record->approval.has_value());
  REQUIRE(record->approval->kind == ava::tools::ToolApprovalKind::Rejected);
  REQUIRE(record->approval->reason == std::optional<std::string>{"operator rejected"});
  REQUIRE_FALSE(bridge->approval_requests().current_pending().has_value());
}

TEST_CASE("tui adapter action reject uses default reason when none provided", "[ava_tui]") {
  auto bridge = std::make_shared<ava::orchestration::InteractiveBridge>("run-reject-default");
  const auto pending = bridge->register_approval_for_adapter();
  ava::tui::InteractiveActionAdapter adapter(bridge);

  const auto result = adapter.apply(ava::tui::InteractiveAdapterAction{
      .kind = ava::tui::InteractiveAdapterActionKind::Reject,
      .request_id = pending.request_id,
  });

  REQUIRE(result.accepted);
  const auto record = bridge->adapter_resolution_for(pending.request_id);
  REQUIRE(record.has_value());
  REQUIRE(record->approval.has_value());
  REQUIRE(record->approval->reason == std::optional<std::string>{"rejected from TUI"});
}

TEST_CASE("tui adapter action answer carries request_id to bridge", "[ava_tui]") {
  auto bridge = std::make_shared<ava::orchestration::InteractiveBridge>("run-answer");
  const auto pending = bridge->register_question_for_adapter();
  ava::tui::InteractiveActionAdapter adapter(bridge);

  const auto result = adapter.apply(ava::tui::InteractiveAdapterAction{
      .kind = ava::tui::InteractiveAdapterActionKind::Answer,
      .request_id = pending.request_id,
      .value = "yes",
  });

  REQUIRE(result.accepted);
  REQUIRE(result.terminal_request.has_value());
  REQUIRE(result.terminal_request->request_id == pending.request_id);
  REQUIRE(result.terminal_request->state == ava::control_plane::InteractiveRequestState::Resolved);
  REQUIRE(result.terminal_request->run_id == std::optional<std::string>{"run-answer"});
  const auto record = bridge->adapter_resolution_for(pending.request_id);
  REQUIRE(record.has_value());
  REQUIRE(record->kind == ava::control_plane::InteractiveRequestKind::Question);
  REQUIRE(record->state == ava::control_plane::InteractiveRequestState::Resolved);
  REQUIRE(record->answer == std::optional<std::string>{"yes"});
  REQUIRE_FALSE(bridge->question_requests().current_pending().has_value());
}

TEST_CASE("tui adapter action answer accepts empty answer fallback", "[ava_tui]") {
  auto bridge = std::make_shared<ava::orchestration::InteractiveBridge>("run-answer-empty");
  const auto pending = bridge->register_question_for_adapter();
  ava::tui::InteractiveActionAdapter adapter(bridge);

  const auto result = adapter.apply(ava::tui::InteractiveAdapterAction{
      .kind = ava::tui::InteractiveAdapterActionKind::Answer,
      .request_id = pending.request_id,
  });

  REQUIRE(result.accepted);
  const auto record = bridge->adapter_resolution_for(pending.request_id);
  REQUIRE(record.has_value());
  REQUIRE(record->answer == std::optional<std::string>{""});
}

TEST_CASE("tui adapter action cancel-question cancels pending question via bridge", "[ava_tui]") {
  auto bridge = std::make_shared<ava::orchestration::InteractiveBridge>("run-question-cancel");
  const auto pending = bridge->register_question_for_adapter();
  ava::tui::InteractiveActionAdapter adapter(bridge);

  const auto result = adapter.apply(ava::tui::InteractiveAdapterAction{
      .kind = ava::tui::InteractiveAdapterActionKind::CancelQuestion,
      .request_id = pending.request_id,
  });

  REQUIRE(result.accepted);
  REQUIRE(result.terminal_request.has_value());
  REQUIRE(result.terminal_request->request_id == pending.request_id);
  REQUIRE(result.terminal_request->state == ava::control_plane::InteractiveRequestState::Cancelled);
  const auto record = bridge->adapter_resolution_for(pending.request_id);
  REQUIRE(record.has_value());
  REQUIRE(record->kind == ava::control_plane::InteractiveRequestKind::Question);
  REQUIRE(record->state == ava::control_plane::InteractiveRequestState::Cancelled);
  REQUIRE_FALSE(record->answer.has_value());
  REQUIRE_FALSE(bridge->question_requests().current_pending().has_value());
}

TEST_CASE("tui adapter action accept-plan delegates to orchestration bridge", "[ava_tui]") {
  auto bridge = std::make_shared<ava::orchestration::InteractiveBridge>("run-plan");
  const auto pending = bridge->register_plan_for_adapter();
  ava::tui::InteractiveActionAdapter adapter(bridge);

  const auto result = adapter.apply(ava::tui::InteractiveAdapterAction{
      .kind = ava::tui::InteractiveAdapterActionKind::AcceptPlan,
      .request_id = pending.request_id,
  });

  REQUIRE(result.accepted);
  REQUIRE(result.terminal_request.has_value());
  REQUIRE(result.terminal_request->request_id == pending.request_id);
  REQUIRE(result.terminal_request->state == ava::control_plane::InteractiveRequestState::Resolved);
  REQUIRE(result.terminal_request->run_id == std::optional<std::string>{"run-plan"});
  const auto record = bridge->adapter_resolution_for(pending.request_id);
  REQUIRE(record.has_value());
  REQUIRE(record->kind == ava::control_plane::InteractiveRequestKind::Plan);
  REQUIRE(record->state == ava::control_plane::InteractiveRequestState::Resolved);
  REQUIRE(record->plan_accepted == std::optional<bool>{true});
  REQUIRE_FALSE(bridge->plan_requests().current_pending().has_value());
}

TEST_CASE("tui adapter action reject-plan cancels pending plan via bridge", "[ava_tui]") {
  auto bridge = std::make_shared<ava::orchestration::InteractiveBridge>("run-plan-reject");
  const auto pending = bridge->register_plan_for_adapter();
  ava::tui::InteractiveActionAdapter adapter(bridge);

  const auto result = adapter.apply(ava::tui::InteractiveAdapterAction{
      .kind = ava::tui::InteractiveAdapterActionKind::RejectPlan,
      .request_id = pending.request_id,
  });

  REQUIRE(result.accepted);
  REQUIRE(result.terminal_request.has_value());
  REQUIRE(result.terminal_request->request_id == pending.request_id);
  REQUIRE(result.terminal_request->state == ava::control_plane::InteractiveRequestState::Cancelled);
  REQUIRE(result.terminal_request->run_id == std::optional<std::string>{"run-plan-reject"});
  const auto record = bridge->adapter_resolution_for(pending.request_id);
  REQUIRE(record.has_value());
  REQUIRE(record->kind == ava::control_plane::InteractiveRequestKind::Plan);
  REQUIRE(record->state == ava::control_plane::InteractiveRequestState::Cancelled);
  REQUIRE(record->plan_accepted == std::optional<bool>{false});
  REQUIRE_FALSE(bridge->plan_requests().current_pending().has_value());
}

TEST_CASE("tui adapter action rejects stale or missing request id through bridge", "[ava_tui]") {
  auto bridge = std::make_shared<ava::orchestration::InteractiveBridge>("run-stale");
  const auto pending = bridge->register_approval_for_adapter();
  ava::tui::InteractiveActionAdapter adapter(bridge);

  const auto missing = adapter.apply(ava::tui::InteractiveAdapterAction{
      .kind = ava::tui::InteractiveAdapterActionKind::Approve,
      .request_id = "",
  });
  REQUIRE_FALSE(missing.accepted);
  REQUIRE_FALSE(missing.terminal_request.has_value());
  REQUIRE(missing.error.find("request_id is required") != std::string::npos);

  const auto result = adapter.apply(ava::tui::InteractiveAdapterAction{
      .kind = ava::tui::InteractiveAdapterActionKind::Approve,
      .request_id = "approval-stale",
  });

  REQUIRE_FALSE(result.accepted);
  REQUIRE_FALSE(result.terminal_request.has_value());
  REQUIRE(result.error.find("missing, stale, or already terminal") != std::string::npos);
  const auto still_pending = bridge->approval_requests().current_pending();
  REQUIRE(still_pending.has_value());
  REQUIRE(still_pending->request_id == pending.request_id);

  const auto resolved = adapter.apply(ava::tui::InteractiveAdapterAction{
      .kind = ava::tui::InteractiveAdapterActionKind::Approve,
      .request_id = pending.request_id,
  });
  REQUIRE(resolved.accepted);

  const auto terminal_again = adapter.apply(ava::tui::InteractiveAdapterAction{
      .kind = ava::tui::InteractiveAdapterActionKind::Reject,
      .request_id = pending.request_id,
  });
  REQUIRE_FALSE(terminal_again.accepted);
  REQUIRE_FALSE(terminal_again.terminal_request.has_value());
}

TEST_CASE("tui adapter action rejects unknown action kind", "[ava_tui]") {
  auto bridge = std::make_shared<ava::orchestration::InteractiveBridge>("run-unknown");
  const auto pending = bridge->register_approval_for_adapter();
  ava::tui::InteractiveActionAdapter adapter(bridge);

  const auto result = adapter.apply(ava::tui::InteractiveAdapterAction{
      .kind = static_cast<ava::tui::InteractiveAdapterActionKind>(999),
      .request_id = pending.request_id,
  });

  REQUIRE_FALSE(result.accepted);
  REQUIRE_FALSE(result.terminal_request.has_value());
  REQUIRE(result.error.find("unknown interactive adapter action kind") != std::string::npos);
  REQUIRE(bridge->approval_requests().current_pending().has_value());
}

TEST_CASE("tui adapter action rejects unavailable bridge", "[ava_tui]") {
  ava::tui::InteractiveActionAdapter adapter(nullptr);

  const auto result = adapter.apply(ava::tui::InteractiveAdapterAction{
      .kind = ava::tui::InteractiveAdapterActionKind::Approve,
      .request_id = "approval-1",
  });

  REQUIRE_FALSE(result.accepted);
  REQUIRE_FALSE(result.terminal_request.has_value());
  REQUIRE(result.error.find("interactive bridge is not available") != std::string::npos);
}

TEST_CASE("tui state clears interactive request only on backend clear event", "[ava_tui]") {
  auto bridge = std::make_shared<ava::orchestration::InteractiveBridge>("run-clear");
  const auto pending = bridge->register_approval_for_adapter();
  ava::tui::AppState state;
  state.set_interactive_request(ava::control_plane::InteractiveRequestKind::Approval, pending);
  ava::tui::InteractiveActionAdapter adapter(bridge);

  const auto result = adapter.apply(ava::tui::InteractiveAdapterAction{
      .kind = ava::tui::InteractiveAdapterActionKind::Approve,
      .request_id = pending.request_id,
  });

  REQUIRE(result.accepted);
  REQUIRE(state.interactive_requests().approval.has_value());
  REQUIRE(state.interactive_requests().approval->request_id == pending.request_id);

  state.set_interactive_request(ava::control_plane::InteractiveRequestKind::Approval, bridge->approval_requests().current_pending());
  REQUIRE_FALSE(state.interactive_requests().approval.has_value());
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

  const auto options = ava::tui::parse_tui_options_or_throw(static_cast<int>(args.size()), argv.data());

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
      ava::tui::parse_tui_options_or_throw(static_cast<int>(args.size()), argv.data()),
      std::invalid_argument
  );
}

TEST_CASE("tui options include CLI parse error details", "[ava_tui]") {
  std::vector<std::string> args{"ava_tui", "--max-turns", "0"};
  auto argv = argv_for(args);

  try {
    (void) ava::tui::parse_tui_options_or_throw(static_cast<int>(args.size()), argv.data());
    FAIL("expected invalid max-turns to throw");
  } catch(const std::invalid_argument& error) {
    const std::string message = error.what();
    REQUIRE(message.find("invalid CLI arguments:") != std::string::npos);
    REQUIRE(message.find("--max-turns") != std::string::npos);
  }
}

TEST_CASE("tui options help exits through CLI help path", "[ava_tui]") {
  std::vector<std::string> args{"ava_tui", "--help"};
  auto argv = argv_for(args);

  REQUIRE_THROWS_AS(ava::tui::parse_tui_options_or_throw(static_cast<int>(args.size()), argv.data()), CLI::CallForHelp);
}
