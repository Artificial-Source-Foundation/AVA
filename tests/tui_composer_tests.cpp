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
#include <string_view>
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
#include "ava/tui/composer_editor.h"
#include "ava/tui/event_state.h"
#include "ava/tui/keybindings.h"
#include "ava/tui/runtime.h"
#include "ava/tui/terminal.h"
#include "tests/support/fake_transport.h"
#include "tests/support/test_harness.h"

namespace {

void test_tui_composer_rendering_and_input() {
  expect(ava::tui::terminal_escape_sequence_key("[27;2;13~") == ava::tui::Key::ShiftEnter &&
             ava::tui::terminal_escape_sequence_key("[13;2u") == ava::tui::Key::ShiftEnter &&
             ava::tui::terminal_escape_sequence_key("[200~") == ava::tui::Key::Unknown,
         "terminal escape parser maps modified enter sequences to shift-enter without treating paste markers as text");

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

  auto single_question =
      ava::tui::QuestionPromptView{.header = "Choose",
                                   .question = "Pick one",
                                   .options = {ava::tui::QuestionPromptOptionView{.value = "alpha", .label = "Alpha"},
                                               ava::tui::QuestionPromptOptionView{.value = "beta", .label = "Beta"}},
                                   .multiple = false,
                                   .allow_custom = true,
                                   .selected_option_index = 0,
                                   .custom_text = ""};
  auto question_input = ava::tui::handle_question_prompt_input(
      single_question, ava::tui::InputEvent{.key = ava::tui::Key::Character, .character = '2'});
  expect(question_input.action == ava::tui::QuestionPromptInputAction::Resolve &&
             question_input.selected_option_index == 1 && question_input.options[1].selected,
         "question prompt numeric shortcut selects and resolves a single-select option");
  question_input = ava::tui::handle_question_prompt_input(
      single_question, ava::tui::InputEvent{.key = ava::tui::Key::Character, .character = 'x'});
  expect(question_input.action == ava::tui::QuestionPromptInputAction::Redraw && question_input.custom_text == "x" &&
             std::ranges::none_of(question_input.options,
                                  [](const ava::tui::QuestionPromptOptionView& option) { return option.selected; }),
         "question prompt custom text edits clear single-select option state");
  single_question.custom_text = question_input.custom_text;
  question_input = ava::tui::handle_question_prompt_input(
      single_question, ava::tui::InputEvent{.key = ava::tui::Key::Character, .character = ' '});
  expect(question_input.action == ava::tui::QuestionPromptInputAction::Redraw && question_input.custom_text == "x ",
         "question prompt custom text can include spaces after typing starts");
  single_question.custom_text = question_input.custom_text;
  question_input = ava::tui::handle_question_prompt_input(
      single_question,
      ava::tui::InputEvent{.key = ava::tui::Key::Character, .character = static_cast<char>(0xC3), .text = "\xC3\xA9"});
  expect(question_input.action == ava::tui::QuestionPromptInputAction::Redraw &&
             question_input.custom_text == std::string("x ") + "\xC3\xA9",
         "question prompt custom text preserves utf-8 input");
  single_question.custom_text = "x";
  question_input =
      ava::tui::handle_question_prompt_input(single_question, ava::tui::InputEvent{.key = ava::tui::Key::Backspace});
  expect(question_input.action == ava::tui::QuestionPromptInputAction::Redraw && question_input.custom_text.empty(),
         "question prompt backspace edits custom text");

  auto multi_question =
      ava::tui::QuestionPromptView{.header = "Choose",
                                   .question = "Pick many",
                                   .options = {ava::tui::QuestionPromptOptionView{.value = "read", .label = "Read"},
                                               ava::tui::QuestionPromptOptionView{.value = "grep", .label = "Grep"}},
                                   .multiple = true,
                                   .allow_custom = true,
                                   .selected_option_index = 0,
                                   .custom_text = ""};
  question_input = ava::tui::handle_question_prompt_input(
      multi_question, ava::tui::InputEvent{.key = ava::tui::Key::Character, .character = ' '});
  expect(question_input.action == ava::tui::QuestionPromptInputAction::Redraw && question_input.options[0].selected,
         "question prompt space toggles selected multi-select option");
  multi_question.options = question_input.options;
  question_input = ava::tui::handle_question_prompt_input(
      multi_question, ava::tui::InputEvent{.key = ava::tui::Key::Character, .character = '2'});
  expect(question_input.action == ava::tui::QuestionPromptInputAction::Redraw && question_input.options[0].selected &&
             question_input.options[1].selected,
         "question prompt numeric shortcut toggles multi-select options without resolving");
  multi_question.options = question_input.options;
  question_input =
      ava::tui::handle_question_prompt_input(multi_question, ava::tui::InputEvent{.key = ava::tui::Key::Enter});
  expect(question_input.action == ava::tui::QuestionPromptInputAction::Resolve && question_input.options[0].selected &&
             question_input.options[1].selected,
         "question prompt enter resolves current multi-select choices");
  question_input =
      ava::tui::handle_question_prompt_input(multi_question, ava::tui::InputEvent{.key = ava::tui::Key::Escape});
  expect(question_input.action == ava::tui::QuestionPromptInputAction::Cancel, "question prompt escape cancels safely");
  auto empty_multi_question = multi_question;
  for (auto& option : empty_multi_question.options) option.selected = false;
  empty_multi_question.custom_text.clear();
  auto empty_multi_answer = ava::tui::question_answer_from_prompt_view(empty_multi_question);
  expect(empty_multi_answer && empty_multi_answer->selected_options.empty() && empty_multi_answer->custom_text.empty(),
         "question prompt accepts an empty multi-select answer");

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

  ava::tui::ComposerDraftState draft;
  expect(ava::tui::insert_composer_draft_text(draft, std::string("a") + "\xC3\xA9") &&
             draft.text == std::string("a") + "\xC3\xA9" && draft.cursor == draft.text.size(),
         "tui draft editor inserts utf-8 text at the cursor");
  expect(ava::tui::apply_composer_draft_action(draft, ava::tui::TuiAction::DeleteBackward) && draft.text == "a",
         "tui draft editor deletes a complete utf-8 codepoint before the cursor");
  expect(ava::tui::apply_composer_draft_action(draft, ava::tui::TuiAction::Undo) &&
             draft.text == std::string("a") + "\xC3\xA9",
         "tui draft editor undo restores the previous edit");
  expect(ava::tui::apply_composer_draft_action(draft, ava::tui::TuiAction::CursorLeft) && draft.cursor == 1,
         "tui draft editor moves left by full utf-8 codepoint boundaries");
  expect(ava::tui::apply_composer_draft_action(draft, ava::tui::TuiAction::DeleteBackward) &&
             draft.text == std::string("\xC3\xA9"),
         "tui draft editor preserves the utf-8 codepoint after deleting preceding ascii text");
  ava::tui::reset_composer_draft(draft, "one two three");
  expect(ava::tui::apply_composer_draft_action(draft, ava::tui::TuiAction::CursorWordLeft) && draft.cursor == 8,
         "tui draft editor moves to the previous word start");
  expect(ava::tui::apply_composer_draft_action(draft, ava::tui::TuiAction::DeleteWordBackward) &&
             draft.text == "one three" && draft.kill_buffer == "two ",
         "tui draft editor deletes the previous word into the kill buffer");
  ava::tui::reset_composer_draft(draft, "first\nsecond line");
  expect(ava::tui::apply_composer_draft_action(draft, ava::tui::TuiAction::CursorLineStart) && draft.cursor == 6,
         "tui draft editor moves to the start of the current line");
  expect(ava::tui::apply_composer_draft_action(draft, ava::tui::TuiAction::CursorLineEnd) &&
             draft.cursor == draft.text.size(),
         "tui draft editor moves to the end of the current line");
  expect(ava::tui::apply_composer_draft_action(draft, ava::tui::TuiAction::DeleteToLineStart) &&
             draft.text == "first\n" && draft.kill_buffer == "second line",
         "tui draft editor deletes from cursor to line start into the kill buffer");
  expect(ava::tui::apply_composer_draft_action(draft, ava::tui::TuiAction::Yank) && draft.text == "first\nsecond line",
         "tui draft editor yanks the last killed line text");
  expect(ava::tui::apply_composer_draft_action(draft, ava::tui::TuiAction::Undo) && draft.text == "first\n",
         "tui draft editor undo removes the yanked text");
  ava::tui::reset_composer_draft(draft, "first\nsecond line", 6);
  expect(ava::tui::apply_composer_draft_action(draft, ava::tui::TuiAction::DeleteToLineEnd) &&
             draft.text == "first\n" && draft.kill_buffer == "second line",
         "tui draft editor deletes from cursor to line end into the kill buffer");
  ava::tui::reset_composer_draft(draft, "pending message");
  expect(ava::tui::apply_composer_draft_action(draft, ava::tui::TuiAction::ClearInput) && draft.text.empty() &&
             draft.cursor == 0 && draft.kill_buffer == "pending message",
         "tui draft editor clears a non-empty composer draft into the kill buffer");
  expect(ava::tui::apply_composer_draft_action(draft, ava::tui::TuiAction::Undo) && draft.text == "pending message",
         "tui draft editor undo restores a cleared composer draft");
  expect(ava::tui::normalize_composer_paste_text("one\r\ntwo\rthree") == "one\ntwo\nthree",
         "tui paste normalizes crlf and lone carriage returns into newlines");
  expect(ava::tui::normalize_composer_paste_text(std::string("a\x01\tb\n", 5) + "\xC3\xA9") ==
             std::string("a\tb\n", 4) + "\xC3\xA9",
         "tui paste strips controls while preserving tabs, newlines, and utf-8 bytes");

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
                                 .status = "slash palette dismissed",
                                 .transcript = {ava::tui::TranscriptItem{.label = "you", .text = "hello"},
                                                ava::tui::TranscriptItem{.label = "ava", .text = "world"}},
                                 .width = 80,
                                 .height = 14});
  expect(lines.size() == 14, "tui fills the viewport with transcript, spacer, and composer lines");
  expect(!lines.empty() && strip_sgr(lines.front()).find("╭─ You") != std::string::npos,
         "tui starts short chats at the top of the transcript area");
  expect(!lines.empty() && lines.back().find("\x1b[48;2;26;31;46m") != std::string::npos &&
             std::ranges::none_of(lines,
                                  [](const std::string& line) { return line.find("/ commands") != std::string::npos; }),
         "tui keeps only the composer block at the bottom");
  expect(std::ranges::any_of(
             lines, [](const std::string& line) { return strip_sgr(line).find("▎  ❯ /help") != std::string::npos; }),
         "tui renders old AVA-style composer input");
  expect(std::ranges::none_of(lines,
                              [](const std::string& line) {
                                return strip_sgr(line).find("slash palette dismissed") != std::string::npos;
                              }),
         "tui keeps transient composer status text out of the footer");
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

  const auto processing_lines =
      ava::tui::render_composer(ava::tui::ComposerSnapshot{.mode = "build",
                                                           .provider = "openai",
                                                           .model = "gpt-5.5",
                                                           .session_id = "session_test",
                                                           .input = "",
                                                           .status = "thinking...",
                                                           .processing = true,
                                                           .spinner_frame = 1,
                                                           .token_status = "tokens 1.3k (0.7%)",
                                                           .transcript = {},
                                                           .width = 80,
                                                           .height = 10});
  expect(std::ranges::any_of(processing_lines,
                             [](const std::string& line) {
                               const auto visible = strip_sgr(line);
                               return visible.find("thinking...") == std::string::npos &&
                                      visible.find("working") == std::string::npos &&
                                      visible.find("⠙") != std::string::npos &&
                                      visible.find("tokens 1.3k (0.7%)") != std::string::npos;
                             }),
         "tui renders a spinner-only processing indicator and token-status slot");

  const auto token_margin_lines =
      ava::tui::render_composer(ava::tui::ComposerSnapshot{.mode = "build",
                                                           .provider = "openai",
                                                           .model = "gpt-5.5",
                                                           .session_id = "session_test",
                                                           .input = "",
                                                           .status = "ready",
                                                           .token_status = "tokens 1.3k (0.7%)",
                                                           .transcript = {},
                                                           .width = 80,
                                                           .height = 10});
  expect(std::ranges::any_of(token_margin_lines,
                             [](const std::string& line) {
                               const auto visible = strip_sgr(line);
                               const auto token_text = std::string_view("tokens 1.3k (0.7%)");
                               const auto token_pos = visible.find(token_text);
                               return token_pos != std::string::npos &&
                                      visible.substr(token_pos + token_text.size(), 2) == "  ";
                             }),
         "tui leaves right margin after token-status text");

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

  const auto key_bindings = ava::tui::parse_key_bindings_json(
      "{\"submit\":\"Ctrl+T, Enter\",\"new_line\":\"Shift+Enter\",\"delete_to_line_start\":\"Ctrl+U\","
      "\"autocomplete_accept\":\"Tab\",\"variant_cycle\":\"Ctrl+D\"}");
  expect(
      key_bindings && ava::tui::action_for_key(*key_bindings, ava::tui::Key::CtrlT) == ava::tui::TuiAction::Submit &&
          ava::tui::action_for_key(*key_bindings, ava::tui::Key::CtrlD) == ava::tui::TuiAction::VariantCycle &&
          ava::tui::key_matches_action(*key_bindings, ava::tui::TuiAction::DeleteToLineStart, ava::tui::Key::CtrlU) &&
          ava::tui::key_matches_action(*key_bindings, ava::tui::TuiAction::AutocompleteAccept, ava::tui::Key::Tab) &&
          ava::tui::keys_display(*key_bindings, ava::tui::TuiAction::Submit).find("Ctrl+T") != std::string::npos,
      "tui keybind parser maps configured keys to semantic actions and display text");
  const auto default_bindings = ava::tui::default_key_bindings();
  expect(
      ava::tui::key_matches_action(default_bindings, ava::tui::TuiAction::HistoryPrev, ava::tui::Key::ArrowUp) &&
          ava::tui::key_matches_action(default_bindings, ava::tui::TuiAction::PalettePrev, ava::tui::Key::ArrowUp) &&
          ava::tui::key_matches_action(default_bindings, ava::tui::TuiAction::ModeToggle, ava::tui::Key::Tab) &&
          ava::tui::key_matches_action(default_bindings, ava::tui::TuiAction::AutocompleteAccept, ava::tui::Key::Tab) &&
          ava::tui::key_matches_action(default_bindings, ava::tui::TuiAction::Undo, ava::tui::Key::CtrlZ) &&
          ava::tui::key_matches_action(default_bindings, ava::tui::TuiAction::Yank, ava::tui::Key::CtrlY) &&
          ava::tui::key_matches_action(default_bindings, ava::tui::TuiAction::Interrupt, ava::tui::Key::CtrlC),
      "tui default keybinds preserve context-specific semantic actions for shared keys");
  const auto help_items = ava::tui::key_binding_help_items(default_bindings);
  expect(std::ranges::any_of(help_items,
                             [](const ava::tui::TuiKeyBindingHelpItem& item) {
                               return item.action == "variant_cycle" && item.keys.find("Ctrl+T") != std::string::npos;
                             }) &&
             std::ranges::any_of(help_items,
                                 [](const ava::tui::TuiKeyBindingHelpItem& item) {
                                   return item.action == "delete_to_line_start" &&
                                          item.keys.find("Ctrl+U") != std::string::npos;
                                 }) &&
             std::ranges::any_of(help_items,
                                 [](const ava::tui::TuiKeyBindingHelpItem& item) {
                                   return item.action == "undo" && item.keys.find("Ctrl+Z") != std::string::npos;
                                 }) &&
             std::ranges::any_of(help_items,
                                 [](const ava::tui::TuiKeyBindingHelpItem& item) {
                                   return item.action == "yank" && item.keys.find("Ctrl+Y") != std::string::npos;
                                 }),
         "tui keybind help lists concrete semantic action names and effective keys");
  expect(!ava::tui::parse_key_bindings_json("{\"submit\":\"Hyper+Enter\"}"),
         "tui keybind parser rejects unknown key names");
  expect(!ava::tui::parse_key_bindings_json("{\"submt\":\"Enter\"}"),
         "tui keybind parser rejects unknown action names");
  const auto escaped_action_keybinds =
      ava::tui::parse_key_bindings_json("{\"\\u0073\\u0075\\u0062\\u006d\\u0069\\u0074\":\"Ctrl+T\"}");
  expect(escaped_action_keybinds &&
             ava::tui::key_matches_action(*escaped_action_keybinds, ava::tui::TuiAction::Submit, ava::tui::Key::CtrlT),
         "tui keybind parser accepts JSON unicode escapes in action names");
  expect(!ava::tui::parse_key_bindings_json("{\"submit\":123}"), "tui keybind parser rejects non-string action values");

  const auto keybind_root = temp_root() / "tui-keybinds";
  std::filesystem::remove_all(keybind_root);
  std::filesystem::create_directories(keybind_root);
  const auto keybinds_file = keybind_root / "keybinds.json";
  const auto missing_keybinds = ava::tui::load_key_bindings(keybinds_file);
  expect(missing_keybinds &&
             ava::tui::key_matches_action(*missing_keybinds, ava::tui::TuiAction::Submit, ava::tui::Key::Enter),
         "tui keybind file loader falls back to defaults when the file is missing");
  {
    std::ofstream output(keybinds_file);
    output << "{\"submit\":\"Ctrl+T\",\"variant_cycle\":\"Ctrl+D\"}";
  }
  const auto loaded_keybinds = ava::tui::load_key_bindings(keybinds_file);
  expect(loaded_keybinds &&
             ava::tui::key_matches_action(*loaded_keybinds, ava::tui::TuiAction::Submit, ava::tui::Key::CtrlT) &&
             ava::tui::key_matches_action(*loaded_keybinds, ava::tui::TuiAction::VariantCycle, ava::tui::Key::CtrlD),
         "tui keybind file loader reads valid configured bindings");
  {
    std::ofstream output(keybinds_file);
    output << "{\"submit\":\"Enter\"";
  }
  expect(!ava::tui::load_key_bindings(keybinds_file), "tui keybind file loader rejects malformed JSON");
  {
    std::ofstream output(keybinds_file);
    output << "{\"submt\":\"Enter\"}";
  }
  expect(!ava::tui::load_key_bindings(keybinds_file), "tui keybind file loader rejects unknown actions");

  const std::vector<ava::tui::SlashCommandItem> disabled_slash_commands = {
      ava::tui::SlashCommandItem{.command = "/models",
                                 .description = "Select model",
                                 .category = "Planned",
                                 .aliases = {"/model"},
                                 .key_display = "Ctrl+M",
                                 .enabled = false,
                                 .disabled_reason = "model switching is not implemented"},
      ava::tui::SlashCommandItem{.command = "/mode", .description = "Toggle mode", .category = "General"}};
  const auto alias_matches = ava::tui::filter_slash_commands("/model", disabled_slash_commands);
  expect(alias_matches.size() == 1 && alias_matches.front().command == "/models",
         "tui slash palette filters aliases as well as primary command names");
  expect(ava::tui::slash_palette_visible("/model", disabled_slash_commands),
         "tui slash palette keeps disabled exact alias matches visible");
  const auto disabled_reason = ava::tui::slash_command_selection_disabled_reason("/model", disabled_slash_commands, 0);
  expect(disabled_reason && disabled_reason->find("not implemented") != std::string::npos,
         "tui slash selection exposes disabled command explanations");

  const auto disabled_palette =
      ava::tui::render_composer(ava::tui::ComposerSnapshot{.mode = "build",
                                                           .provider = "openai",
                                                           .model = "gpt-5.5",
                                                           .session_id = "session_test",
                                                           .input = "/model",
                                                           .status = "ready",
                                                           .transcript = {},
                                                           .slash_commands = disabled_slash_commands,
                                                           .selected_slash_command_index = 0,
                                                           .width = 140,
                                                           .height = 10});
  expect(std::ranges::any_of(disabled_palette,
                             [](const std::string& line) {
                               const auto visible = strip_sgr(line);
                               return visible.find("/models (/model)") != std::string::npos &&
                                      visible.find("Ctrl+M") != std::string::npos &&
                                      visible.find("disabled: model switching is not implemented") != std::string::npos;
                             }),
         "tui slash palette renders aliases, key displays, and disabled reasons");

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
             palette, [](const std::string& line) { return line.find("commands matching /g") == std::string::npos; }) &&
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
                                   return visible.find("› /glob") != std::string::npos &&
                                          visible.find("selected") == std::string::npos &&
                                          visible.find("(2/2)") == std::string::npos;
                                 }) &&
             std::ranges::any_of(palette,
                                 [](const std::string& line) {
                                   return line.find("\x1b[7m› /glob") != std::string::npos &&
                                          line.find("\x1b[0m") != std::string::npos;
                                 }) &&
             std::ranges::none_of(palette,
                                  [](const std::string& line) { return line.find("/help") != std::string::npos; }),
         "tui renders filtered slash-command palette with composer-integrated selected item highlight");
  const auto suppressed_palette = ava::tui::render_composer(ava::tui::ComposerSnapshot{.mode = "build",
                                                                                       .provider = "openai",
                                                                                       .model = "gpt-5.5",
                                                                                       .session_id = "session_test",
                                                                                       .input = "/g",
                                                                                       .status = "ready",
                                                                                       .transcript = {},
                                                                                       .slash_commands = slash_commands,
                                                                                       .slash_palette_suppressed = true,
                                                                                       .width = 80,
                                                                                       .height = 12});
  expect(std::ranges::none_of(
             suppressed_palette,
             [](const std::string& line) { return strip_sgr(line).find("/grep") != std::string::npos; }) &&
             std::ranges::any_of(
                 suppressed_palette,
                 [](const std::string& line) { return strip_sgr(line).find("❯ /g") != std::string::npos; }),
         "tui can dismiss slash autocomplete without clearing the draft input");
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
  const auto suppressed_clicked_palette_index =
      ava::tui::slash_palette_selection_for_screen_row(ava::tui::ComposerSnapshot{.mode = "build",
                                                                                  .provider = "openai",
                                                                                  .model = "gpt-5.5",
                                                                                  .session_id = "session_test",
                                                                                  .input = "/g",
                                                                                  .status = "ready",
                                                                                  .transcript = {},
                                                                                  .slash_commands = slash_commands,
                                                                                  .selected_slash_command_index = 1,
                                                                                  .slash_palette_suppressed = true,
                                                                                  .width = 80,
                                                                                  .height = 12},
                                                       9);
  expect(!suppressed_clicked_palette_index, "tui ignores slash palette click mapping after autocomplete is dismissed");
  const auto blocked_question_palette_index = ava::tui::slash_palette_selection_for_screen_row(
      ava::tui::ComposerSnapshot{.mode = "build",
                                 .provider = "openai",
                                 .model = "gpt-5.5",
                                 .session_id = "session_test",
                                 .input = "/g",
                                 .status = "ready",
                                 .transcript = {},
                                 .slash_commands = slash_commands,
                                 .question_prompt = ava::tui::QuestionPromptView{.header = "Question",
                                                                                 .question = "Choose",
                                                                                 .options = {},
                                                                                 .multiple = false,
                                                                                 .allow_custom = false,
                                                                                 .selected_option_index = 0,
                                                                                 .custom_text = ""},
                                 .selected_slash_command_index = 1,
                                 .width = 80,
                                 .height = 12},
      9);
  expect(!blocked_question_palette_index, "tui ignores slash palette click mapping while a question prompt is active");

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
                             [](const std::string& line) { return line.find("› /item6") != std::string::npos; }) &&
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
                                                       1);
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
  expect(first_scrolled_palette_click && *first_scrolled_palette_click == 3 && selected_scrolled_palette_click &&
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
                 [](const std::string& line) { return strip_sgr(line).find("› /item4") != std::string::npos; }) &&
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
             std::ranges::none_of(
                 permission_starved,
                 [](const std::string& line) { return strip_sgr(line).find("lines hidden") != std::string::npos; }) &&
             std::ranges::none_of(
                 permission_starved,
                 [](const std::string& line) { return strip_sgr(line).find("❯ hidden input") != std::string::npos; }),
         "tui permission prompt handles height-starved transcript overflow without hidden-line banners");

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

  const auto question_frame = ava::tui::render_composer(ava::tui::ComposerSnapshot{
      .mode = "build",
      .provider = "openai",
      .model = "gpt-5.5",
      .session_id = "session_test",
      .input = "do not focus composer",
      .status = "question required",
      .transcript = {},
      .question_prompt =
          ava::tui::QuestionPromptView{
              .header = "Choose tools",
              .question = "Pick the tools to run",
              .options = {ava::tui::QuestionPromptOptionView{.value = "read", .label = "Read files"},
                          ava::tui::QuestionPromptOptionView{
                              .value = "grep", .label = "Search text", .selected = true}},
              .multiple = true,
              .allow_custom = true,
              .selected_option_index = 1,
              .custom_text = "explain"},
      .width = 64,
      .height = 12});
  expect(question_frame.size() == 12 &&
             std::ranges::none_of(question_frame,
                                  [](const std::string& line) {
                                    return strip_sgr(line).find("❯ do not focus composer") != std::string::npos;
                                  }) &&
             std::ranges::any_of(question_frame,
                                 [](const std::string& line) {
                                   return strip_sgr(line).find("Choose tools (multi-select)") != std::string::npos;
                                 }) &&
             std::ranges::any_of(question_frame,
                                 [](const std::string& line) {
                                   return strip_sgr(line).find("2. [x] Search text") != std::string::npos;
                                 }) &&
             std::ranges::any_of(
                 question_frame,
                 [](const std::string& line) { return strip_sgr(line).find("Custom: explain") != std::string::npos; }),
         "tui composer frame replaces composer input with a multi-select question dock while active");

  const auto secret_question_frame = ava::tui::render_composer(
      ava::tui::ComposerSnapshot{.mode = "build",
                                 .provider = "openai",
                                 .model = "gpt-5.5",
                                 .session_id = "session_test",
                                 .input = "",
                                 .status = "question required",
                                 .transcript = {},
                                 .question_prompt = ava::tui::QuestionPromptView{.header = "Connect",
                                                                                 .question = "Paste API key",
                                                                                 .options = {},
                                                                                 .multiple = false,
                                                                                 .allow_custom = true,
                                                                                 .secret = true,
                                                                                 .custom_text = "sk-visible-secret"},
                                 .width = 64,
                                 .height = 9});
  expect(std::ranges::none_of(
             secret_question_frame,
             [](const std::string& line) { return strip_sgr(line).find("sk-visible-secret") != std::string::npos; }) &&
             std::ranges::any_of(secret_question_frame,
                                 [](const std::string& line) {
                                   return strip_sgr(line).find("Custom: *****************") != std::string::npos;
                                 }),
         "tui question dock masks secret custom input");

  const auto modal_question_frame = ava::tui::render_composer(ava::tui::ComposerSnapshot{
      .mode = "build",
      .provider = "openai",
      .model = "gpt-5.5",
      .session_id = "session_test",
      .input = "composer stays behind modal",
      .status = "question required",
      .transcript = {ava::tui::TranscriptItem{.label = "ava", .text = "background transcript"}},
      .question_prompt = ava::tui::QuestionPromptView{.header = "Connect a provider",
                                                      .question = "Select provider",
                                                      .options = {ava::tui::QuestionPromptOptionView{.value = "openai",
                                                                                                     .label = "OpenAI"},
                                                                  ava::tui::QuestionPromptOptionView{
                                                                      .value = "anthropic", .label = "Anthropic"}},
                                                      .multiple = false,
                                                      .allow_custom = true,
                                                      .secret = false,
                                                      .modal = true,
                                                      .searchable = true,
                                                      .selected_option_index = 1,
                                                      .custom_text = "anth"},
      .width = 80,
      .height = 16});
  expect(modal_question_frame.size() == 16 &&
             std::ranges::any_of(modal_question_frame,
                                 [](const std::string& line) {
                                   return strip_sgr(line).find("Connect a provider") != std::string::npos;
                                 }) &&
             std::ranges::any_of(
                 modal_question_frame,
                 [](const std::string& line) { return strip_sgr(line).find("Search: anth") != std::string::npos; }) &&
             std::ranges::any_of(
                 modal_question_frame,
                 [](const std::string& line) { return strip_sgr(line).find("Anthropic") != std::string::npos; }) &&
             std::ranges::none_of(
                 modal_question_frame,
                 [](const std::string& line) { return strip_sgr(line).find("OpenAI") != std::string::npos; }),
         "tui renders searchable provider questions as centered filtered modals");

  auto searchable_question = ava::tui::QuestionPromptView{
      .header = "Connect a provider",
      .question = "Select provider",
      .options = {ava::tui::QuestionPromptOptionView{.value = "openai", .label = "OpenAI"},
                  ava::tui::QuestionPromptOptionView{.value = "anthropic", .label = "Anthropic"}},
      .multiple = false,
      .allow_custom = true,
      .secret = false,
      .modal = true,
      .searchable = true,
      .selected_option_index = 0,
      .custom_text = ""};
  auto searchable_input = ava::tui::handle_question_prompt_input(
      searchable_question, ava::tui::InputEvent{.key = ava::tui::Key::Character, .character = 'h'});
  expect(searchable_input.action == ava::tui::QuestionPromptInputAction::Redraw &&
             searchable_input.custom_text == "h" && searchable_input.selected_option_index == 1,
         "searchable question typing filters and moves selection to the first match");
  searchable_question.custom_text = "anth";
  searchable_question.selected_option_index = 1;
  searchable_input =
      ava::tui::handle_question_prompt_input(searchable_question, ava::tui::InputEvent{.key = ava::tui::Key::Enter});
  expect(
      searchable_input.action == ava::tui::QuestionPromptInputAction::Resolve && searchable_input.options[1].selected,
      "searchable question enter selects the matched provider option");
  searchable_question.custom_text = "custom-provider";
  searchable_question.selected_option_index = 0;
  searchable_input =
      ava::tui::handle_question_prompt_input(searchable_question, ava::tui::InputEvent{.key = ava::tui::Key::Enter});
  const auto custom_search_answer = ava::tui::question_answer_from_prompt_view(
      ava::tui::QuestionPromptView{.header = searchable_question.header,
                                   .question = searchable_question.question,
                                   .options = searchable_input.options,
                                   .multiple = searchable_question.multiple,
                                   .allow_custom = searchable_question.allow_custom,
                                   .secret = searchable_question.secret,
                                   .modal = searchable_question.modal,
                                   .searchable = searchable_question.searchable,
                                   .selected_option_index = searchable_input.selected_option_index,
                                   .custom_text = searchable_input.custom_text});
  expect(custom_search_answer && custom_search_answer->selected_options.empty() &&
             custom_search_answer->custom_text == "custom-provider",
         "searchable question enter resolves custom provider ids when no option matches");

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
  const auto tall_draft = ava::tui::render_composer(
      ava::tui::ComposerSnapshot{.mode = "build",
                                 .provider = "openai",
                                 .model = "gpt-5.5",
                                 .session_id = "session_test",
                                 .input = "one\ntwo\nthree\nfour\nfive\nsix\nseven\neight\nnine",
                                 .status = "ready",
                                 .transcript = {},
                                 .width = 70,
                                 .height = 12});
  expect(
      std::ranges::none_of(
          tall_draft, [](const std::string& line) { return strip_sgr(line).find("draft +") != std::string::npos; }) &&
          std::ranges::any_of(
              tall_draft,
              [](const std::string& line) { return strip_sgr(line).find("▎    nine") != std::string::npos; }) &&
          std::ranges::none_of(
              tall_draft,
              [](const std::string& line) { return strip_sgr(line).find("▎  ❯ one") != std::string::npos; }),
      "tui composer hides draft overflow text while keeping the latest draft line visible");
  const auto scrolled_draft = ava::tui::render_composer(
      ava::tui::ComposerSnapshot{.mode = "build",
                                 .provider = "openai",
                                 .model = "gpt-5.5",
                                 .session_id = "session_test",
                                 .input = "one\ntwo\nthree\nfour\nfive\nsix\nseven\neight\nnine",
                                 .status = "ready",
                                 .transcript = {},
                                 .width = 70,
                                 .height = 12,
                                 .input_cursor = std::string::npos,
                                 .sidebar = std::nullopt,
                                 .draft_scroll_offset = 2});
  expect(std::ranges::any_of(
             scrolled_draft,
             [](const std::string& line) { return strip_sgr(line).find("▎  ❯ one") != std::string::npos; }) &&
             std::ranges::none_of(
                 scrolled_draft,
                 [](const std::string& line) { return strip_sgr(line).find("▎    nine") != std::string::npos; }) &&
             std::ranges::none_of(
                 scrolled_draft,
                 [](const std::string& line) { return strip_sgr(line).find("draft +") != std::string::npos; }),
         "tui composer draft scroll offset shows older draft lines without footer overflow text");

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
  expect(std::ranges::any_of(scrolled,
                             [](const std::string& line) { return line.find("item 19") != std::string::npos; }) &&
             std::ranges::none_of(
                 scrolled, [](const std::string& line) { return line.find("lines hidden") != std::string::npos; }) &&
             std::ranges::none_of(scrolled,
                                  [](const std::string& line) { return line.find("item 0") != std::string::npos; }),
         "tui transcript viewport keeps newest lines without hidden-line banners");

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
  expect(std::ranges::none_of(scrolled_up,
                              [](const std::string& line) { return line.find("lines hidden") != std::string::npos; }) &&
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
  const auto wrapped_latest = ava::tui::render_composer(ava::tui::ComposerSnapshot{
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
      .transcript_scroll_offset = 0,
      .width = 60,
      .height = 8});
  expect(
      std::ranges::none_of(
          wrapped_transcript,
          [](const std::string& line) { return strip_sgr(line).find("lines hidden") != std::string::npos; }) &&
          wrapped_transcript != wrapped_latest,
      "tui transcript viewport wraps long transcript text before applying scroll offset without hidden-line banners");

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
  expect(mixed_visible.find("lines hidden") == std::string::npos && mixed_visible.find("[+]") != std::string::npos &&
             mixed_visible.find("2 matches") != std::string::npos && mixed_visible.find("AVA") != std::string::npos &&
             mixed_visible.find("│ done") != std::string::npos && mixed_visible.find("old 0") == std::string::npos,
         "tui transcript viewport scrolls mixed text and tool-card lines together without hidden-line banners");

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

  const auto sidebar_frame = ava::tui::render_composer(ava::tui::ComposerSnapshot{
      .mode = "build",
      .provider = "openai",
      .model = "gpt-5.5",
      .session_id = "session_test",
      .input = "",
      .status = "ready",
      .transcript = {ava::tui::TranscriptItem{.label = "you", .text = "hello"}},
      .width = 128,
      .height = 16,
      .sidebar = ava::tui::SidebarSnapshot{
          .activity = {ava::tui::SidebarActivityItem{.id = "call_1",
                                                     .label = "bash",
                                                     .detail = "running tests",
                                                     .status = ava::tui::ToolTimelineStatus::Running}},
          .modified_files = {ava::tui::SidebarModifiedFile{
              .path = "src/ava/tui/runtime.cpp", .added = 12, .removed = 3}},
          .session_id = "session_test\x1b[31m",
          .mode = "build\x1b[31m",
          .provider = "openai\x1b[31m",
          .model = "gpt-5.5\x1b[31m",
          .workspace = "/workspace/project\x1b[31m",
          .git_branch = "develop\x1b[31m",
          .version = "0.32"}});
  expect(std::ranges::any_of(sidebar_frame,
                             [](const std::string& line) {
                               const auto visible = strip_sgr(line);
                               return visible.find("Activity") != std::string::npos &&
                                      visible.find("Modified Files") == std::string::npos;
                             }) &&
             std::ranges::any_of(sidebar_frame,
                                 [](const std::string& line) {
                                   const auto visible = strip_sgr(line);
                                   return visible.find("bash") != std::string::npos &&
                                          visible.find("running tests") != std::string::npos;
                                 }) &&
             std::ranges::any_of(sidebar_frame,
                                 [](const std::string& line) {
                                   const auto visible = strip_sgr(line);
                                   return visible.find("src/ava/tui/runtime.cpp") != std::string::npos &&
                                          visible.find("+12") != std::string::npos &&
                                          visible.find("-3") != std::string::npos;
                                 }) &&
             std::ranges::any_of(sidebar_frame,
                                 [](const std::string& line) {
                                   const auto visible = strip_sgr(line);
                                   return visible.find("branch develop") != std::string::npos ||
                                          visible.find("AVA 0.32") != std::string::npos;
                                 }) &&
             std::ranges::none_of(sidebar_frame,
                                  [](const std::string& line) { return line.find("\x1b[31m") != std::string::npos; }) &&
             std::ranges::all_of(sidebar_frame, [](const std::string& line) { return visible_columns(line) <= 128; }),
         "tui renders an OpenCode-style sidebar with activity, modified files, session metadata, and version");
  expect(std::ranges::any_of(sidebar_frame,
                             [](const std::string& line) {
                               const auto visible = strip_sgr(line);
                               const auto activity = visible.find("Activity");
                               const auto separator = visible.find("│");
                               return activity != std::string::npos && separator != std::string::npos &&
                                      separator < activity && activity >= 90;
                             }),
         "tui pads blank main rows so sidebar content stays in the right column");

  const auto narrow_no_sidebar = ava::tui::render_composer(ava::tui::ComposerSnapshot{
      .mode = "build",
      .provider = "openai",
      .model = "gpt-5.5",
      .session_id = "session_test",
      .input = "",
      .status = "ready",
      .transcript = {},
      .width = 90,
      .height = 10,
      .sidebar =
          ava::tui::SidebarSnapshot{.activity = {ava::tui::SidebarActivityItem{.id = "a", .label = "sidebar-only"}}}});
  expect(std::ranges::none_of(
             narrow_no_sidebar,
             [](const std::string& line) { return strip_sgr(line).find("sidebar-only") != std::string::npos; }),
         "tui hides the sidebar on narrow terminals");

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

void test_tui_event_state_reduces_runtime_events() {
  ava::tui::TuiEventState state;

  ava::app::RuntimeEvent user;
  user.type = ava::app::RuntimeEventType::UserMessage;
  user.text = "hello";
  ava::tui::apply_runtime_event(state, user);
  expect(state.run_status == ava::tui::TuiEventRunStatus::Running && state.transcript.size() == 1 &&
             state.transcript[0].label == "you" && state.transcript[0].text == "hello",
         "tui event state records user messages as completed transcript items");

  ava::app::RuntimeEvent delta;
  delta.type = ava::app::RuntimeEventType::MessageUpdate;
  delta.text = "hel";
  ava::tui::apply_runtime_event(state, delta);
  delta.text = "lo";
  ava::tui::apply_runtime_event(state, delta);
  auto streaming_snapshot = ava::tui::event_state_transcript_snapshot(state);
  expect(state.pending_assistant_text == "hello" && streaming_snapshot.size() == 2 &&
             streaming_snapshot[1].label == "ava" && streaming_snapshot[1].text == "hello",
         "tui event state exposes pending assistant deltas in snapshots");

  ava::app::RuntimeEvent end;
  end.type = ava::app::RuntimeEventType::MessageEnd;
  ava::tui::apply_runtime_event(state, end);
  expect(state.run_status == ava::tui::TuiEventRunStatus::Completed && state.pending_assistant_text.empty() &&
             state.transcript.size() == 2 && state.transcript[1].label == "ava" && state.transcript[1].text == "hello",
         "tui event state commits assistant deltas on message end");

  ava::app::RuntimeEvent assistant_final;
  assistant_final.type = ava::app::RuntimeEventType::AssistantMessage;
  assistant_final.text = "hello";
  ava::tui::apply_runtime_event(state, assistant_final);
  expect(state.transcript.size() == 2 && state.transcript[1].text == "hello",
         "tui event state avoids duplicating matching streamed assistant final events");

  assistant_final.text = "hello\n";
  ava::tui::apply_runtime_event(state, assistant_final);
  expect(state.transcript.size() == 2 && state.transcript[1].text == "hello",
         "tui event state treats trailing whitespace-only final changes as duplicate streamed assistant events");

  ava::tui::TuiEventState reused_state;
  ava::tui::apply_runtime_event(reused_state, user);
  delta.text = "streamed";
  ava::tui::apply_runtime_event(reused_state, delta);
  ava::tui::apply_runtime_event(reused_state, end);
  ava::app::RuntimeEvent next_user;
  next_user.type = ava::app::RuntimeEventType::UserMessage;
  next_user.text = "next";
  ava::tui::apply_runtime_event(reused_state, next_user);
  assistant_final.text = "fresh final";
  ava::tui::apply_runtime_event(reused_state, assistant_final);
  expect(reused_state.transcript.size() == 4 && reused_state.transcript[1].text == "streamed" &&
             reused_state.transcript.back().text == "fresh final",
         "tui event state clears streaming index before a reused-state next turn");

  ava::tui::TuiEventState final_state;
  assistant_final.text = "direct final";
  ava::tui::apply_runtime_event(final_state, assistant_final);
  expect(final_state.run_status == ava::tui::TuiEventRunStatus::Completed && final_state.transcript.size() == 1 &&
             final_state.transcript[0].label == "ava" && final_state.transcript[0].text == "direct final",
         "tui event state records assistant final events without streaming deltas");

  ava::tui::TuiEventState provider_state;
  ava::app::RuntimeEvent provider_start;
  provider_start.type = ava::app::RuntimeEventType::ProviderEvent;
  provider_start.status = "tool_call_start";
  provider_start.call_id = "provider_call_1";
  provider_start.tool_name = "read_file";
  provider_start.text = R"({"path": "README.md"})";
  ava::tui::apply_runtime_event(provider_state, provider_start);
  auto provider_snapshot = ava::tui::event_state_transcript_snapshot(provider_state);
  const auto provider_activity_id = provider_state.activity.empty() ? std::string{} : provider_state.activity[0].id;
  expect(provider_state.activity.size() == 1 && !provider_activity_id.empty() &&
             provider_state.activity[0].label == "read_file" &&
             provider_state.activity[0].detail == "provider is preparing tool call" &&
             provider_state.activity[0].status == ava::tui::ToolTimelineStatus::Running &&
             provider_state.transcript.empty() && provider_snapshot.empty(),
         "tui event state shows provider tool-call starts as sidebar-only activity");

  ava::app::RuntimeEvent provider_delta = provider_start;
  provider_delta.status = "tool_call_delta";
  provider_delta.tool_name.clear();
  provider_delta.text = R"({"path": "README.md", "partial": true})";
  ava::tui::apply_runtime_event(provider_state, provider_delta);
  provider_snapshot = ava::tui::event_state_transcript_snapshot(provider_state);
  expect(provider_state.activity.size() == 1 && provider_state.activity[0].id == provider_activity_id &&
             provider_state.activity[0].label == "read_file" &&
             provider_state.activity[0].detail == "streaming tool arguments" &&
             provider_state.activity[0].status == ava::tui::ToolTimelineStatus::Running &&
             provider_state.transcript.empty() && provider_snapshot.empty(),
         "tui event state keeps provider tool-call deltas in the sidebar and preserves labels by call id");

  ava::app::RuntimeEvent provider_end = provider_delta;
  provider_end.status = "tool_call_end";
  provider_end.text = R"({"path": "README.md", "complete": true})";
  ava::tui::apply_runtime_event(provider_state, provider_end);
  provider_snapshot = ava::tui::event_state_transcript_snapshot(provider_state);
  expect(provider_state.activity.size() == 1 && provider_state.activity[0].id == provider_activity_id &&
             provider_state.activity[0].label == "read_file" &&
             provider_state.activity[0].detail == "tool call ready" &&
             provider_state.activity[0].status == ava::tui::ToolTimelineStatus::Success &&
             provider_state.transcript.empty() && provider_state.pending_tools.empty() && provider_snapshot.empty(),
         "tui event state completes provider tool-call sidebar activity without adding transcript items");

  ava::tui::TuiEventState provider_without_id_state;
  ava::app::RuntimeEvent provider_without_id;
  provider_without_id.type = ava::app::RuntimeEventType::ProviderEvent;
  provider_without_id.status = "tool_call_start";
  provider_without_id.tool_name = "grep";
  ava::tui::apply_runtime_event(provider_without_id_state, provider_without_id);
  const auto provider_without_id_activity_id =
      provider_without_id_state.activity.empty() ? std::string{} : provider_without_id_state.activity[0].id;
  provider_without_id.status = "tool_call_delta";
  ava::tui::apply_runtime_event(provider_without_id_state, provider_without_id);
  provider_without_id.status = "tool_call_end";
  ava::tui::apply_runtime_event(provider_without_id_state, provider_without_id);
  expect(provider_without_id_state.activity.size() == 1 && !provider_without_id_activity_id.empty() &&
             provider_without_id_state.activity[0].id == provider_without_id_activity_id &&
             provider_without_id_state.activity[0].label == "grep" &&
             provider_without_id_state.activity[0].detail == "tool call ready" &&
             provider_without_id_state.activity[0].status == ava::tui::ToolTimelineStatus::Success,
         "tui event state coalesces provider tool-call activity when provider events omit call ids");

  ava::app::RuntimeEvent tool_start;
  tool_start.type = ava::app::RuntimeEventType::ToolStart;
  tool_start.call_id = "call_1";
  tool_start.tool_name = "bash";
  tool_start.text = "pwd";
  ava::tui::apply_runtime_event(state, tool_start);
  expect(state.pending_tools.size() == 1 && state.pending_tools[0].call_id == "call_1" &&
             state.pending_tools[0].item.status == ava::tui::ToolTimelineStatus::Running &&
             state.pending_tools[0].item.name == "bash" && state.pending_tools[0].item.argument_summary == "pwd",
         "tui event state tracks started tools by call id");

  ava::app::RuntimeEvent tool_progress;
  tool_progress.type = ava::app::RuntimeEventType::ToolProgress;
  tool_progress.call_id = "call_1";
  tool_progress.tool_name = "bash";
  tool_progress.text = "running pwd";
  ava::tui::apply_runtime_event(state, tool_progress);
  auto tool_snapshot = ava::tui::event_state_transcript_snapshot(state);
  expect(state.pending_tools.size() == 1 && state.pending_tools[0].item.result_summary == "running pwd" &&
             !tool_snapshot.empty() && tool_snapshot.back().tool &&
             tool_snapshot.back().tool->status == ava::tui::ToolTimelineStatus::Running,
         "tui event state updates pending tool progress and includes it in snapshots");

  ava::app::RuntimeEvent tool_result;
  tool_result.type = ava::app::RuntimeEventType::ToolResult;
  tool_result.call_id = "call_1";
  tool_result.tool_name = "bash";
  tool_result.status = "success";
  tool_result.text = "ok";
  ava::tui::apply_runtime_event(state, tool_result);
  expect(state.pending_tools.empty() && !state.transcript.empty() && state.transcript.back().tool &&
             state.transcript.back().tool->status == ava::tui::ToolTimelineStatus::Success &&
             state.transcript.back().tool->argument_summary == "pwd" &&
             state.transcript.back().tool->result_summary == "ok",
         "tui event state moves successful tool results into completed transcript items");

  ava::app::RuntimeEvent write_start;
  write_start.type = ava::app::RuntimeEventType::ToolStart;
  write_start.call_id = "call_write";
  write_start.tool_name = "write_file";
  write_start.text = "path=src/main.cpp, content=12 bytes";
  ava::tui::apply_runtime_event(state, write_start);
  ava::app::RuntimeEvent write_result;
  write_result.type = ava::app::RuntimeEventType::ToolResult;
  write_result.call_id = "call_write";
  write_result.tool_name = "write_file";
  write_result.status = "success";
  write_result.text = "wrote 12 bytes";
  ava::tui::apply_runtime_event(state, write_result);
  expect(!state.activity.empty() && state.activity.back().label == "write_file" &&
             state.activity.back().status == ava::tui::ToolTimelineStatus::Success &&
             state.modified_files.size() == 1 && state.modified_files[0].path == "src/main.cpp",
         "tui event state feeds sidebar activity and modified-file summaries from successful mutating tools");

  ava::app::RuntimeEvent tool_error;
  tool_error.type = ava::app::RuntimeEventType::ToolResult;
  tool_error.call_id = "call_2";
  tool_error.tool_name = "read";
  tool_error.status = "error";
  tool_error.text = "denied";
  ava::tui::apply_runtime_event(state, tool_error);
  expect(state.transcript.back().tool && state.transcript.back().tool->status == ava::tui::ToolTimelineStatus::Error &&
             state.transcript.back().tool->result_summary == "denied",
         "tui event state records errored tool results as error tool cards");

  ava::app::RuntimeEvent error;
  error.type = ava::app::RuntimeEventType::Error;
  error.error_message = "provider failed";
  error.error_details = "Provider: provider failed";
  ava::tui::apply_runtime_event(state, error);
  expect(state.run_status == ava::tui::TuiEventRunStatus::Error && state.error_text == "provider failed" &&
             state.error_details == "Provider: provider failed" && state.transcript.back().label == "error" &&
             state.transcript.back().text == "Provider: provider failed",
         "tui event state records runtime errors and exposes error transcript text");

  ava::tui::TuiEventState canceled_state;
  ava::app::RuntimeEvent canceled;
  canceled.type = ava::app::RuntimeEventType::Error;
  canceled.error_message = "agent loop canceled";
  canceled.error_details = "Unknown: agent loop canceled";
  ava::tui::apply_runtime_event(canceled_state, canceled);
  expect(canceled_state.run_status == ava::tui::TuiEventRunStatus::Canceled &&
             canceled_state.error_text == "stopped by user" && canceled_state.transcript.size() == 1 &&
             canceled_state.transcript[0].label == "ava" && canceled_state.transcript[0].text == "stopped by user" &&
             !canceled_state.activity.empty() && canceled_state.activity.back().label == "stopped",
         "tui event state presents cooperative cancellation as a friendly stopped state");

  ava::tui::TuiEventState done_state;
  delta.text = "done text";
  ava::tui::apply_runtime_event(done_state, delta);
  ava::app::RuntimeEvent done;
  done.type = ava::app::RuntimeEventType::Done;
  done.stop_reason = "stop";
  done.provider_iterations = 2;
  done.tool_calls = 1;
  ava::tui::apply_runtime_event(done_state, done);
  expect(done_state.run_status == ava::tui::TuiEventRunStatus::Done && done_state.stop_reason == "stop" &&
             done_state.provider_iterations == 2 && done_state.tool_calls == 1 &&
             done_state.pending_assistant_text.empty() && done_state.transcript.size() == 1 &&
             done_state.transcript[0].text == "done text",
         "tui event state records done metadata and commits pending assistant text");
}

}  // namespace

void run_tui_composer_tests() {
  test_tui_composer_rendering_and_input();
  test_tui_event_state_reduces_runtime_events();
}
