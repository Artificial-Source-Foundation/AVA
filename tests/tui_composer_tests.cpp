#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <climits>
#include <cstdlib>
#include <cwchar>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "ava/agent/agent_loop.h"
#include "ava/agent/mode.h"
#include "ava/agent/tool_dispatcher.h"
#include "ava/app/commands.h"
#include "ava/app/events.h"
#include "ava/app/headless_policy.h"
#include "ava/app/print_mode.h"
#include "ava/app/rpc_mode.h"
#include "ava/app/runtime.h"
#include "ava/config/auth.h"
#include "ava/config/model_config.h"
#include "ava/config/openai_oauth.h"
#include "ava/config/prompt_config.h"
#include "ava/config/xdg_paths.h"
#include "ava/context/context_loader.h"
#include "ava/core/ids.h"
#include "ava/core/json.h"
#include "ava/permissions/permission.h"
#include "ava/provider/openai_provider.h"
#include "ava/session/compaction.h"
#include "ava/session/export.h"
#include "ava/session/session_store.h"
#include "ava/tools/bash_tool.h"
#include "ava/tools/file_tools.h"
#include "ava/tools/search_tools.h"
#include "ava/tui/composer.h"
#include "ava/tui/terminal.h"
#include "tests/support/fake_transport.h"

#include "tests/support/test_harness.h"

