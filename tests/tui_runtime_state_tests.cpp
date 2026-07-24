#include "sys.h"
#include "tests/support/test_harness.h"
#include "ava/app/EventEnvelope.h"
#include "ava/app/events.h"
#include "ava/tui/composer.h"
#include "ava/tui/event_state.h"
#include "ava/tui/runtime.h"
#include "ava/tui/runtime_internal.h"
#include "ava/tui/tool_cards.h"
#include "ava/core/error.h"
#include "ava/core/result.h"

#include <algorithm>
#include <condition_variable>
#include <cstddef>
#include <expected>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace {
void test_tui_event_state_reduces_runtime_events()
{
  ava::tui::TuiEventState state;

  ava::app::runtime::Event user;
  user.type = ava::app::runtime::EventType::UserMessage;
  user.text = "hello";
  ava::tui::apply_runtime_event(state, user);
  expect(state.run_status == ava::tui::TuiEventRunStatus::Running && state.transcript.size() == 1 && state.transcript[0].label == "you" &&
             state.transcript[0].text == "hello" && ava::tui::to_plain_text(state.transcript[0].text_model) == "hello",
         "tui event state records user messages as completed transcript items");

  ava::app::runtime::Event delta;
  delta.type = ava::app::runtime::EventType::MessageUpdate;
  delta.model_id = "gpt-5.5";
  delta.text = "hel";
  ava::tui::apply_runtime_event(state, delta);
  delta.text = "lo";
  ava::tui::apply_runtime_event(state, delta);
  auto streaming_snapshot = ava::tui::event_state_transcript_snapshot(state);
  expect(state.pending_assistant_text == "hello" && streaming_snapshot.size() == 2 && streaming_snapshot[1].label == "ava" &&
             streaming_snapshot[1].text == "hello" && streaming_snapshot[1].meta == "Build · GPT-5.5" &&
             ava::tui::to_plain_text(streaming_snapshot[1].text_model) == "hello",
         "tui event state exposes pending assistant deltas and mode/model metadata in snapshots");

  ava::app::runtime::Event end;
  end.type = ava::app::runtime::EventType::MessageEnd;
  ava::tui::apply_runtime_event(state, end);
  expect(state.run_status == ava::tui::TuiEventRunStatus::Completed && state.pending_assistant_text.empty() && state.transcript.size() == 2 &&
             state.transcript[1].label == "ava" && state.transcript[1].text == "hello" && state.transcript[1].meta == "Build · GPT-5.5" &&
             ava::tui::to_plain_text(state.transcript[1].text_model) == "hello",
         "tui event state commits assistant deltas on message end");
  expect(!state.activity.empty() && state.activity.back().id == "responding" && state.activity.back().status == ava::tui::ToolTimelineStatus::Success &&
             state.activity.back().detail == "assistant responded",
         "tui event state settles responding activity when assistant streaming ends");

  ava::tui::TuiEventState non_gpt_state;
  ava::app::runtime::Event non_gpt_delta;
  non_gpt_delta.type = ava::app::runtime::EventType::MessageUpdate;
  non_gpt_delta.provider_id = "anthropic";
  non_gpt_delta.model_id = "claude-sonnet-4-5";
  non_gpt_delta.text = "hi";
  ava::tui::apply_runtime_event(non_gpt_state, non_gpt_delta);
  auto const non_gpt_snapshot = ava::tui::event_state_transcript_snapshot(non_gpt_state);
  expect(non_gpt_snapshot.size() == 1 && non_gpt_snapshot[0].meta == "Build · Claude Sonnet 4.5",
         "tui event state uses centralized model profile display labels for non-GPT assistant metadata");

  ava::app::runtime::Event assistant_final;
  assistant_final.type = ava::app::runtime::EventType::AssistantMessage;
  assistant_final.text = "hello";
  ava::tui::apply_runtime_event(state, assistant_final);
  expect(state.transcript.size() == 2 && state.transcript[1].text == "hello", "tui event state avoids duplicating matching streamed assistant final events");

  assistant_final.text = "hello\n";
  ava::tui::apply_runtime_event(state, assistant_final);
  expect(state.transcript.size() == 2 && state.transcript[1].text == "hello",
         "tui event state treats trailing whitespace-only final changes as duplicate streamed assistant events");

  ava::tui::TuiEventState reasoning_state;
  ava::app::runtime::Event reasoning_start;
  reasoning_start.type = ava::app::runtime::EventType::ReasoningStart;
  reasoning_start.reasoning_format = "summary";
  ava::tui::apply_runtime_event(reasoning_state, reasoning_start);
  ava::app::runtime::Event reasoning_delta;
  reasoning_delta.type = ava::app::runtime::EventType::ReasoningDelta;
  reasoning_delta.text = "checking";
  ava::tui::apply_runtime_event(reasoning_state, reasoning_delta);
  reasoning_delta.text = " options";
  ava::tui::apply_runtime_event(reasoning_state, reasoning_delta);
  auto reasoning_snapshot = ava::tui::event_state_transcript_snapshot(reasoning_state);
  expect(reasoning_state.pending_reasoning_text == "checking options" && reasoning_snapshot.size() == 1 && reasoning_snapshot[0].label == "ava" &&
             reasoning_snapshot[0].thinking == "checking options" && reasoning_snapshot[0].text.empty() &&
             ava::tui::to_plain_text(reasoning_snapshot[0].thinking_model) == "checking options",
         "tui event state exposes pending reasoning as part of the assistant turn");
  ava::app::runtime::Event reasoning_end;
  reasoning_end.type = ava::app::runtime::EventType::ReasoningEnd;
  ava::tui::apply_runtime_event(reasoning_state, reasoning_end);
  expect(reasoning_state.pending_reasoning_text == "checking options" && reasoning_state.transcript.empty() && reasoning_state.activity.size() == 1 &&
             reasoning_state.activity[0].label == "reasoning" && reasoning_state.activity[0].status == ava::tui::ToolTimelineStatus::Success,
         "tui event state keeps completed reasoning attached to the pending assistant turn");

  ava::app::runtime::Event reasoning_answer;
  reasoning_answer.type = ava::app::runtime::EventType::MessageUpdate;
  reasoning_answer.model_id = "gpt-5.5";
  reasoning_answer.text = "answer";
  ava::tui::apply_runtime_event(reasoning_state, reasoning_answer);
  ava::app::runtime::Event reasoning_answer_end;
  reasoning_answer_end.type = ava::app::runtime::EventType::MessageEnd;
  reasoning_answer_end.model_id = "gpt-5.5";
  ava::tui::apply_runtime_event(reasoning_state, reasoning_answer_end);
  expect(reasoning_state.pending_reasoning_text.empty() && reasoning_state.transcript.size() == 1 && reasoning_state.transcript[0].label == "ava" &&
             reasoning_state.transcript[0].text == "answer" && reasoning_state.transcript[0].thinking == "checking options" &&
             ava::tui::to_plain_text(reasoning_state.transcript[0].text_model) == "answer" &&
             ava::tui::to_plain_text(reasoning_state.transcript[0].thinking_model) == "checking options",
         "tui event state commits reasoning and answer as one assistant transcript item");

  auto const thinking_render = ava::tui::render_composer(ava::tui::ComposerSnapshot{.mode = "build",
                                                                                    .provider = "openai",
                                                                                    .model = "gpt-5.5",
                                                                                    .session_id = "session_test",
                                                                                    .input = "",
                                                                                    .status = "ready",
                                                                                    .transcript = reasoning_state.transcript,
                                                                                    .width = 60,
                                                                                    .height = 10});
  expect(std::ranges::any_of(thinking_render, [](std::string const& line) { return strip_sgr(line).find("Thinking: checking options") != std::string::npos; }),
         "tui renders reasoning content as an inline thinking transcript block with a stable prefix");
  expect(std::ranges::any_of(thinking_render,
                             [](std::string const& line) {
                               return strip_sgr(line).find("Thinking:") != std::string::npos && line.find("\x1b[38;2;88;96;112m") != std::string::npos;
                             }),
         "tui renders thinking text with dim grey styling");
  expect(std::ranges::none_of(thinking_render,
                              [](std::string const& line) {
                                auto const visible = strip_sgr(line);
                                return visible.find("╭─ AVA") != std::string::npos || visible.find("AVA:") != std::string::npos ||
                                       visible.find("╭─ You") != std::string::npos || visible.find("You:") != std::string::npos;
                              }),
         "tui transcript role headers stay hidden for compact chat rendering");
  expect(std::ranges::none_of(thinking_render, [](std::string const& line) { return strip_sgr(line).find("╭─ Thinking") != std::string::npos; }),
         "tui thinking transcript block avoids the normal boxed message header");
  auto const hidden_thinking_render = ava::tui::render_composer(ava::tui::ComposerSnapshot{.mode = "build",
                                                                                           .provider = "openai",
                                                                                           .model = "gpt-5.5",
                                                                                           .session_id = "session_test",
                                                                                           .input = "",
                                                                                           .status = "ready",
                                                                                           .transcript = reasoning_state.transcript,
                                                                                           .width = 60,
                                                                                           .height = 10,
                                                                                           .thinking_visible = false});
  expect(std::ranges::none_of(hidden_thinking_render,
                              [](std::string const& line) { return strip_sgr(line).find("Thinking: checking options") != std::string::npos; }) &&
             std::ranges::any_of(hidden_thinking_render, [](std::string const& line) { return strip_sgr(line).find("answer") != std::string::npos; }),
         "tui thinking visibility hides inline thinking blocks without hiding assistant text");

  ava::tui::TuiEventState redacted_reasoning_state;
  ava::app::runtime::Event redacted_reasoning;
  redacted_reasoning.type = ava::app::runtime::EventType::ReasoningDelta;
  redacted_reasoning.reasoning_redacted = true;
  redacted_reasoning.text = "provider-private-secret";
  ava::tui::apply_runtime_event(redacted_reasoning_state, redacted_reasoning);
  auto redacted_snapshot = ava::tui::event_state_transcript_snapshot(redacted_reasoning_state);
  auto const redacted_render = ava::tui::render_composer(ava::tui::ComposerSnapshot{.mode = "build",
                                                                                    .provider = "openai",
                                                                                    .model = "gpt-5.5",
                                                                                    .session_id = "session_test",
                                                                                    .input = "",
                                                                                    .status = "ready",
                                                                                    .transcript = redacted_snapshot,
                                                                                    .width = 60,
                                                                                    .height = 10});
  expect(
      redacted_snapshot.size() == 1 && redacted_snapshot[0].thinking == "[reasoning redacted]" &&
          std::ranges::none_of(redacted_render, [](std::string const& line) { return strip_sgr(line).find("provider-private-secret") != std::string::npos; }),
      "tui event state never renders text from redacted reasoning deltas");

  ava::tui::TuiEventState audit_state;
  ava::app::runtime::Event permission_audit;
  permission_audit.type = ava::app::runtime::EventType::ProviderEvent;
  permission_audit.status = "tui:permission_request";
  permission_audit.text = "permission requested: bash pwd";
  ava::tui::apply_runtime_event(audit_state, permission_audit);
  ava::app::runtime::Event question_audit;
  question_audit.type = ava::app::runtime::EventType::ProviderEvent;
  question_audit.status = "tui:question_answer";
  question_audit.text = "question answered: yes";
  ava::tui::apply_runtime_event(audit_state, question_audit);
  expect(audit_state.transcript.size() == 2 && audit_state.transcript[0].label == "audit" &&
             audit_state.transcript[0].text == "permission requested: bash pwd" && audit_state.transcript[1].text == "question answered: yes" &&
             !audit_state.activity.empty(),
         "tui event state records permission and question audit markers from resolver events");

  ava::tui::TuiEventState live_permission_state;
  ava::app::runtime::Event live_permission_tool_start;
  live_permission_tool_start.type = ava::app::runtime::EventType::ToolStart;
  live_permission_tool_start.call_id = "call_live_permission";
  live_permission_tool_start.tool_name = "bash";
  live_permission_tool_start.text = "git push origin main";
  ava::tui::apply_runtime_event(live_permission_state, live_permission_tool_start);
  ava::app::runtime::Event live_permission_request;
  live_permission_request.type = ava::app::runtime::EventType::ProviderEvent;
  live_permission_request.status = "tui:permission_request";
  live_permission_request.tool_name = "bash";
  live_permission_request.permission_request_ids = {"perm_live_permission"};
  live_permission_request.text = "git push origin main";
  live_permission_request.reason = "changes remote state";
  ava::tui::apply_runtime_event(live_permission_state, live_permission_request);
  expect(live_permission_state.pending_tools.size() == 1 && live_permission_state.pending_tools[0].item.permissions.size() == 1 &&
             live_permission_state.pending_tools[0].item.permissions[0].permission_request_id == "perm_live_permission" &&
             live_permission_state.pending_tools[0].item.permissions[0].decision.empty() &&
             strip_sgr(ava::tui::detail::render_tool_card(live_permission_state.pending_tools[0].item, 100, false).front()).find("permission required") !=
                 std::string::npos,
         "tui event state immediately attaches a permission request to the unique matching running tool");
  auto live_permission_allow = live_permission_request;
  live_permission_allow.status = "tui:permission_allow";
  live_permission_allow.tool_name.clear();
  live_permission_allow.text = "permission allowed";
  ava::tui::apply_runtime_event(live_permission_state, live_permission_allow);
  expect(live_permission_state.pending_tools[0].item.permissions.size() == 1 &&
             live_permission_state.pending_tools[0].item.permissions[0].decision == "allow" &&
             strip_sgr(ava::tui::detail::render_tool_card(live_permission_state.pending_tools[0].item, 100, false).front()).find("permission allow") !=
                 std::string::npos,
         "tui event state merges a later permission resolution into the same pending tool audit");
  ava::app::runtime::Event live_permission_result;
  live_permission_result.type = ava::app::runtime::EventType::ToolResult;
  live_permission_result.call_id = live_permission_tool_start.call_id;
  live_permission_result.tool_name = live_permission_tool_start.tool_name;
  live_permission_result.text = "pushed";
  ava::tui::apply_runtime_event(live_permission_state, live_permission_result);
  expect(live_permission_state.pending_tools.empty() && !live_permission_state.transcript.empty() && live_permission_state.transcript.back().tool &&
             live_permission_state.transcript.back().tool->permissions.size() == 1 &&
             live_permission_state.transcript.back().tool->permissions[0].decision == "allow",
         "tui event state carries the correlated permission audit into the later settled tool result");

  auto verify_settled_provider_permission = [&](std::string resolution_detail, std::string expected_decision, std::string expected_label) {
    ava::tui::TuiEventState provider_permission_state;
    auto tool_start = live_permission_tool_start;
    tool_start.call_id = "call_provider_permission_" + expected_decision + resolution_detail;
    ava::tui::apply_runtime_event(provider_permission_state, tool_start);
    auto request = live_permission_request;
    request.permission_request_ids = {"perm_provider_permission"};
    ava::tui::apply_runtime_event(provider_permission_state, request);
    auto resolution = request;
    resolution.status = "tui:permission_allow";
    resolution.text = "permission allowed";
    resolution.error_details = std::move(resolution_detail);
    ava::tui::apply_runtime_event(provider_permission_state, resolution);
    auto result = live_permission_result;
    result.call_id = tool_start.call_id;
    ava::tui::apply_runtime_event(provider_permission_state, result);
    auto const* item = provider_permission_state.transcript.empty() || !provider_permission_state.transcript.back().tool
                           ? nullptr
                           : &*provider_permission_state.transcript.back().tool;
    auto rendered = std::string{};
    if (item)
    {
      for (auto const& line : ava::tui::detail::render_tool_card(*item, 100, true)) rendered += strip_sgr(line) + '\n';
    }
    auto const copied = item ? ava::tui::detail::tool_card_copy_text(*item) : std::string{};
    expect(item && item->permissions.size() == 1 && item->permissions[0].decision == expected_decision &&
               rendered.find("permission: " + expected_label) != std::string::npos && copied.find("permission: " + expected_label) != std::string::npos,
           "tui provider permission audit preserves " + expected_label + " on the settled card, expanded details, and copy text");
  };
  verify_settled_provider_permission("selected allow", "allow", "allow");
  verify_settled_provider_permission("selected allow session", "allow_session", "allow session");
  verify_settled_provider_permission("reused tui session grant", "allow_session", "allow session");
  verify_settled_provider_permission("selected allow and remember", "allow_remember", "allow always");

  ava::tui::TuiEventState ambiguous_permission_state;
  auto ambiguous_start = live_permission_tool_start;
  ambiguous_start.tool_name = "read_file";
  ambiguous_start.call_id = "call_ambiguous_one";
  ava::tui::apply_runtime_event(ambiguous_permission_state, ambiguous_start);
  ambiguous_start.call_id = "call_ambiguous_two";
  ava::tui::apply_runtime_event(ambiguous_permission_state, ambiguous_start);
  auto ambiguous_request = live_permission_request;
  ambiguous_request.tool_name = "read_file";
  ambiguous_request.permission_request_ids = {"perm_ambiguous"};
  ava::tui::apply_runtime_event(ambiguous_permission_state, ambiguous_request);
  expect(ambiguous_permission_state.permission_audits.size() == 1 &&
             std::ranges::all_of(ambiguous_permission_state.pending_tools,
                                 [](ava::tui::PendingToolItem const& pending) { return pending.item.permissions.empty(); }),
         "tui event state does not guess between multiple running tools with the same permission tool name");
  auto ambiguous_first_result = live_permission_result;
  ambiguous_first_result.call_id = "call_ambiguous_one";
  ambiguous_first_result.tool_name = "read_file";
  ava::tui::apply_runtime_event(ambiguous_permission_state, ambiguous_first_result);
  auto ambiguous_reply = ambiguous_request;
  ambiguous_reply.status = "tui:permission_allow";
  ambiguous_reply.text = "permission allowed";
  ambiguous_reply.error_details = "selected allow";
  ava::tui::apply_runtime_event(ambiguous_permission_state, ambiguous_reply);
  expect(ambiguous_permission_state.pending_tools.size() == 1 && ambiguous_permission_state.pending_tools[0].call_id == "call_ambiguous_two" &&
             ambiguous_permission_state.pending_tools[0].item.permissions.empty(),
         "tui permission audit merge never reuses unique-name fallback after an initially ambiguous request");
  auto ambiguous_second_result = ambiguous_first_result;
  ambiguous_second_result.call_id = "call_ambiguous_two";
  ava::tui::apply_runtime_event(ambiguous_permission_state, ambiguous_second_result);
  expect(!ambiguous_permission_state.transcript.empty() && ambiguous_permission_state.transcript.back().tool &&
             ambiguous_permission_state.transcript.back().tool->permissions.empty(),
         "tui initially ambiguous permission audit remains unattached when the surviving same-name tool settles");

  ava::tui::TuiEventState exact_permission_state;
  auto exact_first = ambiguous_start;
  exact_first.call_id = "call_exact_one";
  exact_first.permission_request_ids.clear();
  ava::tui::apply_runtime_event(exact_permission_state, exact_first);
  auto exact_second = exact_first;
  exact_second.call_id = "call_exact_two";
  exact_second.permission_request_ids = {"perm_exact"};
  ava::tui::apply_runtime_event(exact_permission_state, exact_second);
  auto exact_request = ambiguous_request;
  exact_request.permission_request_ids = {"perm_exact"};
  ava::tui::apply_runtime_event(exact_permission_state, exact_request);
  expect(exact_permission_state.pending_tools[0].item.permissions.empty() && exact_permission_state.pending_tools[1].item.permissions.size() == 1,
         "tui event state prefers an exact permission-id match when same-name pending tools are ambiguous");
  auto question_with_permission_shape = ambiguous_request;
  question_with_permission_shape.status = "tui:question_request";
  question_with_permission_shape.permission_request_ids = {"perm_must_not_attach"};
  auto const permission_audit_count = ambiguous_permission_state.permission_audits.size();
  ava::tui::apply_runtime_event(ambiguous_permission_state, question_with_permission_shape);
  expect(ambiguous_permission_state.permission_audits.size() == permission_audit_count &&
             std::ranges::all_of(ambiguous_permission_state.pending_tools,
                                 [](ava::tui::PendingToolItem const& pending) { return pending.item.permissions.empty(); }),
         "tui question audit events never create or attach permission state");

  ava::tui::TuiEventState reused_state;
  ava::tui::apply_runtime_event(reused_state, user);
  delta.text = "streamed";
  ava::tui::apply_runtime_event(reused_state, delta);
  ava::tui::apply_runtime_event(reused_state, end);
  ava::app::runtime::Event next_user;
  next_user.type = ava::app::runtime::EventType::UserMessage;
  next_user.text = "next";
  ava::tui::apply_runtime_event(reused_state, next_user);
  assistant_final.text = "fresh final";
  ava::tui::apply_runtime_event(reused_state, assistant_final);
  expect(reused_state.transcript.size() == 4 && reused_state.transcript[1].text == "streamed" && reused_state.transcript.back().text == "fresh final",
         "tui event state clears streaming index before a reused-state next turn");

  ava::tui::TuiEventState final_state;
  assistant_final.text = "direct final";
  ava::tui::apply_runtime_event(final_state, assistant_final);
  expect(final_state.run_status == ava::tui::TuiEventRunStatus::Completed && final_state.transcript.size() == 1 && final_state.transcript[0].label == "ava" &&
             final_state.transcript[0].text == "direct final",
         "tui event state records assistant final events without streaming deltas");
  assistant_final.text = "Use `ava` and **bold**";
  ava::tui::apply_runtime_event(final_state, assistant_final);
  expect(final_state.transcript.size() == 2 && ava::tui::to_plain_text(final_state.transcript.back().text_model) == "Use ava and bold",
         "tui event state stores assistant Markdown as frontend-owned semantic Text");

  ava::tui::TuiEventState provider_state;
  ava::app::runtime::Event provider_start;
  provider_start.type = ava::app::runtime::EventType::ProviderEvent;
  provider_start.status = "tool_call_start";
  provider_start.call_id = "provider_call_1";
  provider_start.tool_name = "read_file";
  provider_start.text = R"({"path": "README.md"})";
  ava::tui::apply_runtime_event(provider_state, provider_start);
  auto provider_snapshot = ava::tui::event_state_transcript_snapshot(provider_state);
  auto const provider_activity_id = provider_state.activity.empty() ? std::string{} : provider_state.activity[0].id;
  expect(provider_state.activity.size() == 1 && !provider_activity_id.empty() && provider_state.activity[0].label == "read_file" &&
             provider_state.activity[0].detail == "provider is preparing tool call" &&
             provider_state.activity[0].status == ava::tui::ToolTimelineStatus::Running && provider_state.transcript.empty() &&
             provider_state.pending_tools.size() == 1 && provider_snapshot.size() == 1 && provider_snapshot.back().tool &&
             provider_snapshot.back().tool->lifecycle == ava::tui::ToolLifecycleState::ProviderAnnounced,
         "tui event state shows provider tool-call starts as pending announced tool cards");

  ava::app::runtime::Event provider_delta = provider_start;
  provider_delta.status = "tool_call_delta";
  provider_delta.tool_name.clear();
  provider_delta.text = R"({"path": "README.md", "partial": true})";
  ava::tui::apply_runtime_event(provider_state, provider_delta);
  provider_snapshot = ava::tui::event_state_transcript_snapshot(provider_state);
  expect(provider_state.activity.size() == 1 && provider_state.activity[0].id == provider_activity_id && provider_state.activity[0].label == "read_file" &&
             provider_state.activity[0].detail == "streaming tool arguments" && provider_state.activity[0].status == ava::tui::ToolTimelineStatus::Running &&
             provider_state.transcript.empty() && provider_state.pending_tools.size() == 1 &&
             provider_state.pending_tools[0].item.lifecycle == ava::tui::ToolLifecycleState::ArgumentsStreaming &&
             provider_state.pending_tools[0].item.argument_summary.find("\"partial\": true") != std::string::npos && provider_snapshot.size() == 1 &&
             provider_snapshot.back().tool,
         "tui event state keeps provider tool-call deltas on the pending tool card and preserves labels by call id");

  ava::app::runtime::Event provider_end = provider_delta;
  provider_end.status = "tool_call_end";
  provider_end.text = R"({"path": "README.md", "complete": true})";
  ava::tui::apply_runtime_event(provider_state, provider_end);
  provider_snapshot = ava::tui::event_state_transcript_snapshot(provider_state);
  expect(provider_state.activity.size() == 1 && provider_state.activity[0].id == provider_activity_id && provider_state.activity[0].label == "read_file" &&
             provider_state.activity[0].detail == "tool call ready" && provider_state.activity[0].status == ava::tui::ToolTimelineStatus::Success &&
             provider_state.transcript.empty() && provider_state.pending_tools.size() == 1 &&
             provider_state.pending_tools[0].item.lifecycle == ava::tui::ToolLifecycleState::ArgumentsComplete && provider_snapshot.size() == 1 &&
             provider_snapshot.back().tool,
         "tui event state marks provider tool-call arguments complete without settling completed transcript history");

  ava::app::runtime::Event provider_execution_start;
  provider_execution_start.type = ava::app::runtime::EventType::ToolStart;
  provider_execution_start.call_id = "provider_call_1";
  provider_execution_start.tool_name = "read_file";
  provider_execution_start.text = "path=README.md";
  ava::tui::apply_runtime_event(provider_state, provider_execution_start);
  expect(provider_state.pending_tools.size() == 1 && provider_state.pending_tools[0].item.lifecycle == ava::tui::ToolLifecycleState::ExecutionStarted &&
             provider_state.pending_tools[0].item.argument_summary == "path=README.md",
         "tui event state advances an announced provider tool card into execution by call id");

  ava::app::runtime::Event provider_execution_progress;
  provider_execution_progress.type = ava::app::runtime::EventType::ToolProgress;
  provider_execution_progress.call_id = "provider_call_1";
  provider_execution_progress.tool_name = "read_file";
  provider_execution_progress.text = "reading file";
  ava::tui::apply_runtime_event(provider_state, provider_execution_progress);
  expect(provider_state.pending_tools.size() == 1 && provider_state.pending_tools[0].item.lifecycle == ava::tui::ToolLifecycleState::Progress &&
             provider_state.pending_tools[0].item.result_summary == "reading file",
         "tui event state records partial tool progress on the pending card");

  ava::app::runtime::Event provider_execution_result;
  provider_execution_result.type = ava::app::runtime::EventType::ToolResult;
  provider_execution_result.call_id = "provider_call_1";
  provider_execution_result.tool_name = "read_file";
  provider_execution_result.status = "success";
  provider_execution_result.text = "read lines 1-10/10";
  ava::tui::apply_runtime_event(provider_state, provider_execution_result);
  expect(provider_state.pending_tools.empty() && !provider_state.transcript.empty() && provider_state.transcript.back().tool &&
             provider_state.transcript.back().tool->lifecycle == ava::tui::ToolLifecycleState::Complete &&
             provider_state.transcript.back().tool->argument_summary == "path=README.md",
         "tui event state settles completed tools into immutable transcript history");

  ava::tui::TuiEventState provider_without_id_state;
  ava::app::runtime::Event provider_without_id;
  provider_without_id.type = ava::app::runtime::EventType::ProviderEvent;
  provider_without_id.status = "tool_call_start";
  provider_without_id.tool_name = "grep";
  ava::tui::apply_runtime_event(provider_without_id_state, provider_without_id);
  auto const provider_without_id_activity_id = provider_without_id_state.activity.empty() ? std::string{} : provider_without_id_state.activity[0].id;
  provider_without_id.status = "tool_call_delta";
  ava::tui::apply_runtime_event(provider_without_id_state, provider_without_id);
  provider_without_id.status = "tool_call_end";
  ava::tui::apply_runtime_event(provider_without_id_state, provider_without_id);
  expect(provider_without_id_state.activity.size() == 1 && !provider_without_id_activity_id.empty() &&
             provider_without_id_state.activity[0].id == provider_without_id_activity_id && provider_without_id_state.activity[0].label == "grep" &&
             provider_without_id_state.activity[0].detail == "tool call ready" &&
             provider_without_id_state.activity[0].status == ava::tui::ToolTimelineStatus::Success && provider_without_id_state.pending_tools.size() == 1 &&
             provider_without_id_state.pending_tools[0].item.lifecycle == ava::tui::ToolLifecycleState::ArgumentsComplete,
         "tui event state coalesces provider tool-call activity and pending cards when provider events omit call ids");

  ava::app::runtime::Event tool_start;
  tool_start.type = ava::app::runtime::EventType::ToolStart;
  tool_start.call_id = "call_1";
  tool_start.tool_name = "bash";
  tool_start.text = "pwd";
  tool_start.tool_arguments_json = "{\"command\":\"pwd\"}";
  ava::tui::apply_runtime_event(state, tool_start);
  expect(state.pending_tools.size() == 1 && state.pending_tools[0].call_id == "call_1" &&
             state.pending_tools[0].item.status == ava::tui::ToolTimelineStatus::Running && state.pending_tools[0].item.name == "bash" &&
             state.pending_tools[0].item.argument_summary == "pwd" && state.pending_tools[0].item.arguments_json == "{\"command\":\"pwd\"}",
         "tui event state tracks started tools by call id");

  ava::app::runtime::Event tool_progress;
  tool_progress.type = ava::app::runtime::EventType::ToolProgress;
  tool_progress.call_id = "call_1";
  tool_progress.tool_name = "bash";
  tool_progress.text = "running pwd";
  tool_progress.tool_result_json = "{\"partial\":true}";
  ava::tui::apply_runtime_event(state, tool_progress);
  auto tool_snapshot = ava::tui::event_state_transcript_snapshot(state);
  expect(state.pending_tools.size() == 1 && state.pending_tools[0].item.result_summary == "running pwd" &&
             state.pending_tools[0].item.result_json == "{\"partial\":true}" && !tool_snapshot.empty() && tool_snapshot.back().tool &&
             tool_snapshot.back().tool->status == ava::tui::ToolTimelineStatus::Running,
         "tui event state updates pending tool progress and includes it in snapshots");

  ava::app::runtime::Event tool_result;
  tool_result.type = ava::app::runtime::EventType::ToolResult;
  tool_result.call_id = "call_1";
  tool_result.tool_name = "bash";
  tool_result.status = "success";
  tool_result.text = "ok";
  tool_result.tool_result_json = "{\"ok\":true}";
  ava::tui::apply_runtime_event(state, tool_result);
  expect(state.pending_tools.empty() && !state.transcript.empty() && state.transcript.back().tool &&
             state.transcript.back().tool->status == ava::tui::ToolTimelineStatus::Success && state.transcript.back().tool->argument_summary == "pwd" &&
             state.transcript.back().tool->result_summary == "ok" && state.transcript.back().tool->arguments_json == "{\"command\":\"pwd\"}" &&
             state.transcript.back().tool->result_json == "{\"ok\":true}",
         "tui event state moves successful tool results into completed transcript items");

  ava::app::runtime::Event write_start;
  write_start.type = ava::app::runtime::EventType::ToolStart;
  write_start.call_id = "call_write";
  write_start.tool_name = "write_file";
  write_start.text = "path=src/main.cpp, content=12 bytes";
  ava::tui::apply_runtime_event(state, write_start);
  ava::app::runtime::Event write_result;
  write_result.type = ava::app::runtime::EventType::ToolResult;
  write_result.call_id = "call_write";
  write_result.tool_name = "write_file";
  write_result.status = "success";
  write_result.text = "wrote 12 bytes";
  ava::tui::apply_runtime_event(state, write_result);
  expect(!state.activity.empty() && state.activity.back().label == "write_file" && state.activity.back().status == ava::tui::ToolTimelineStatus::Success &&
             state.modified_files.size() == 1 && state.modified_files[0].path == "src/main.cpp",
         "tui event state feeds sidebar activity and modified-file summaries from successful mutating tools");

  ava::app::runtime::Event semantic_write;
  semantic_write.type = ava::app::runtime::EventType::ToolResult;
  semantic_write.call_id = "call_semantic_write";
  semantic_write.tool_name = "edit_file";
  semantic_write.status = "success";
  semantic_write.text = "edited file";
  semantic_write.changed_paths = {"src/semantic.cpp"};
  ava::tui::apply_runtime_event(state, semantic_write);
  expect(std::ranges::any_of(state.modified_files, [](ava::tui::SidebarModifiedFile const& file) { return file.path == "src/semantic.cpp"; }),
         "tui event state prefers semantic changed paths over parsing mutating tool summaries");

  ava::app::runtime::Event tool_error;
  tool_error.type = ava::app::runtime::EventType::ToolResult;
  tool_error.call_id = "call_2";
  tool_error.tool_name = "read";
  tool_error.status = "error";
  tool_error.text = "denied";
  ava::tui::apply_runtime_event(state, tool_error);
  expect(state.transcript.back().tool && state.transcript.back().tool->status == ava::tui::ToolTimelineStatus::Error &&
             state.transcript.back().tool->lifecycle == ava::tui::ToolLifecycleState::Error && state.transcript.back().tool->result_summary == "denied",
         "tui event state records errored tool results as error tool cards");

  ava::app::runtime::Event tool_canceled_start;
  tool_canceled_start.type = ava::app::runtime::EventType::ToolStart;
  tool_canceled_start.call_id = "call_canceled";
  tool_canceled_start.tool_name = "bash";
  tool_canceled_start.text = "sleep 30";
  tool_canceled_start.tool_arguments_json = "{\"command\":\"sleep 30\"}";
  ava::tui::apply_runtime_event(state, tool_canceled_start);
  ava::app::runtime::Event tool_canceled;
  tool_canceled.type = ava::app::runtime::EventType::ToolResult;
  tool_canceled.call_id = "call_canceled";
  tool_canceled.tool_name = "bash";
  tool_canceled.status = "canceled";
  tool_canceled.text = "stopped by user";
  tool_canceled.tool_result_json = "{\"tool\":\"bash\",\"canceled\":true}";
  ava::tui::apply_runtime_event(state, tool_canceled);
  expect(state.transcript.back().tool && state.transcript.back().tool->status == ava::tui::ToolTimelineStatus::Canceled &&
             state.transcript.back().tool->lifecycle == ava::tui::ToolLifecycleState::Canceled &&
             state.transcript.back().tool->argument_summary == "sleep 30" && state.transcript.back().tool->result_summary == "stopped by user" &&
             state.activity.back().status == ava::tui::ToolTimelineStatus::Canceled,
         "tui event state records canceled tool results as canceled tool cards");

  ava::tui::TuiEventState permission_tool_state;
  ava::app::EventEnvelope permission_tool_requested{.schema_version = 1,
                                                    .event_id = "event_permission_tool_request",
                                                    .timestamp = "2026-04-30T00:00:00Z",
                                                    .session_id = "session_test",
                                                    .run_id = "run_permission_tool",
                                                    .turn_id = "turn_permission_tool",
                                                    .message_id = std::nullopt,
                                                    .request_id = "permission_1",
                                                    .correlation_id = "permission_1",
                                                    .name = "permission_requested",
                                                    .payload_json =
                                                        "{\"resolver_request_id\":\"permission_1\","
                                                        "\"permission_request_id\":\"permreq_push\","
                                                        "\"operation\":\"bash\",\"mode\":\"build\","
                                                        "\"target_path\":\"\",\"command\":\"git push origin main\","
                                                        "\"tool_name\":\"bash\",\"risk\":\"high\","
                                                        "\"reason\":\"command can change external state\"}"};
  ava::tui::apply_event_envelope(permission_tool_state, permission_tool_requested);
  ava::app::EventEnvelope permission_tool_replied{.schema_version = 1,
                                                  .event_id = "event_permission_tool_reply",
                                                  .timestamp = "2026-04-30T00:00:01Z",
                                                  .session_id = "session_test",
                                                  .run_id = "run_permission_tool",
                                                  .turn_id = "turn_permission_tool",
                                                  .message_id = std::nullopt,
                                                  .request_id = "permission_1",
                                                  .correlation_id = "permission_1",
                                                  .name = "permission_replied",
                                                  .payload_json =
                                                      "{\"resolver_request_id\":\"permission_1\","
                                                      "\"decision\":\"deny\",\"reason\":\"selected deny\"}"};
  ava::tui::apply_event_envelope(permission_tool_state, permission_tool_replied);
  ava::app::EventEnvelope permission_tool_result{.schema_version = 1,
                                                 .event_id = "event_permission_tool_result",
                                                 .timestamp = "2026-04-30T00:00:02Z",
                                                 .session_id = "session_test",
                                                 .run_id = "run_permission_tool",
                                                 .turn_id = "turn_permission_tool",
                                                 .message_id = "message_permission_tool",
                                                 .request_id = "request_tool",
                                                 .correlation_id = "call_permission_tool",
                                                 .name = "tool_result",
                                                 .payload_json =
                                                     "{\"tool_name\":\"bash\",\"text\":\"permission denied\","
                                                     "\"status\":\"error\",\"permission_request_ids\":[\"permreq_push\"],"
                                                     "\"args\":{\"command\":\"git push origin main\"},"
                                                     "\"result\":{\"tool\":\"bash\",\"exit_code\":126}}"};
  ava::tui::apply_event_envelope(permission_tool_state, permission_tool_result);
  auto permission_tool_snapshot = ava::tui::event_state_transcript_snapshot(permission_tool_state);
  auto const permission_tool_render = ava::tui::render_composer(ava::tui::ComposerSnapshot{.mode = "build",
                                                                                           .provider = "openai",
                                                                                           .model = "gpt-5.5",
                                                                                           .session_id = "session_test",
                                                                                           .input = "",
                                                                                           .status = "ready",
                                                                                           .transcript = permission_tool_snapshot,
                                                                                           .width = 88,
                                                                                           .height = 18});
  expect(permission_tool_snapshot.size() == 3 && permission_tool_snapshot.back().tool && permission_tool_snapshot.back().tool->permissions.size() == 1 &&
             permission_tool_snapshot.back().tool->permissions[0].permission_request_id == "permreq_push" &&
             permission_tool_snapshot.back().tool->permissions[0].decision == "deny" && permission_tool_snapshot.back().tool->permissions[0].risk == "high" &&
             std::ranges::any_of(permission_tool_render,
                                 [](std::string const& line) {
                                   auto const visible = strip_sgr(line);
                                   return visible.find("permission deny") != std::string::npos && visible.find("risk high") != std::string::npos &&
                                          visible.find("reason command can change external state") != std::string::npos;
                                 }),
         "tui EventEnvelope reducer attaches permission request/reply metadata to linked tool result cards");

  ava::tui::TuiEventState canonical_session_permission_state;
  auto canonical_permission_requested = permission_tool_requested;
  canonical_permission_requested.event_id = "event_canonical_session_request";
  canonical_permission_requested.request_id = "permission_canonical_session";
  canonical_permission_requested.correlation_id = "permission_canonical_session";
  canonical_permission_requested.payload_json =
      "{\"resolver_request_id\":\"permission_canonical_session\",\"permission_request_id\":\"permreq_canonical_session\","
      "\"tool_name\":\"bash\",\"reason\":\"needs approval\"}";
  ava::tui::apply_event_envelope(canonical_session_permission_state, canonical_permission_requested);
  auto canonical_permission_replied = permission_tool_replied;
  canonical_permission_replied.event_id = "event_canonical_session_reply";
  canonical_permission_replied.request_id = "permission_canonical_session";
  canonical_permission_replied.correlation_id = "permission_canonical_session";
  canonical_permission_replied.payload_json =
      "{\"resolver_request_id\":\"permission_canonical_session\",\"decision\":\"allow_session\",\"reason\":\"rpc session grant\"}";
  ava::tui::apply_event_envelope(canonical_session_permission_state, canonical_permission_replied);
  auto canonical_permission_result = permission_tool_result;
  canonical_permission_result.event_id = "event_canonical_session_result";
  canonical_permission_result.request_id = "request_canonical_session_tool";
  canonical_permission_result.correlation_id = "call_canonical_session_tool";
  canonical_permission_result.payload_json =
      "{\"tool_name\":\"bash\",\"text\":\"ran\",\"status\":\"success\",\"permission_request_ids\":[\"permreq_canonical_session\"]}";
  ava::tui::apply_event_envelope(canonical_session_permission_state, canonical_permission_result);
  auto const* canonical_session_item = canonical_session_permission_state.transcript.empty() || !canonical_session_permission_state.transcript.back().tool
                                           ? nullptr
                                           : &*canonical_session_permission_state.transcript.back().tool;
  auto canonical_session_render = std::string{};
  if (canonical_session_item)
  {
    for (auto const& line : ava::tui::detail::render_tool_card(*canonical_session_item, 100, true)) canonical_session_render += strip_sgr(line) + '\n';
  }
  auto const canonical_session_copy = canonical_session_item ? ava::tui::detail::tool_card_copy_text(*canonical_session_item) : std::string{};
  expect(canonical_session_item && canonical_session_item->permissions.size() == 1 && canonical_session_item->permissions[0].decision == "allow_session" &&
             canonical_session_render.find("permission: allow session") != std::string::npos &&
             canonical_session_copy.find("permission: allow session") != std::string::npos,
         "tui canonical permission_replied allow_session renders consistently on the settled card and copy text");

  ava::tui::TuiEventState correlated_tool_state;
  ava::app::EventEnvelope correlated_provider_delta{.schema_version = 1,
                                                    .event_id = "event_tool_delta",
                                                    .timestamp = "2026-04-30T00:00:00Z",
                                                    .session_id = "session_test",
                                                    .run_id = "run_tool",
                                                    .turn_id = "turn_tool",
                                                    .message_id = "message_tool",
                                                    .request_id = "request_tool",
                                                    .correlation_id = "corr_tool",
                                                    .name = "provider_event",
                                                    .payload_json =
                                                        "{\"status\":\"tool_call_delta\",\"tool_name\":\"grep\","
                                                        "\"text\":\"{\\\"pattern\\\":\"}"};
  ava::tui::apply_event_envelope(correlated_tool_state, correlated_provider_delta);
  ava::app::EventEnvelope correlated_progress{.schema_version = 1,
                                              .event_id = "event_tool_progress",
                                              .timestamp = "2026-04-30T00:00:01Z",
                                              .session_id = "session_test",
                                              .run_id = "run_tool",
                                              .turn_id = "turn_tool",
                                              .message_id = "message_tool",
                                              .request_id = "request_tool",
                                              .correlation_id = "corr_tool",
                                              .name = "tool_progress",
                                              .payload_json =
                                                  "{\"tool_name\":\"grep\",\"text\":\"scanned 10 files\","
                                                  "\"status\":\"running\"}"};
  ava::tui::apply_event_envelope(correlated_tool_state, correlated_progress);
  expect(correlated_tool_state.pending_tools.size() == 1 && correlated_tool_state.pending_tools[0].call_id == "corr_tool" &&
             correlated_tool_state.pending_tools[0].request_id == "request_tool" && correlated_tool_state.pending_tools[0].correlation_id == "corr_tool" &&
             correlated_tool_state.pending_tools[0].item.result_summary == "scanned 10 files",
         "tui EventEnvelope reducer updates pending tools by backend request and correlation ids");

  ava::app::EventEnvelope correlated_result{.schema_version = 1,
                                            .event_id = "event_tool_result",
                                            .timestamp = "2026-04-30T00:00:02Z",
                                            .session_id = "session_test",
                                            .run_id = "run_tool",
                                            .turn_id = "turn_tool",
                                            .message_id = "message_tool",
                                            .request_id = "request_tool",
                                            .correlation_id = "corr_tool",
                                            .name = "tool_result",
                                            .payload_json =
                                                "{\"tool_name\":\"grep\",\"result_summary\":\"2 matches\","
                                                "\"args\":{\"pattern\":\"needle\"},"
                                                "\"result\":{\"ok\":true,\"matches\":2},"
                                                "\"status\":\"success\",\"truncated\":true,"
                                                "\"details_visible\":true,"
                                                "\"output_bytes\":256,\"total_bytes\":1024,"
                                                "\"output_lines\":4,\"total_lines\":20,"
                                                "\"start_line\":5,\"end_line\":8,\"next_offset_line\":9,"
                                                "\"omitted_bytes\":768,\"omitted_lines\":12,"
                                                "\"visible_matches\":2,\"total_matches\":8,"
                                                "\"spill_path\":\"/tmp/ava-spill/grep.txt\","
                                                "\"changed_paths\":[\"logs/output.txt\"],"
                                                "\"diff\":\"--- note.txt\\n+++ note.txt\\n-old\\n+new\","
                                                "\"diff_truncated\":true}"};
  ava::tui::apply_event_envelope(correlated_tool_state, correlated_result);
  auto const correlated_tool_render =
      ava::tui::render_composer(ava::tui::ComposerSnapshot{.mode = "build",
                                                           .provider = "openai",
                                                           .model = "gpt-5.5",
                                                           .session_id = "session_test",
                                                           .input = "",
                                                           .status = "ready",
                                                           .transcript = ava::tui::event_state_transcript_snapshot(correlated_tool_state),
                                                           .width = 120,
                                                           .height = 14,
                                                           .tool_details_visible = false});
  expect(correlated_tool_state.pending_tools.empty() && correlated_tool_state.transcript.size() == 1 && correlated_tool_state.transcript[0].tool &&
             correlated_tool_state.transcript[0].tool->lifecycle == ava::tui::ToolLifecycleState::Complete &&
             correlated_tool_state.transcript[0].tool->truncated && correlated_tool_state.transcript[0].tool->details_visible == true &&
             correlated_tool_state.transcript[0].tool->arguments_json == "{\"pattern\":\"needle\"}" &&
             correlated_tool_state.transcript[0].tool->result_json == "{\"ok\":true,\"matches\":2}" &&
             correlated_tool_state.transcript[0].tool->changed_paths.size() == 1 &&
             correlated_tool_state.transcript[0].tool->changed_paths[0] == "logs/output.txt" &&
             correlated_tool_state.transcript[0].tool->spill_path == "/tmp/ava-spill/grep.txt" &&
             std::ranges::any_of(correlated_tool_render,
                                 [](std::string const& line) {
                                   auto const visible = strip_sgr(line);
                                   return visible.find("truncation: truncated lines 5-8/20; next offset 9") != std::string::npos &&
                                          visible.find("omitted 768 bytes, 12 lines") != std::string::npos;
                                 }) &&
             std::ranges::any_of(correlated_tool_render, [](std::string const& line) { return strip_sgr(line).find("[diff truncated]") != std::string::npos; }),
         "tui EventEnvelope reducer settles completed tools with backend-provided truncation, spill, diff, and per-tool "
         "detail metadata");

  ava::app::runtime::Event error;
  error.type = ava::app::runtime::EventType::Error;
  error.error_message = "provider failed";
  error.error_details = "Provider: provider failed";
  ava::tui::apply_runtime_event(state, error);
  expect(state.run_status == ava::tui::TuiEventRunStatus::Error && state.error_text == "provider failed" &&
             state.error_details == "Provider: provider failed" && state.transcript.back().label == "error" &&
             state.transcript.back().text == "provider failed",
         "tui event state records runtime errors and exposes error transcript text");

  ava::tui::TuiEventState streaming_error_state;
  ava::app::runtime::Event streaming_delta;
  streaming_delta.type = ava::app::runtime::EventType::MessageUpdate;
  streaming_delta.model_id = "gpt-5.5";
  streaming_delta.text = "partial answer";
  ava::tui::apply_runtime_event(streaming_error_state, streaming_delta);
  ava::app::runtime::Event streaming_error;
  streaming_error.type = ava::app::runtime::EventType::Error;
  streaming_error.model_id = "gpt-5.5";
  streaming_error.error_message = "provider: curl transport failed";
  streaming_error.error_details = "provider: curl transport failed\noutput: event: response.created\ndata: {...}";
  ava::tui::apply_runtime_event(streaming_error_state, streaming_error);
  expect(streaming_error_state.transcript.size() == 2 && streaming_error_state.transcript[0].label == "ava" &&
             streaming_error_state.transcript[0].text == "partial answer" && streaming_error_state.transcript[1].label == "error" &&
             streaming_error_state.transcript[1].text == "provider: curl transport failed" &&
             streaming_error_state.transcript[1].text.find("output: event:") == std::string::npos,
         "tui event state commits partial assistant text before compact provider error messages");
  auto const collapsed_streaming_error =
      ava::tui::render_composer(ava::tui::ComposerSnapshot{.mode = "build",
                                                           .provider = "openai",
                                                           .model = "gpt-5.5",
                                                           .session_id = "session_test",
                                                           .input = "",
                                                           .status = "ready",
                                                           .transcript = ava::tui::event_state_transcript_snapshot(streaming_error_state),
                                                           .width = 88,
                                                           .height = 12,
                                                           .tool_details_visible = false});
  auto const expanded_streaming_error =
      ava::tui::render_composer(ava::tui::ComposerSnapshot{.mode = "build",
                                                           .provider = "openai",
                                                           .model = "gpt-5.5",
                                                           .session_id = "session_test",
                                                           .input = "",
                                                           .status = "ready",
                                                           .transcript = ava::tui::event_state_transcript_snapshot(streaming_error_state),
                                                           .width = 88,
                                                           .height = 16,
                                                           .tool_details_visible = true});
  expect(std::ranges::any_of(collapsed_streaming_error, [](std::string const& line) { return strip_sgr(line).find("partial answer") != std::string::npos; }) &&
             std::ranges::any_of(collapsed_streaming_error,
                                 [](std::string const& line) { return strip_sgr(line).find("! provider: curl transport failed") != std::string::npos; }) &&
             std::ranges::any_of(collapsed_streaming_error,
                                 [](std::string const& line) { return strip_sgr(line).find("details hidden · /details") != std::string::npos; }) &&
             std::ranges::none_of(collapsed_streaming_error,
                                  [](std::string const& line) { return strip_sgr(line).find("output: event:") != std::string::npos; }) &&
             std::ranges::any_of(expanded_streaming_error,
                                 [](std::string const& line) { return strip_sgr(line).find("output: event: response.created") != std::string::npos; }),
         "tui renders concise collapsed errors while preserving partial assistant text and details-on-demand");

  ava::tui::TuiEventState canceled_state;
  ava::app::runtime::Event canceled;
  canceled.type = ava::app::runtime::EventType::Error;
  canceled.error_message = "agent loop canceled";
  canceled.error_details = "Unknown: agent loop canceled";
  ava::tui::apply_runtime_event(canceled_state, canceled);
  expect(canceled_state.run_status == ava::tui::TuiEventRunStatus::Canceled && canceled_state.error_text == "stopped by user" &&
             canceled_state.transcript.size() == 1 && canceled_state.transcript[0].label == "ava" &&
             canceled_state.transcript[0].text == "stopped by user. Submit a new prompt to continue." && !canceled_state.activity.empty() &&
             canceled_state.activity.back().label == "stopped" && canceled_state.activity.back().status == ava::tui::ToolTimelineStatus::Canceled &&
             canceled_state.activity.back().detail.find("submit a new prompt to continue") != std::string::npos,
         "tui event state presents cooperative cancellation with continuation guidance");

  ava::tui::TuiEventState explicit_canceled_state;
  ava::app::runtime::Event explicit_canceled;
  explicit_canceled.type = ava::app::runtime::EventType::Canceled;
  explicit_canceled.text = "stopped by user";
  explicit_canceled.reason = "cancel_requested";
  ava::tui::apply_runtime_event(explicit_canceled_state, explicit_canceled);
  expect(explicit_canceled_state.run_status == ava::tui::TuiEventRunStatus::Canceled && explicit_canceled_state.transcript.size() == 1 &&
             explicit_canceled_state.transcript[0].text == "stopped by user. Submit a new prompt to continue." &&
             explicit_canceled_state.activity.back().status == ava::tui::ToolTimelineStatus::Canceled &&
             explicit_canceled_state.activity.back().detail.find("cancel_requested") != std::string::npos &&
             explicit_canceled_state.activity.back().detail.find("submit a new prompt to continue") != std::string::npos,
         "tui event state accepts explicit backend canceled lifecycle events");

  ava::tui::TuiEventState lifecycle_state;
  ava::app::runtime::Event compaction_start;
  compaction_start.type = ava::app::runtime::EventType::CompactionStart;
  compaction_start.trigger = "auto";
  compaction_start.estimated_tokens = 9000;
  compaction_start.threshold_tokens = 8000;
  ava::tui::apply_runtime_event(lifecycle_state, compaction_start);
  expect(lifecycle_state.transcript.empty() && lifecycle_state.activity.size() == 1 && lifecycle_state.activity[0].label == "compaction" &&
             lifecycle_state.activity[0].detail.find("tokens~9000/8000") != std::string::npos,
         "tui event state keeps compaction starts in status activity without inventing transcript content");
  ava::app::runtime::Event retry;
  retry.type = ava::app::runtime::EventType::Retry;
  retry.reason = "context_overflow";
  retry.trigger = "context_overflow";
  retry.attempt = 1;
  retry.max_attempts = 1;
  retry.delay_ms = 250;
  retry.estimated_tokens = 9000;
  retry.threshold_tokens = 8000;
  retry.snapshot_entries = 3;
  retry.current_entries = 4;
  ava::tui::apply_runtime_event(lifecycle_state, retry);
  ava::app::runtime::Event retry_tick;
  retry_tick.type = ava::app::runtime::EventType::RetryTick;
  retry_tick.reason = "context_overflow";
  retry_tick.trigger = "context_overflow";
  retry_tick.attempt = 1;
  retry_tick.max_attempts = 1;
  retry_tick.delay_ms = 250;
  retry_tick.remaining_ms = 125;
  ava::tui::apply_runtime_event(lifecycle_state, retry_tick);
  ava::app::runtime::Event retry_tick_update = retry_tick;
  retry_tick_update.remaining_ms = 25;
  ava::tui::apply_runtime_event(lifecycle_state, retry_tick_update);
  ava::app::runtime::Event compaction_end;
  compaction_end.type = ava::app::runtime::EventType::CompactionEnd;
  compaction_end.trigger = "context_overflow";
  compaction_end.attempt = 1;
  compaction_end.max_attempts = 2;
  compaction_end.summary_bytes = 1234;
  ava::tui::apply_runtime_event(lifecycle_state, compaction_end);
  expect(lifecycle_state.transcript.size() == 3 && lifecycle_state.transcript[0].label == "audit" &&
             lifecycle_state.transcript[0].text.find("retrying after context_overflow") != std::string::npos &&
             lifecycle_state.transcript[0].text.find("attempt 1/1") != std::string::npos &&
             lifecycle_state.transcript[0].text.find("delay=250ms") != std::string::npos &&
             lifecycle_state.transcript[0].text.find("tokens~9000/8000") != std::string::npos &&
             lifecycle_state.transcript[0].text.find("entries=3/4") != std::string::npos && lifecycle_state.transcript[1].label == "audit" &&
             lifecycle_state.transcript[1].text.find("retry countdown after context_overflow") != std::string::npos &&
             lifecycle_state.transcript[1].text.find("remaining=25ms") != std::string::npos &&
             lifecycle_state.transcript[1].text.find("remaining=125ms") == std::string::npos && lifecycle_state.transcript[2].label == "compaction" &&
             lifecycle_state.transcript[2].text.find("compaction completed") != std::string::npos &&
             lifecycle_state.transcript[2].text.find("attempt 1/2") != std::string::npos &&
             lifecycle_state.transcript[2].text.find("summary=1234 bytes") != std::string::npos &&
             std::ranges::any_of(lifecycle_state.activity,
                                 [](ava::tui::SidebarActivityItem const& activity) {
                                   return activity.label == "retry" && activity.detail.find("remaining=25ms") != std::string::npos;
                                 }),
         "tui event state renders backend retry, retry countdown, and compaction markers with backend-provided detail");
  auto const compaction_render = ava::tui::render_composer(ava::tui::ComposerSnapshot{.mode = "build",
                                                                                      .provider = "openai",
                                                                                      .model = "gpt-5.5",
                                                                                      .session_id = "session_test",
                                                                                      .input = "",
                                                                                      .status = "ready",
                                                                                      .transcript = ava::tui::event_state_transcript_snapshot(lifecycle_state),
                                                                                      .width = 88,
                                                                                      .height = 12});
  expect(std::ranges::any_of(compaction_render,
                             [](std::string const& line) {
                               auto const visible = strip_sgr(line);
                               return visible.find("compact compaction completed") != std::string::npos &&
                                      visible.find("summary=1234 bytes") != std::string::npos;
                             }),
         "tui renders compaction lifecycle entries as dedicated long-session status cards");

  ava::tui::TuiEventState done_state;
  delta.text = "done text";
  ava::tui::apply_runtime_event(done_state, delta);
  ava::app::runtime::Event done;
  done.type = ava::app::runtime::EventType::Done;
  done.stop_reason = "stop";
  done.provider_iterations = 2;
  done.tool_calls = 1;
  ava::tui::apply_runtime_event(done_state, done);
  expect(done_state.run_status == ava::tui::TuiEventRunStatus::Done && done_state.stop_reason == "stop" && done_state.provider_iterations == 2 &&
             done_state.tool_calls == 1 && done_state.pending_assistant_text.empty() && done_state.transcript.size() == 1 &&
             done_state.transcript[0].text == "done text",
         "tui event state records done metadata and commits pending assistant text");

  std::vector<ava::app::runtime::Event> live_events;
  ava::app::runtime::Event parity_session;
  parity_session.type = ava::app::runtime::EventType::SessionStart;
  parity_session.provider_id = "openai";
  parity_session.model_id = "gpt-5.5";
  live_events.push_back(parity_session);
  ava::app::runtime::Event parity_user;
  parity_user.type = ava::app::runtime::EventType::UserMessage;
  parity_user.text = "inspect";
  live_events.push_back(parity_user);
  ava::app::runtime::Event parity_retry;
  parity_retry.type = ava::app::runtime::EventType::Retry;
  parity_retry.reason = "context_overflow";
  parity_retry.trigger = "context_overflow";
  parity_retry.attempt = 1;
  parity_retry.max_attempts = 1;
  live_events.push_back(parity_retry);
  ava::app::runtime::Event parity_retry_tick;
  parity_retry_tick.type = ava::app::runtime::EventType::RetryTick;
  parity_retry_tick.reason = "context_overflow";
  parity_retry_tick.trigger = "context_overflow";
  parity_retry_tick.attempt = 1;
  parity_retry_tick.max_attempts = 1;
  parity_retry_tick.delay_ms = 250;
  parity_retry_tick.remaining_ms = 0;
  live_events.push_back(parity_retry_tick);
  ava::app::runtime::Event parity_compaction_start;
  parity_compaction_start.type = ava::app::runtime::EventType::CompactionStart;
  parity_compaction_start.trigger = "context_overflow";
  parity_compaction_start.attempt = 1;
  parity_compaction_start.max_attempts = 2;
  live_events.push_back(parity_compaction_start);
  ava::app::runtime::Event parity_compaction_end;
  parity_compaction_end.type = ava::app::runtime::EventType::CompactionEnd;
  parity_compaction_end.trigger = "context_overflow";
  parity_compaction_end.attempt = 1;
  parity_compaction_end.max_attempts = 2;
  parity_compaction_end.summary_bytes = 512;
  live_events.push_back(parity_compaction_end);
  ava::app::runtime::Event parity_reasoning;
  parity_reasoning.type = ava::app::runtime::EventType::ReasoningDelta;
  parity_reasoning.text = "checking";
  live_events.push_back(parity_reasoning);
  ava::app::runtime::Event parity_delta;
  parity_delta.type = ava::app::runtime::EventType::MessageUpdate;
  parity_delta.model_id = "gpt-5.5";
  parity_delta.text = "answer";
  live_events.push_back(parity_delta);
  ava::app::runtime::Event parity_end;
  parity_end.type = ava::app::runtime::EventType::MessageEnd;
  parity_end.model_id = "gpt-5.5";
  live_events.push_back(parity_end);
  ava::app::runtime::Event parity_tool_start;
  parity_tool_start.type = ava::app::runtime::EventType::ToolStart;
  parity_tool_start.call_id = "call_parity";
  parity_tool_start.tool_name = "read_file";
  parity_tool_start.text = "path=README.md";
  live_events.push_back(parity_tool_start);
  ava::app::runtime::Event parity_tool_progress;
  parity_tool_progress.type = ava::app::runtime::EventType::ToolProgress;
  parity_tool_progress.call_id = "call_parity";
  parity_tool_progress.tool_name = "read_file";
  parity_tool_progress.text = "reading";
  parity_tool_progress.status = "running";
  live_events.push_back(parity_tool_progress);
  ava::app::runtime::Event parity_tool_result;
  parity_tool_result.type = ava::app::runtime::EventType::ToolResult;
  parity_tool_result.call_id = "call_parity";
  parity_tool_result.tool_name = "read_file";
  parity_tool_result.text = "12 bytes";
  parity_tool_result.status = "success";
  live_events.push_back(parity_tool_result);
  ava::app::runtime::Event parity_audit;
  parity_audit.type = ava::app::runtime::EventType::ProviderEvent;
  parity_audit.status = "tui:question_answer";
  parity_audit.text = "question answered: yes";
  live_events.push_back(parity_audit);
  ava::app::runtime::Event parity_done;
  parity_done.type = ava::app::runtime::EventType::Done;
  parity_done.stop_reason = "stop";
  parity_done.provider_iterations = 1;
  parity_done.tool_calls = 1;
  live_events.push_back(parity_done);

  ava::tui::TuiEventState live_state;
  ava::tui::TuiEventState replayed_state;
  ava::app::EventEnvelopeContext parity_context;
  parity_context.run_id = "run_1";
  parity_context.turn_id = "turn_1";
  parity_context.message_id = "message_1";
  parity_context.request_id = "request_1";
  parity_context.correlation_id = "correlation_1";
  for (auto const& event : live_events)
  {
    ava::tui::apply_runtime_event(live_state, event);
    ava::tui::apply_event_envelope(replayed_state, ava::app::to_event_envelope(event, parity_context));
  }
  auto visible_lines = [](std::vector<std::string> const& rendered) {
    std::vector<std::string> visible;
    visible.reserve(rendered.size());
    for (auto const& line : rendered) visible.push_back(strip_sgr(line));
    return visible;
  };
  auto const live_render =
      visible_lines(ava::tui::render_composer(ava::tui::ComposerSnapshot{.mode = "build",
                                                                         .provider = "openai",
                                                                         .model = "gpt-5.5",
                                                                         .session_id = "session_test",
                                                                         .input = "",
                                                                         .status = "ready",
                                                                         .transcript = ava::tui::event_state_transcript_snapshot(live_state),
                                                                         .width = 72,
                                                                         .height = 20}));
  auto const replayed_render =
      visible_lines(ava::tui::render_composer(ava::tui::ComposerSnapshot{.mode = "build",
                                                                         .provider = "openai",
                                                                         .model = "gpt-5.5",
                                                                         .session_id = "session_test",
                                                                         .input = "",
                                                                         .status = "ready",
                                                                         .transcript = ava::tui::event_state_transcript_snapshot(replayed_state),
                                                                         .width = 72,
                                                                         .height = 20}));
  expect(live_render == replayed_render && replayed_state.active_run_id == "run_1" && replayed_state.active_turn_id == "turn_1" &&
             replayed_state.active_message_id == "message_1" && replayed_state.active_request_id == "request_1" &&
             replayed_state.active_correlation_id == "correlation_1",
         "tui EventEnvelope replay renders the same visible transcript story as live runtime::Event reduction and tracks "
         "backend ids");

  ava::tui::TuiEventState resolver_state;
  ava::app::EventEnvelope permission_requested{.schema_version = 1,
                                               .event_id = "event_permission",
                                               .timestamp = "2026-04-30T00:00:00Z",
                                               .session_id = "session_test",
                                               .run_id = "run_prompt",
                                               .turn_id = "turn_prompt",
                                               .message_id = std::nullopt,
                                               .request_id = "request_prompt",
                                               .correlation_id = "request_prompt",
                                               .name = "permission_requested",
                                               .payload_json =
                                                   "{\"resolver_request_id\":\"permission_1\","
                                                   "\"operation\":\"shell.run\",\"mode\":\"build\","
                                                   "\"target_path\":\"\",\"command\":\"pwd\","
                                                   "\"tool_name\":\"bash\",\"reason\":\"needs approval\"}"};
  ava::tui::apply_event_envelope(resolver_state, permission_requested);
  expect(resolver_state.transcript.size() == 1 && resolver_state.transcript[0].label == "audit" &&
             resolver_state.transcript[0].text.find("permission requested: bash pwd") != std::string::npos && !resolver_state.activity.empty() &&
             resolver_state.activity[0].label == "permission" && resolver_state.active_run_id == "run_prompt",
         "tui EventEnvelope reducer records shared permission request envelopes without inventing prompt decisions");

  ava::app::EventEnvelope permission_replied{.schema_version = 1,
                                             .event_id = "event_permission_reply",
                                             .timestamp = "2026-04-30T00:00:00Z",
                                             .session_id = "session_test",
                                             .run_id = "run_prompt",
                                             .turn_id = "turn_prompt",
                                             .message_id = std::nullopt,
                                             .request_id = "request_prompt",
                                             .correlation_id = "request_prompt",
                                             .name = "permission_replied",
                                             .payload_json =
                                                 "{\"resolver_request_id\":\"permission_1\","
                                                 "\"decision\":\"deny\"}"};
  ava::tui::apply_event_envelope(resolver_state, permission_replied);
  expect(resolver_state.transcript.size() == 2 && resolver_state.transcript[1].label == "audit" && resolver_state.transcript[1].text == "permission replied" &&
             std::ranges::any_of(resolver_state.activity,
                                 [](ava::tui::SidebarActivityItem const& item) { return item.label == "permission" && item.detail == "permission replied"; }),
         "tui EventEnvelope reducer records shared permission reply envelopes");

  ava::app::EventEnvelope question_requested{.schema_version = 1,
                                             .event_id = "event_question",
                                             .timestamp = "2026-04-30T00:00:01Z",
                                             .session_id = "session_test",
                                             .run_id = "run_prompt",
                                             .turn_id = "turn_prompt",
                                             .message_id = std::nullopt,
                                             .request_id = "request_question",
                                             .correlation_id = "request_question",
                                             .name = "question_requested",
                                             .payload_json = "{\"question\":\"Pick an option\"}"};
  ava::tui::apply_event_envelope(resolver_state, question_requested);
  expect(resolver_state.transcript.size() == 3 && resolver_state.transcript[2].label == "audit" &&
             resolver_state.transcript[2].text == "question requested: Pick an option" &&
             std::ranges::any_of(
                 resolver_state.activity,
                 [](ava::tui::SidebarActivityItem const& item) { return item.label == "question" && item.detail.find("Pick an option") != std::string::npos; }),
         "tui EventEnvelope reducer records shared question request envelopes without inventing prompt answers");

  ava::app::EventEnvelope question_replied{.schema_version = 1,
                                           .event_id = "event_question_reply",
                                           .timestamp = "2026-04-30T00:00:01Z",
                                           .session_id = "session_test",
                                           .run_id = "run_prompt",
                                           .turn_id = "turn_prompt",
                                           .message_id = std::nullopt,
                                           .request_id = "request_question",
                                           .correlation_id = "request_question",
                                           .name = "question_replied",
                                           .payload_json =
                                               "{\"resolver_request_id\":\"question_1\","
                                               "\"answer\":\"custom ok\"}"};
  ava::tui::apply_event_envelope(resolver_state, question_replied);
  expect(resolver_state.transcript.size() == 4 && resolver_state.transcript[3].label == "audit" && resolver_state.transcript[3].text == "question replied" &&
             std::ranges::any_of(resolver_state.activity,
                                 [](ava::tui::SidebarActivityItem const& item) { return item.label == "question" && item.detail == "question replied"; }),
         "tui EventEnvelope reducer records shared question reply envelopes");

  ava::app::EventEnvelope steer_queued{.schema_version = 1,
                                       .event_id = "event_steer",
                                       .timestamp = "2026-04-30T00:00:02Z",
                                       .session_id = "session_test",
                                       .run_id = "run_prompt",
                                       .turn_id = "turn_prompt",
                                       .message_id = std::nullopt,
                                       .request_id = "request_steer",
                                       .correlation_id = "request_steer",
                                       .name = "steer_queued",
                                       .payload_json = "{\"message\":\"Use smaller patch groups\"}"};
  ava::tui::apply_event_envelope(resolver_state, steer_queued);
  expect(resolver_state.transcript.size() == 5 && resolver_state.transcript.back().label == "audit" &&
             resolver_state.transcript.back().text.find("steer queued") != std::string::npos &&
             resolver_state.transcript.back().text.find("Use smaller patch groups") != std::string::npos && resolver_state.queued_messages.size() == 1 &&
             resolver_state.queued_messages.back().kind == "steer" &&
             std::ranges::any_of(resolver_state.activity,
                                 [](ava::tui::SidebarActivityItem const& item) {
                                   return item.label == "steer" && item.detail.find("Use smaller patch groups") != std::string::npos &&
                                          item.status == ava::tui::ToolTimelineStatus::Running;
                                 }),
         "tui EventEnvelope reducer surfaces backend queued steer events in transcript and sidebar activity");

  ava::app::EventEnvelope follow_up_queued{.schema_version = 1,
                                           .event_id = "event_follow_queue",
                                           .timestamp = "2026-04-30T00:00:02Z",
                                           .session_id = "session_test",
                                           .run_id = "run_prompt",
                                           .turn_id = "turn_prompt",
                                           .message_id = std::nullopt,
                                           .request_id = "request_follow",
                                           .correlation_id = "request_steer",
                                           .name = "follow_up_queued",
                                           .payload_json = "{\"message\":\"Continue after tests\"}"};
  ava::tui::apply_event_envelope(resolver_state, follow_up_queued);
  expect(resolver_state.transcript.size() == 6 && resolver_state.transcript.back().text.find("follow-up queued") != std::string::npos &&
             resolver_state.queued_messages.size() == 2 && resolver_state.queued_messages.back().kind == "follow-up" &&
             std::ranges::any_of(resolver_state.activity,
                                 [](ava::tui::SidebarActivityItem const& item) {
                                   return item.label == "follow-up" && item.status == ava::tui::ToolTimelineStatus::Running &&
                                          item.detail.find("Continue after tests") != std::string::npos;
                                 }),
         "tui EventEnvelope reducer records backend queued follow-up events");

  ava::app::EventEnvelope follow_up_started{.schema_version = 1,
                                            .event_id = "event_follow_start",
                                            .timestamp = "2026-04-30T00:00:02Z",
                                            .session_id = "session_test",
                                            .run_id = "run_prompt",
                                            .turn_id = "turn_prompt",
                                            .message_id = std::nullopt,
                                            .request_id = "request_follow",
                                            .correlation_id = "request_follow",
                                            .name = "follow_up_started",
                                            .payload_json = "{\"message\":\"Continue after tests\"}"};
  ava::tui::apply_event_envelope(resolver_state, follow_up_started);
  expect(resolver_state.transcript.size() == 7 && resolver_state.transcript.back().text.find("follow-up started") != std::string::npos &&
             resolver_state.queued_messages.size() == 1 &&
             std::ranges::any_of(resolver_state.activity,
                                 [](ava::tui::SidebarActivityItem const& item) {
                                   return item.label == "follow-up" && item.status == ava::tui::ToolTimelineStatus::Running &&
                                          item.detail.find("follow-up started") != std::string::npos;
                                 }),
         "tui EventEnvelope reducer records backend follow-up start events");

  ava::app::EventEnvelope follow_up_skipped{.schema_version = 1,
                                            .event_id = "event_follow_skip",
                                            .timestamp = "2026-04-30T00:00:02Z",
                                            .session_id = "session_test",
                                            .run_id = "run_prompt",
                                            .turn_id = "turn_prompt",
                                            .message_id = std::nullopt,
                                            .request_id = "request_follow",
                                            .correlation_id = "request_follow",
                                            .name = "follow_up_skipped",
                                            .payload_json =
                                                "{\"message\":\"Continue after tests\","
                                                "\"reason\":\"canceled\","
                                                "\"message_truncated\":true,"
                                                "\"message_bytes\":4096}"};
  ava::tui::apply_event_envelope(resolver_state, follow_up_skipped);
  expect(resolver_state.transcript.size() == 8 &&
             resolver_state.transcript.back().text.find("follow-up skipped: run stopped before delivery; submit it again to continue") != std::string::npos &&
             resolver_state.transcript.back().text.find("message truncated from 4096 bytes") != std::string::npos &&
             std::ranges::any_of(resolver_state.activity,
                                 [](ava::tui::SidebarActivityItem const& item) {
                                   return item.label == "follow-up" && item.status == ava::tui::ToolTimelineStatus::Error &&
                                          item.detail.find("Continue after tests") != std::string::npos;
                                 }),
         "tui EventEnvelope reducer records skipped follow-up events with continuation guidance and truncation metadata");

  ava::tui::TuiEventState steering_skip_state;
  ava::app::EventEnvelope steer_skipped{.schema_version = 1,
                                        .event_id = "event_steer_skip",
                                        .timestamp = "2026-04-30T00:00:02Z",
                                        .session_id = "session_test",
                                        .run_id = "run_prompt",
                                        .turn_id = "turn_prompt",
                                        .message_id = std::nullopt,
                                        .request_id = "request_steer",
                                        .correlation_id = "request_steer",
                                        .name = "steer_skipped",
                                        .payload_json =
                                            "{\"message\":\"Use smaller patch groups\","
                                            "\"reason\":\"run_completed_before_safe_point\"}"};
  ava::tui::apply_event_envelope(steering_skip_state, steer_skipped);
  expect(steering_skip_state.transcript.size() == 1 &&
             steering_skip_state.transcript.back().text.find("steer skipped: run finished before the next safe steering point") != std::string::npos &&
             steering_skip_state.transcript.back().text.find("Use smaller patch groups") != std::string::npos,
         "tui EventEnvelope reducer explains skipped steering when the turn finishes before a safe point");

  ava::app::EventEnvelope cancel_requested{.schema_version = 1,
                                           .event_id = "event_cancel",
                                           .timestamp = "2026-04-30T00:00:03Z",
                                           .session_id = "session_test",
                                           .run_id = "run_prompt",
                                           .turn_id = "turn_prompt",
                                           .message_id = std::nullopt,
                                           .request_id = "cancel_request",
                                           .correlation_id = "request_prompt",
                                           .name = "cancel_requested",
                                           .payload_json =
                                               "{\"active_run\":true,\"cleared_steer\":1,"
                                               "\"cleared_follow_up\":2,\"active_request_id\":\"request_prompt\"}"};
  ava::tui::apply_event_envelope(resolver_state, cancel_requested);
  expect(resolver_state.transcript.back().label == "audit" &&
             resolver_state.transcript.back().text.find("cancel requested for active run") != std::string::npos &&
             resolver_state.transcript.back().text.find("steer=1 follow-up=2") != std::string::npos && resolver_state.activity.back().label == "cancel",
         "tui EventEnvelope reducer surfaces backend cancel requests without pretending the run has finished");
}
void test_tui_active_nonblocking_command_lane()
{
  ava::tui::TuiActiveRunQueues queues;
  std::size_t callback_calls = 0;
  std::size_t queue_calls = 0;
  queues.run_nonblocking_command = [&callback_calls](std::string const& submitted) -> std::optional<std::vector<std::string>> {
    if (!submitted.starts_with("/jobs"))
      return std::nullopt;
    ++callback_calls;
    return std::vector<std::string>{"promoted without modal"};
  };
  queues.queue_follow_up = [&queue_calls](std::string) -> ava::core::VoidResult {
    ++queue_calls;
    return {};
  };

  auto disabled_snapshot = ava::tui::ComposerSnapshot{};
  disabled_snapshot.input = "/jobs blocked";
  disabled_snapshot.input_cursor = disabled_snapshot.input.size();
  disabled_snapshot.path_completion_force_active = true;
  disabled_snapshot.file_references = {
      ava::tui::FileReferenceItem{.value = "blocked", .description = {}, .enabled = false, .disabled_reason = "outside workspace"}};
  auto const original_input = disabled_snapshot.input;
  auto const original_cursor = disabled_snapshot.input_cursor;
  auto const blocked = ava::tui::dispatch_tui_active_nonblocking_command_gated(disabled_snapshot, queues, disabled_snapshot.input);
  auto const blocked_callback_calls = callback_calls;
  auto const blocked_queue_calls = queue_calls;

  auto enabled_snapshot = disabled_snapshot;
  enabled_snapshot.file_references.front().enabled = true;
  auto const handled = ava::tui::dispatch_tui_active_nonblocking_command_gated(enabled_snapshot, queues, "/jobs promote job_1");
  auto const unrecognized = ava::tui::dispatch_tui_active_nonblocking_command_gated(enabled_snapshot, queues, "/models");
  ava::tui::TuiActiveRunQueues unavailable;
  auto const missing = ava::tui::dispatch_tui_active_nonblocking_command_gated(enabled_snapshot, unavailable, "/jobs");

  expect(blocked.kind == ava::tui::TuiActiveNonblockingCommandDispatchKind::Blocked && blocked.status == "path disabled: outside workspace" &&
             blocked.output.empty() && blocked_callback_calls == 0 && blocked_queue_calls == 0 && callback_calls == 1 && queue_calls == 0 &&
             disabled_snapshot.input == original_input && disabled_snapshot.input_cursor == original_cursor &&
             handled.kind == ava::tui::TuiActiveNonblockingCommandDispatchKind::Handled &&
             handled.output == std::vector<std::string>{"promoted without modal"} &&
             unrecognized.kind == ava::tui::TuiActiveNonblockingCommandDispatchKind::Unrecognized &&
             missing.kind == ava::tui::TuiActiveNonblockingCommandDispatchKind::Unrecognized,
         "tui active route seam blocks a disabled forced path before callback or queue mutation, while enabled and unrecognized commands retain dispatch "
         "classification");
}

