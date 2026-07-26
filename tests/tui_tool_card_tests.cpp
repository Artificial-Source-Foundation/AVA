#include "sys.h"
#include "tests/support/test_harness.h"
#include "tests/support/tui_test_support.h"
#include "ava/tui/composer.h"
#include "ava/tui/composer_internal.h"
#include "ava/tui/runtime_internal.h"
#include "ava/tui/tool_cards.h"
#include "ava/core/json.h"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

void run_tui_tool_card_tests_part_1()
{
  auto const tool_card = ava::tui::render_composer(ava::tui::ComposerSnapshot{
      .mode = "build",
      .provider = "openai",
      .model = "gpt-5.5",
      .session_id = "session_test",
      .input = "",
      .status = "ready",
      .transcript = {ava::tui::TranscriptItem{.tool = ava::tui::ToolTimelineItem{.status = ava::tui::ToolTimelineStatus::Success,
                                                                                 .name = "read_file",
                                                                                 .argument_summary = "path=note.txt\x1b[31m",
                                                                                 .result_summary = "read lines 1-12/12",
                                                                                 .lifecycle = ava::tui::ToolLifecycleState::Complete}}},
      .width = 80,
      .height = 10});
  expect(std::ranges::any_of(tool_card,
                             [](std::string const& line) {
                               auto visible = strip_sgr(line);
                               return visible.find("+ read_file") != std::string::npos && visible.find("read lines 1-12/12") != std::string::npos &&
                                      visible.find("complete") == std::string::npos;
                             }) &&
             std::ranges::any_of(tool_card,
                                 [](std::string const& line) {
                                   return line.find("\x1b[38;2;52;211;153m+") != std::string::npos &&
                                          line.find("\x1b[1m\x1b[38;2;77;158;246mread_file") != std::string::npos;
                                 }),
         "tui renders a compact successful tool row without redundant lifecycle text");

  auto plain_tool_row = [](ava::tui::ToolTimelineItem const& item) {
    auto const rows = ava::tui::detail::render_tool_card(item, 120, false);
    return rows.empty() ? std::string{} : strip_sgr(rows.front());
  };
  auto running_outcome = ava::tui::ToolTimelineItem{.status = ava::tui::ToolTimelineStatus::Running,
                                                    .name = "read_file",
                                                    .argument_summary = "path=note.txt",
                                                    .call_id = "raw-call-running",
                                                    .lifecycle = ava::tui::ToolLifecycleState::Progress};
  auto successful_outcome = ava::tui::ToolTimelineItem{.status = ava::tui::ToolTimelineStatus::Success,
                                                       .name = "read_file",
                                                       .result_summary = "12 lines",
                                                       .result_json = "{\"duration_ms\":1200}",
                                                       .call_id = "raw-call-success",
                                                       .lifecycle = ava::tui::ToolLifecycleState::Complete};
  auto failed_outcome = ava::tui::ToolTimelineItem{
      .status = ava::tui::ToolTimelineStatus::Error, .name = "write_file", .call_id = "raw-call-failed", .lifecycle = ava::tui::ToolLifecycleState::Error};
  auto canceled_outcome = ava::tui::ToolTimelineItem{
      .status = ava::tui::ToolTimelineStatus::Canceled, .name = "task", .call_id = "raw-call-canceled", .lifecycle = ava::tui::ToolLifecycleState::Canceled};
  auto const running_row = plain_tool_row(running_outcome);
  auto const successful_row = plain_tool_row(successful_outcome);
  auto const failed_row = plain_tool_row(failed_outcome);
  auto const canceled_row = plain_tool_row(canceled_outcome);
  expect(running_row == "  │ ~ read_file · running · path=note.txt" && successful_row == "  │ + read_file · 12 lines · 1.2s" &&
             failed_row == "  │ x write_file · failed" && canceled_row == "  │ - task · canceled",
         "tui compact tool rows distinguish running, successful, failed, and canceled outcomes in plain text");
  expect(running_row.find("progress") == std::string::npos && successful_row.find("complete") == std::string::npos &&
             failed_row.find("raw-call-failed") == std::string::npos && canceled_row.find("raw-call-canceled") == std::string::npos,
         "tui compact tool rows omit low-level lifecycle words and raw call ids");
  expect(std::ranges::none_of(tool_card, [](std::string const& line) { return line.find("\x1b[31m") != std::string::npos; }),
         "tui tool card rendering removes untrusted raw sgr escape sequences");

  auto permission_tool_item = ava::tui::ToolTimelineItem{.status = ava::tui::ToolTimelineStatus::Error,
                                                         .name = "bash",
                                                         .argument_summary = "command=git push origin main",
                                                         .result_summary = "permission denied",
                                                         .arguments_json = "{\"command\":\"git push origin main\"}",
                                                         .result_json = "{\"tool\":\"bash\",\"exit_code\":126}",
                                                         .call_id = "call_permission",
                                                         .lifecycle = ava::tui::ToolLifecycleState::Error,
                                                         .permission_request_ids = {"permreq_push"},
                                                         .permissions = {ava::tui::ToolPermissionAuditItem{.permission_request_id = "permreq_push",
                                                                                                           .resolver_request_id = "permission_1",
                                                                                                           .decision = "deny",
                                                                                                           .operation = "bash",
                                                                                                           .tool_name = "bash",
                                                                                                           .risk = "critical",
                                                                                                           .reason = "command permission denied\x1b[31m",
                                                                                                           .target = "",
                                                                                                           .command = "<redacted one-shot command>",
                                                                                                           .resolution_reason = "selected deny"}}};
  auto pending_permission_item = permission_tool_item;
  pending_permission_item.status = ava::tui::ToolTimelineStatus::Running;
  pending_permission_item.lifecycle = ava::tui::ToolLifecycleState::ExecutionStarted;
  pending_permission_item.result_summary.clear();
  pending_permission_item.result_json.clear();
  pending_permission_item.permissions.front().decision.clear();
  pending_permission_item.permissions.front().resolution_reason.clear();
  auto const pending_permission_rows = ava::tui::detail::render_tool_card(pending_permission_item, 120, false);
  auto const pending_permission_text = pending_permission_rows.empty() ? std::string{} : strip_sgr(pending_permission_rows.front());
  auto const pending_permission_expanded = ava::tui::detail::render_tool_card(pending_permission_item, 120, true);
  auto const pending_permission_expanded_text = tui_test_support::join_visible_lines(pending_permission_expanded);
  auto const pending_permission_copy = ava::tui::detail::tool_card_copy_text(pending_permission_item);
  expect(pending_permission_rows.size() == 1 && pending_permission_text.find("permission required") != std::string::npos &&
             pending_permission_text.find("permission checked") == std::string::npos && pending_permission_text.find("permreq_push") == std::string::npos &&
             pending_permission_text.find("permission_1") == std::string::npos && pending_permission_text.find("executing") == std::string::npos,
         "tui compact pending tool audit is permission required without lifecycle or raw ids");
  expect(pending_permission_expanded_text.find("permission: required") != std::string::npos &&
             pending_permission_expanded_text.find("id: permreq_push") != std::string::npos &&
             pending_permission_copy.find("lifecycle: executing") != std::string::npos && pending_permission_copy.find("id permreq_push") != std::string::npos,
         "tui expanded and copied pending tool diagnostics retain lifecycle and permission ids");
  auto running_id_only_permission_item = pending_permission_item;
  running_id_only_permission_item.permissions.clear();
  auto settled_id_only_permission_item = running_id_only_permission_item;
  settled_id_only_permission_item.status = ava::tui::ToolTimelineStatus::Error;
  settled_id_only_permission_item.lifecycle = ava::tui::ToolLifecycleState::Error;
  auto settled_empty_decision_permission_item = pending_permission_item;
  settled_empty_decision_permission_item.status = ava::tui::ToolTimelineStatus::Error;
  settled_empty_decision_permission_item.lifecycle = ava::tui::ToolLifecycleState::Error;
  expect(plain_tool_row(running_id_only_permission_item).find("permission required") != std::string::npos &&
             plain_tool_row(settled_id_only_permission_item).find("permission checked") != std::string::npos &&
             plain_tool_row(pending_permission_item).find("permission required") != std::string::npos &&
             plain_tool_row(settled_empty_decision_permission_item).find("permission checked") != std::string::npos,
         "tui renders id-only and empty-decision permission audits as required only while tools are running");
  auto allowed_permission_item = pending_permission_item;
  allowed_permission_item.permissions.front().decision = "allow";
  auto denied_permission_item = pending_permission_item;
  denied_permission_item.permissions.front().decision = "deny";
  expect(plain_tool_row(allowed_permission_item).find("permission allow") != std::string::npos &&
             plain_tool_row(denied_permission_item).find("permission deny") != std::string::npos,
         "tui compact permission state distinguishes unresolved, allowed, and denied audits");

  auto const compact_permission_card =
      ava::tui::render_composer(ava::tui::ComposerSnapshot{.mode = "build",
                                                           .provider = "openai",
                                                           .model = "gpt-5.5",
                                                           .session_id = "session_test",
                                                           .input = "",
                                                           .status = "ready",
                                                           .transcript = {ava::tui::TranscriptItem{.tool = permission_tool_item}},
                                                           .width = 72,
                                                           .height = 12});
  expect(std::ranges::any_of(compact_permission_card,
                             [](std::string const& line) {
                               auto const visible = strip_sgr(line);
                               return visible.find("permission deny") != std::string::npos && visible.find("risk critical") != std::string::npos &&
                                      visible.find("reason command permiss...") != std::string::npos;
                             }) &&
             std::ranges::none_of(compact_permission_card,
                                  [](std::string const& line) {
                                    auto const visible = strip_sgr(line);
                                    return visible.find("permreq_push") != std::string::npos || visible.find("permission_1") != std::string::npos ||
                                           visible.find("/permissions audit") != std::string::npos ||
                                           visible.find("/permissions diagnose") != std::string::npos || visible.find("output:") != std::string::npos;
                                  }) &&
             std::ranges::none_of(compact_permission_card, [](std::string const& line) { return line.find("\x1b[31m") != std::string::npos; }) &&
             std::ranges::all_of(compact_permission_card, [](std::string const& line) { return visible_columns(line) <= 72; }),
         "tui compact tool cards preserve linked permission decision, risk, and reason while hiding audit ids and detail bodies");

  auto running_permission_item = permission_tool_item;
  running_permission_item.status = ava::tui::ToolTimelineStatus::Running;
  running_permission_item.lifecycle = ava::tui::ToolLifecycleState::ExecutionStarted;
  running_permission_item.result_json = "{\"output\":\"SECRET OUTPUT PREVIEW\"}";
  auto const running_permission_card = ava::tui::detail::render_tool_card(running_permission_item, 88, false);
  auto const running_permission_text = tui_test_support::join_visible_lines(running_permission_card);
  expect(running_permission_card.size() == 1 && running_permission_text.find("~ bash · permission deny") != std::string::npos &&
             running_permission_text.find("risk critical") != std::string::npos &&
             running_permission_text.find("reason command permission denied") != std::string::npos &&
             running_permission_text.find("permreq_push") == std::string::npos && running_permission_text.find("permission_1") == std::string::npos &&
             running_permission_text.find("SECRET OUTPUT PREVIEW") == std::string::npos && running_permission_text.find("executing") == std::string::npos,
         "tui collapsed running permission card keeps decision, risk, and reason on one row without ids or output bodies");

  auto structured_permission_item = permission_tool_item;
  structured_permission_item.result_summary =
      "permission_denied: command requires permission\n  action: ask\n  request_id: permreq_push\n  inspect: /permissions audit show permreq_push";
  structured_permission_item.argument_summary = structured_permission_item.result_summary;
  auto const structured_permission_card = ava::tui::detail::render_tool_card(structured_permission_item, 88, false);
  auto const structured_permission_text = tui_test_support::join_visible_lines(structured_permission_card);
  expect(structured_permission_text.find("permission deny") != std::string::npos && structured_permission_text.find("risk critical") != std::string::npos &&
             structured_permission_text.find("permission_denied:") == std::string::npos &&
             structured_permission_text.find("permreq_push") == std::string::npos && structured_permission_text.find("/permissions audit") == std::string::npos,
         "tui collapsed permission cards suppress raw structured result blocks while retaining the safety decision");

  structured_permission_item.arguments_json.clear();
  auto const& joined_tool_card_text = tui_test_support::join_visible_lines;
  auto const& count_text = tui_test_support::count_occurrences;
  auto const structured_permission_expanded = joined_tool_card_text(ava::tui::detail::render_tool_card(structured_permission_item, 56, true));
  auto const structured_permission_copy = ava::tui::detail::tool_card_copy_text(structured_permission_item);
  auto const structured_permission_only_copy = ava::tui::detail::tool_card_permission_copy_text(structured_permission_item);
  auto const curated_expanded_permission_fields_once = [&](std::string_view text) {
    return count_text(text, "permission: deny") == 1 && count_text(text, "risk: critical") == 1 && count_text(text, "id: permreq_push") == 1 &&
           count_text(text, "resolver: permission_1") == 1 && count_text(text, "reason: command permission denied") == 1 &&
           count_text(text, "resolution: selected deny") == 1 && count_text(text, "operation: bash") == 1 && count_text(text, "tool: bash") == 1 &&
           count_text(text, "command: <redacted one-shot command>") == 1 && count_text(text, "inspect: /permissions audit show permreq_push") == 1 &&
           count_text(text, "diagnose: /permissions diagnose permreq_push") == 1;
  };
  auto const curated_copied_permission_fields_once = [&](std::string_view text) {
    return count_text(text, "permission: deny · risk critical · id permreq_push") == 1 && count_text(text, "resolver permission_1") == 1 &&
           count_text(text, "reason command permission denied") == 1 && count_text(text, "resolution selected deny") == 1 &&
           count_text(text, "operation bash") == 1 && count_text(text, "tool bash") == 1 && count_text(text, "command <redacted one-shot command>") == 1 &&
           count_text(text, "inspect: /permissions audit show permreq_push") == 1 && count_text(text, "diagnose: /permissions diagnose permreq_push") == 1;
  };
  auto const excludes_raw_permission_dump = [](std::string_view text) {
    return text.find("permission_denied") == std::string_view::npos && text.find("action: ask") == std::string_view::npos &&
           text.find("request_id:") == std::string_view::npos;
  };
  expect(excludes_raw_permission_dump(structured_permission_expanded) && curated_expanded_permission_fields_once(structured_permission_expanded),
         "tui expanded denied shell cards omit the raw permission status dump and render each curated audit field once");
  expect(excludes_raw_permission_dump(structured_permission_copy) && curated_copied_permission_fields_once(structured_permission_copy) &&
             structured_permission_copy.find("status: error") != std::string::npos && structured_permission_copy.find("lifecycle: error") != std::string::npos,
         "tui general tool copy omits the raw permission status dump while preserving one curated audit and lifecycle status");
  expect(curated_copied_permission_fields_once(structured_permission_only_copy),
         "tui standalone permission copy continues to expose one complete curated permission audit");

  auto unrelated_shell_failure = permission_tool_item;
  unrelated_shell_failure.argument_summary = "command=git push origin main";
  unrelated_shell_failure.result_summary = "remote rejected the push";
  unrelated_shell_failure.result_json.clear();
  auto const unrelated_shell_failure_expanded = joined_tool_card_text(ava::tui::detail::render_tool_card(unrelated_shell_failure, 120, true));
  auto const unrelated_shell_failure_copy = ava::tui::detail::tool_card_copy_text(unrelated_shell_failure);
  expect(unrelated_shell_failure_expanded.find("remote rejected the push") != std::string::npos &&
             unrelated_shell_failure_copy.find("shell status: remote rejected the push") != std::string::npos,
         "tui permission audit deduplication preserves unrelated single-line shell failure status");

  auto unrelated_multiline_shell_failure = permission_tool_item;
  unrelated_multiline_shell_failure.argument_summary = "command=git push origin main";
  unrelated_multiline_shell_failure.result_summary = "remote rejected the push\nfailure: remote changed";
  unrelated_multiline_shell_failure.result_json = R"({"tool":"bash","exit_code":1,"stderr":"fatal: remote rejected the push\nhint: fetch before retrying"})";
  auto const unrelated_multiline_shell_failure_expanded =
      joined_tool_card_text(ava::tui::detail::render_tool_card(unrelated_multiline_shell_failure, 120, true));
  auto const unrelated_multiline_shell_failure_copy = ava::tui::detail::tool_card_copy_text(unrelated_multiline_shell_failure);
  expect(unrelated_multiline_shell_failure_expanded.find("remote rejected the push") != std::string::npos &&
             unrelated_multiline_shell_failure_expanded.find("failure: remote changed") != std::string::npos &&
             unrelated_multiline_shell_failure_expanded.find("fatal: remote rejected the push") != std::string::npos &&
             unrelated_multiline_shell_failure_expanded.find("hint: fetch before retrying") != std::string::npos &&
             unrelated_multiline_shell_failure_copy.find("remote rejected the push") != std::string::npos &&
             unrelated_multiline_shell_failure_copy.find("failure: remote changed") != std::string::npos &&
             unrelated_multiline_shell_failure_copy.find("fatal: remote rejected the push") != std::string::npos &&
             unrelated_multiline_shell_failure_copy.find("hint: fetch before retrying") != std::string::npos,
         "tui permission audit deduplication preserves unrelated multiline shell failure and output");

  auto unrelated_permission_denied_shell_failure = permission_tool_item;
  unrelated_permission_denied_shell_failure.argument_summary = "command=git status";
  unrelated_permission_denied_shell_failure.result_summary = "test category permission_denied remains user-visible";
  unrelated_permission_denied_shell_failure.result_json =
      R"({"tool":"bash","exit_code":1,"category":"permission_denied","stderr":"test output: permission_denied\nunrelated tool output"})";
  auto const unrelated_permission_denied_shell_failure_expanded =
      joined_tool_card_text(ava::tui::detail::render_tool_card(unrelated_permission_denied_shell_failure, 120, true));
  auto const unrelated_permission_denied_shell_failure_copy = ava::tui::detail::tool_card_copy_text(unrelated_permission_denied_shell_failure);
  expect(unrelated_permission_denied_shell_failure_expanded.find("test category permission_denied remains user-visible") != std::string::npos &&
             unrelated_permission_denied_shell_failure_expanded.find("test output: permission_denied") != std::string::npos &&
             unrelated_permission_denied_shell_failure_expanded.find("unrelated tool output") != std::string::npos &&
             unrelated_permission_denied_shell_failure_copy.find("test category permission_denied remains user-visible") != std::string::npos &&
             unrelated_permission_denied_shell_failure_copy.find("test output: permission_denied") != std::string::npos &&
             unrelated_permission_denied_shell_failure_copy.find("unrelated tool output") != std::string::npos,
         "tui permission audit deduplication preserves unrelated permission_denied shell result and output without a linked request id");

  auto completed_write = ava::tui::ToolTimelineItem{.status = ava::tui::ToolTimelineStatus::Success,
                                                    .name = "write_file",
                                                    .argument_summary = "path=src/cache.cpp",
                                                    .result_summary = "wrote 27 bytes",
                                                    .lifecycle = ava::tui::ToolLifecycleState::Complete,
                                                    .changed_paths = {"src/cache.cpp"}};
  auto const write_without_target_result = ava::tui::detail::render_tool_card(completed_write, 88, false);
  completed_write.result_summary = "wrote 27 bytes to src/cache.cpp";
  auto const write_with_target_result = ava::tui::detail::render_tool_card(completed_write, 88, false);
  auto count_target = [](std::vector<std::string> const& lines) {
    std::size_t count = 0;
    for (auto const& line : lines)
    {
      auto visible = strip_sgr(line);
      for (auto offset = visible.find("src/cache.cpp"); offset != std::string::npos; offset = visible.find("src/cache.cpp", offset + 1)) ++count;
    }
    return count;
  };
  expect(count_target(write_without_target_result) == 1 && count_target(write_with_target_result) == 1 && write_without_target_result.size() <= 2 &&
             write_with_target_result.size() <= 2,
         "tui completed write cards retain one workspace-relative target without duplicating a target already present in the result");

  auto absolute_path_item = ava::tui::ToolTimelineItem{.status = ava::tui::ToolTimelineStatus::Success,
                                                       .name = "write_file",
                                                       .argument_summary = "path=/home/user/workspace/src/main.cpp",
                                                       .result_summary = "wrote 27 bytes",
                                                       .lifecycle = ava::tui::ToolLifecycleState::Complete,
                                                       .changed_paths = {"/home/user/workspace/src/main.cpp"}};
  auto const absolute_path_card = ava::tui::detail::render_tool_card(absolute_path_item, 88, false);
  expect(std::ranges::none_of(absolute_path_card, [](std::string const& line) { return strip_sgr(line).find("/home/user/workspace") != std::string::npos; }) &&
             std::ranges::any_of(absolute_path_card, [](std::string const& line) { return strip_sgr(line).find("wrote 27 bytes") != std::string::npos; }),
         "tui collapsed tool cards omit absolute workspace paths rather than reducing them to ambiguous basenames");

  auto changed_path_priority = completed_write;
  changed_path_priority.argument_summary = "path=stale/input.cpp";
  changed_path_priority.result_summary = "wrote 27 bytes";
  changed_path_priority.changed_paths = {"src/main.cpp"};
  auto const changed_path_priority_card = ava::tui::detail::render_tool_card(changed_path_priority, 88, false);
  expect(changed_path_priority_card.size() == 1 && strip_sgr(changed_path_priority_card.front()).find("src/main.cpp · wrote 27 bytes") != std::string::npos &&
             strip_sgr(changed_path_priority_card.front()).find("stale/input.cpp") == std::string::npos,
         "settled mutation cards prefer a safe relative changed path over stale argument summaries");

  auto same_basename_multi_path_item = ava::tui::ToolTimelineItem{.status = ava::tui::ToolTimelineStatus::Success,
                                                                  .name = "write_file",
                                                                  .argument_summary = "path=a.cpp",
                                                                  .result_summary = "wrote /workspace/pkg/a.cpp and a.cpp",
                                                                  .lifecycle = ava::tui::ToolLifecycleState::Complete,
                                                                  .changed_paths = {"/workspace/pkg/a.cpp", "a.cpp"}};
  auto const same_basename_multi_path_text = tui_test_support::join_visible_lines(ava::tui::detail::render_tool_card(same_basename_multi_path_item, 120, true));
  expect(same_basename_multi_path_text.find("/workspace/pkg/a.cpp") != std::string::npos &&
             same_basename_multi_path_text.find("wrote /workspace/pkg/a.cpp and a.cpp") != std::string::npos,
         "tui multi-file cards retain distinct absolute and same-basename relative paths without cross-path aliasing");

  auto action_query_item = changed_path_priority;
  action_query_item.call_id = "call\nunsafe";
  auto const action_query_card = ava::tui::detail::render_tool_card(action_query_item, 120, true);
  auto const action_query_text = tui_test_support::join_visible_lines(action_query_card);

  expect(action_query_text.find("toggle: /tool src/main.cpp") != std::string::npos &&
             action_query_text.find("copy: /copy tool src/main.cpp") != std::string::npos && action_query_text.find("inspect: /tool") == std::string::npos &&
             ava::tui::detail::tool_card_matches_copy_query(action_query_item, "src/main.cpp"),
         "tool action rows reject sanitized call ids, fall back to a round-trippable changed path, and label /tool as toggle");
  action_query_item.call_id = "call-safe";
  auto const safe_call_action_text = tui_test_support::join_visible_lines(ava::tui::detail::render_tool_card(action_query_item, 120, true));
  expect(safe_call_action_text.find("toggle: /tool call-safe") != std::string::npos &&
             ava::tui::detail::tool_card_matches_copy_query(action_query_item, "call-safe"),
         "tool action rows use an unchanged single-line call id that round-trips through query matching");
  auto parse_rendered_tool_action = [](std::string_view rendered) {
    constexpr std::string_view marker = "toggle: ";
    auto const start = rendered.find(marker);
    if (start == std::string_view::npos)
      return std::optional<std::string>{};
    auto command = rendered.substr(start + marker.size());
    if (auto const end = command.find('\n'); end != std::string_view::npos)
      command = command.substr(0, end);
    return ava::tui::parse_tui_tool_command_argument(command);
  };
  auto narrow_action_item = ava::tui::ToolTimelineItem{.status = ava::tui::ToolTimelineStatus::Success,
                                                       .name = "write",
                                                       .result_summary = "wrote",
                                                       .call_id = "call-" + std::string(48, 'x'),
                                                       .lifecycle = ava::tui::ToolLifecycleState::Complete,
                                                       .diff = "--- a/src/main.cpp\n+++ b/src/main.cpp"};
  auto const narrow_action_card = ava::tui::detail::render_tool_card(narrow_action_item, 36, true);
  auto const narrow_action_text = tui_test_support::join_visible_lines(narrow_action_card);
  auto const parsed_narrow_action = parse_rendered_tool_action(narrow_action_text);
  expect(narrow_action_text.find("toggle: /tool write") != std::string::npos && narrow_action_text.find("copy: /copy tool write") != std::string::npos &&
             narrow_action_text.find("copy diff: /copy diff write") != std::string::npos &&
             std::ranges::all_of(narrow_action_card,
                                 [](std::string const& row) {
                                   auto const visible = strip_sgr(row);
                                   auto const action = visible.find("toggle:") != std::string::npos || visible.find("copy:") != std::string::npos ||
                                                       visible.find("copy diff:") != std::string::npos;
                                   return !action || visible.find("...") == std::string::npos;
                                 }) &&
             parsed_narrow_action == "write" && ava::tui::detail::tool_card_matches_copy_query(narrow_action_item, *parsed_narrow_action) &&
             std::ranges::all_of(narrow_action_card, [](std::string const& row) { return visible_columns(row) <= 36; }),
         "narrow tool action rows fall back from a long call id to a matching tool name and preserve every command exactly");
  auto no_fit_action_item = narrow_action_item;
  no_fit_action_item.name = "impossibly-long-tool-name";
  no_fit_action_item.diff.clear();
  auto const no_fit_action_text = tui_test_support::join_visible_lines(ava::tui::detail::render_tool_card(no_fit_action_item, 32, true));
  expect(no_fit_action_text.find("toggle: /tool") == std::string::npos && no_fit_action_text.find("copy: /copy tool") == std::string::npos,
         "tool action rows are omitted when no safe matching query fits every emitted command row");
  auto whitespace_id_item = action_query_item;
  whitespace_id_item.call_id = "   ";
  auto const whitespace_id_action_text = tui_test_support::join_visible_lines(ava::tui::detail::render_tool_card(whitespace_id_item, 120, true));

  auto edge_whitespace_item = action_query_item;
  edge_whitespace_item.call_id = " call-edge ";
  edge_whitespace_item.changed_paths = {" changed-edge.cpp "};
  auto const edge_whitespace_action_text = tui_test_support::join_visible_lines(ava::tui::detail::render_tool_card(edge_whitespace_item, 120, true));

  auto const parsed_whitespace_fallback = parse_rendered_tool_action(whitespace_id_action_text);
  auto const parsed_edge_fallback = parse_rendered_tool_action(edge_whitespace_action_text);
  expect(parsed_whitespace_fallback == "src/main.cpp" && parsed_edge_fallback == "write_file" &&
             ava::tui::detail::tool_card_matches_copy_query(whitespace_id_item, *parsed_whitespace_fallback) &&
             ava::tui::detail::tool_card_matches_copy_query(edge_whitespace_item, *parsed_edge_fallback) &&
             edge_whitespace_action_text.find("toggle: /tool call-edge") == std::string::npos &&
             edge_whitespace_action_text.find("toggle: /tool\n") == std::string::npos,
         "tool action queries reject whitespace-only and edge-whitespace values, then render/parse a matching fallback instead of bare latest /tool");
  action_query_item.call_id = "\x1b[31munsafe";
  action_query_item.changed_paths = {"/absolute/not-safe"};
  action_query_item.name = "write\nunsafe";
  auto const unsafe_action_text = tui_test_support::join_visible_lines(ava::tui::detail::render_tool_card(action_query_item, 120, true));
  expect(unsafe_action_text.find("toggle: /tool") == std::string::npos && unsafe_action_text.find("copy: /copy tool") == std::string::npos,
         "tool action rows are omitted when no supplied id, relative path, or tool name is an exact safe query");

  permission_tool_item.details_visible = true;
  auto const expanded_permission_card =
      ava::tui::render_composer(ava::tui::ComposerSnapshot{.mode = "build",
                                                           .provider = "openai",
                                                           .model = "gpt-5.5",
                                                           .session_id = "session_test",
                                                           .input = "",
                                                           .status = "ready",
                                                           .transcript = {ava::tui::TranscriptItem{.tool = permission_tool_item}},
                                                           .width = 88,
                                                           .height = 28});
  expect(
      std::ranges::any_of(expanded_permission_card, [](std::string const& line) { return strip_sgr(line).find("permission: deny") != std::string::npos; }) &&
          std::ranges::any_of(expanded_permission_card, [](std::string const& line) { return strip_sgr(line).find("risk: critical") != std::string::npos; }) &&
          std::ranges::any_of(expanded_permission_card,
                              [](std::string const& line) { return strip_sgr(line).find("id: permreq_push") != std::string::npos; }) &&
          std::ranges::any_of(expanded_permission_card,
                              [](std::string const& line) { return strip_sgr(line).find("resolver: permission_1") != std::string::npos; }) &&
          std::ranges::any_of(expanded_permission_card,
                              [](std::string const& line) { return strip_sgr(line).find("reason: command permission denied?") != std::string::npos; }) &&
          std::ranges::any_of(expanded_permission_card,
                              [](std::string const& line) { return strip_sgr(line).find("resolution: selected deny") != std::string::npos; }) &&
          std::ranges::any_of(expanded_permission_card, [](std::string const& line) { return strip_sgr(line).find("operation: bash") != std::string::npos; }) &&
          std::ranges::any_of(expanded_permission_card, [](std::string const& line) { return strip_sgr(line).find("tool: bash") != std::string::npos; }) &&
          std::ranges::any_of(expanded_permission_card,
                              [](std::string const& line) { return strip_sgr(line).find("command: <redacted one-shot command>") != std::string::npos; }) &&
          std::ranges::any_of(
              expanded_permission_card,
              [](std::string const& line) { return strip_sgr(line).find("inspect: /permissions audit show permreq_push") != std::string::npos; }) &&
          std::ranges::any_of(
              expanded_permission_card,
              [](std::string const& line) { return strip_sgr(line).find("diagnose: /permissions diagnose permreq_push") != std::string::npos; }) &&
          std::ranges::all_of(expanded_permission_card, [](std::string const& line) { return visible_columns(line) <= 88; }),
      "tui expanded tool cards expose permission audit ids, reviewed tool arguments, and follow-up commands on demand");

  {
    ScopedEnvVar no_color_permission_guard("NO_COLOR", "1");
    auto const plain_narrow_permission_card =
        ava::tui::render_composer(ava::tui::ComposerSnapshot{.mode = "build",
                                                             .provider = "openai",
                                                             .model = "gpt-5.5",
                                                             .session_id = "session_test",
                                                             .input = "",
                                                             .status = "ready",
                                                             .transcript = {ava::tui::TranscriptItem{.tool = permission_tool_item}},
                                                             .width = 56,
                                                             .height = 24,
                                                             .tool_details_visible = true});
    auto plain_permission_text = std::string{};
    for (auto const& line : plain_narrow_permission_card)
    {
      plain_permission_text += line;
      plain_permission_text += '\n';
    }
    auto const plain_permission_accessible =
        std::ranges::all_of(plain_narrow_permission_card,
                            [](std::string const& line) { return line.find('\x1b') == std::string::npos && visible_columns(line) <= 56; }) &&
        plain_permission_text.find("x bash") != std::string::npos && plain_permission_text.find("permission: deny") != std::string::npos &&
        plain_permission_text.find("risk: critical") != std::string::npos && plain_permission_text.find("id: permreq_push") != std::string::npos &&
        plain_permission_text.find("reason: command permission denied") != std::string::npos &&
        plain_permission_text.find("command: <redacted one-shot command>") != std::string::npos &&
        plain_permission_text.find("inspect: /permissions audit show permreq_push") != std::string::npos &&
        plain_permission_text.find("diagnose: /permissions diagnose permreq_push") != std::string::npos;
    expect(plain_permission_accessible, "tui plain narrow permission cards keep critical state readable without color");
  }

  auto const grouped_context_tools = ava::tui::render_composer(
      ava::tui::ComposerSnapshot{.mode = "build",
                                 .provider = "openai",
                                 .model = "gpt-5.5",
                                 .session_id = "session_test",
                                 .input = "",
                                 .status = "ready",
                                 .transcript = {ava::tui::TranscriptItem{.tool = ava::tui::ToolTimelineItem{.status = ava::tui::ToolTimelineStatus::Success,
                                                                                                            .name = "glob",
                                                                                                            .argument_summary = "pattern=src/**/*.cpp",
                                                                                                            .result_summary = "12 files"}},
                                                ava::tui::TranscriptItem{.tool = ava::tui::ToolTimelineItem{.status = ava::tui::ToolTimelineStatus::Success,
                                                                                                            .name = "grep",
                                                                                                            .argument_summary = "pattern=TODO",
                                                                                                            .result_summary = "3 matches"}},
                                                ava::tui::TranscriptItem{.tool = ava::tui::ToolTimelineItem{.status = ava::tui::ToolTimelineStatus::Success,
                                                                                                            .name = "read_file",
                                                                                                            .argument_summary = "path=src/main.cpp",
                                                                                                            .result_summary = "read lines 1-40"}}},
                                 .width = 96,
                                 .height = 16});
  expect(std::ranges::count_if(grouped_context_tools,
                               [](std::string const& line) { return strip_sgr(line).find("context gathering · 3 tools") != std::string::npos; }) == 1 &&
             std::ranges::any_of(grouped_context_tools, [](std::string const& line) { return strip_sgr(line).find("glob") != std::string::npos; }) &&
             std::ranges::any_of(grouped_context_tools, [](std::string const& line) { return strip_sgr(line).find("read_file") != std::string::npos; }),
         "tui groups consecutive context-gathering tool cards with a single readable heading while keeping details");

  auto const empty_tool_card = ava::tui::render_composer(ava::tui::ComposerSnapshot{
      .mode = "build",
      .provider = "openai",
      .model = "gpt-5.5",
      .session_id = "session_test",
      .input = "",
      .status = "ready",
      .transcript = {ava::tui::TranscriptItem{
          .tool = ava::tui::ToolTimelineItem{.status = ava::tui::ToolTimelineStatus::Success, .name = "", .argument_summary = "", .result_summary = ""}}},
      .width = 40,
      .height = 8});
  expect(std::ranges::any_of(empty_tool_card,
                             [](std::string const& line) {
                               auto const visible = strip_sgr(line);
                               return visible.find("+ unknown") != std::string::npos && visible.find("unknown") != std::string::npos;
                             }) &&
             std::ranges::all_of(empty_tool_card, [](std::string const& line) { return visible_columns(line) <= 40; }),
         "tui renders empty tool-card fields with a safe fallback name");

  auto const& visible_text = tui_test_support::join_visible_lines;
  auto const webfetch_item = ava::tui::ToolTimelineItem{.status = ava::tui::ToolTimelineStatus::Success,
                                                        .name = "webfetch",
                                                        .argument_summary = "url=https://example.test/docs\x1b[31m",
                                                        .result_summary = "fetched text/plain",
                                                        .result_json = "{\"content_type\":\"text/plain\",\"content\":\"alpha\\nbeta\\ngamma\"}",
                                                        .lifecycle = ava::tui::ToolLifecycleState::Complete,
                                                        .truncated = true,
                                                        .output_lines = 2,
                                                        .total_lines = 3};
  auto const webfetch_card = ava::tui::detail::render_tool_card(webfetch_item, 72, true);
  auto const webfetch_text = visible_text(webfetch_card);
  auto const webfetch_copy = ava::tui::detail::tool_card_copy_text(webfetch_item);
  expect(webfetch_text.find("webfetch") != std::string::npos && webfetch_text.find("url=https://example.test/docs") != std::string::npos,
         "tui non-shell webfetch cards preserve the tool name and sanitized arguments");
  expect(webfetch_text.find("output:") != std::string::npos && webfetch_text.find("alpha") != std::string::npos,
         "tui non-shell webfetch cards render expanded content previews");
  expect(webfetch_copy.find("output:\n  alpha\n  beta\n  gamma") != std::string::npos &&
             webfetch_copy.find("truncation: truncated 2/3 lines") != std::string::npos && webfetch_copy.find("\x1b[") == std::string::npos,
         "tui non-shell webfetch copy text includes preview, truncation, and no ANSI styling");
  expect(std::ranges::none_of(webfetch_card, [](std::string const& line) { return line.find("\x1b[31m") != std::string::npos; }) &&
             std::ranges::all_of(webfetch_card, [](std::string const& line) { return visible_columns(line) <= 72; }),
         "tui non-shell webfetch cards remove raw escapes and stay within width");

  auto const non_shell_cards =
      std::vector<ava::tui::ToolTimelineItem>{ava::tui::ToolTimelineItem{.status = ava::tui::ToolTimelineStatus::Error,
                                                                         .name = "websearch",
                                                                         .argument_summary = "query=terminal renderer",
                                                                         .result_summary = "provider error",
                                                                         .result_json = "{\"error\":\"rate limited\"}",
                                                                         .lifecycle = ava::tui::ToolLifecycleState::Error},
                                              ava::tui::ToolTimelineItem{.status = ava::tui::ToolTimelineStatus::Success,
                                                                         .name = "skill",
                                                                         .argument_summary = "name=frontend-review",
                                                                         .result_summary = "loaded skill instructions",
                                                                         .result_json = "{\"preview\":\"Use semantic landmarks\"}",
                                                                         .lifecycle = ava::tui::ToolLifecycleState::Complete,
                                                                         .permission_request_ids = {"permreq_skill"}},
                                              ava::tui::ToolTimelineItem{.status = ava::tui::ToolTimelineStatus::Success,
                                                                         .name = "question",
                                                                         .argument_summary = "choose deployment target",
                                                                         .result_summary = "answered: staging",
                                                                         .result_json = "{\"answer\":\"staging\"}",
                                                                         .lifecycle = ava::tui::ToolLifecycleState::Complete},
                                              ava::tui::ToolTimelineItem{.status = ava::tui::ToolTimelineStatus::Success,
                                                                         .name = "lsp_diagnostics",
                                                                         .argument_summary = "path=src/main.cpp",
                                                                         .result_summary = "2 diagnostics",
                                                                         .result_json = "{\"output\":\"line 12 warning\\nline 18 error\"}",
                                                                         .lifecycle = ava::tui::ToolLifecycleState::Complete}};
  for (auto const& item : non_shell_cards)
  {
    auto const card = ava::tui::detail::render_tool_card(item, 36, true);
    auto const text = visible_text(card);
    auto const copy = ava::tui::detail::tool_card_copy_text(item);
    expect(text.find(item.name) != std::string::npos && text.find("shell status") == std::string::npos &&
               copy.find("tool: " + item.name) != std::string::npos && copy.find("shell status") == std::string::npos &&
               copy.find("\x1b[") == std::string::npos && std::ranges::all_of(card, [](std::string const& line) { return visible_columns(line) <= 36; }),
           "tui non-shell tool cards render and copy safely on narrow terminals for " + item.name);
  }
  auto const skill_permission_copy = ava::tui::detail::tool_card_permission_copy_text(non_shell_cards[1]);
  expect(skill_permission_copy.empty(), "tui permission copy text stays empty when only unresolved permission ids are present");
  auto const expanded_skill_card = ava::tui::detail::render_tool_card(non_shell_cards[1], 48, true);
  auto const expanded_skill_text = visible_text(expanded_skill_card);
  expect(expanded_skill_text.find("permission: checked") != std::string::npos && expanded_skill_text.find("id: permreq_skill") != std::string::npos,
         "tui expanded settled non-shell tool cards keep unresolved permission ids visible as checked");

  {
    ScopedEnvVar no_color_tool_guard("NO_COLOR", "1");
    auto const plain_non_shell_card = ava::tui::render_composer(
        ava::tui::ComposerSnapshot{.mode = "build",
                                   .provider = "openai",
                                   .model = "gpt-5.5",
                                   .session_id = "session_test",
                                   .input = "",
                                   .status = "ready",
                                   .transcript = {ava::tui::TranscriptItem{.tool = webfetch_item}, ava::tui::TranscriptItem{.tool = non_shell_cards[3]}},
                                   .width = 40,
                                   .height = 30,
                                   .tool_details_visible = true});
    auto const plain_text = visible_text(plain_non_shell_card);
    expect(std::ranges::all_of(plain_non_shell_card,
                               [](std::string const& line) { return line.find('\x1b') == std::string::npos && visible_columns(line) <= 40; }) &&
               plain_text.find("webfetch") != std::string::npos && plain_text.find("lsp_diagnostics") != std::string::npos &&
               plain_text.find("output:") != std::string::npos,
           "tui plain narrow non-shell tool cards keep names and output readable without color");
  }

  auto const running_error_cards = ava::tui::render_composer(ava::tui::ComposerSnapshot{
      .mode = "build",
      .provider = "openai",
      .model = "gpt-5.5",
      .session_id = "session_test",
      .input = "",
      .status = "ready",
      .transcript = {ava::tui::TranscriptItem{.tool = ava::tui::ToolTimelineItem{.status = ava::tui::ToolTimelineStatus::Running,
                                                                                 .name = "bash",
                                                                                 .argument_summary = "command=build\x1b[31m now",
                                                                                 .result_summary = ""}},
                     ava::tui::TranscriptItem{.tool = ava::tui::ToolTimelineItem{.status = ava::tui::ToolTimelineStatus::Error,
                                                                                 .name = "write_file",
                                                                                 .argument_summary = std::string(120, 'a') + "\x1b[31m",
                                                                                 .result_summary = "error: denied\x1b[31m"}}},
      .width = 60,
      .height = 14});
  expect(std::ranges::any_of(running_error_cards,
                             [](std::string const& line) {
                               auto visible = strip_sgr(line);
                               return visible.find("~ bash") != std::string::npos && visible.find("bash") != std::string::npos &&
                                      visible.find("command=build?") != std::string::npos;
                             }) &&
             std::ranges::any_of(running_error_cards,
                                 [](std::string const& line) {
                                   auto visible = strip_sgr(line);
                                   return visible.find("x write_file") != std::string::npos && visible.find("write_file") != std::string::npos;
                                 }) &&
             std::ranges::all_of(running_error_cards, [](std::string const& line) { return visible_columns(line) <= 60; }),
         "tui renders running/error tool cards with sanitized truncated summaries");
  expect(std::ranges::none_of(running_error_cards, [](std::string const& line) { return line.find("\x1b[31m") != std::string::npos; }),
         "tui running/error tool cards remove untrusted raw sgr escape sequences");
  expect(std::ranges::any_of(running_error_cards, [](std::string const& line) { return line.find("\x1b[38;2;251;191;36m~") != std::string::npos; }) &&
             std::ranges::any_of(running_error_cards, [](std::string const& line) { return line.find("\x1b[38;2;248;113;113mx") != std::string::npos; }),
         "tui emits trusted sgr status colors for running and error tool cards");

  auto const canceled_tool_card = ava::tui::render_composer(ava::tui::ComposerSnapshot{
      .mode = "build",
      .provider = "openai",
      .model = "gpt-5.5",
      .session_id = "session_test",
      .input = "",
      .status = "ready",
      .transcript = {ava::tui::TranscriptItem{.tool = ava::tui::ToolTimelineItem{.status = ava::tui::ToolTimelineStatus::Canceled,
                                                                                 .name = "bash",
                                                                                 .argument_summary = "command=sleep 30",
                                                                                 .result_summary = "stopped by user",
                                                                                 .arguments_json = "{\"command\":\"sleep 30\"}",
                                                                                 .result_json = "{\"tool\":\"bash\",\"canceled\":true}",
                                                                                 .lifecycle = ava::tui::ToolLifecycleState::Canceled}}},
      .width = 64,
      .height = 16,
      .tool_details_visible = true});
  expect(std::ranges::any_of(canceled_tool_card,
                             [](std::string const& line) {
                               auto visible = strip_sgr(line);
                               return visible.find("- bash") != std::string::npos && visible.find("bash") != std::string::npos &&
                                      visible.find("canceled") != std::string::npos;
                             }) &&
             std::ranges::any_of(canceled_tool_card, [](std::string const& line) { return strip_sgr(line).find("cancel: stopped") != std::string::npos; }) &&
             std::ranges::none_of(canceled_tool_card, [](std::string const& line) { return strip_sgr(line).find("x bash") != std::string::npos; }) &&
             std::ranges::all_of(canceled_tool_card, [](std::string const& line) { return visible_columns(line) <= 64; }),
         "tui renders canceled tool cards as a distinct non-error status");
  auto const canceled_copy_text = ava::tui::detail::tool_card_copy_text(ava::tui::ToolTimelineItem{.status = ava::tui::ToolTimelineStatus::Canceled,
                                                                                                   .name = "bash",
                                                                                                   .argument_summary = "command=sleep 30",
                                                                                                   .result_summary = "stopped by user",
                                                                                                   .arguments_json = "{\"command\":\"sleep 30\"}",
                                                                                                   .result_json = "{\"tool\":\"bash\",\"canceled\":true}",
                                                                                                   .lifecycle = ava::tui::ToolLifecycleState::Canceled});
  expect(canceled_copy_text.find("status: canceled") != std::string::npos && canceled_copy_text.find("lifecycle: canceled") != std::string::npos &&
             canceled_copy_text.find("\x1b[") == std::string::npos,
         "tui copy text preserves canceled tool state without ANSI styling");

  auto const detailed_tool_card = ava::tui::render_composer(ava::tui::ComposerSnapshot{
      .mode = "build",
      .provider = "openai",
      .model = "gpt-5.5",
      .session_id = "session_test",
      .input = "",
      .status = "ready",
      .transcript = {ava::tui::TranscriptItem{.tool = ava::tui::ToolTimelineItem{.status = ava::tui::ToolTimelineStatus::Success,
                                                                                 .name = "bash",
                                                                                 .argument_summary = "command=cmake --build build",
                                                                                 .result_summary = "line one line two line three line four"}}},
      .width = 48,
      .height = 12,
      .tool_details_visible = true});
  expect(std::ranges::any_of(detailed_tool_card, [](std::string const& line) { return strip_sgr(line).find("args: command=cmake") != std::string::npos; }) &&
             std::ranges::any_of(detailed_tool_card, [](std::string const& line) { return strip_sgr(line).find("bash · line one") != std::string::npos; }) &&
             std::ranges::any_of(detailed_tool_card, [](std::string const& line) { return strip_sgr(line).find("status: line one") != std::string::npos; }) &&
             std::ranges::none_of(detailed_tool_card, [](std::string const& line) { return strip_sgr(line).find("result: line one") != std::string::npos; }) &&
             std::ranges::all_of(detailed_tool_card, [](std::string const& line) { return visible_columns(line) <= 48; }),
         "tui expands tool cards below a clipped primary shell without duplicating equivalent status and result payloads");

  auto const copyable_tool_text = ava::tui::detail::tool_card_copy_text(
      ava::tui::ToolTimelineItem{.status = ava::tui::ToolTimelineStatus::Error,
                                 .name = "bash",
                                 .argument_summary = "command=git push origin main",
                                 .result_summary = "permission denied",
                                 .arguments_json = "{\"command\":\"git push origin main\"}",
                                 .result_json = "{\"tool\":\"bash\",\"exit_code\":126,\"stderr\":\"denied\\ntry again\"}",
                                 .lifecycle = ava::tui::ToolLifecycleState::Error,
                                 .permissions = {ava::tui::ToolPermissionAuditItem{.permission_request_id = "permreq_push",
                                                                                   .resolver_request_id = "permission_1",
                                                                                   .decision = "deny",
                                                                                   .operation = "bash",
                                                                                   .tool_name = "bash",
                                                                                   .risk = "high",
                                                                                   .reason = "command can change external state",
                                                                                   .command = "git push origin main"}},
                                 .diff = "--- a\n+++ b\n-old\n+new",
                                 .changed_paths = {"src/main.cpp", "\x1b[31msecret.txt"},
                                 .truncated = true,
                                 .output_lines = 2,
                                 .total_lines = 10,
                                 .spill_path = "/tmp/ava-spill/bash.txt"});
  expect(copyable_tool_text.find("tool: bash") != std::string::npos && copyable_tool_text.find("command: git push origin main") != std::string::npos &&
             copyable_tool_text.find("shell status: exit 126") != std::string::npos && copyable_tool_text.find("permission: deny") != std::string::npos &&
             copyable_tool_text.find("risk high") != std::string::npos &&
             copyable_tool_text.find("reason command can change external state") != std::string::npos &&
             copyable_tool_text.find("inspect: /permissions audit show permreq_push") != std::string::npos &&
             copyable_tool_text.find("diagnose: /permissions diagnose permreq_push") != std::string::npos &&
             copyable_tool_text.find("output:\n  denied\n  try again") != std::string::npos &&
             copyable_tool_text.find("truncation: truncated 2/10 lines") != std::string::npos &&
             copyable_tool_text.find("changed: src/main.cpp, ?[31msecret.txt") != std::string::npos &&
             copyable_tool_text.find("full output: /tmp/ava-spill/bash.txt") != std::string::npos &&
             copyable_tool_text.find("diff:\n  --- a\n  +++ b") != std::string::npos && copyable_tool_text.find("\x1b[") == std::string::npos,
         "tui exposes plain copy text for detailed tool cards without ANSI styling");

  auto const permission_copy_text = ava::tui::detail::tool_card_permission_copy_text(
      ava::tui::ToolTimelineItem{.status = ava::tui::ToolTimelineStatus::Error,
                                 .name = "bash",
                                 .permissions = {ava::tui::ToolPermissionAuditItem{.permission_request_id = "permreq_push",
                                                                                   .resolver_request_id = "permission_1",
                                                                                   .decision = "deny",
                                                                                   .operation = "bash",
                                                                                   .tool_name = "bash",
                                                                                   .risk = "high",
                                                                                   .reason = "command can change external state",
                                                                                   .command = "git push origin main"},
                                                 ava::tui::ToolPermissionAuditItem{.permission_request_id = "permreq_read",
                                                                                   .decision = "allow",
                                                                                   .operation = "read",
                                                                                   .tool_name = "read_file",
                                                                                   .risk = "low",
                                                                                   .target = "docs/USAGE.md"}}},
      "git push");
  auto const missing_permission_copy_text = ava::tui::detail::tool_card_permission_copy_text(
      ava::tui::ToolTimelineItem{
          .status = ava::tui::ToolTimelineStatus::Success,
          .name = "read",
          .permissions = {ava::tui::ToolPermissionAuditItem{
              .permission_request_id = "permreq_read", .decision = "allow", .operation = "read", .tool_name = "read_file", .target = "docs/USAGE.md"}}},
      "git push");
  expect(permission_copy_text.find("permission: deny") != std::string::npos && permission_copy_text.find("id permreq_push") != std::string::npos &&
             permission_copy_text.find("risk high") != std::string::npos &&
             permission_copy_text.find("reason command can change external state") != std::string::npos &&
             permission_copy_text.find("command git push origin main") != std::string::npos &&
             permission_copy_text.find("inspect: /permissions audit show permreq_push") != std::string::npos &&
             permission_copy_text.find("diagnose: /permissions diagnose permreq_push") != std::string::npos &&
             permission_copy_text.find("permreq_read") == std::string::npos && permission_copy_text.find("\x1b[") == std::string::npos &&
             missing_permission_copy_text.empty(),
         "tui exposes focused permission copy text with follow-up commands, query filtering, and no ANSI styling");

  auto const copyable_diff_text = ava::tui::detail::tool_card_diff_copy_text(ava::tui::ToolTimelineItem{
      .status = ava::tui::ToolTimelineStatus::Success, .name = "write", .diff = "--- note.txt\n+++ note.txt\n-\x1b[31mold\n+new", .diff_truncated = true});
  expect(copyable_diff_text.find("--- note.txt") != std::string::npos && copyable_diff_text.find("-old") != std::string::npos &&
             copyable_diff_text.find("+new") != std::string::npos && copyable_diff_text.find("[diff truncated]") != std::string::npos &&
             copyable_diff_text.find("\x1b[") == std::string::npos,
         "tui exposes focused plain copy text for latest tool diffs without ANSI styling");

  auto const queryable_tool_card =
      ava::tui::ToolTimelineItem{.status = ava::tui::ToolTimelineStatus::Success,
                                 .name = "write",
                                 .argument_summary = "src/main.cpp",
                                 .result_summary = "wrote 27 bytes",
                                 .permissions = {ava::tui::ToolPermissionAuditItem{
                                     .permission_request_id = "permreq_write", .decision = "allow", .tool_name = "write_file", .reason = "workspace edit"}},
                                 .diff = "--- src/main.cpp\n+++ src/main.cpp\n-old\n+new",
                                 .changed_paths = {"src/main.cpp"}};
  expect(ava::tui::detail::tool_card_matches_copy_query(queryable_tool_card, "MAIN.CPP") &&
             ava::tui::detail::tool_card_matches_copy_query(queryable_tool_card, "workspace edit") &&
             ava::tui::detail::tool_card_matches_copy_query(queryable_tool_card, "+new") &&
             !ava::tui::detail::tool_card_matches_copy_query(queryable_tool_card, "package-lock"),
         "tui copy query matching finds visible tool, permission, changed-path, and diff context case-insensitively");

  auto const collapsed_override_card = ava::tui::render_composer(
      ava::tui::ComposerSnapshot{.mode = "build",
                                 .provider = "openai",
                                 .model = "gpt-5.5",
                                 .session_id = "session_test",
                                 .input = "",
                                 .status = "ready",
                                 .transcript = {ava::tui::TranscriptItem{.tool = ava::tui::ToolTimelineItem{.status = ava::tui::ToolTimelineStatus::Success,
                                                                                                            .name = "bash",
                                                                                                            .argument_summary = "command=ctest",
                                                                                                            .result_summary = "ok",
                                                                                                            .details_visible = false}}},
                                 .width = 48,
                                 .height = 10,
                                 .tool_details_visible = true});
  expect(
      std::ranges::none_of(collapsed_override_card, [](std::string const& line) { return strip_sgr(line).find("args: command=ctest") != std::string::npos; }),
      "tui supports per-tool detail collapse even when the global details toggle is enabled");

  auto const expanded_override_card = ava::tui::render_composer(
      ava::tui::ComposerSnapshot{.mode = "build",
                                 .provider = "openai",
                                 .model = "gpt-5.5",
                                 .session_id = "session_test",
                                 .input = "",
                                 .status = "ready",
                                 .transcript = {ava::tui::TranscriptItem{.tool = ava::tui::ToolTimelineItem{.status = ava::tui::ToolTimelineStatus::Success,
                                                                                                            .name = "grep",
                                                                                                            .argument_summary = "pattern=todo",
                                                                                                            .result_summary = "2 matches",
                                                                                                            .details_visible = true,
                                                                                                            .truncated = true,
                                                                                                            .visible_matches = 2,
                                                                                                            .total_matches = 10,
                                                                                                            .spill_path = "/tmp/ava-spill/grep.txt",
                                                                                                            .spill_truncated = true}}},
                                 .width = 72,
                                 .height = 12});
  expect(std::ranges::any_of(expanded_override_card,
                             [](std::string const& line) { return strip_sgr(line).find("truncation: truncated 2/10 matches") != std::string::npos; }) &&
             std::ranges::any_of(expanded_override_card,
                                 [](std::string const& line) { return strip_sgr(line).find("full output: /tmp/ava-spill/grep.txt") != std::string::npos; }) &&
             std::ranges::any_of(expanded_override_card, [](std::string const& line) { return strip_sgr(line).find("spill incomplete") != std::string::npos; }),
         "tui renders backend-provided truncation counts and separate truthful spill metadata only when present");

  auto const changed_paths_card = ava::tui::render_composer(ava::tui::ComposerSnapshot{
      .mode = "build",
      .provider = "openai",
      .model = "gpt-5.5",
      .session_id = "session_test",
      .input = "",
      .status = "ready",
      .transcript = {ava::tui::TranscriptItem{
          .tool = ava::tui::ToolTimelineItem{.status = ava::tui::ToolTimelineStatus::Success,
                                             .name = "apply_patch",
                                             .argument_summary = "2 edits",
                                             .result_summary = "updated files",
                                             .details_visible = true,
                                             .changed_paths = {"src/main.cpp", "tests/tui.cpp", "docs/mvp.md", "goals/ledger.md", "\x1b[31mhidden.txt"}}}},
      .width = 120,
      .height = 16});
  expect(std::ranges::any_of(changed_paths_card,
                             [](std::string const& line) {
                               auto const visible = strip_sgr(line);
                               return visible.find("changed:") != std::string::npos && visible.find("src/main.cpp") != std::string::npos &&
                                      visible.find("+1 more") != std::string::npos && visible.find("\x1b[") == std::string::npos;
                             }) &&
             std::ranges::all_of(changed_paths_card, [](std::string const& line) { return visible_columns(line) <= 120; }),
         "tui expanded tool cards render bounded changed-file summaries even without a diff");

  auto const inferred_changed_tool = ava::tui::ToolTimelineItem{.status = ava::tui::ToolTimelineStatus::Success,
                                                                .name = "write_file",
                                                                .argument_summary = "path=notes/output.txt",
                                                                .result_summary = "wrote 12 bytes",
                                                                .result_json = "{\"tool\":\"write_file\",\"ok\":true,\"path\":\"notes/output.txt\"}",
                                                                .lifecycle = ava::tui::ToolLifecycleState::Complete,
                                                                .details_visible = true,
                                                                .diff = "--- notes/output.txt\n+++ notes/output.txt\n-old\n+new"};
  auto const inferred_changed_card =
      ava::tui::render_composer(ava::tui::ComposerSnapshot{.mode = "build",
                                                           .provider = "openai",
                                                           .model = "gpt-5.5",
                                                           .session_id = "session_test",
                                                           .input = "",
                                                           .status = "ready",
                                                           .transcript = {ava::tui::TranscriptItem{.tool = inferred_changed_tool}},
                                                           .width = 88,
                                                           .height = 14});
  auto const inferred_changed_copy = ava::tui::detail::tool_card_copy_text(inferred_changed_tool);
  expect(std::ranges::any_of(inferred_changed_card,
                             [](std::string const& line) { return strip_sgr(line).find("changed: notes/output.txt") != std::string::npos; }) &&
             std::ranges::any_of(inferred_changed_card,
                                 [](std::string const& line) { return strip_sgr(line).find("diff notes/output.txt:") != std::string::npos; }) &&
             inferred_changed_copy.find("changed: notes/output.txt") != std::string::npos &&
             ava::tui::detail::tool_card_matches_copy_query(inferred_changed_tool, "output.txt"),
         "tui infers changed-file summaries from mutating tool result JSON when timeline metadata is sparse");

  auto const wide_diff_card = ava::tui::render_composer(
      ava::tui::ComposerSnapshot{.mode = "build",
                                 .provider = "openai",
                                 .model = "gpt-5.5",
                                 .session_id = "session_test",
                                 .input = "",
                                 .status = "ready",
                                 .transcript = {ava::tui::TranscriptItem{.tool = ava::tui::ToolTimelineItem{.status = ava::tui::ToolTimelineStatus::Success,
                                                                                                            .name = "edit_file",
                                                                                                            .argument_summary = "path=note.txt",
                                                                                                            .result_summary = "wrote 9 bytes",
                                                                                                            .details_visible = true,
                                                                                                            .diff = "--- note.txt\n+++ note.txt\n-old\n+new",
                                                                                                            .diff_truncated = true}}},
                                 .width = 88,
                                 .height = 14});
  expect(std::ranges::any_of(wide_diff_card, [](std::string const& line) { return strip_sgr(line).find("diff:") != std::string::npos; }) &&
             std::ranges::any_of(wide_diff_card,
                                 [](std::string const& line) {
                                   return strip_sgr(line).find("+new") != std::string::npos && line.find("\x1b[38;2;52;211;153m") != std::string::npos;
                                 }) &&
             std::ranges::any_of(wide_diff_card, [](std::string const& line) { return strip_sgr(line).find("diff truncated") != std::string::npos; }),
         "tui renders backend-provided unified diff previews with mutation colors and truncation markers");

  auto const narrow_diff_card = ava::tui::render_composer(ava::tui::ComposerSnapshot{
      .mode = "build",
      .provider = "openai",
      .model = "gpt-5.5",
      .session_id = "session_test",
      .input = "",
      .status = "ready",
      .transcript = {ava::tui::TranscriptItem{.tool = ava::tui::ToolTimelineItem{.status = ava::tui::ToolTimelineStatus::Success,
                                                                                 .name = "edit_file",
                                                                                 .argument_summary = "path=very/long/path/to/note.txt",
                                                                                 .result_summary = "wrote 9 bytes",
                                                                                 .details_visible = true,
                                                                                 .diff = "--- very/long/path/to/note.txt\n+++ very/long/path/to/"
                                                                                         "note.txt\n-old value\n+new value"}}},
      .width = 36,
      .height = 14});
  expect(std::ranges::any_of(narrow_diff_card, [](std::string const& line) { return strip_sgr(line).find("diff:") != std::string::npos; }) &&
             std::ranges::all_of(narrow_diff_card, [](std::string const& line) { return visible_columns(line) <= 36; }),
         "tui keeps backend-provided diff previews width-safe on narrow terminals");

  auto const bash_ux_card = ava::tui::render_composer(ava::tui::ComposerSnapshot{
      .mode = "build",
      .provider = "openai",
      .model = "gpt-5.5",
      .session_id = "session_test",
      .input = "",
      .status = "ready",
      .transcript = {ava::tui::TranscriptItem{.tool = ava::tui::ToolTimelineItem{.status = ava::tui::ToolTimelineStatus::Error,
                                                                                 .name = "bash",
                                                                                 .argument_summary = "command=ctest --test-dir build",
                                                                                 .result_summary = "exit 1",
                                                                                 .arguments_json = "{\"command\":\"ctest --test-dir build\"}",
                                                                                 .result_json = "{\"tool\":\"bash\",\"exit_code\":1,"
                                                                                                "\"duration_ms\":1530,\"total_lines\":4,"
                                                                                                "\"output_lines\":4,\"output\":"
                                                                                                "\"configure\\nbuild\\nfail\\nsummary\"}"}}},
      .width = 72,
      .height = 16});
  expect(std::ranges::any_of(bash_ux_card,
                             [](std::string const& line) {
                               auto const visible = strip_sgr(line);
                               return visible.find("bash") != std::string::npos && visible.find("executing") == std::string::npos &&
                                      visible.find("exit 1 · 1.5s") != std::string::npos;
                             }) &&
             std::ranges::none_of(bash_ux_card,
                                  [](std::string const& line) { return strip_sgr(line).find("command=ctest --test-dir build") != std::string::npos; }) &&
             std::ranges::none_of(bash_ux_card,
                                  [](std::string const& line) {
                                    auto const visible = strip_sgr(line);
                                    return visible.find("output:") != std::string::npos || visible.find("configure") != std::string::npos;
                                  }) &&
             std::ranges::all_of(bash_ux_card, [](std::string const& line) { return visible_columns(line) <= 72; }),
         "tui collapsed bash cards keep status and duration on one header without output preview bodies");

  auto const expanded_bash_ux_card = ava::tui::render_composer(ava::tui::ComposerSnapshot{
      .mode = "build",
      .provider = "openai",
      .model = "gpt-5.5",
      .session_id = "session_test",
      .input = "",
      .status = "ready",
      .transcript = {ava::tui::TranscriptItem{.tool = ava::tui::ToolTimelineItem{.status = ava::tui::ToolTimelineStatus::Success,
                                                                                 .name = "bash",
                                                                                 .argument_summary = "command=cmake --build build",
                                                                                 .result_summary = "exit 0",
                                                                                 .arguments_json = "{\"command\":\"cmake --build build\"}",
                                                                                 .result_json = "{\"tool\":\"bash\",\"exit_code\":0,"
                                                                                                "\"duration_ms\":250,\"total_lines\":3,"
                                                                                                "\"output_lines\":3,\"output\":"
                                                                                                "\"[1/2] compile\\n[2/2] link\\nok\"}"}}},
      .width = 80,
      .height = 18,
      .tool_details_visible = true});
  expect(std::ranges::any_of(expanded_bash_ux_card,
                             [](std::string const& line) { return strip_sgr(line).find("command: cmake --build build") != std::string::npos; }) &&
             std::ranges::any_of(expanded_bash_ux_card, [](std::string const& line) { return strip_sgr(line).find("exit 0 · 250ms") != std::string::npos; }) &&
             std::ranges::any_of(expanded_bash_ux_card,
                                 [](std::string const& line) { return strip_sgr(line).find("output: 3 shown lines") != std::string::npos; }),
         "tui expanded bash cards show command/status/duration and a wider output preview");

  auto const quoted_bash_summary_card = ava::tui::render_composer(ava::tui::ComposerSnapshot{
      .mode = "build",
      .provider = "openai",
      .model = "gpt-5.5",
      .session_id = "session_test",
      .input = "",
      .status = "ready",
      .transcript = {ava::tui::TranscriptItem{.tool = ava::tui::ToolTimelineItem{.status = ava::tui::ToolTimelineStatus::Success,
                                                                                 .name = "bash",
                                                                                 .argument_summary = "command=echo \"a, b\", timeout=1000",
                                                                                 .result_summary = "exit 0",
                                                                                 .details_visible = true}}},
      .width = 80,
      .height = 14});
  expect(std::ranges::any_of(quoted_bash_summary_card,
                             [](std::string const& line) { return strip_sgr(line).find("command: echo \"a, b\"") != std::string::npos; }) &&
             std::ranges::none_of(quoted_bash_summary_card,
                                  [](std::string const& line) {
                                    return strip_sgr(line).find("command: echo \"a") != std::string::npos && strip_sgr(line).find("b\"") == std::string::npos;
                                  }),
         "tui bash cards do not split fallback command summaries at comma-space inside shell quotes");

  auto const escaped_bash_summary_card = ava::tui::render_composer(ava::tui::ComposerSnapshot{
      .mode = "build",
      .provider = "openai",
      .model = "gpt-5.5",
      .session_id = "session_test",
      .input = "",
      .status = "ready",
      .transcript = {ava::tui::TranscriptItem{.tool = ava::tui::ToolTimelineItem{.status = ava::tui::ToolTimelineStatus::Success,
                                                                                 .name = "bash",
                                                                                 .argument_summary = "command=printf foo\\, bar, timeout=1000",
                                                                                 .result_summary = "exit 0",
                                                                                 .details_visible = true}}},
      .width = 80,
      .height = 14});
  expect(std::ranges::any_of(escaped_bash_summary_card,
                             [](std::string const& line) { return strip_sgr(line).find("command: printf foo\\, bar") != std::string::npos; }),
         "tui bash cards do not split fallback command summaries at escaped comma-space");

  auto const running_bash_ux_card = ava::tui::render_composer(
      ava::tui::ComposerSnapshot{.mode = "build",
                                 .provider = "openai",
                                 .model = "gpt-5.5",
                                 .session_id = "session_test",
                                 .input = "",
                                 .status = "ready",
                                 .transcript = {ava::tui::TranscriptItem{.tool = ava::tui::ToolTimelineItem{.status = ava::tui::ToolTimelineStatus::Running,
                                                                                                            .name = "bash",
                                                                                                            .argument_summary = "command=long-test",
                                                                                                            .arguments_json = "{\"command\":\"long-test\"}"}}},
                                 .width = 52,
                                 .height = 10});
  expect(std::ranges::any_of(running_bash_ux_card,
                             [](std::string const& line) { return strip_sgr(line).find("~ bash · running · command=long-test") != std::string::npos; }) &&
             std::ranges::none_of(running_bash_ux_card, [](std::string const& line) { return strip_sgr(line).find("Esc/Ctrl+C stop") != std::string::npos; }) &&
             std::ranges::all_of(running_bash_ux_card, [](std::string const& line) { return visible_columns(line) <= 52; }),
         "tui resting running bash cards remain one concise primary row");

  auto const rich_diff_card = ava::tui::render_composer(ava::tui::ComposerSnapshot{
      .mode = "build",
      .provider = "openai",
      .model = "gpt-5.5",
      .session_id = "session_test",
      .input = "",
      .status = "ready",
      .transcript = {ava::tui::TranscriptItem{.tool = ava::tui::ToolTimelineItem{.status = ava::tui::ToolTimelineStatus::Success,
                                                                                 .name = "edit_file",
                                                                                 .argument_summary = "path=note.txt",
                                                                                 .result_summary = "edited note.txt",
                                                                                 .details_visible = true,
                                                                                 .diff = "--- note.txt\n+++ note.txt\n@@ -1,2 +1,2 @@\n-old\n+new\n context",
                                                                                 .changed_paths = {"note.txt"}}}},
      .width = 92,
      .height = 18});
  expect(std::ranges::any_of(rich_diff_card, [](std::string const& line) { return strip_sgr(line).find("diff note.txt:") != std::string::npos; }) &&
             std::ranges::any_of(rich_diff_card, [](std::string const& line) { return strip_sgr(line).find("hunk @@ -1,2 +1,2 @@") != std::string::npos; }) &&
             std::ranges::any_of(rich_diff_card,
                                 [](std::string const& line) {
                                   auto const visible = strip_sgr(line);
                                   return visible.find("1") != std::string::npos && visible.find("-old") != std::string::npos;
                                 }) &&
             std::ranges::any_of(rich_diff_card,
                                 [](std::string const& line) {
                                   auto const visible = strip_sgr(line);
                                   return visible.find("1") != std::string::npos && visible.find("+new") != std::string::npos;
                                 }),
         "tui diff cards render unified hunk boundaries with line-numbered added and removed rows");
}
namespace {
void test_tui_large_tool_output_preview_is_bounded()
{
  auto const small_output_card = ava::tui::detail::render_tool_card(ava::tui::ToolTimelineItem{.status = ava::tui::ToolTimelineStatus::Success,
                                                                                               .name = "grep",
                                                                                               .result_summary = "4 matches",
                                                                                               .result_json = "{\"output\":\"one\\ntwo\\nthree\\nfour\"}",
                                                                                               .lifecycle = ava::tui::ToolLifecycleState::Complete},
                                                                    96, false);
  expect(small_output_card.size() == 1 &&
             std::ranges::none_of(small_output_card, [](std::string const& line) { return strip_sgr(line).find("output:") != std::string::npos; }),
         "tui collapsed tool cards omit small output preview bodies");

  auto output_preview_text = [](std::string result_json) {
    auto const lines = ava::tui::detail::render_tool_card(ava::tui::ToolTimelineItem{.status = ava::tui::ToolTimelineStatus::Success,
                                                                                     .name = "bash",
                                                                                     .result_summary = "exit 0",
                                                                                     .result_json = std::move(result_json),
                                                                                     .lifecycle = ava::tui::ToolLifecycleState::Complete},
                                                          96, true);
    return tui_test_support::join_visible_lines(lines);
  };
  auto const empty_preview = output_preview_text("{\"output\":\"\"}");
  auto const unterminated_preview = output_preview_text("{\"output\":\"one\"}");
  auto const lf_preview = output_preview_text("{\"output\":\"one\\n\"}");
  auto const crlf_preview = output_preview_text("{\"output\":\"one\\r\\n\"}");
  expect(empty_preview.find("output:") == std::string::npos && unterminated_preview.find("output: 1 shown line") != std::string::npos &&
             lf_preview.find("output: 1 shown line") != std::string::npos && crlf_preview.find("output: 1 shown line") != std::string::npos &&
             unterminated_preview.find("output: 2 shown") == std::string::npos && lf_preview.find("output: 2 shown") == std::string::npos &&
             crlf_preview.find("output: 2 shown") == std::string::npos,
         "tui output preview counts empty, unterminated, LF-terminated, and CRLF-terminated logical lines without a synthetic trailing line");

  std::string seq_output_json = "{\"output\":\"";
  for (std::size_t line = 19801; line <= 20000; ++line) seq_output_json += std::to_string(line) + "\\n";
  seq_output_json += "\"}";
  auto const seq_preview_lines = ava::tui::detail::render_tool_card(ava::tui::ToolTimelineItem{.status = ava::tui::ToolTimelineStatus::Success,
                                                                                               .name = "bash",
                                                                                               .result_summary = "exit 0",
                                                                                               .result_json = std::move(seq_output_json),
                                                                                               .lifecycle = ava::tui::ToolLifecycleState::Complete,
                                                                                               .truncated = true,
                                                                                               .output_lines = 200,
                                                                                               .total_lines = 20000,
                                                                                               .omitted_lines = 19800},
                                                                    96, true);
  auto const seq_preview_text = tui_test_support::join_visible_lines(seq_preview_lines);

  expect(seq_preview_text.find("output: 8 shown/20000 lines · 19992 hidden") != std::string::npos && seq_preview_text.find("20001") == std::string::npos &&
             seq_preview_text.find("19993 hidden") == std::string::npos,
         "tui truncated seq-like output keeps authoritative 20000-line totals and hidden-line math after a final LF");

  constexpr auto total_lines = std::size_t{20000};
  std::string output_json = "{\"output\":\"";
  output_json.reserve(total_lines * 28);
  for (std::size_t index = 0; index < total_lines; ++index)
  {
    if (index > 0)
      output_json += "\\n";
    output_json += "large output line ";
    output_json += std::to_string(index);
  }
  output_json += "\"}";

  auto item = ava::tui::ToolTimelineItem{.status = ava::tui::ToolTimelineStatus::Success,
                                         .name = "grep",
                                         .argument_summary = "pattern=large",
                                         .result_summary = "20000 matches",
                                         .result_json = std::move(output_json),
                                         .lifecycle = ava::tui::ToolLifecycleState::Complete,
                                         .total_lines = total_lines};

  auto const start = std::chrono::steady_clock::now();
  auto const compact = ava::tui::detail::render_tool_card(item, 96, false);
  item.details_visible = true;
  auto const expanded = ava::tui::detail::render_tool_card(item, 96, true);
  auto const elapsed = std::chrono::steady_clock::now() - start;

  auto const compact_text = tui_test_support::join_visible_lines(compact);
  auto const expanded_text = tui_test_support::join_visible_lines(expanded);

  expect(compact.size() == 1 && compact_text.find("output:") == std::string::npos && compact_text.find("large output line") == std::string::npos,
         "tui collapsed tool cards never render large output preview bodies");
  expect(expanded_text.find("output: 8 shown/20000 lines · 19992 hidden") != std::string::npos &&
             expanded_text.find("large output line 7") != std::string::npos && expanded_text.find("large output line 8") == std::string::npos &&
             expanded_text.find("large output line 19999") == std::string::npos &&
             std::ranges::all_of(expanded, [](std::string const& line) { return visible_columns(line) <= 96; }),
         "tui expanded output preview remains bounded for large tool output");
  expect(elapsed < std::chrono::seconds(2), "tui large tool-output preview avoids pathological redraw cost");
}

void test_tui_f5_progressive_tool_details()
{
  auto const& joined = tui_test_support::join_plain_lines;
  auto const& occurrences = tui_test_support::count_occurrences;
  using tui_test_support::plain_lines;
  auto row_text = [](ava::tui::ToolTimelineItem item) {
    auto const lines = ava::tui::detail::render_tool_card(item, 120, false);
    return lines.empty() ? std::string{} : strip_sgr(lines.front());
  };
  for (auto const lifecycle :
       {ava::tui::ToolLifecycleState::ProviderAnnounced, ava::tui::ToolLifecycleState::ArgumentsStreaming, ava::tui::ToolLifecycleState::ArgumentsComplete})
  {
    auto const text = row_text(ava::tui::ToolTimelineItem{
        .status = ava::tui::ToolTimelineStatus::Running, .name = "read_file", .argument_summary = "path=src/pending.cpp", .lifecycle = lifecycle});
    expect(text.find("pending") != std::string::npos && text.find("running") == std::string::npos, "tui F5 pre-execution tool lifecycle is plainly pending");
  }
  for (auto const lifecycle : {ava::tui::ToolLifecycleState::ExecutionStarted, ava::tui::ToolLifecycleState::Progress})
  {
    auto const text = row_text(ava::tui::ToolTimelineItem{
        .status = ava::tui::ToolTimelineStatus::Running, .name = "read_file", .argument_summary = "path=src/running.cpp", .lifecycle = lifecycle});
    expect(text.find("running") != std::string::npos && text.find("pending") == std::string::npos, "tui F5 executing tool lifecycle is plainly running");
  }
  expect(row_text(ava::tui::ToolTimelineItem{.status = ava::tui::ToolTimelineStatus::Success,
                                             .name = "read_file",
                                             .result_summary = "12 lines",
                                             .lifecycle = ava::tui::ToolLifecycleState::Complete}) == "  │ + read_file · 12 lines" &&
             row_text(ava::tui::ToolTimelineItem{
                 .status = ava::tui::ToolTimelineStatus::Error, .name = "write_file", .lifecycle = ava::tui::ToolLifecycleState::Error}) ==
                 "  │ x write_file · failed" &&
             row_text(ava::tui::ToolTimelineItem{
                 .status = ava::tui::ToolTimelineStatus::Canceled, .name = "task", .lifecycle = ava::tui::ToolLifecycleState::Canceled}) ==
                 "  │ - task · canceled",
         "tui F5 settled cards preserve distinct success, error, and canceled markers without redundant lifecycle words");
  auto const compact_identity_row = row_text(ava::tui::ToolTimelineItem{.status = ava::tui::ToolTimelineStatus::Success,
                                                                        .name = "read_file",
                                                                        .result_summary = "call_private",
                                                                        .call_id = "call_private",
                                                                        .lifecycle = ava::tui::ToolLifecycleState::Complete});
  expect(compact_identity_row.find("call_private") == std::string::npos,
         "tui F5 compact summaries suppress explicit tool identifiers even when echoed by a result");

  auto arguments = std::string("{\"path\":\"src/detail.cpp\",\"replacement\":\"") + std::string(900, 'x') + "TAIL" +
                   "\x1b"
                   "[31m\"}";
  auto detail_item = ava::tui::ToolTimelineItem{.status = ava::tui::ToolTimelineStatus::Error,
                                                .name = "edit_file",
                                                .argument_summary = "path=src/detail.cpp",
                                                .result_summary = " permission   denied ",
                                                .arguments_json = arguments,
                                                .result_json = "{\"output\":\"permission denied\\t\"}",
                                                .call_id = "call_detail\x1b[31m",
                                                .request_id = "request_detail",
                                                .correlation_id = "correlation_detail",
                                                .lifecycle = ava::tui::ToolLifecycleState::Error,
                                                .permissions = {ava::tui::ToolPermissionAuditItem{.permission_request_id = "perm_detail", .decision = "deny"}},
                                                .diff = "--- src/detail.cpp\n+++ src/detail.cpp\n-old\n+new",
                                                .changed_paths = {"src/detail.cpp", "tests/hidden.cpp"},
                                                .truncated = true,
                                                .output_bytes = 40,
                                                .total_bytes = 400,
                                                .next_offset_line = 9,
                                                .omitted_bytes = 360,
                                                .spill_path = "/tmp/ava-spill/detail.txt",
                                                .spill_truncated = true};
  auto const collapsed = plain_lines(ava::tui::detail::render_tool_card(detail_item, 120, false));
  auto const expanded = plain_lines(ava::tui::detail::render_tool_card(detail_item, 700, true));
  auto const expanded_text = joined(expanded);
  auto const copy = ava::tui::detail::tool_card_copy_text(detail_item);
  expect(joined(collapsed).find("call_detail") == std::string::npos && joined(collapsed).find("request_detail") == std::string::npos &&
             joined(collapsed).find("correlation_detail") == std::string::npos && joined(collapsed).find("tests/hidden.cpp") == std::string::npos &&
             expanded_text.find("call id: call_detail?[31m") != std::string::npos && expanded_text.find("request id: request_detail") != std::string::npos &&
             expanded_text.find("correlation id: correlation_detail") != std::string::npos,
         "tui F5 supplied identifiers and changed-path lists are expanded-only while resting rows stay clean");
  expect(expanded_text.find("\n  │     result:  permission   denied ") != std::string::npos &&
             expanded_text.find("\n  │     output: 1 shown line") != std::string::npos && expanded_text.find("input: {") != std::string::npos &&
             expanded_text.find("TAIL") == std::string::npos,
         "tui F5 expanded truncated payloads preserve distinct result/output whitespace and bound generic input");
  auto const redundant_input_text =
      joined(plain_lines(ava::tui::detail::render_tool_card(ava::tui::ToolTimelineItem{.status = ava::tui::ToolTimelineStatus::Running,
                                                                                       .name = "bash",
                                                                                       .argument_summary = "command=blocked",
                                                                                       .arguments_json = "{\"command\":\"blocked\"}",
                                                                                       .lifecycle = ava::tui::ToolLifecycleState::ExecutionStarted},
                                                            120, true)));
  auto const exact_input_text =
      joined(plain_lines(ava::tui::detail::render_tool_card(ava::tui::ToolTimelineItem{.status = ava::tui::ToolTimelineStatus::Running,
                                                                                       .name = "custom_tool",
                                                                                       .argument_summary = "blocked",
                                                                                       .arguments_json = "blocked",
                                                                                       .lifecycle = ava::tui::ToolLifecycleState::ExecutionStarted},
                                                            120, true)));
  expect(redundant_input_text.find("input: {\"command\":\"blocked\"}") != std::string::npos && exact_input_text.find("input:") == std::string::npos,
         "tui F5 expanded cards omit structured input only for exact sanitized content, not punctuation-stripped resemblance");
  expect(expanded_text.find("truncation: truncated 40/400 bytes; next offset 9; omitted 360 bytes") != std::string::npos,
         "tui F5 expanded truncation retains exact counts and next-offset metadata");
  auto const shell_byte_text =
      joined(plain_lines(ava::tui::detail::render_tool_card(ava::tui::ToolTimelineItem{.status = ava::tui::ToolTimelineStatus::Success,
                                                                                       .name = "bash",
                                                                                       .result_summary = "exit 0",
                                                                                       .result_json = "{\"output\":\"one\\ntwo\"}",
                                                                                       .lifecycle = ava::tui::ToolLifecycleState::Complete,
                                                                                       .truncated = true,
                                                                                       .output_bytes = 40,
                                                                                       .total_bytes = 400,
                                                                                       .output_lines = 2,
                                                                                       .total_lines = 20},
                                                            160, true)));
  expect(shell_byte_text.find("bytes: 40/400 bytes retained") != std::string::npos,
         "tui F5 expanded shell lifecycle details retain explicit byte counts alongside line truncation");
  expect(occurrences(expanded_text, "/tmp/ava-spill/detail.txt") == 1 && expanded_text.find("full output: /tmp/ava-spill/detail.txt") != std::string::npos &&
             expanded_text.find("spill incomplete") != std::string::npos,
         "tui F5 expanded spill metadata is separate, nonduplicated, and truthful");
  expect(expanded_text.find("toggle: /tool request_detail") != std::string::npos &&
             expanded_text.find("copy: /copy tool request_detail") != std::string::npos &&
             expanded_text.find("diff: /diff request_detail") != std::string::npos &&
             expanded_text.find("copy diff: /copy diff request_detail") != std::string::npos && expanded_text.find("/read") == std::string::npos &&
             expanded_text.find("show more") == std::string::npos && expanded_text.find("open spill") == std::string::npos,
         "tui F5 expanded cards advertise only existing stable-query toggle, copy, and diff command contracts");
  auto request_query_item = detail_item;
  request_query_item.call_id.clear();
  auto const request_query_text = joined(plain_lines(ava::tui::detail::render_tool_card(request_query_item, 160, true)));
  expect(request_query_text.find("toggle: /tool request_detail") != std::string::npos,
         "tui F5 stable action queries fall back from call to request identity before path or name");
  auto path_query_item = detail_item;
  path_query_item.call_id.clear();
  path_query_item.request_id.clear();
  path_query_item.correlation_id.clear();
  auto path_query_text = joined(plain_lines(ava::tui::detail::render_tool_card(path_query_item, 160, true)));
  path_query_item.changed_paths = {"/tmp/private/detail.cpp"};
  path_query_item.argument_summary = "path=/tmp/private/detail.cpp";
  path_query_item.arguments_json = "{\"path\":\"/tmp/private/detail.cpp\"}";
  auto name_query_text = joined(plain_lines(ava::tui::detail::render_tool_card(path_query_item, 160, true)));
  expect(path_query_text.find("toggle: /tool src/detail.cpp") != std::string::npos && name_query_text.find("toggle: /tool edit_file") != std::string::npos &&
             name_query_text.find("/tool /tmp/private") == std::string::npos,
         "tui F5 stable action queries prefer a supplied relative changed path and never use an absolute path");
  expect(copy.find("input: ") != std::string::npos && copy.find("TAIL") != std::string::npos && copy.find("call id: call_detail") != std::string::npos &&
             copy.find("request id: request_detail") != std::string::npos && copy.find("correlation id: correlation_detail") != std::string::npos &&
             copy.find("full output: /tmp/ava-spill/detail.txt") != std::string::npos && copy.find("spill incomplete: true") != std::string::npos &&
             copy.find('\x1b') == std::string::npos,
         "tui F5 copied tool diagnostics retain complete supplied input, identifiers, output metadata, and control hygiene");
  auto const duplicate_shell_copy = ava::tui::detail::tool_card_copy_text(ava::tui::ToolTimelineItem{.status = ava::tui::ToolTimelineStatus::Error,
                                                                                                     .name = "bash",
                                                                                                     .argument_summary = "command=blocked",
                                                                                                     .result_summary = "permission denied",
                                                                                                     .arguments_json = "{\"command\":\"blocked\"}",
                                                                                                     .result_json = "{\"output\":\" permission   denied\\n\"}",
                                                                                                     .lifecycle = ava::tui::ToolLifecycleState::Error});
  expect(occurrences(duplicate_shell_copy, "permission denied") == 1 && duplicate_shell_copy.find("result: permission denied") == std::string::npos &&
             duplicate_shell_copy.find("output:\n   permission   denied") != std::string::npos,
         "tui F5 copied diagnostics deduplicate exact shell status/result while preserving output with distinct internal whitespace");

  auto render_payload_detail = [&](std::string result, std::string output, bool truncated) {
    auto item = ava::tui::ToolTimelineItem{.status = ava::tui::ToolTimelineStatus::Success,
                                           .name = "payload_tool",
                                           .result_summary = std::move(result),
                                           .result_json = "{\"output\":\"" + ava::core::json::escape(output) + "\"}",
                                           .lifecycle = ava::tui::ToolLifecycleState::Complete,
                                           .truncated = truncated};
    return std::pair{joined(plain_lines(ava::tui::detail::render_tool_card(item, 160, true))), ava::tui::detail::tool_card_copy_text(item)};
  };
  auto const [newline_render, newline_copy] = render_payload_detail("a b", "a\nb", false);
  auto const [punctuation_render, punctuation_copy] = render_payload_detail("{\"value\":1}", "{\"value\":\"1\"}", false);
  auto const [exact_render, exact_copy] = render_payload_detail("exact payload", "exact payload", false);
  auto const [truncated_render, truncated_copy] = render_payload_detail("exact payload", "exact payload", true);
  auto const [trailing_lf_render, trailing_lf_copy] = render_payload_detail("exact payload", "exact payload\n", false);
  expect(newline_render.find("output: 2 shown lines") != std::string::npos && newline_copy.find("result: a b") != std::string::npos &&
             newline_copy.find("output:\n  a\n  b") != std::string::npos && punctuation_render.find("output: 1 shown line") != std::string::npos &&
             punctuation_copy.find("result: {\"value\":1}") != std::string::npos && punctuation_copy.find("output: {\"value\":\"1\"}") != std::string::npos,
         "tui F5 payload suppression preserves internal newlines and JSON punctuation in expanded and copied details");
  expect(exact_render.find("\n  │     output:") == std::string::npos && exact_copy.find("output:") == std::string::npos &&
             truncated_render.find("\n  │     result: exact payload") != std::string::npos &&
             truncated_render.find("\n  │     output: 1 shown line") != std::string::npos &&
             truncated_copy.find("result: exact payload") != std::string::npos && truncated_copy.find("output: exact payload") != std::string::npos &&
             trailing_lf_render.find("\n  │     output:") == std::string::npos && trailing_lf_copy.find("output:") == std::string::npos,
         "tui F5 payload suppression removes only exact complete duplicates, permits trailing LF equivalence, and retains exact truncated duplicates");

  std::vector<ava::tui::TranscriptItem> toggle_transcript{
      ava::tui::TranscriptItem{.tool = ava::tui::ToolTimelineItem{.status = ava::tui::ToolTimelineStatus::Success,
                                                                  .name = "read_file",
                                                                  .call_id = "older",
                                                                  .lifecycle = ava::tui::ToolLifecycleState::Complete}},
      ava::tui::TranscriptItem{.label = "ava", .text = "between"},
      ava::tui::TranscriptItem{
          .tool = ava::tui::ToolTimelineItem{
              .status = ava::tui::ToolTimelineStatus::Success, .name = "read_file", .call_id = "latest", .lifecycle = ava::tui::ToolLifecycleState::Complete}}};
  auto const original_size = toggle_transcript.size();
  auto toggled = ava::tui::toggle_latest_matching_tool_details(toggle_transcript, "read_file", false);
  auto toggled_back = ava::tui::toggle_latest_matching_tool_details(toggle_transcript, "latest", false);
  expect(toggled && *toggled == 2 && toggled_back && *toggled_back == 2 && toggle_transcript.size() == original_size && toggle_transcript[0].tool &&
             !toggle_transcript[0].tool->details_visible && toggle_transcript[2].tool && toggle_transcript[2].tool->details_visible == false,
         "tui F5 exact tool routing toggles the latest matching original card in place without duplicating transcript items");

  auto hit_snapshot = ava::tui::ComposerSnapshot{
      .mode = "build",
      .provider = "openai",
      .model = "gpt-5.5",
      .session_id = "session_hit",
      .input = "draft",
      .status = "ready",
      .transcript = {ava::tui::TranscriptItem{.label = "ava", .text = "intro"},
                     ava::tui::TranscriptItem{.tool = ava::tui::ToolTimelineItem{.status = ava::tui::ToolTimelineStatus::Success,
                                                                                 .name = "glob",
                                                                                 .result_summary = "4 files",
                                                                                 .call_id = "glob_hit",
                                                                                 .lifecycle = ava::tui::ToolLifecycleState::Complete}},
                     ava::tui::TranscriptItem{.tool = detail_item}, ava::tui::TranscriptItem{.label = "ava", .text = "tail"}},
      .width = 160,
      .height = 48,
      .sidebar = ava::tui::SidebarSnapshot{.session_id = "session_hit"}};
  auto const hit_frame = plain_lines(ava::tui::render_composer(hit_snapshot));
  auto const intro_line = std::ranges::find_if(hit_frame, [](std::string const& line) { return line.find("intro") != std::string::npos; });
  auto const glob_line = std::ranges::find_if(hit_frame, [](std::string const& line) { return line.find("+ glob") != std::string::npos; });
  auto const detail_line = std::ranges::find_if(hit_frame, [](std::string const& line) { return line.find("x edit_file") != std::string::npos; });
  auto const heading_line = std::ranges::find_if(hit_frame, [](std::string const& line) { return line.find("context gathering") != std::string::npos; });
  auto const intro_row = intro_line == hit_frame.end() ? 0 : static_cast<std::size_t>(intro_line - hit_frame.begin()) + 1;
  auto const glob_row = glob_line == hit_frame.end() ? 0 : static_cast<std::size_t>(glob_line - hit_frame.begin()) + 1;
  auto const detail_row = detail_line == hit_frame.end() ? 0 : static_cast<std::size_t>(detail_line - hit_frame.begin()) + 1;
  auto const heading_row = heading_line == hit_frame.end() ? 0 : static_cast<std::size_t>(heading_line - hit_frame.begin()) + 1;
  auto const hit_canvas = ava::tui::composer_canvas_layout(hit_snapshot);
  expect(hit_canvas.content_width == 120 && hit_canvas.left == 20 &&
             ava::tui::detail::transcript_tool_card_header_for_screen_position(hit_snapshot, glob_row, 21) == 1 &&
             ava::tui::detail::transcript_tool_card_header_for_screen_position(hit_snapshot, detail_row, 140) == 2 &&
             !ava::tui::detail::transcript_tool_card_header_for_screen_position(hit_snapshot, intro_row, 24) &&
             !ava::tui::detail::transcript_tool_card_header_for_screen_position(hit_snapshot, heading_row, 24) &&
             !ava::tui::detail::transcript_tool_card_header_for_screen_position(hit_snapshot, detail_row, 20) &&
             !ava::tui::detail::transcript_tool_card_header_for_screen_position(hit_snapshot, detail_row, 141) &&
             !ava::tui::detail::transcript_tool_card_header_for_screen_position(hit_snapshot, hit_snapshot.height, 24),
         "tui F5 tool header hit testing uses centered transcript geometry and rejects headings, both exact gutters, and composer rows");

  hit_snapshot.transcript[2].tool->details_visible = true;
  auto expanded_hit_frame = plain_lines(ava::tui::render_composer(hit_snapshot));
  auto const expanded_detail_line =
      std::ranges::find_if(expanded_hit_frame, [](std::string const& line) { return line.find("x edit_file") != std::string::npos; });
  auto const expanded_detail_row =
      expanded_detail_line == expanded_hit_frame.end() ? 0 : static_cast<std::size_t>(expanded_detail_line - expanded_hit_frame.begin()) + 1;
  expect(ava::tui::detail::transcript_tool_card_header_for_screen_position(hit_snapshot, expanded_detail_row, 28) == 2 &&
             !ava::tui::detail::transcript_tool_card_header_for_screen_position(hit_snapshot, expanded_detail_row + 1, 28),
         "tui F5 per-card expansion hit testing toggles only the original header and rejects payload rows");
  hit_snapshot.tool_details_visible = true;
  expect(ava::tui::detail::transcript_tool_card_header_for_screen_position(hit_snapshot, glob_row, 28) == 1,
         "tui F5 header hit testing shares global expansion geometry");

  auto short_clipped = hit_snapshot;
  short_clipped.width = 100;
  short_clipped.height = 12;
  short_clipped.sidebar.reset();
  short_clipped.transcript = {ava::tui::TranscriptItem{.tool = detail_item}};
  short_clipped.transcript.front().tool->details_visible = true;
  bool clipped_header_hit = false;
  for (std::size_t row = 1; row <= short_clipped.height; ++row)
  {
    clipped_header_hit = clipped_header_hit || ava::tui::detail::transcript_tool_card_header_for_screen_position(short_clipped, row, 8).has_value();
  }
  expect(!clipped_header_hit, "tui F5 short viewports reject visible expanded payload rows when the original card header is clipped offscreen");

  auto detached = hit_snapshot;
  detached.width = 80;
  detached.height = 24;
  detached.tool_details_visible = false;
  detached.sidebar.reset();
  detached.transcript.insert(detached.transcript.begin(), 30, ava::tui::TranscriptItem{.label = "you", .text = "older transcript line"});
  detached.transcript.insert(detached.transcript.end(), 30, ava::tui::TranscriptItem{.label = "ava", .text = "newer transcript line"});
  auto const detached_layout = ava::tui::detail::render_transcript_layout(detached.transcript, ava::tui::composer_main_width(detached), false, true, false);
  auto const detached_tool = std::ranges::find(detached_layout.message_item_indices, std::size_t{31});
  auto const detached_position = static_cast<std::size_t>(detached_tool - detached_layout.message_item_indices.begin());
  auto const max_scroll = ava::tui::composer_max_transcript_scroll_offset(detached, detached.width, detached.height);
  auto const desired_start = detached_layout.content_starts[detached_position] - 1;
  detached.transcript_scroll_offset = max_scroll - std::min(max_scroll, desired_start);
  expect(!ava::tui::detail::transcript_tool_card_header_for_screen_position(detached, 1, 4) &&
             ava::tui::detail::transcript_tool_card_header_for_screen_position(detached, 2, 4) == 31,
         "tui F5 detached transcript hit testing rejects the scroll indicator and maps the anchored visible tool header");

  for (auto const& [width, height] : {std::pair{std::size_t{160}, std::size_t{48}}, std::pair{std::size_t{120}, std::size_t{36}},
                                      std::pair{std::size_t{80}, std::size_t{24}}, std::pair{std::size_t{100}, std::size_t{12}}})
  {
    auto bounded = hit_snapshot;
    bounded.width = width;
    bounded.height = height;
    bounded.sidebar.reset();
    auto const frame = ava::tui::render_composer(bounded);
    expect(frame.size() == height && std::ranges::all_of(frame, [width](std::string const& line) { return visible_columns(line) <= width; }),
           "tui F5 progressive tool details stay within requested frame dimensions");
  }
  {
    ScopedEnvVar no_color("NO_COLOR", "1");
    auto plain = hit_snapshot;
    plain.width = 100;
    plain.height = 12;
    plain.sidebar.reset();
    auto const frame = ava::tui::render_composer(plain);
    expect(frame.size() == 12 &&
               std::ranges::all_of(frame, [](std::string const& line) { return line.find('\x1b') == std::string::npos && visible_columns(line) <= 100; }),
           "tui F5 progressive tool details retain compact NO_COLOR bounds");
  }
}
}  // namespace

void run_tui_tool_card_detail_tests()
{
  test_tui_large_tool_output_preview_is_bounded();
  test_tui_f5_progressive_tool_details();
}