namespace {

void test_tui_composer_rendering_and_input() {
  auto prompt_input = ava::tui::handle_permission_prompt_input(ava::tui::PermissionPromptChoice::Deny,
                                                               ava::tui::InputEvent{.key = ava::tui::Key::Tab});
  expect(prompt_input.action == ava::tui::PermissionPromptInputAction::Redraw &&
             prompt_input.selected_choice == ava::tui::PermissionPromptChoice::Allow,
         "permission prompt tab toggles focus to allow");
  prompt_input = ava::tui::handle_permission_prompt_input(ava::tui::PermissionPromptChoice::Allow,
                                                          ava::tui::InputEvent{.key = ava::tui::Key::Tab});
  expect(prompt_input.action == ava::tui::PermissionPromptInputAction::Redraw &&
             prompt_input.selected_choice == ava::tui::PermissionPromptChoice::Deny,
         "permission prompt tab toggles focus back to deny");
  prompt_input = ava::tui::handle_permission_prompt_input(ava::tui::PermissionPromptChoice::Allow,
                                                          ava::tui::InputEvent{.key = ava::tui::Key::ArrowLeft});
  expect(prompt_input.action == ava::tui::PermissionPromptInputAction::Redraw &&
             prompt_input.selected_choice == ava::tui::PermissionPromptChoice::Deny,
         "permission prompt left arrow selects deny");
  prompt_input = ava::tui::handle_permission_prompt_input(ava::tui::PermissionPromptChoice::Deny,
                                                          ava::tui::InputEvent{.key = ava::tui::Key::ArrowRight});
  expect(prompt_input.action == ava::tui::PermissionPromptInputAction::Redraw &&
             prompt_input.selected_choice == ava::tui::PermissionPromptChoice::Allow,
         "permission prompt right arrow selects allow");
  prompt_input = ava::tui::handle_permission_prompt_input(ava::tui::PermissionPromptChoice::Allow,
                                                          ava::tui::InputEvent{.key = ava::tui::Key::Enter});
  expect(prompt_input.action == ava::tui::PermissionPromptInputAction::ResolveAllow,
         "permission prompt enter confirms selected allow");
  prompt_input = ava::tui::handle_permission_prompt_input(
      ava::tui::PermissionPromptChoice::Deny, ava::tui::InputEvent{.key = ava::tui::Key::Character, .character = ' '});
  expect(prompt_input.action == ava::tui::PermissionPromptInputAction::ResolveDeny,
         "permission prompt space confirms selected deny");
  prompt_input = ava::tui::handle_permission_prompt_input(ava::tui::PermissionPromptChoice::Allow,
                                                          ava::tui::InputEvent{.key = ava::tui::Key::Escape});
  expect(prompt_input.action == ava::tui::PermissionPromptInputAction::ResolveDeny,
         "permission prompt escape resolves deny");
  prompt_input = ava::tui::handle_permission_prompt_input(ava::tui::PermissionPromptChoice::Allow,
                                                          ava::tui::InputEvent{.key = ava::tui::Key::CtrlC});
  expect(prompt_input.action == ava::tui::PermissionPromptInputAction::ResolveDeny,
         "permission prompt ctrl-c resolves deny");
  prompt_input = ava::tui::handle_permission_prompt_input(ava::tui::PermissionPromptChoice::Allow,
                                                          ava::tui::InputEvent{.key = ava::tui::Key::CtrlD});
  expect(prompt_input.action == ava::tui::PermissionPromptInputAction::ResolveDeny,
         "permission prompt ctrl-d resolves deny");
  prompt_input = ava::tui::handle_permission_prompt_input(
      ava::tui::PermissionPromptChoice::Deny, ava::tui::InputEvent{.key = ava::tui::Key::Character, .character = 'A'});
  expect(prompt_input.action == ava::tui::PermissionPromptInputAction::ResolveAllow,
         "permission prompt A resolves allow");
  prompt_input = ava::tui::handle_permission_prompt_input(
      ava::tui::PermissionPromptChoice::Allow, ava::tui::InputEvent{.key = ava::tui::Key::Character, .character = 'D'});
  expect(prompt_input.action == ava::tui::PermissionPromptInputAction::ResolveDeny,
         "permission prompt D resolves deny");
  prompt_input = ava::tui::handle_permission_prompt_input(
      ava::tui::PermissionPromptChoice::Allow, ava::tui::InputEvent{.key = ava::tui::Key::Character, .character = 'x'});
  expect(prompt_input.action == ava::tui::PermissionPromptInputAction::None &&
             prompt_input.selected_choice == ava::tui::PermissionPromptChoice::Allow,
         "permission prompt ignores unmapped character keys without changing focus");
  std::string utf8_input = std::string("ab") + "\xC3\xA9";
  ava::tui::erase_last_utf8_codepoint(utf8_input);
  expect(utf8_input == "ab", "tui backspace erases a complete utf-8 codepoint");
  std::string orphan_continuation = std::string("a") + std::string("\x80", 1);
  ava::tui::erase_last_utf8_codepoint(orphan_continuation);
  expect(orphan_continuation == "a", "tui backspace erases only a trailing orphan utf-8 continuation byte");
  std::string orphan_after_utf8 = std::string("a") + "\xC3\xA9" + std::string("\x80", 1);
  ava::tui::erase_last_utf8_codepoint(orphan_after_utf8);
  expect(orphan_after_utf8 == std::string("a") + "\xC3\xA9",
         "tui backspace preserves the preceding utf-8 codepoint when erasing an orphan continuation byte");
  std::string incomplete_starter = std::string("a") + std::string("\xE2", 1);
  ava::tui::erase_last_utf8_codepoint(incomplete_starter);
  expect(incomplete_starter == "a", "tui backspace erases an incomplete trailing utf-8 starter byte");
  std::string incomplete_starter_with_continuation = std::string("a") + std::string("\xF0\x9F", 2);
  ava::tui::erase_last_utf8_codepoint(incomplete_starter_with_continuation);
  expect(incomplete_starter_with_continuation == std::string("a") + std::string("\xF0", 1),
         "tui backspace erases only one byte from an incomplete trailing utf-8 sequence");

  const auto split_empty = ava::tui::split_lines("");
  expect(split_empty.size() == 1 && split_empty.front().empty(), "tui split keeps empty input as one line");
  const auto split_trailing = ava::tui::split_lines("a\n");
  expect(split_trailing.size() == 2 && split_trailing[0] == "a" && split_trailing[1].empty(),
         "tui split preserves trailing empty line");
  const auto split_crlf = ava::tui::split_lines("a\r\nb\rc");
  expect(split_crlf.size() == 3 && split_crlf[0] == "a" && split_crlf[1] == "b" && split_crlf[2] == "c",
         "tui split treats crlf and carriage-return output as line breaks");

  const auto lines = ava::tui::render_composer(
      ava::tui::ComposerSnapshot{.mode = "build",
                                 .provider = "openai",
                                 .model = "gpt-5.5",
                                 .session_id = "session_test",
                                 .input = "/help",
                                 .status = "ready",
                                 .transcript = {ava::tui::TranscriptItem{.label = "you", .text = "hello"},
                                                ava::tui::TranscriptItem{.label = "ava", .text = "world"}},
                                 .width = 80,
                                 .height = 14});
  expect(lines.size() == 14, "tui pins compact content to the bottom of the viewport");
  expect(!lines.empty() && lines.back().find("\x1b[48;2;26;31;46m") != std::string::npos &&
             std::ranges::none_of(lines,
                                  [](const std::string& line) { return line.find("/ commands") != std::string::npos; }),
         "tui keeps only the composer block at the bottom");
  expect(std::ranges::any_of(
             lines, [](const std::string& line) { return strip_sgr(line).find("▎  ❯ /help") != std::string::npos; }),
         "tui renders old AVA-style composer input");
  expect(std::ranges::any_of(lines,
                             [](const std::string& line) {
                               return line.find("\x1b[48;2;26;31;46m") != std::string::npos &&
                                      line.find("\x1b[38;2;77;158;246m▎") != std::string::npos &&
                                      line.find("\x1b[1m\x1b[38;2;77;158;246m❯") != std::string::npos;
                             }),
         "tui uses old AVA elevated composer surface, primary rail, and prompt color");
  expect(std::ranges::any_of(
             lines, [](const std::string& line) { return strip_sgr(line).find("╭─ You") != std::string::npos; }) &&
             std::ranges::any_of(
                 lines, [](const std::string& line) { return strip_sgr(line).find("│ hello") != std::string::npos; }) &&
             std::ranges::any_of(
                 lines, [](const std::string& line) { return strip_sgr(line).find("╭─ AVA") != std::string::npos; }) &&
             std::ranges::any_of(
                 lines, [](const std::string& line) { return strip_sgr(line).find("│ world") != std::string::npos; }),
         "tui renders visually separated user and assistant message blocks");

  const auto markdown_transcript = ava::tui::render_composer(ava::tui::ComposerSnapshot{
      .mode = "build",
      .provider = "openai",
      .model = "gpt-5.5",
      .session_id = "session_test",
      .input = "",
      .status = "ready",
      .transcript = {ava::tui::TranscriptItem{
          .label = "ava",
          .text =
              "First paragraph wraps cleanly across words.\n\nSecond paragraph stays separate.\n- bullet item\n* star "
              "item\n1. numbered item\n> quoted text\n```cpp\nint main() {}\n```\nUse `ava build` and "
              "**bold text**."}},
      .width = 72,
      .height = 24});
  expect(
      std::ranges::any_of(markdown_transcript,
                          [](const std::string& line) {
                            return strip_sgr(line).find("│ First paragraph wraps cleanly") != std::string::npos;
                          }) &&
          std::ranges::any_of(markdown_transcript,
                              [](const std::string& line) {
                                return strip_sgr(line).find("│ Second paragraph stays separate") != std::string::npos;
                              }) &&
          std::ranges::any_of(
              markdown_transcript,
              [](const std::string& line) { return strip_sgr(line).find("│ - bullet item") != std::string::npos; }) &&
          std::ranges::any_of(
              markdown_transcript,
              [](const std::string& line) { return strip_sgr(line).find("│ * star item") != std::string::npos; }) &&
          std::ranges::any_of(markdown_transcript,
                              [](const std::string& line) {
                                return strip_sgr(line).find("│ 1. numbered item") != std::string::npos;
                              }) &&
          std::ranges::any_of(
              markdown_transcript,
              [](const std::string& line) { return strip_sgr(line).find("│ > quoted text") != std::string::npos; }) &&
          std::ranges::any_of(
              markdown_transcript,
              [](const std::string& line) { return strip_sgr(line).find("│ ``` cpp") != std::string::npos; }) &&
          std::ranges::any_of(
              markdown_transcript,
              [](const std::string& line) { return strip_sgr(line).find("│   int main() {}") != std::string::npos; }) &&
          std::ranges::any_of(markdown_transcript,
                              [](const std::string& line) {
                                return strip_sgr(line).find("Use ava build and bold text") != std::string::npos;
                              }) &&
          std::ranges::none_of(markdown_transcript,
                               [](const std::string& line) {
                                 const auto visible = strip_sgr(line);
                                 return visible.find("`ava build`") != std::string::npos ||
                                        visible.find("**bold text**") != std::string::npos;
                               }),
      "tui assistant renderer handles paragraphs, lists, quotes, fenced code, inline code, and bold");

  constexpr auto kBoldSgr = std::string_view{"\x1b[1m"};
  constexpr auto kMutedSgr = std::string_view{"\x1b[38;2;139;149;165m"};
  constexpr auto kWarningSgr = std::string_view{"\x1b[38;2;251;191;36m"};

  const auto role_markup_transcript = ava::tui::render_composer(
      ava::tui::ComposerSnapshot{.mode = "build",
                                 .provider = "openai",
                                 .model = "gpt-5.5",
                                 .session_id = "session_test",
                                 .input = "",
                                 .status = "ready",
                                 .transcript = {ava::tui::TranscriptItem{.label = "you", .text = "Use `x` and **y**."},
                                                ava::tui::TranscriptItem{.label = "ava", .text = "Use `x` and **y**."}},
                                 .width = 28,
                                 .height = 16});
  const auto user_markup_line = std::ranges::find_if(role_markup_transcript, [](const std::string& line) {
    return strip_sgr(line).find("You: Use `x` and **y**.") != std::string::npos;
  });
  const auto assistant_markup_line = std::ranges::find_if(role_markup_transcript, [](const std::string& line) {
    return strip_sgr(line).find("AVA: Use x and y.") != std::string::npos;
  });
  expect(user_markup_line != role_markup_transcript.end() && assistant_markup_line != role_markup_transcript.end() &&
             !has_active_sgr_at_text(*user_markup_line, "x", kWarningSgr) &&
             !has_active_sgr_at_text(*user_markup_line, "y", kBoldSgr) &&
             has_active_sgr_at_text(*assistant_markup_line, "x", kWarningSgr) &&
             has_active_sgr_at_text(*assistant_markup_line, "y", kBoldSgr),
         "tui keeps user inline markdown literal while formatting assistant inline markdown");

  const auto wrapped_markdown_transcript = ava::tui::render_composer(ava::tui::ComposerSnapshot{
      .mode = "build",
      .provider = "openai",
      .model = "gpt-5.5",
      .session_id = "session_test",
      .input = "",
      .status = "ready",
      .transcript = {ava::tui::TranscriptItem{
          .label = "ava",
          .text = "- alpha beta gamma delta epsilon zeta eta theta iota kappa lambda mu\n> quoted alpha beta gamma "
                  "delta epsilon zeta eta theta iota kappa"}},
      .width = 48,
      .height = 16});
  const auto bullet_continuation = std::ranges::find_if(wrapped_markdown_transcript, [](const std::string& line) {
    return strip_sgr(line).find("│   theta iota") != std::string::npos;
  });
  const auto quote_continuation = std::ranges::find_if(wrapped_markdown_transcript, [](const std::string& line) {
    return strip_sgr(line).find("│   eta theta") != std::string::npos;
  });
  const auto bullet_continuation_is_plain = bullet_continuation != wrapped_markdown_transcript.end() &&
                                            !has_active_sgr_at_text(*bullet_continuation, "theta iota", kMutedSgr);
  const auto quote_continuation_is_plain = quote_continuation != wrapped_markdown_transcript.end() &&
                                           !has_active_sgr_at_text(*quote_continuation, "eta theta", kMutedSgr);
  expect(bullet_continuation_is_plain && quote_continuation_is_plain,
         "tui assistant renderer keeps wrapped list and quote continuations out of code styling");

  const auto wrapped_code_fence_transcript = ava::tui::render_composer(ava::tui::ComposerSnapshot{
      .mode = "build",
      .provider = "openai",
      .model = "gpt-5.5",
      .session_id = "session_test",
      .input = "",
      .status = "ready",
      .transcript = {ava::tui::TranscriptItem{
          .label = "ava",
          .text = std::string("```text\n") + std::string(42, 'a') + "```literal\nomega\n```\nAfter **bold**"}},
      .width = 48,
      .height = 16});
  const auto code_after_wrapped_ticks = std::ranges::find_if(
      wrapped_code_fence_transcript,
      [](const std::string& line) { return strip_sgr(line).find("│   omega") != std::string::npos; });
  const auto text_after_code = std::ranges::find_if(wrapped_code_fence_transcript, [](const std::string& line) {
    return strip_sgr(line).find("│ After bold") != std::string::npos;
  });
  expect(code_after_wrapped_ticks != wrapped_code_fence_transcript.end() &&
             has_active_sgr_at_text(*code_after_wrapped_ticks, "omega", kMutedSgr) &&
             text_after_code != wrapped_code_fence_transcript.end() &&
             !has_active_sgr_at_text(*text_after_code, "After", kMutedSgr) &&
             has_active_sgr_at_text(*text_after_code, "bold", kBoldSgr),
         "tui assistant renderer keeps wrapped code content beginning with backticks inside the code block");

  const auto indented_fence_content_transcript = ava::tui::render_composer(
      ava::tui::ComposerSnapshot{.mode = "build",
                                 .provider = "openai",
                                 .model = "gpt-5.5",
                                 .session_id = "session_test",
                                 .input = "",
                                 .status = "ready",
                                 .transcript = {ava::tui::TranscriptItem{
                                     .label = "ava", .text = "```text\n  ```literal\nomega\n```\nAfter **bold**"}},
                                 .width = 56,
                                 .height = 16});
  const auto indented_ticks = std::ranges::find_if(indented_fence_content_transcript, [](const std::string& line) {
    return strip_sgr(line).find("```literal") != std::string::npos;
  });
  const auto code_after_indented_ticks = std::ranges::find_if(
      indented_fence_content_transcript,
      [](const std::string& line) { return strip_sgr(line).find("│   omega") != std::string::npos; });
  const auto text_after_indented_code = std::ranges::find_if(
      indented_fence_content_transcript,
      [](const std::string& line) { return strip_sgr(line).find("│ After bold") != std::string::npos; });
  expect(indented_ticks != indented_fence_content_transcript.end() &&
             strip_sgr(*indented_ticks).find("``` literal") == std::string::npos &&
             code_after_indented_ticks != indented_fence_content_transcript.end() &&
             has_active_sgr_at_text(*code_after_indented_ticks, "omega", kMutedSgr) &&
             text_after_indented_code != indented_fence_content_transcript.end() &&
             !has_active_sgr_at_text(*text_after_indented_code, "After", kMutedSgr) &&
             has_active_sgr_at_text(*text_after_indented_code, "bold", kBoldSgr),
         "tui assistant renderer keeps indented backtick content inside fenced code blocks");

  const auto narrow_code_transcript = ava::tui::render_composer(ava::tui::ComposerSnapshot{
      .mode = "build",
      .provider = "openai",
      .model = "gpt-5.5",
      .session_id = "session_test",
      .input = "",
      .status = "ready",
      .transcript = {ava::tui::TranscriptItem{
          .label = "ava", .text = "Intro `ok` and **bold**.\n```text\nvalue `x` and **y**\n```\nDone **bold**."}},
      .width = 30,
      .height = 18});
  const auto narrow_inline = std::ranges::find_if(narrow_code_transcript, [](const std::string& line) {
    return strip_sgr(line).find("AVA: Intro ok and bold.") != std::string::npos;
  });
  const auto narrow_code = std::ranges::find_if(narrow_code_transcript, [](const std::string& line) {
    return strip_sgr(line).find("value `x` and **y**") != std::string::npos;
  });
  const auto narrow_after_code = std::ranges::find_if(narrow_code_transcript, [](const std::string& line) {
    return strip_sgr(line).find("Done bold.") != std::string::npos;
  });
  expect(narrow_inline != narrow_code_transcript.end() && has_active_sgr_at_text(*narrow_inline, "ok", kWarningSgr) &&
             has_active_sgr_at_text(*narrow_inline, "bold", kBoldSgr) && narrow_code != narrow_code_transcript.end() &&
             !has_active_sgr_at_text(*narrow_code, "x", kWarningSgr) &&
             !has_active_sgr_at_text(*narrow_code, "y", kBoldSgr) &&
             narrow_after_code != narrow_code_transcript.end() &&
             has_active_sgr_at_text(*narrow_after_code, "bold", kBoldSgr),
         "tui narrow assistant renderer keeps code literal while formatting inline markdown outside code");

  const auto narrow_transcript = ava::tui::render_composer(ava::tui::ComposerSnapshot{
      .mode = "build",
      .provider = "openai",
      .model = "gpt-5.5",
      .session_id = "session_test",
      .input = "",
      .status = "ready",
      .transcript = {ava::tui::TranscriptItem{.label = "ava", .text = std::string(64, 'x') + " done"}},
      .width = 20,
      .height = 14});
  expect(
      std::ranges::any_of(narrow_transcript,
                          [](const std::string& line) { return strip_sgr(line).find("AVA: ") != std::string::npos; }) &&
          std::ranges::all_of(narrow_transcript, [](const std::string& line) { return visible_columns(line) <= 20; }),
      "tui assistant renderer keeps long words readable at narrow widths");

  const auto rows_transcript = ava::tui::render_composer(
      ava::tui::ComposerSnapshot{.mode = "build",
                                 .provider = "openai",
                                 .model = "gpt-5.5",
                                 .session_id = "session_test",
                                 .input = "",
                                 .status = "ready",
                                 .transcript = {ava::tui::TranscriptItem{.label = "error", .text = "bad command"},
                                                ava::tui::TranscriptItem{.label = "command", .text = "/help"}},
                                 .width = 50,
                                 .height = 10});
  expect(std::ranges::any_of(
             rows_transcript,
             [](const std::string& line) { return strip_sgr(line).find("! bad command") != std::string::npos; }) &&
             std::ranges::any_of(
                 rows_transcript,
                 [](const std::string& line) { return strip_sgr(line).find("· /help") != std::string::npos; }),
         "tui keeps errors and generic command rows distinct from message blocks");
  const auto compact_status = ava::tui::render_composer(ava::tui::ComposerSnapshot{
      .mode = "build",
      .provider = "openai",
      .model = "gpt-5.5",
      .session_id = "session_test",
      .input = "",
      .status = "Enter submits. Ctrl-J/Shift+Enter inserts newline. / opens commands. Page/wheel scroll.",
      .transcript = {},
      .width = 120,
      .height = 8});
  expect(std::ranges::none_of(compact_status,
                              [](const std::string& line) {
                                return strip_sgr(line).find("Ctrl-J/Shift+Enter inserts newline") != std::string::npos;
                              }),
         "tui keeps the composer status compact instead of rendering verbose help");

  const auto minimum_width = ava::tui::render_composer(ava::tui::ComposerSnapshot{.mode = "build",
                                                                                  .provider = "openai",
                                                                                  .model = "gpt-5.5",
                                                                                  .session_id = "session_test",
                                                                                  .input = "hello",
                                                                                  .status = "ready",
                                                                                  .transcript = {},
                                                                                  .width = 1,
                                                                                  .height = 1});
  expect(std::ranges::all_of(minimum_width,
                             [](const std::string& line) {
                               return line.find('\n') == std::string::npos && visible_columns(line) <= 20;
                             }) &&
             std::ranges::any_of(
                 minimum_width,
                 [](const std::string& line) { return strip_sgr(line).find("❯ hello") != std::string::npos; }),
         "tui clamps normal composer rendering to the minimum viewport");

  const std::vector<ava::tui::SlashCommandItem> slash_commands = {
      ava::tui::SlashCommandItem{.command = "/help", .description = "Show help", .category = "General"},
      ava::tui::SlashCommandItem{
          .command = "/grep", .description = "Search files", .hint = "<text> [glob]", .category = "Files"},
      ava::tui::SlashCommandItem{
          .command = "/glob", .description = "List matching files", .hint = "<pattern>", .category = "Files"},
      ava::tui::SlashCommandItem{.command = "/quit", .description = "Exit", .category = "General"}};
  const auto grep_commands = ava::tui::filter_slash_commands("/gr", slash_commands);
  expect(grep_commands.size() == 1 && grep_commands.front().command == "/grep",
         "tui slash palette filters commands by typed prefix");
  expect(ava::tui::filter_slash_commands("hello", slash_commands).empty(),
         "tui slash palette stays hidden for normal chat input");
  expect(ava::tui::slash_palette_visible("/g", slash_commands),
         "tui slash palette is visible while filtering commands");
  expect(!ava::tui::slash_palette_visible("/help", slash_commands),
         "tui slash palette hides after an exact no-argument command");
  expect(ava::tui::slash_command_selection_text("/g", slash_commands, 1) == "/glob ",
         "tui slash selection fills argument-taking command with a trailing space");
  expect(ava::tui::slash_command_selection_text("/h", slash_commands, 0) == "/help",
         "tui slash selection fills no-argument command without submitting it");
  expect(ava::tui::clamp_slash_palette_selection("/g", slash_commands, 99) == 1,
         "tui clamps out-of-range slash palette selection to the last match");
  expect(ava::tui::previous_slash_palette_selection("/g", slash_commands, 0) == 1 &&
             ava::tui::next_slash_palette_selection("/g", slash_commands, 1) == 0,
         "tui slash palette arrow selection wraps through filtered commands");

  const auto palette = ava::tui::render_composer(ava::tui::ComposerSnapshot{.mode = "build",
                                                                            .provider = "openai",
                                                                            .model = "gpt-5.5",
                                                                            .session_id = "session_test",
                                                                            .input = "/g",
                                                                            .status = "ready",
                                                                            .transcript = {},
                                                                            .slash_commands = slash_commands,
                                                                            .selected_slash_command_index = 1,
                                                                            .width = 80,
                                                                            .height = 12});
  expect(std::ranges::any_of(
             palette, [](const std::string& line) { return line.find("commands matching /g") != std::string::npos; }) &&
             std::ranges::any_of(palette,
                                 [](const std::string& line) {
                                   return line.find("/grep") != std::string::npos &&
                                          line.find("Files") != std::string::npos &&
                                          line.find("Search files") != std::string::npos;
                                 }) &&
             std::ranges::any_of(palette,
                                 [](const std::string& line) {
                                   return line.find("/glob") != std::string::npos &&
                                          line.find("List matching files") != std::string::npos;
                                 }) &&
             std::ranges::any_of(palette,
                                 [](const std::string& line) {
                                   const auto visible = strip_sgr(line);
                                   return visible.find("> /glob (2/2)") != std::string::npos &&
                                          visible.find("selected") != std::string::npos;
                                 }) &&
             std::ranges::any_of(palette,
                                 [](const std::string& line) {
                                   return line.find("\x1b[7m> /glob (2/2)") != std::string::npos &&
                                          line.find("\x1b[0m") != std::string::npos;
                                 }) &&
             std::ranges::none_of(palette,
                                  [](const std::string& line) { return line.find("/help") != std::string::npos; }),
         "tui renders filtered slash-command palette with columns and selected item marker");
  const auto clicked_palette_index =
      ava::tui::slash_palette_selection_for_screen_row(ava::tui::ComposerSnapshot{.mode = "build",
                                                                                  .provider = "openai",
                                                                                  .model = "gpt-5.5",
                                                                                  .session_id = "session_test",
                                                                                  .input = "/g",
                                                                                  .status = "ready",
                                                                                  .transcript = {},
                                                                                  .slash_commands = slash_commands,
                                                                                  .selected_slash_command_index = 1,
                                                                                  .width = 80,
                                                                                  .height = 12},
                                                       8);
  expect(clicked_palette_index && *clicked_palette_index == 1,
         "tui maps slash palette screen rows back to selectable commands for clicks");

  std::vector<ava::tui::SlashCommandItem> many_slash_commands;
  for (int index = 0; index < 8; ++index) {
    many_slash_commands.push_back(ava::tui::SlashCommandItem{.command = "/item" + std::to_string(index),
                                                             .description = "Command " + std::to_string(index)});
  }
  const auto tiny_palette = ava::tui::render_composer(ava::tui::ComposerSnapshot{.mode = "build",
                                                                                 .provider = "openai",
                                                                                 .model = "gpt-5.5",
                                                                                 .session_id = "session_test",
                                                                                 .input = "/",
                                                                                 .status = "ready",
                                                                                 .transcript = {},
                                                                                 .slash_commands = many_slash_commands,
                                                                                 .selected_slash_command_index = 6,
                                                                                 .width = 80,
                                                                                 .height = 8});
  expect(std::ranges::any_of(tiny_palette,
                             [](const std::string& line) { return line.find("> /item6") != std::string::npos; }) &&
             std::ranges::none_of(tiny_palette,
                                  [](const std::string& line) { return line.find("/item0") != std::string::npos; }),
         "tui keeps selected slash palette item visible when height is tight");
  const auto first_scrolled_palette_click =
      ava::tui::slash_palette_selection_for_screen_row(ava::tui::ComposerSnapshot{.mode = "build",
                                                                                  .provider = "openai",
                                                                                  .model = "gpt-5.5",
                                                                                  .session_id = "session_test",
                                                                                  .input = "/",
                                                                                  .status = "ready",
                                                                                  .transcript = {},
                                                                                  .slash_commands = many_slash_commands,
                                                                                  .selected_slash_command_index = 6,
                                                                                  .width = 80,
                                                                                  .height = 8},
                                                       2);
  const auto selected_scrolled_palette_click =
      ava::tui::slash_palette_selection_for_screen_row(ava::tui::ComposerSnapshot{.mode = "build",
                                                                                  .provider = "openai",
                                                                                  .model = "gpt-5.5",
                                                                                  .session_id = "session_test",
                                                                                  .input = "/",
                                                                                  .status = "ready",
                                                                                  .transcript = {},
                                                                                  .slash_commands = many_slash_commands,
                                                                                  .selected_slash_command_index = 6,
                                                                                  .width = 80,
                                                                                  .height = 8},
                                                       4);
  const auto outside_scrolled_palette_click =
      ava::tui::slash_palette_selection_for_screen_row(ava::tui::ComposerSnapshot{.mode = "build",
                                                                                  .provider = "openai",
                                                                                  .model = "gpt-5.5",
                                                                                  .session_id = "session_test",
                                                                                  .input = "/",
                                                                                  .status = "ready",
                                                                                  .transcript = {},
                                                                                  .slash_commands = many_slash_commands,
                                                                                  .selected_slash_command_index = 6,
                                                                                  .width = 80,
                                                                                  .height = 8},
                                                       5);
  expect(first_scrolled_palette_click && *first_scrolled_palette_click == 4 && selected_scrolled_palette_click &&
             *selected_scrolled_palette_click == 6 && !outside_scrolled_palette_click,
         "tui maps slash palette click rows through a scrolled visible window and ignores outside rows");

  const auto starved_palette = ava::tui::render_composer(
      ava::tui::ComposerSnapshot{.mode = "build",
                                 .provider = "openai",
                                 .model = "gpt-5.5",
                                 .session_id = "session_test",
                                 .input = "/",
                                 .status = "ready",
                                 .transcript = {ava::tui::TranscriptItem{.label = "ava", .text = "must not leak"}},
                                 .slash_commands = many_slash_commands,
                                 .selected_slash_command_index = 4,
                                 .width = 40,
                                 .height = 8});
  expect(starved_palette.size() == 8 &&
             std::ranges::any_of(
                 starved_palette,
                 [](const std::string& line) { return strip_sgr(line).find("> /item4") != std::string::npos; }) &&
             std::ranges::none_of(
                 starved_palette,
                 [](const std::string& line) { return strip_sgr(line).find("must not leak") != std::string::npos; }),
         "tui keeps the bottom composer fixed when the slash palette exhausts transcript height");

  const auto no_match_palette = ava::tui::render_composer(ava::tui::ComposerSnapshot{.mode = "build",
                                                                                     .provider = "openai",
                                                                                     .model = "gpt-5.5",
                                                                                     .session_id = "session_test",
                                                                                     .input = "/zz",
                                                                                     .status = "ready",
                                                                                     .transcript = {},
                                                                                     .slash_commands = slash_commands,
                                                                                     .width = 80,
                                                                                     .height = 12});
  expect(std::ranges::any_of(no_match_palette,
                             [](const std::string& line) {
                               return strip_sgr(line).find("no commands match /zz") != std::string::npos;
                             }),
         "tui slash-command palette renders deterministic empty state");

  const auto permission_modal = ava::tui::render_composer(ava::tui::ComposerSnapshot{
      .mode = "build",
      .provider = "openai",
      .model = "gpt-5.5",
      .session_id = "session_test",
      .input = "",
      .status = "permission required",
      .transcript = {ava::tui::TranscriptItem{.label = "ava", .text = "thinking"}},
      .permission_prompt = ava::tui::PermissionPromptView{.tool_name = "bash\x1b[31m",
                                                          .operation = "bash",
                                                          .target = "/tmp/outside",
                                                          .command = "git push origin main",
                                                          .reason = "command can change external state"},
      .width = 80,
      .height = 12});
  expect(std::ranges::any_of(
             permission_modal,
             [](const std::string& line) { return line.find("PERMISSION REQUIRED") != std::string::npos; }) &&
             std::ranges::any_of(permission_modal,
                                 [](const std::string& line) {
                                   auto visible = strip_sgr(line);
                                   return visible.find("git push origin main") != std::string::npos;
                                 }) &&
             std::ranges::any_of(permission_modal,
                                 [](const std::string& line) {
                                   return line.find("[Deny]") != std::string::npos &&
                                          line.find("[Allow once]") != std::string::npos;
                                 }) &&
             std::ranges::any_of(permission_modal,
                                 [](const std::string& line) {
                                   return line.find("\x1b[7m> [Deny]") != std::string::npos &&
                                          strip_sgr(line).find("[Deny] (selected)") != std::string::npos;
                                 }) &&
             std::ranges::any_of(permission_modal,
                                 [](const std::string& line) {
                                   auto visible = strip_sgr(line);
                                   return visible.find("A allow") != std::string::npos &&
                                          visible.find("D deny") != std::string::npos &&
                                          visible.find("Enter confirm") != std::string::npos &&
                                          visible.find("Esc deny") != std::string::npos;
                                 }) &&
             std::ranges::none_of(permission_modal,
                                  [](const std::string& line) {
                                    return line.find("bash") != std::string::npos &&
                                           line.find("\x1b[31m") != std::string::npos;
                                  }),
         "tui renders Rust AVA-style permission dock with default deny focus");
  expect(std::ranges::all_of(permission_modal,
                             [](const std::string& line) {
                               return line.find('\n') == std::string::npos && visible_columns(line) <= 80;
                             }) &&
             std::ranges::any_of(
                 permission_modal,
                 [](const std::string& line) { return strip_sgr(line).find("[Deny]") != std::string::npos; }) &&
             std::ranges::any_of(
                 permission_modal,
                 [](const std::string& line) { return strip_sgr(line).find("[Allow once]") != std::string::npos; }) &&
             std::ranges::any_of(
                 permission_modal,
                 [](const std::string& line) { return strip_sgr(line).find("Enter confirm") != std::string::npos; }) &&
             std::ranges::any_of(
                 permission_modal,
                 [](const std::string& line) { return strip_sgr(line).find("Esc deny") != std::string::npos; }),
         "tui permission dock controls stay within 80 visible columns without losing controls");

  const auto allow_focused_modal = ava::tui::render_composer(ava::tui::ComposerSnapshot{
      .mode = "build",
      .provider = "openai",
      .model = "gpt-5.5",
      .session_id = "session_test",
      .input = "",
      .status = "permission required",
      .transcript = {},
      .permission_prompt = ava::tui::PermissionPromptView{.tool_name = "bash",
                                                          .operation = "bash",
                                                          .target = "",
                                                          .command = "true",
                                                          .reason = "unknown risk",
                                                          .selected_choice = ava::tui::PermissionPromptChoice::Allow},
      .width = 80,
      .height = 14});
  expect(std::ranges::any_of(allow_focused_modal,
                             [](const std::string& line) {
                               return line.find("\x1b[7m> [Allow once]") != std::string::npos &&
                                      strip_sgr(line).find("[Allow once] (selected)") != std::string::npos;
                             }),
         "tui permission dock highlights the selected allow choice");

  const auto long_permission_modal = ava::tui::render_composer(
      ava::tui::ComposerSnapshot{.mode = "build",
                                 .provider = "openai",
                                 .model = "gpt-5.5",
                                 .session_id = "session_test",
                                 .input = "",
                                 .status = "permission required",
                                 .transcript = {},
                                 .permission_prompt = ava::tui::PermissionPromptView{.tool_name = "",
                                                                                     .operation = "write_file",
                                                                                     .target = std::string(120, 't'),
                                                                                     .command = std::string(120, 'c'),
                                                                                     .reason = std::string(120, 'r')},
                                 .width = 80,
                                 .height = 10});
  expect(std::ranges::all_of(long_permission_modal,
                             [](const std::string& line) {
                               return line.find('\n') == std::string::npos && visible_columns(line) <= 80;
                             }) &&
             std::ranges::any_of(long_permission_modal,
                                 [](const std::string& line) {
                                   const auto visible = strip_sgr(line);
                                   return visible.find("cccc") != std::string::npos &&
                                          visible.find("...") != std::string::npos;
                                 }),
         "tui permission dock truncates long detail text and handles an empty tool name");

  const auto tight_permission_modal = ava::tui::render_composer(ava::tui::ComposerSnapshot{
      .mode = "build",
      .provider = "openai",
      .model = "gpt-5.5",
      .session_id = "session_test",
      .input = "",
      .status = "permission required",
      .transcript = {ava::tui::TranscriptItem{.label = "ava", .text = "thinking"}},
      .permission_prompt =
          ava::tui::PermissionPromptView{
              .tool_name = "bash", .operation = "bash", .target = "", .command = "true", .reason = ""},
      .width = 36,
      .height = 8});
  expect(std::ranges::any_of(
             tight_permission_modal,
             [](const std::string& line) { return line.find("PERMISSION REQUIRED") != std::string::npos; }) &&
             tight_permission_modal.size() <= 8 &&
             std::ranges::all_of(tight_permission_modal,
                                 [](const std::string& line) {
                                   return line.find('\n') == std::string::npos && visible_columns(line) <= 36;
                                 }) &&
             std::ranges::any_of(tight_permission_modal,
                                 [](const std::string& line) {
                                   return line.find("[Deny]") != std::string::npos &&
                                          line.find("[Allow once]") != std::string::npos;
                                 }),
         "tui permission dock keeps header and controls visible in tight height");

  const auto ultra_tight_permission_modal = ava::tui::render_composer(ava::tui::ComposerSnapshot{
      .mode = "build",
      .provider = "openai",
      .model = "gpt-5.5",
      .session_id = "session_test",
      .input = "",
      .status = "permission required",
      .transcript = {},
      .permission_prompt =
          ava::tui::PermissionPromptView{
              .tool_name = "bash", .operation = "bash", .target = "", .command = "true", .reason = ""},
      .width = 20,
      .height = 8});
  expect(std::ranges::all_of(ultra_tight_permission_modal,
                             [](const std::string& line) {
                               return line.find('\n') == std::string::npos && visible_columns(line) <= 20;
                             }) &&
             ultra_tight_permission_modal.size() <= 8 &&
             std::ranges::any_of(ultra_tight_permission_modal,
                                 [](const std::string& line) {
                                   const auto visible = strip_sgr(line);
                                   return visible.find("PERMISSION") != std::string::npos;
                                 }) &&
             std::ranges::any_of(ultra_tight_permission_modal,
                                 [](const std::string& line) {
                                   const auto visible = strip_sgr(line);
                                   return visible.find("> [D] sel") != std::string::npos &&
                                          visible.find("[A]") != std::string::npos;
                                 }) &&
             std::ranges::any_of(ultra_tight_permission_modal,
                                 [](const std::string& line) {
                                   const auto visible = strip_sgr(line);
                                   return visible.find("A=allow") != std::string::npos &&
                                          visible.find("D=deny") != std::string::npos;
                                 }),
         "tui permission dock preserves deny and allow choices at minimum width");

  std::vector<ava::tui::TranscriptItem> permission_overflow_items;
  for (int index = 0; index < 8; ++index) {
    permission_overflow_items.push_back(
        ava::tui::TranscriptItem{.label = "ava", .text = "permission item " + std::to_string(index)});
  }
  const auto permission_starved = ava::tui::render_composer(ava::tui::ComposerSnapshot{
      .mode = "build",
      .provider = "openai",
      .model = "gpt-5.5",
      .session_id = "session_test",
      .input = "hidden input",
      .status = "permission required",
      .transcript = permission_overflow_items,
      .permission_prompt =
          ava::tui::PermissionPromptView{
              .tool_name = "bash", .operation = "bash", .target = "", .command = "true", .reason = ""},
      .width = 40,
      .height = 8});
  expect(permission_starved.size() <= 8 &&
             std::ranges::all_of(permission_starved,
                                 [](const std::string& line) { return visible_columns(line) <= 40; }) &&
             std::ranges::any_of(permission_starved,
                                 [](const std::string& line) {
                                   return strip_sgr(line).find("older lines hidden") != std::string::npos;
                                 }) &&
             std::ranges::none_of(
                 permission_starved,
                 [](const std::string& line) { return strip_sgr(line).find("❯ hidden input") != std::string::npos; }),
         "tui permission prompt handles height-starved transcript overflow without showing the composer");

  const auto sanitized = ava::tui::render_composer(
      ava::tui::ComposerSnapshot{.mode = "build",
                                 .provider = "openai",
                                 .model = "gpt-5.5",
                                 .session_id = "session_test",
                                 .input = "",
                                 .status = "bad\x1b[31mstatus",
                                 .transcript = {ava::tui::TranscriptItem{.label = "ava", .text = "bad\x1b[31mred"}},
                                 .width = 80,
                                 .height = 8});
  expect(std::ranges::any_of(sanitized,
                             [](const std::string& line) {
                               auto visible = strip_sgr(line);
                               return visible.find("?[31mred") != std::string::npos;
                             }),
         "tui render sanitizes transcript escape bytes in user content");
  const auto sanitized_input = ava::tui::render_composer(ava::tui::ComposerSnapshot{.mode = "build",
                                                                                    .provider = "openai",
                                                                                    .model = "gpt-5.5",
                                                                                    .session_id = "session_test",
                                                                                    .input = "bad\x1b[31mred",
                                                                                    .status = "ready",
                                                                                    .transcript = {},
                                                                                    .width = 80,
                                                                                    .height = 8});
  expect(std::ranges::any_of(
             sanitized_input,
             [](const std::string& line) { return strip_sgr(line).find("❯ bad?[31mred") != std::string::npos; }),
         "tui render sanitizes composer input escape bytes");
  expect(ava::tui::sanitize_terminal_text(std::string("osc") + static_cast<char>(0x9D) + "payload") == "osc?payload",
         "tui sanitizes raw c1 terminal control bytes");
  expect(ava::tui::sanitize_terminal_text("a\tb") == "a  b", "tui expands tabs before width accounting");
  expect(ava::tui::sanitize_terminal_text(std::string("ok ") + "\xC3\xA9") == std::string("ok ") + "\xC3\xA9",
         "tui sanitizer preserves valid utf-8 text");
  expect(ava::tui::sanitize_terminal_text(std::string("x") + std::string("\xC0\x80", 2) + "y") == "x??y",
         "tui sanitizer rejects overlong two-byte utf-8 controls");
  expect(ava::tui::sanitize_terminal_text(std::string("x") + std::string("\xE0\x80\x80", 3) + "y") == "x???y",
         "tui sanitizer rejects overlong three-byte utf-8 forms");
  expect(ava::tui::sanitize_terminal_text(std::string("x") + std::string("\xF0\x80\x80\x80", 4) + "y") == "x????y",
         "tui sanitizer rejects overlong four-byte utf-8 forms");
  expect(ava::tui::sanitize_terminal_text(std::string("x") + std::string("\xE2\x82", 2)) == "x??",
         "tui sanitizer replaces truncated utf-8 at the string boundary");
  expect(ava::tui::sanitize_terminal_text(std::string("x") + std::string("\xED\xA0\x80", 3) + "y") == "x???y",
         "tui sanitizer rejects utf-8 surrogate codepoints");
  expect(ava::tui::sanitize_terminal_text(std::string("x") + std::string("\xF4\x90\x80\x80", 4) + "y") == "x????y",
         "tui sanitizer rejects utf-8 codepoints above the unicode maximum");

  const auto composer_frame = ava::tui::render_composer(ava::tui::ComposerSnapshot{.mode = "plan",
                                                                                   .provider = "openai",
                                                                                   .model = "gpt-5.5",
                                                                                   .session_id = "session_test",
                                                                                   .input = "hello",
                                                                                   .status = "ready",
                                                                                   .transcript = {},
                                                                                   .width = 40,
                                                                                   .height = 8});
  expect(composer_frame.size() == 8, "tui composer frame fills the requested terminal height");
  expect(std::ranges::any_of(
             composer_frame,
             [](const std::string& line) { return strip_sgr(line).find("▎  ❯ hello") != std::string::npos; }),
         "tui composer frame renders the input prompt content");
  const auto wide_frame = ava::tui::render_composer(
      ava::tui::ComposerSnapshot{.mode = "build",
                                 .provider = "openai",
                                 .model = "gpt-5.5",
                                 .session_id = "session_test",
                                 .input = std::string("wide ") + "\xE6\xBC\xA2\xE6\xBC\xA2\xF0\x9F\x98\x80",
                                 .status = "ready",
                                 .transcript = {ava::tui::TranscriptItem{
                                     .label = "ava", .text = "\xE6\xBC\xA2\xE6\xBC\xA2\xE6\xBC\xA2\xE6\xBC\xA2"}},
                                 .width = 24,
                                 .height = 10});
  expect(std::ranges::all_of(wide_frame, [](const std::string& line) { return visible_columns(line) <= 24; }),
         "tui treats CJK and emoji as wide cells when fitting rendered lines");
  ava::tui::clear_terminal_signal();
  expect(!ava::tui::terminal_signal_received(), "tui terminal signal state can be cleared before curses entry");

  const auto permission_frame = ava::tui::render_composer(ava::tui::ComposerSnapshot{
      .mode = "build",
      .provider = "openai",
      .model = "gpt-5.5",
      .session_id = "session_test",
      .input = "do not focus composer",
      .status = "permission required",
      .transcript = {},
      .permission_prompt =
          ava::tui::PermissionPromptView{
              .tool_name = "bash", .operation = "bash", .target = "", .command = "true", .reason = ""},
      .width = 60,
      .height = 12});
  expect(permission_frame.size() == 12 &&
             std::ranges::none_of(permission_frame,
                                  [](const std::string& line) {
                                    return strip_sgr(line).find("❯ do not focus composer") != std::string::npos;
                                  }) &&
             std::ranges::any_of(permission_frame,
                                 [](const std::string& line) {
                                   return strip_sgr(line).find("PERMISSION REQUIRED") != std::string::npos;
                                 }),
         "tui composer frame replaces composer input with permission dock while active");

  const auto multiline_input = ava::tui::render_composer(ava::tui::ComposerSnapshot{.mode = "build",
                                                                                    .provider = "openai",
                                                                                    .model = "gpt-5.5",
                                                                                    .session_id = "session_test",
                                                                                    .input = "first\nsecond",
                                                                                    .status = "ready",
                                                                                    .transcript = {},
                                                                                    .width = 50,
                                                                                    .height = 8});
  expect(std::ranges::any_of(
             multiline_input,
             [](const std::string& line) { return strip_sgr(line).find("▎  ❯ first") != std::string::npos; }) &&
             std::ranges::any_of(
                 multiline_input,
                 [](const std::string& line) { return strip_sgr(line).find("▎    second") != std::string::npos; }),
         "tui renders shift-enter newlines as multiline composer input");
  const auto empty_composer_height = ava::tui::render_composer(ava::tui::ComposerSnapshot{.mode = "build",
                                                                                          .provider = "openai",
                                                                                          .model = "gpt-5.5",
                                                                                          .session_id = "session_test",
                                                                                          .input = "",
                                                                                          .status = "ready",
                                                                                          .transcript = {},
                                                                                          .width = 50,
                                                                                          .height = 12});
  const auto grown_composer_height =
      ava::tui::render_composer(ava::tui::ComposerSnapshot{.mode = "build",
                                                           .provider = "openai",
                                                           .model = "gpt-5.5",
                                                           .session_id = "session_test",
                                                           .input = "one\ntwo\nthree\nfour\nfive",
                                                           .status = "ready",
                                                           .transcript = {},
                                                           .width = 50,
                                                           .height = 12});
  const auto composer_bg_rows = [](const std::vector<std::string>& rendered) {
    return static_cast<std::size_t>(std::ranges::count_if(
        rendered, [](const std::string& line) { return line.find("\x1b[48;2;26;31;46m") != std::string::npos; }));
  };
  expect(composer_bg_rows(grown_composer_height) > composer_bg_rows(empty_composer_height) &&
             std::ranges::any_of(
                 grown_composer_height,
                 [](const std::string& line) { return strip_sgr(line).find("▎    five") != std::string::npos; }),
         "tui composer grows with multiline input and keeps the latest line visible");

  std::vector<ava::tui::TranscriptItem> many_items;
  for (int index = 0; index < 20; ++index) {
    many_items.push_back(ava::tui::TranscriptItem{.label = "line", .text = "item " + std::to_string(index)});
  }
  const auto scrolled = ava::tui::render_composer(ava::tui::ComposerSnapshot{.mode = "build",
                                                                             .provider = "openai",
                                                                             .model = "gpt-5.5",
                                                                             .session_id = "session_test",
                                                                             .input = "",
                                                                             .status = "ready",
                                                                             .transcript = many_items,
                                                                             .width = 40,
                                                                             .height = 12});
  expect(
      std::ranges::any_of(scrolled,
                          [](const std::string& line) { return line.find("item 19") != std::string::npos; }) &&
          std::ranges::any_of(
              scrolled, [](const std::string& line) { return line.find("older lines hidden") != std::string::npos; }) &&
          std::ranges::none_of(scrolled,
                               [](const std::string& line) { return line.find("item 0") != std::string::npos; }),
      "tui transcript viewport keeps newest lines and indicates hidden history");

  const auto scrolled_up = ava::tui::render_composer(ava::tui::ComposerSnapshot{.mode = "build",
                                                                                .provider = "openai",
                                                                                .model = "gpt-5.5",
                                                                                .session_id = "session_test",
                                                                                .input = "",
                                                                                .status = "ready",
                                                                                .transcript = many_items,
                                                                                .selected_slash_command_index = 0,
                                                                                .transcript_scroll_offset = 4,
                                                                                .width = 80,
                                                                                .height = 12});
  expect(
      std::ranges::any_of(
          scrolled_up, [](const std::string& line) { return line.find("newer lines hidden") != std::string::npos; }) &&
          std::ranges::any_of(scrolled_up,
                              [](const std::string& line) { return line.find("item 15") != std::string::npos; }) &&
          std::ranges::none_of(scrolled_up,
                               [](const std::string& line) { return line.find("item 19") != std::string::npos; }),
      "tui transcript viewport supports an explicit scroll offset");

  const auto wrapped_transcript = ava::tui::render_composer(ava::tui::ComposerSnapshot{
      .mode = "build",
      .provider = "openai",
      .model = "gpt-5.5",
      .session_id = "session_test",
      .input = "",
      .status = "ready",
      .transcript = {ava::tui::TranscriptItem{.label = "ava",
                                              .text = "abcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyz"
                                                      "abcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyz"
                                                      "abcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyz"
                                                      "abcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyz"
                                                      "abcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyz"}},
      .selected_slash_command_index = 0,
      .transcript_scroll_offset = 1,
      .width = 60,
      .height = 8});
  expect(std::ranges::any_of(
             wrapped_transcript,
             [](const std::string& line) { return strip_sgr(line).find("newer lines hidden") != std::string::npos; }),
         "tui transcript viewport wraps long transcript text before applying scroll offset");

  std::vector<ava::tui::TranscriptItem> mixed_items;
  for (int index = 0; index < 8; ++index) {
    mixed_items.push_back(ava::tui::TranscriptItem{.label = "line", .text = "old " + std::to_string(index)});
  }
  mixed_items.push_back(
      ava::tui::TranscriptItem{.tool = ava::tui::ToolTimelineItem{.status = ava::tui::ToolTimelineStatus::Success,
                                                                  .name = "grep",
                                                                  .argument_summary = "needle",
                                                                  .result_summary = "2 matches"}});
  mixed_items.push_back(ava::tui::TranscriptItem{.label = "ava", .text = "done"});
  const auto mixed_scrolled = ava::tui::render_composer(ava::tui::ComposerSnapshot{.mode = "build",
                                                                                   .provider = "openai",
                                                                                   .model = "gpt-5.5",
                                                                                   .session_id = "session_test",
                                                                                   .input = "",
                                                                                   .status = "ready",
                                                                                   .transcript = mixed_items,
                                                                                   .width = 60,
                                                                                   .height = 12});
  std::string mixed_visible;
  for (const auto& line : mixed_scrolled) {
    mixed_visible += strip_sgr(line);
    mixed_visible += '\n';
  }
  expect(mixed_visible.find("older lines hidden") != std::string::npos &&
             mixed_visible.find("[+]") != std::string::npos && mixed_visible.find("2 matches") != std::string::npos &&
             mixed_visible.find("AVA") != std::string::npos && mixed_visible.find("│ done") != std::string::npos &&
             mixed_visible.find("old 0") == std::string::npos,
         "tui transcript viewport scrolls mixed text and tool-card lines together");

  const auto multiline = ava::tui::render_composer(
      ava::tui::ComposerSnapshot{.mode = "build",
                                 .provider = "openai",
                                 .model = "gpt-5.5",
                                 .session_id = "session_test",
                                 .input = "",
                                 .status = "ready",
                                 .transcript = {ava::tui::TranscriptItem{.label = "ava", .text = "one\ntwo"}},
                                 .width = 80,
                                 .height = 14});
  expect(std::ranges::any_of(multiline,
                             [](const std::string& line) {
                               auto visible = strip_sgr(line);
                               return visible.find("│ one") != std::string::npos;
                             }) &&
             std::ranges::any_of(multiline,
                                 [](const std::string& line) {
                                   auto visible = strip_sgr(line);
                                   return visible.find("│ two") != std::string::npos;
                                 }),
         "tui renders multiline assistant transcript content inside the message block");

  const auto tool_card = ava::tui::render_composer(
      ava::tui::ComposerSnapshot{.mode = "build",
                                 .provider = "openai",
                                 .model = "gpt-5.5",
                                 .session_id = "session_test",
                                 .input = "",
                                 .status = "ready",
                                 .transcript = {ava::tui::TranscriptItem{
                                     .tool = ava::tui::ToolTimelineItem{.status = ava::tui::ToolTimelineStatus::Success,
                                                                        .name = "read_file",
                                                                        .argument_summary = "path=note.txt\x1b[31m",
                                                                        .result_summary = "read 12/12 bytes"}}},
                                 .width = 80,
                                 .height = 10});
  expect(std::ranges::any_of(tool_card,
                             [](const std::string& line) {
                               auto visible = strip_sgr(line);
                               return visible.find("[+]") != std::string::npos &&
                                      visible.find("read_file") != std::string::npos &&
                                      visible.find("path=note.txt") != std::string::npos;
                             }) &&
             std::ranges::any_of(tool_card,
                                 [](const std::string& line) {
                                   auto visible = strip_sgr(line);
                                   return visible.find("read 12/12 bytes") != std::string::npos;
                                 }) &&
             std::ranges::any_of(tool_card,
                                 [](const std::string& line) {
                                   return line.find("\x1b[38;2;52;211;153m[+]") != std::string::npos &&
                                          line.find("\x1b[1m\x1b[38;2;77;158;246mread_file") != std::string::npos &&
                                          line.find("\x1b[38;2;139;149;165mpath=note.txt") != std::string::npos;
                                 }),
         "tui renders compact styled tool timeline cards");
  expect(std::ranges::none_of(tool_card,
                              [](const std::string& line) { return line.find("\x1b[31m") != std::string::npos; }),
         "tui tool card rendering removes untrusted raw sgr escape sequences");

  const auto empty_tool_card = ava::tui::render_composer(
      ava::tui::ComposerSnapshot{.mode = "build",
                                 .provider = "openai",
                                 .model = "gpt-5.5",
                                 .session_id = "session_test",
                                 .input = "",
                                 .status = "ready",
                                 .transcript = {ava::tui::TranscriptItem{
                                     .tool = ava::tui::ToolTimelineItem{.status = ava::tui::ToolTimelineStatus::Success,
                                                                        .name = "",
                                                                        .argument_summary = "",
                                                                        .result_summary = ""}}},
                                 .width = 40,
                                 .height = 8});
  expect(std::ranges::any_of(empty_tool_card,
                             [](const std::string& line) {
                               const auto visible = strip_sgr(line);
                               return visible.find("[+]") != std::string::npos &&
                                      visible.find("unknown") != std::string::npos;
                             }) &&
             std::ranges::all_of(empty_tool_card, [](const std::string& line) { return visible_columns(line) <= 40; }),
         "tui renders empty tool-card fields with a safe fallback name");

  const auto running_error_cards = ava::tui::render_composer(ava::tui::ComposerSnapshot{
      .mode = "build",
      .provider = "openai",
      .model = "gpt-5.5",
      .session_id = "session_test",
      .input = "",
      .status = "ready",
      .transcript = {ava::tui::TranscriptItem{
                         .tool = ava::tui::ToolTimelineItem{.status = ava::tui::ToolTimelineStatus::Running,
                                                            .name = "bash",
                                                            .argument_summary = "command=build\x1b[31m now",
                                                            .result_summary = ""}},
                     ava::tui::TranscriptItem{
                         .tool = ava::tui::ToolTimelineItem{.status = ava::tui::ToolTimelineStatus::Error,
                                                            .name = "write_file",
                                                            .argument_summary = std::string(120, 'a') + "\x1b[31m",
                                                            .result_summary = "error: denied\x1b[31m"}}},
      .width = 60,
      .height = 14});
  expect(
      std::ranges::any_of(running_error_cards,
                          [](const std::string& line) {
                            auto visible = strip_sgr(line);
                            return visible.find("[~]") != std::string::npos &&
                                   visible.find("bash") != std::string::npos &&
                                   visible.find("command=build?") != std::string::npos;
                          }) &&
          std::ranges::any_of(running_error_cards,
                              [](const std::string& line) {
                                auto visible = strip_sgr(line);
                                return visible.find("[x]") != std::string::npos &&
                                       visible.find("write_file") != std::string::npos;
                              }) &&
          std::ranges::all_of(running_error_cards, [](const std::string& line) { return visible_columns(line) <= 60; }),
      "tui renders running/error tool cards with sanitized truncated summaries");
  expect(std::ranges::none_of(running_error_cards,
                              [](const std::string& line) { return line.find("\x1b[31m") != std::string::npos; }),
         "tui running/error tool cards remove untrusted raw sgr escape sequences");
  expect(std::ranges::any_of(
             running_error_cards,
             [](const std::string& line) { return line.find("\x1b[38;2;251;191;36m[~]") != std::string::npos; }) &&
             std::ranges::any_of(
                 running_error_cards,
                 [](const std::string& line) { return line.find("\x1b[38;2;248;113;113m[x]") != std::string::npos; }),
         "tui emits trusted sgr status colors for running and error tool cards");

  const auto tabbed = ava::tui::render_composer(
      ava::tui::ComposerSnapshot{.mode = "build",
                                 .provider = "openai",
                                 .model = "gpt-5.5",
                                 .session_id = "session_test",
                                 .input = "",
                                 .status = "tab\tstatus",
                                 .transcript = {ava::tui::TranscriptItem{.label = "ava", .text = "tab\ttext"}},
                                 .width = 30,
                                 .height = 8});
  expect(std::ranges::none_of(tabbed, [](const std::string& line) { return line.find('\t') != std::string::npos; }) &&
             std::ranges::all_of(tabbed, [](const std::string& line) { return visible_columns(line) <= 30; }),
         "tui expands tabs before rendering width-bounded lines");

  std::string exact_width_utf8_status;
  for (int index = 0; index < 12; ++index) {
    exact_width_utf8_status += "\xC3\xA9";
  }
  const auto exact_width_utf8 = ava::tui::render_composer(ava::tui::ComposerSnapshot{.mode = "build",
                                                                                     .provider = "openai",
                                                                                     .model = "gpt-5.5",
                                                                                     .session_id = "session_test",
                                                                                     .input = "",
                                                                                     .status = exact_width_utf8_status,
                                                                                     .transcript = {},
                                                                                     .width = 20,
                                                                                     .height = 8});
  expect(std::ranges::all_of(exact_width_utf8, [](const std::string& line) { return visible_columns(line) <= 20; }) &&
             std::ranges::any_of(
                 exact_width_utf8,
                 [](const std::string& line) { return strip_sgr(line).find("▎  [build]") != std::string::npos; }),
         "tui width fitting preserves the old AVA composer surface at minimum width");

  const auto utf8 = ava::tui::render_composer(ava::tui::ComposerSnapshot{
      .mode = "build",
      .provider = "openai",
      .model = "gpt-5.5",
      .session_id = "session_test",
      .input = "",
      .status = "ready",
      .transcript = {ava::tui::TranscriptItem{.label = "ava", .text = std::string(13, 'x') + "\xC3\xA9" + "zzz"}},
      .width = 20,
      .height = 8});
  expect(std::ranges::none_of(utf8,
                              [](const std::string& line) {
                                return !line.empty() && (static_cast<unsigned char>(line.back()) & 0xC0U) == 0xC0U;
                              }),
         "tui truncation does not leave a trailing utf-8 starter byte");
}

}  // namespace

void run_tui_composer_tests() {
  test_tui_composer_rendering_and_input();
}