void test_tui_selector_authority_dispatch_paints_before_callback()
{
  ava::tui::ComposerSnapshot snapshot;
  snapshot.mode = "build";
  snapshot.provider = "openai";
  snapshot.model = "gpt-5.5";
  snapshot.session_id = "session_current";
  snapshot.input = "draft survives";
  snapshot.status = "selecting";
  snapshot.select_list = ava::tui::SelectListView{.title = "Select model",
                                                  .subtitle = {},
                                                  .items = {ava::tui::SelectListItemView{.value = "openai/gpt-5.5",
                                                                                         .label = "GPT-5.5",
                                                                                         .description = {},
                                                                                         .group = "Models",
                                                                                         .detail = {},
                                                                                         .badge = {},
                                                                                         .current = false,
                                                                                         .enabled = true,
                                                                                         .disabled_reason = {}}},
                                                  .selected_item_index = 0,
                                                  .query = {},
                                                  .placeholder = "Search",
                                                  .empty_text = "No matches",
                                                  .footer_hint = "Esc cancel"};
  snapshot.input_cursor = 5;
  auto const original_input = snapshot.input;
  auto const original_cursor = snapshot.input_cursor;
  std::mutex mutex;
  std::condition_variable ready;
  std::condition_variable release;
  bool callback_started = false;
  bool callback_released = false;
  bool dispatch_succeeded = false;
  std::vector<std::string> events;

  std::thread dispatch([&]() {
    auto result = ava::tui::dispatch_tui_selector_authority(
        snapshot, "switching model…",
        [&]() {
          events.push_back("render");
          return true;
        },
        [&]() -> ava::core::Result<ava::tui::TuiRuntimeStateSnapshot> {
          std::unique_lock lock(mutex);
          events.push_back("callback");
          callback_started = true;
          ready.notify_one();
          release.wait(lock, [&]() { return callback_released; });
          ava::tui::TuiRuntimeStateSnapshot state;
          state.model = "GPT-5.5";
          state.status = "model already selected";
          return state;
        });
    dispatch_succeeded = result.has_value();
  });
  {
    std::unique_lock lock(mutex);
    ready.wait(lock, [&]() { return callback_started; });
    expect(events == std::vector<std::string>({"render", "callback"}) && !snapshot.select_list && snapshot.status == "switching model…",
           "selector authority dispatch clears and paints truthful pending status before a blocking callback starts");
    callback_released = true;
    release.notify_one();
  }
  dispatch.join();
  expect(dispatch_succeeded && snapshot.input == original_input && snapshot.input_cursor == original_cursor,
         "successful selector authority dispatch preserves the underlying draft and cursor until authoritative state applies");

  snapshot.select_list = ava::tui::SelectListView{};
  snapshot.select_list->title = "Select session";
  auto failed = ava::tui::dispatch_tui_selector_authority(
      snapshot, "opening session…", []() { return true; },
      []() -> ava::core::Result<ava::tui::TuiRuntimeStateSnapshot> {
        return std::unexpected(ava::core::Error(ava::core::ErrorCategory::NotFound, "session unavailable"));
      });
  expect(!failed && !snapshot.select_list && snapshot.status.find("session unavailable") != std::string::npos && snapshot.input == original_input &&
             snapshot.input_cursor == original_cursor,
         "failed selector authority dispatch keeps modal cleared, reports authority failure, and retains draft and cursor");
}
}  // namespace

void run_tui_runtime_event_state_tests()
{
  test_tui_event_state_reduces_runtime_events();
}

void run_tui_runtime_dispatch_tests()
{
  test_tui_active_nonblocking_command_lane();
  test_tui_selector_authority_dispatch_paints_before_callback();
}
