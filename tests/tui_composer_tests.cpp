#include "ava/app/commands.h"
#include "ava/app/events.h"
#include "ava/app/headless_policy.h"
#include "ava/app/print_mode.h"
#include "ava/app/rpc_mode.h"
#include "ava/app/runtime.h"

#include "ava/agent/agent_loop.h"
#include "ava/agent/mode.h"
#include "ava/agent/tool_dispatcher.h"

#include "ava/tools/bash_tool.h"
#include "ava/tools/file_tools.h"
#include "ava/tools/search_tools.h"

#include "ava/tui/composer.h"
#include "ava/tui/composer_editor.h"
#include "ava/tui/composer_internal.h"
#include "ava/tui/event_state.h"
#include "ava/tui/keybindings.h"
#include "ava/tui/runtime.h"
#include "ava/tui/terminal.h"

#include "ava/config/auth.h"
#include "ava/config/model_config.h"
#include "ava/config/openai_oauth.h"
#include "ava/config/prompt_config.h"
#include "ava/config/xdg_paths.h"

#include "ava/session/compaction.h"
#include "ava/session/export.h"
#include "ava/session/session_store.h"

#include "ava/permissions/permission.h"

#include "ava/provider/openai_provider.h"

#include "ava/context/context_loader.h"

#include "ava/core/ids.h"
#include "ava/core/json.h"

#include "tests/support/fake_transport.h"
#include "tests/support/test_harness.h"

#include <algorithm>
#include <chrono>
#include <climits>
#include <clocale>
#include <cstdio>
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

#include <curses.h>
#include <sys/stat.h>
#include <unistd.h>

namespace {

void test_tui_composer_rendering_and_input()
{
  expect(ava::tui::terminal_escape_sequence_key("[27;2;13~") == ava::tui::Key::ShiftEnter &&
             ava::tui::terminal_escape_sequence_key("[13;2u") == ava::tui::Key::ShiftEnter &&
             ava::tui::terminal_escape_sequence_key("[13;2~") == ava::tui::Key::ShiftEnter &&
             ava::tui::terminal_escape_sequence_key("[13;2") == ava::tui::Key::Unknown &&
             ava::tui::terminal_escape_sequence_key("[13;5u") == ava::tui::Key::Unknown &&
             ava::tui::terminal_escape_sequence_key("[27;2;13") == ava::tui::Key::Unknown &&
             ava::tui::terminal_escape_sequence_key("[999999999999999999999;2u") == ava::tui::Key::Unknown &&
             ava::tui::terminal_escape_sequence_key("[200~") == ava::tui::Key::Unknown,
         "terminal escape parser maps complete shift-enter CSI forms without treating partial keys or paste markers as "
         "text");
  expect(ava::tui::terminal_escape_sequence_complete("[13;2u") &&
             ava::tui::terminal_escape_sequence_complete("[?25l") &&
             ava::tui::terminal_escape_sequence_complete(std::string("]0;AVA") + "\a") &&
             ava::tui::terminal_escape_sequence_complete(std::string("]52;c;AAAA") + "\x1b\\") &&
             ava::tui::terminal_escape_sequence_complete(std::string("P1;2|payload") + "\x1b\\") &&
             !ava::tui::terminal_escape_sequence_complete("[13;2") &&
             !ava::tui::terminal_escape_sequence_complete("]0;AVA") &&
             !ava::tui::terminal_escape_sequence_complete(std::string("P1;2|payload")),
         "terminal escape parser buffers CSI, OSC, and DCS sequences until a real terminator is present");
  expect(
      ava::tui::terminal_escape_sequence_should_discard("[?25l") &&
          ava::tui::terminal_escape_sequence_should_discard(std::string("]0;AVA") + "\a") &&
          ava::tui::terminal_escape_sequence_should_discard(std::string("]52;c;AAAA") + "\x1b\\") &&
          ava::tui::terminal_escape_sequence_should_discard(std::string("P1;2|payload") + "\x1b\\") &&
          !ava::tui::terminal_escape_sequence_should_discard("[13;2u") &&
          !ava::tui::terminal_escape_sequence_should_discard("[200~") &&
          !ava::tui::terminal_escape_sequence_should_discard("[13;2"),
      "terminal escape parser discards completed terminal controls while preserving AVA-owned key and paste markers");

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
                                  [](ava::tui::QuestionPromptOptionView const& option) { return option.selected; }),
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
  auto secret_question = ava::tui::QuestionPromptView{.header = "Connect",
                                                      .question = "Paste API key",
                                                      .options = {},
                                                      .multiple = false,
                                                      .allow_custom = true,
                                                      .secret = true,
                                                      .selected_option_index = 0,
                                                      .custom_text = ""};
  question_input =
      ava::tui::handle_question_prompt_input(secret_question, ava::tui::InputEvent{.key = ava::tui::Key::Enter});
  expect(question_input.action == ava::tui::QuestionPromptInputAction::Redraw && question_input.custom_text.empty(),
         "question prompt enter keeps an empty single custom answer open");

  auto copy_question = ava::tui::QuestionPromptView{
      .header = "Connect",
      .question = "Open URL",
      .options = {ava::tui::QuestionPromptOptionView{.value = "done", .label = "Done"},
                  ava::tui::QuestionPromptOptionView{.value = "copy:https://auth.openai.com", .label = "C Copy"}},
      .multiple = false,
      .allow_custom = false,
      .selected_option_index = 0,
      .custom_text = ""};
  question_input = ava::tui::handle_question_prompt_input(
      copy_question, ava::tui::InputEvent{.key = ava::tui::Key::Character, .character = 'c'});
  expect(question_input.action == ava::tui::QuestionPromptInputAction::Copy &&
             question_input.copy_text == "https://auth.openai.com" &&
             std::ranges::none_of(question_input.options,
                                  [](ava::tui::QuestionPromptOptionView const& option) { return option.selected; }),
         "question prompt copy shortcut copies without resolving a single-select option");
  question_input =
      ava::tui::handle_question_prompt_input(copy_question, ava::tui::InputEvent{.key = ava::tui::Key::Enter});
  expect(question_input.action == ava::tui::QuestionPromptInputAction::Resolve && question_input.options[0].selected,
         "question prompt enter confirms the selected non-copy option");

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

  auto const split_empty = ava::tui::split_lines("");
  expect(split_empty.size() == 1 && split_empty.front().empty(), "tui split keeps empty input as one line");
  auto const split_trailing = ava::tui::split_lines("a\n");
  expect(split_trailing.size() == 2 && split_trailing[0] == "a" && split_trailing[1].empty(),
         "tui split preserves trailing empty line");
  auto const split_crlf = ava::tui::split_lines("a\r\nb\rc");
  expect(split_crlf.size() == 3 && split_crlf[0] == "a" && split_crlf[1] == "b" && split_crlf[2] == "c",
         "tui split treats crlf and carriage-return output as line breaks");

  auto const lines = ava::tui::render_composer(
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
  expect(!lines.empty() && strip_sgr(lines.front()).find("hello") != std::string::npos,
         "tui starts short chats at the top of the transcript area");
  expect(!lines.empty() && lines.back().find("\x1b[48;2;26;31;46m") != std::string::npos &&
             std::ranges::none_of(lines,
                                  [](std::string const& line) { return line.find("/ commands") != std::string::npos; }),
         "tui keeps only the composer block at the bottom");
  expect(std::ranges::any_of(
             lines, [](std::string const& line) { return strip_sgr(line).find("▎  ❯ /help") != std::string::npos; }),
         "tui renders old AVA-style composer input");
  expect(std::ranges::none_of(lines,
                              [](std::string const& line) {
                                return strip_sgr(line).find("slash palette dismissed") != std::string::npos;
                              }),
         "tui keeps transient composer status text out of the footer");
  expect(std::ranges::any_of(lines,
                             [](std::string const& line) {
                               return line.find("\x1b[48;2;26;31;46m") != std::string::npos &&
                                      line.find("\x1b[38;2;77;158;246m▎") != std::string::npos &&
                                      line.find("\x1b[1m\x1b[38;2;77;158;246m❯") != std::string::npos;
                             }),
         "tui uses old AVA elevated composer surface, primary rail, and prompt color");
  expect(std::ranges::none_of(
             lines, [](std::string const& line) { return strip_sgr(line).find("╭─ You") != std::string::npos; }) &&
             std::ranges::none_of(
                 lines, [](std::string const& line) { return strip_sgr(line).find("╭─ AVA") != std::string::npos; }) &&
             std::ranges::any_of(lines,
                                 [](std::string const& line) {
                                   return line.find("\x1b[48;2;26;31;46m") != std::string::npos &&
                                          strip_sgr(line).find("hello") != std::string::npos;
                                 }) &&
             std::ranges::any_of(
                 lines, [](std::string const& line) { return strip_sgr(line).find("world") != std::string::npos; }),
         "tui renders user messages as highlighted input blocks and assistant messages without role headers");

  auto const processing_lines = ava::tui::render_composer(ava::tui::ComposerSnapshot{.mode = "build",
                                                                                     .provider = "openai",
                                                                                     .model = "gpt-5.5",
                                                                                     .session_id = "session_test",
                                                                                     .input = "",
                                                                                     .status = "thinking...",
                                                                                     .processing = true,
                                                                                     .spinner_frame = 1,
                                                                                     .token_status = "1.3k (0.7%)",
                                                                                     .transcript = {},
                                                                                     .width = 80,
                                                                                     .height = 10});
  expect(std::ranges::any_of(processing_lines,
                             [](std::string const& line) {
                               auto const visible = strip_sgr(line);
                               return visible.find("thinking...") == std::string::npos &&
                                      visible.find("working") == std::string::npos &&
                                      visible.find("⠙") != std::string::npos &&
                                      visible.find("1.3k (0.7%)") != std::string::npos;
                             }),
         "tui renders a spinner-only processing indicator and token-status slot");

  auto const queued_lines = ava::tui::render_composer(ava::tui::ComposerSnapshot{
      .mode = "build",
      .provider = "openai",
      .model = "gpt-5.5",
      .session_id = "session_test",
      .input = "",
      .status = "thinking...",
      .processing = true,
      .transcript = {ava::tui::TranscriptItem{.label = "you", .text = "work on queue UI"}},
      .width = 80,
      .height = 12,
      .queued_messages = {ava::tui::QueuedMessageItem{.id = "q1", .kind = "follow-up", .text = "run tests next"},
                          ava::tui::QueuedMessageItem{.id = "q2", .kind = "steer", .text = "keep patch small"}}});
  expect(std::ranges::any_of(queued_lines,
                             [](std::string const& line) {
                               auto const visible = strip_sgr(line);
                               return visible.find("queued follow-up run tests next") != std::string::npos;
                             }) &&
             std::ranges::any_of(queued_lines,
                                 [](std::string const& line) {
                                   auto const visible = strip_sgr(line);
                                   return visible.find("queued steer keep patch small") != std::string::npos &&
                                          visible.find("/restore latest") != std::string::npos;
                                 }),
         "tui renders active-run queued steering/follow-up messages in a compact pending region");

  auto const reasoning_lines = ava::tui::render_composer(ava::tui::ComposerSnapshot{.mode = "build",
                                                                                    .provider = "openai",
                                                                                    .model = "gpt-5.5",
                                                                                    .session_id = "session_test",
                                                                                    .input = "explain this",
                                                                                    .status = "ready",
                                                                                    .reasoning_status = "low",
                                                                                    .transcript = {},
                                                                                    .width = 80,
                                                                                    .height = 10});
  expect(std::ranges::any_of(reasoning_lines,
                             [](std::string const& line) {
                               auto const visible = strip_sgr(line);
                               return visible.find("Build · GPT-5.5 OpenAI · low") != std::string::npos &&
                                      visible.find("reasoning") == std::string::npos;
                             }),
         "tui shows selected reasoning level in the composer metadata");

  auto const default_reasoning_lines =
      ava::tui::render_composer(ava::tui::ComposerSnapshot{.mode = "build",
                                                           .provider = "openai",
                                                           .model = "gpt-5.5",
                                                           .session_id = "session_test",
                                                           .input = "explain this",
                                                           .status = "ready",
                                                           .transcript = {},
                                                           .width = 80,
                                                           .height = 10});
  expect(std::ranges::any_of(default_reasoning_lines,
                             [](std::string const& line) {
                               auto const visible = strip_sgr(line);
                               return visible.find("Build · GPT-5.5 OpenAI") != std::string::npos &&
                                      visible.find("default") == std::string::npos;
                             }),
         "tui leaves reasoning metadata blank when the model uses default reasoning");

  auto const plan_mode_lines = ava::tui::render_composer(ava::tui::ComposerSnapshot{.mode = "plan",
                                                                                    .provider = "openai",
                                                                                    .model = "gpt-5.5",
                                                                                    .session_id = "session_test",
                                                                                    .input = "",
                                                                                    .status = "ready",
                                                                                    .transcript = {},
                                                                                    .width = 80,
                                                                                    .height = 10});
  expect(std::ranges::any_of(default_reasoning_lines,
                             [](std::string const& line) {
                               return line.find(std::string("\x1b[38;2;251;191;36m") + "Build") != std::string::npos;
                             }) &&
             std::ranges::any_of(plan_mode_lines,
                                 [](std::string const& line) {
                                   return line.find(std::string("\x1b[38;2;77;158;246m") + "Plan") != std::string::npos;
                                 }),
         "tui colors build and plan composer modes differently");

  auto const token_margin_lines = ava::tui::render_composer(ava::tui::ComposerSnapshot{.mode = "build",
                                                                                       .provider = "openai",
                                                                                       .model = "gpt-5.5",
                                                                                       .session_id = "session_test",
                                                                                       .input = "",
                                                                                       .status = "ready",
                                                                                       .token_status = "1.3k (0.7%)",
                                                                                       .transcript = {},
                                                                                       .width = 80,
                                                                                       .height = 10});
  expect(std::ranges::any_of(token_margin_lines,
                             [](std::string const& line) {
                               auto const visible = strip_sgr(line);
                               auto const token_text = std::string_view("1.3k (0.7%)");
                               auto const token_pos = visible.find(token_text);
                               return token_pos != std::string::npos &&
                                      visible.substr(token_pos + token_text.size(), 2) == "  ";
                             }),
         "tui leaves right margin after token-status text");

  auto const markdown_transcript = ava::tui::render_composer(ava::tui::ComposerSnapshot{
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
  expect(std::ranges::any_of(markdown_transcript,
                             [](std::string const& line) {
                               return strip_sgr(line).find("First paragraph wraps cleanly") != std::string::npos;
                             }) &&
             std::ranges::any_of(markdown_transcript,
                                 [](std::string const& line) {
                                   return strip_sgr(line).find("Second paragraph stays separate") != std::string::npos;
                                 }) &&
             std::ranges::any_of(
                 markdown_transcript,
                 [](std::string const& line) { return strip_sgr(line).find("- bullet item") != std::string::npos; }) &&
             std::ranges::any_of(
                 markdown_transcript,
                 [](std::string const& line) { return strip_sgr(line).find("* star item") != std::string::npos; }) &&
             std::ranges::any_of(markdown_transcript,
                                 [](std::string const& line) {
                                   return strip_sgr(line).find("1. numbered item") != std::string::npos;
                                 }) &&
             std::ranges::any_of(
                 markdown_transcript,
                 [](std::string const& line) { return strip_sgr(line).find("> quoted text") != std::string::npos; }) &&
             std::ranges::any_of(
                 markdown_transcript,
                 [](std::string const& line) { return strip_sgr(line).find("``` cpp") != std::string::npos; }) &&
             std::ranges::any_of(markdown_transcript,
                                 [](std::string const& line) {
                                   return strip_sgr(line).find("  int main() {}") != std::string::npos;
                                 }) &&
             std::ranges::any_of(markdown_transcript,
                                 [](std::string const& line) {
                                   return strip_sgr(line).find("Use ava build and bold text") != std::string::npos;
                                 }) &&
             std::ranges::none_of(markdown_transcript,
                                  [](std::string const& line) {
                                    auto const visible = strip_sgr(line);
                                    return visible.find("`ava build`") != std::string::npos ||
                                           visible.find("**bold text**") != std::string::npos;
                                  }),
         "tui assistant renderer handles paragraphs, lists, quotes, fenced code, inline code, and bold");

  constexpr auto kBoldSgr = std::string_view{"\x1b[1m"};
  constexpr auto kMutedSgr = std::string_view{"\x1b[38;2;139;149;165m"};
  constexpr auto kWarningSgr = std::string_view{"\x1b[38;2;251;191;36m"};

  auto const role_markup_transcript = ava::tui::render_composer(
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
  auto const user_markup_line = std::ranges::find_if(role_markup_transcript, [](std::string const& line) {
    return strip_sgr(line).find("Use `x` and **y**.") != std::string::npos;
  });
  auto const assistant_markup_line = std::ranges::find_if(role_markup_transcript, [](std::string const& line) {
    return strip_sgr(line).find("Use x and y.") != std::string::npos;
  });
  expect(user_markup_line != role_markup_transcript.end() && assistant_markup_line != role_markup_transcript.end() &&
             !has_active_sgr_at_text(*user_markup_line, "x", kWarningSgr) &&
             !has_active_sgr_at_text(*user_markup_line, "y", kBoldSgr) &&
             has_active_sgr_at_text(*assistant_markup_line, "x", kWarningSgr) &&
             has_active_sgr_at_text(*assistant_markup_line, "y", kBoldSgr),
         "tui keeps user inline markdown literal while formatting assistant inline markdown");

  auto const wrapped_markdown_transcript = ava::tui::render_composer(ava::tui::ComposerSnapshot{
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
  auto const bullet_continuation = std::ranges::find_if(wrapped_markdown_transcript, [](std::string const& line) {
    return strip_sgr(line).find("  theta iota") != std::string::npos;
  });
  auto const quote_continuation = std::ranges::find_if(wrapped_markdown_transcript, [](std::string const& line) {
    return strip_sgr(line).find("  eta theta") != std::string::npos;
  });
  auto const bullet_continuation_is_plain = bullet_continuation != wrapped_markdown_transcript.end() &&
                                            !has_active_sgr_at_text(*bullet_continuation, "theta iota", kMutedSgr);
  auto const quote_continuation_is_plain = quote_continuation != wrapped_markdown_transcript.end() &&
                                           !has_active_sgr_at_text(*quote_continuation, "eta theta", kMutedSgr);
  expect(bullet_continuation_is_plain && quote_continuation_is_plain,
         "tui assistant renderer keeps wrapped list and quote continuations out of code styling");

  auto const wrapped_code_fence_transcript = ava::tui::render_composer(ava::tui::ComposerSnapshot{
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
  auto const code_after_wrapped_ticks = std::ranges::find_if(
      wrapped_code_fence_transcript,
      [](std::string const& line) { return strip_sgr(line).find("  omega") != std::string::npos; });
  auto const text_after_code = std::ranges::find_if(wrapped_code_fence_transcript, [](std::string const& line) {
    return strip_sgr(line).find("After bold") != std::string::npos;
  });
  expect(code_after_wrapped_ticks != wrapped_code_fence_transcript.end() &&
             has_active_sgr_at_text(*code_after_wrapped_ticks, "omega", kMutedSgr) &&
             text_after_code != wrapped_code_fence_transcript.end() &&
             !has_active_sgr_at_text(*text_after_code, "After", kMutedSgr) &&
             has_active_sgr_at_text(*text_after_code, "bold", kBoldSgr),
         "tui assistant renderer keeps wrapped code content beginning with backticks inside the code block");

  auto const indented_fence_content_transcript = ava::tui::render_composer(
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
  auto const indented_ticks = std::ranges::find_if(indented_fence_content_transcript, [](std::string const& line) {
    return strip_sgr(line).find("```literal") != std::string::npos;
  });
  auto const code_after_indented_ticks = std::ranges::find_if(
      indented_fence_content_transcript,
      [](std::string const& line) { return strip_sgr(line).find("  omega") != std::string::npos; });
  auto const text_after_indented_code = std::ranges::find_if(
      indented_fence_content_transcript,
      [](std::string const& line) { return strip_sgr(line).find("After bold") != std::string::npos; });
  expect(indented_ticks != indented_fence_content_transcript.end() &&
             strip_sgr(*indented_ticks).find("``` literal") == std::string::npos &&
             code_after_indented_ticks != indented_fence_content_transcript.end() &&
             has_active_sgr_at_text(*code_after_indented_ticks, "omega", kMutedSgr) &&
             text_after_indented_code != indented_fence_content_transcript.end() &&
             !has_active_sgr_at_text(*text_after_indented_code, "After", kMutedSgr) &&
             has_active_sgr_at_text(*text_after_indented_code, "bold", kBoldSgr),
         "tui assistant renderer keeps indented backtick content inside fenced code blocks");

  auto const narrow_code_transcript = ava::tui::render_composer(ava::tui::ComposerSnapshot{
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
  auto const narrow_inline = std::ranges::find_if(narrow_code_transcript, [](std::string const& line) {
    return strip_sgr(line).find("Intro ok and bold.") != std::string::npos;
  });
  auto const narrow_code = std::ranges::find_if(narrow_code_transcript, [](std::string const& line) {
    return strip_sgr(line).find("value `x` and **y**") != std::string::npos;
  });
  auto const narrow_after_code = std::ranges::find_if(narrow_code_transcript, [](std::string const& line) {
    return strip_sgr(line).find("Done bold.") != std::string::npos;
  });
  expect(narrow_inline != narrow_code_transcript.end() && has_active_sgr_at_text(*narrow_inline, "ok", kWarningSgr) &&
             has_active_sgr_at_text(*narrow_inline, "bold", kBoldSgr) && narrow_code != narrow_code_transcript.end() &&
             !has_active_sgr_at_text(*narrow_code, "x", kWarningSgr) &&
             !has_active_sgr_at_text(*narrow_code, "y", kBoldSgr) &&
             narrow_after_code != narrow_code_transcript.end() &&
             has_active_sgr_at_text(*narrow_after_code, "bold", kBoldSgr),
         "tui narrow assistant renderer keeps code literal while formatting inline markdown outside code");

  auto const narrow_transcript = ava::tui::render_composer(ava::tui::ComposerSnapshot{
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
                          [](std::string const& line) { return strip_sgr(line).find("xxx") != std::string::npos; }) &&
          std::ranges::all_of(narrow_transcript, [](std::string const& line) { return visible_columns(line) <= 20; }),
      "tui assistant renderer keeps long words readable at narrow widths");

  auto const rows_transcript = ava::tui::render_composer(
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
             [](std::string const& line) { return strip_sgr(line).find("! bad command") != std::string::npos; }) &&
             std::ranges::any_of(
                 rows_transcript,
                 [](std::string const& line) { return strip_sgr(line).find("· /help") != std::string::npos; }),
         "tui keeps errors and generic command rows distinct from message blocks");
  auto const compact_status = ava::tui::render_composer(ava::tui::ComposerSnapshot{
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
                              [](std::string const& line) {
                                return strip_sgr(line).find("Ctrl-J/Shift+Enter inserts newline") != std::string::npos;
                              }),
         "tui keeps the composer status compact instead of rendering verbose help");

  auto const minimum_width = ava::tui::render_composer(ava::tui::ComposerSnapshot{.mode = "build",
                                                                                  .provider = "openai",
                                                                                  .model = "gpt-5.5",
                                                                                  .session_id = "session_test",
                                                                                  .input = "hello",
                                                                                  .status = "ready",
                                                                                  .transcript = {},
                                                                                  .width = 1,
                                                                                  .height = 1});
  expect(std::ranges::all_of(minimum_width,
                             [](std::string const& line) {
                               return line.find('\n') == std::string::npos && visible_columns(line) <= 20;
                             }) &&
             std::ranges::any_of(
                 minimum_width,
                 [](std::string const& line) { return strip_sgr(line).find("❯ hello") != std::string::npos; }),
         "tui clamps normal composer rendering to the minimum viewport");

  std::vector<ava::tui::SlashCommandItem> const slash_commands = {
      ava::tui::SlashCommandItem{.command = "/help", .description = "Show help", .category = "General"},
      ava::tui::SlashCommandItem{
          .command = "/grep", .description = "Search files", .hint = "<text> [glob]", .category = "Files"},
      ava::tui::SlashCommandItem{
          .command = "/glob", .description = "List matching files", .hint = "<pattern>", .category = "Files"},
      ava::tui::SlashCommandItem{.command = "/quit", .description = "Exit", .category = "General"}};
  auto const grep_commands = ava::tui::filter_slash_commands("/gr", slash_commands);
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

  std::vector<ava::tui::SlashCommandItem> const argument_slash_commands = {
      ava::tui::SlashCommandItem{
          .command = "/models",
          .description = "List models",
          .hint = "[query|provider/model]",
          .category = "Models",
          .aliases = {"/model"},
          .argument_completions = {ava::tui::SlashCommandArgumentCompletion{.value = "openai/gpt-5.5",
                                                                            .description = "GPT-5.5",
                                                                            .category = "Models",
                                                                            .argument_index = 0,
                                                                            .append_space = false},
                                   ava::tui::SlashCommandArgumentCompletion{.value = "anthropic/claude-sonnet-4-5",
                                                                            .description = "Claude Sonnet 4.5",
                                                                            .category = "Models",
                                                                            .argument_index = 0,
                                                                            .append_space = false}}},
      ava::tui::SlashCommandItem{
          .command = "/mcp",
          .description = "MCP",
          .hint = "<list|inspect|tools|restart> ...",
          .category = "Plugins",
          .argument_completions = {
              ava::tui::SlashCommandArgumentCompletion{
                  .value = "inspect", .description = "Inspect server", .category = "MCP", .argument_index = 0},
              ava::tui::SlashCommandArgumentCompletion{.value = "fs",
                                                       .description = "Filesystem server",
                                                       .category = "MCP",
                                                       .required_previous_args = {"inspect"},
                                                       .argument_index = 1,
                                                       .append_space = false}}}};
  auto const model_argument_matches = ava::tui::filter_slash_commands("/model open", argument_slash_commands);
  expect(model_argument_matches.size() == 1 && model_argument_matches.front().argument_completion &&
             model_argument_matches.front().command == "openai/gpt-5.5",
         "tui slash palette filters backend-provided argument completions after a command alias");
  expect(ava::tui::slash_palette_visible("/models open", argument_slash_commands) &&
             ava::tui::slash_command_selection_text("/models open", argument_slash_commands, 0) ==
                 "/models openai/gpt-5.5",
         "tui slash selection inserts explicit backend-provided argument completion text");
  expect(ava::tui::slash_command_selection_text("/mcp inspect f", argument_slash_commands, 0) == "/mcp inspect fs",
         "tui argument completion preserves required previous arguments for nested command forms");
  std::vector<ava::tui::SlashCommandItem> const connect_slash_commands = {
      ava::tui::SlashCommandItem{.command = "/connect", .description = "Connect a provider", .category = "General"}};
  expect(!ava::tui::slash_palette_visible("/connect", connect_slash_commands) &&
             !ava::tui::slash_palette_visible("/connect ", connect_slash_commands) &&
             !ava::tui::slash_palette_visible("/connect openai", connect_slash_commands) &&
             ava::tui::filter_slash_commands("/connect openai ", connect_slash_commands).empty(),
         "tui slash palette lets /connect submit directly so provider and method choices stay in the centered modal");
  auto const argument_palette =
      ava::tui::render_composer(ava::tui::ComposerSnapshot{.mode = "build",
                                                           .provider = "openai",
                                                           .model = "gpt-5.5",
                                                           .session_id = "session_test",
                                                           .input = "/model open",
                                                           .status = "ready",
                                                           .transcript = {},
                                                           .slash_commands = argument_slash_commands,
                                                           .selected_slash_command_index = 0,
                                                           .width = 96,
                                                           .height = 10});
  expect(std::ranges::any_of(argument_palette,
                             [](std::string const& line) {
                               auto const visible = strip_sgr(line);
                               return visible.find("openai/gpt-5.5") != std::string::npos &&
                                      visible.find("GPT-5.5") != std::string::npos &&
                                      visible.find("Models") != std::string::npos;
                             }),
         "tui slash palette renders argument completion value, category, and description");

  auto const key_bindings = ava::tui::parse_key_bindings_json(
      "{\"submit\":\"Ctrl+T, Enter\",\"new_line\":\"Shift+Enter\",\"delete_to_line_start\":\"Ctrl+U\","
      "\"autocomplete_accept\":\"Tab\",\"variant_cycle\":\"Ctrl+D\"}");
  expect(
      key_bindings && ava::tui::action_for_key(*key_bindings, ava::tui::Key::CtrlT) == ava::tui::TuiAction::Submit &&
          ava::tui::action_for_key(*key_bindings, ava::tui::Key::CtrlD) == ava::tui::TuiAction::VariantCycle &&
          ava::tui::key_matches_action(*key_bindings, ava::tui::TuiAction::DeleteToLineStart, ava::tui::Key::CtrlU) &&
          ava::tui::key_matches_action(*key_bindings, ava::tui::TuiAction::AutocompleteAccept, ava::tui::Key::Tab) &&
          ava::tui::keys_display(*key_bindings, ava::tui::TuiAction::Submit).find("Ctrl+T") != std::string::npos,
      "tui keybind parser maps configured keys to semantic actions and display text");
  auto const default_bindings = ava::tui::default_key_bindings();
  expect(
      ava::tui::key_matches_action(default_bindings, ava::tui::TuiAction::HistoryPrev, ava::tui::Key::ArrowUp) &&
          ava::tui::key_matches_action(default_bindings, ava::tui::TuiAction::PalettePrev, ava::tui::Key::ArrowUp) &&
          ava::tui::key_matches_action(default_bindings, ava::tui::TuiAction::ModeToggle, ava::tui::Key::Tab) &&
          ava::tui::key_matches_action(default_bindings, ava::tui::TuiAction::AutocompleteAccept, ava::tui::Key::Tab) &&
          ava::tui::key_matches_action(default_bindings, ava::tui::TuiAction::Undo, ava::tui::Key::CtrlZ) &&
          ava::tui::key_matches_action(default_bindings, ava::tui::TuiAction::Yank, ava::tui::Key::CtrlY) &&
          ava::tui::key_matches_action(default_bindings, ava::tui::TuiAction::Interrupt, ava::tui::Key::CtrlC),
      "tui default keybinds preserve context-specific semantic actions for shared keys");
  auto const help_items = ava::tui::key_binding_help_items(default_bindings);
  expect(std::ranges::any_of(help_items,
                             [](ava::tui::TuiKeyBindingHelpItem const& item) {
                               return item.action == "variant_cycle" && item.keys.find("Ctrl+T") != std::string::npos;
                             }) &&
             std::ranges::any_of(help_items,
                                 [](ava::tui::TuiKeyBindingHelpItem const& item) {
                                   return item.action == "delete_to_line_start" &&
                                          item.keys.find("Ctrl+U") != std::string::npos;
                                 }) &&
             std::ranges::any_of(help_items,
                                 [](ava::tui::TuiKeyBindingHelpItem const& item) {
                                   return item.action == "undo" && item.keys.find("Ctrl+Z") != std::string::npos;
                                 }) &&
             std::ranges::any_of(help_items,
                                 [](ava::tui::TuiKeyBindingHelpItem const& item) {
                                   return item.action == "yank" && item.keys.find("Ctrl+Y") != std::string::npos;
                                 }),
         "tui keybind help lists concrete semantic action names and effective keys");
  expect(!ava::tui::parse_key_bindings_json("{\"submit\":\"Hyper+Enter\"}"),
         "tui keybind parser rejects unknown key names");
  expect(!ava::tui::parse_key_bindings_json("{\"submt\":\"Enter\"}"),
         "tui keybind parser rejects unknown action names");
  auto const escaped_action_keybinds =
      ava::tui::parse_key_bindings_json("{\"\\u0073\\u0075\\u0062\\u006d\\u0069\\u0074\":\"Ctrl+T\"}");
  expect(escaped_action_keybinds &&
             ava::tui::key_matches_action(*escaped_action_keybinds, ava::tui::TuiAction::Submit, ava::tui::Key::CtrlT),
         "tui keybind parser accepts JSON unicode escapes in action names");
  expect(!ava::tui::parse_key_bindings_json("{\"submit\":123}"), "tui keybind parser rejects non-string action values");

  auto const keybind_root = temp_root() / "tui-keybinds";
  std::filesystem::remove_all(keybind_root);
  std::filesystem::create_directories(keybind_root);
  auto const keybinds_file = keybind_root / "keybinds.json";
  auto const missing_keybinds = ava::tui::load_key_bindings(keybinds_file);
  expect(missing_keybinds &&
             ava::tui::key_matches_action(*missing_keybinds, ava::tui::TuiAction::Submit, ava::tui::Key::Enter),
         "tui keybind file loader falls back to defaults when the file is missing");
  {
    std::ofstream output(keybinds_file);
    output << "{\"submit\":\"Ctrl+T\",\"variant_cycle\":\"Ctrl+D\"}";
  }
  auto const loaded_keybinds = ava::tui::load_key_bindings(keybinds_file);
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

  std::vector<ava::tui::SlashCommandItem> const disabled_slash_commands = {
      ava::tui::SlashCommandItem{.command = "/models",
                                 .description = "Select model",
                                 .category = "Planned",
                                 .aliases = {"/model"},
                                 .key_display = "Ctrl+M",
                                 .enabled = false,
                                 .disabled_reason = "model switching is not implemented"},
      ava::tui::SlashCommandItem{.command = "/mode", .description = "Toggle mode", .category = "General"}};
  auto const alias_matches = ava::tui::filter_slash_commands("/model", disabled_slash_commands);
  expect(alias_matches.size() == 1 && alias_matches.front().command == "/models",
         "tui slash palette filters aliases as well as primary command names");
  expect(ava::tui::slash_palette_visible("/model", disabled_slash_commands),
         "tui slash palette keeps disabled exact alias matches visible");
  auto const disabled_reason = ava::tui::slash_command_selection_disabled_reason("/model", disabled_slash_commands, 0);
  expect(disabled_reason && disabled_reason->find("not implemented") != std::string::npos,
         "tui slash selection exposes disabled command explanations");

  auto const disabled_palette =
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
                             [](std::string const& line) {
                               auto const visible = strip_sgr(line);
                               return visible.find("/models (/model)") != std::string::npos &&
                                      visible.find("Ctrl+M") != std::string::npos &&
                                      visible.find("disabled: model switching is not implemented") != std::string::npos;
                             }),
         "tui slash palette renders aliases, key displays, and disabled reasons");

  auto const palette = ava::tui::render_composer(ava::tui::ComposerSnapshot{.mode = "build",
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
             palette, [](std::string const& line) { return line.find("commands matching /g") == std::string::npos; }) &&
             std::ranges::any_of(palette,
                                 [](std::string const& line) {
                                   return line.find("/grep") != std::string::npos &&
                                          line.find("Files") != std::string::npos &&
                                          line.find("Search files") != std::string::npos;
                                 }) &&
             std::ranges::any_of(palette,
                                 [](std::string const& line) {
                                   return line.find("/glob") != std::string::npos &&
                                          line.find("List matching files") != std::string::npos;
                                 }) &&
             std::ranges::any_of(palette,
                                 [](std::string const& line) {
                                   auto const visible = strip_sgr(line);
                                   return visible.find("› /glob") != std::string::npos &&
                                          visible.find("selected") == std::string::npos &&
                                          visible.find("(2/2)") == std::string::npos;
                                 }) &&
             std::ranges::any_of(palette,
                                 [](std::string const& line) {
                                   return line.find("\x1b[7m› /glob") != std::string::npos &&
                                          line.find("\x1b[0m") != std::string::npos;
                                 }) &&
             std::ranges::none_of(palette,
                                  [](std::string const& line) { return line.find("/help") != std::string::npos; }),
         "tui renders filtered slash-command palette with composer-integrated selected item highlight");
  auto const suppressed_palette = ava::tui::render_composer(ava::tui::ComposerSnapshot{.mode = "build",
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
             [](std::string const& line) { return strip_sgr(line).find("/grep") != std::string::npos; }) &&
             std::ranges::any_of(
                 suppressed_palette,
                 [](std::string const& line) { return strip_sgr(line).find("❯ /g") != std::string::npos; }),
         "tui can dismiss slash autocomplete without clearing the draft input");
  auto const clicked_palette_index =
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
  auto const suppressed_clicked_palette_index =
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
  auto const blocked_question_palette_index = ava::tui::slash_palette_selection_for_screen_row(
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
  auto const tiny_palette = ava::tui::render_composer(ava::tui::ComposerSnapshot{.mode = "build",
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
                             [](std::string const& line) { return line.find("› /item6") != std::string::npos; }) &&
             std::ranges::none_of(tiny_palette,
                                  [](std::string const& line) { return line.find("/item0") != std::string::npos; }),
         "tui keeps selected slash palette item visible when height is tight");
  auto const first_scrolled_palette_click =
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
  auto const selected_scrolled_palette_click =
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
  auto const outside_scrolled_palette_click =
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

  auto const starved_palette = ava::tui::render_composer(
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
                 [](std::string const& line) { return strip_sgr(line).find("› /item4") != std::string::npos; }) &&
             std::ranges::none_of(
                 starved_palette,
                 [](std::string const& line) { return strip_sgr(line).find("must not leak") != std::string::npos; }),
         "tui keeps the bottom composer fixed when the slash palette exhausts transcript height");

  auto const no_match_palette = ava::tui::render_composer(ava::tui::ComposerSnapshot{.mode = "build",
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
                             [](std::string const& line) {
                               return strip_sgr(line).find("no commands match /zz") != std::string::npos;
                             }),
         "tui slash-command palette renders deterministic empty state");

  auto const permission_modal = ava::tui::render_composer(ava::tui::ComposerSnapshot{
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
             [](std::string const& line) { return line.find("PERMISSION REQUIRED") != std::string::npos; }) &&
             std::ranges::any_of(permission_modal,
                                 [](std::string const& line) {
                                   auto visible = strip_sgr(line);
                                   return visible.find("git push origin main") != std::string::npos;
                                 }) &&
             std::ranges::any_of(permission_modal,
                                 [](std::string const& line) {
                                   return line.find("[Deny]") != std::string::npos &&
                                          line.find("[Allow once]") != std::string::npos;
                                 }) &&
             std::ranges::any_of(permission_modal,
                                 [](std::string const& line) {
                                   return line.find("\x1b[7m> [Deny]") != std::string::npos &&
                                          strip_sgr(line).find("(selected)") == std::string::npos;
                                 }) &&
             std::ranges::any_of(permission_modal,
                                 [](std::string const& line) {
                                   auto visible = strip_sgr(line);
                                   return visible.find("A allow") != std::string::npos &&
                                          visible.find("D deny") != std::string::npos &&
                                          visible.find("Enter confirm") != std::string::npos &&
                                          visible.find("Esc deny") != std::string::npos;
                                 }) &&
             std::ranges::none_of(permission_modal,
                                  [](std::string const& line) {
                                    return line.find("bash") != std::string::npos &&
                                           line.find("\x1b[31m") != std::string::npos;
                                  }),
         "tui renders Rust AVA-style permission dock with default deny focus");
  expect(std::ranges::all_of(permission_modal,
                             [](std::string const& line) {
                               return line.find('\n') == std::string::npos && visible_columns(line) <= 80;
                             }) &&
             std::ranges::any_of(
                 permission_modal,
                 [](std::string const& line) { return strip_sgr(line).find("[Deny]") != std::string::npos; }) &&
             std::ranges::any_of(
                 permission_modal,
                 [](std::string const& line) { return strip_sgr(line).find("[Allow once]") != std::string::npos; }) &&
             std::ranges::any_of(
                 permission_modal,
                 [](std::string const& line) { return strip_sgr(line).find("Enter confirm") != std::string::npos; }) &&
             std::ranges::any_of(
                 permission_modal,
                 [](std::string const& line) { return strip_sgr(line).find("Esc deny") != std::string::npos; }),
         "tui permission dock controls stay within 80 visible columns without losing controls");

  auto const allow_focused_modal = ava::tui::render_composer(ava::tui::ComposerSnapshot{
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
                             [](std::string const& line) {
                               return line.find("\x1b[7m> [Allow once]") != std::string::npos &&
                                      strip_sgr(line).find("(selected)") == std::string::npos;
                             }),
         "tui permission dock highlights the selected allow choice");

  auto const diff_permission_modal = ava::tui::render_composer(ava::tui::ComposerSnapshot{
      .mode = "build",
      .provider = "openai",
      .model = "gpt-5.5",
      .session_id = "session_test",
      .input = "",
      .status = "permission required",
      .transcript = {},
      .permission_prompt =
          ava::tui::PermissionPromptView{.tool_name = "write_file",
                                         .operation = "write_file",
                                         .target = "/tmp/outside.txt",
                                         .command = "",
                                         .reason = "external mutation",
                                         .diff_preview = "--- /tmp/outside.txt\n+++ /tmp/outside.txt\n@@ -1,1 +1,1 "
                                                         "@@\n-old line\n+new line\n",
                                         .diff_truncated = true},
      .width = 54,
      .height = 15});
  expect(std::ranges::all_of(diff_permission_modal,
                             [](std::string const& line) {
                               return line.find('\n') == std::string::npos && visible_columns(line) <= 54;
                             }) &&
             std::ranges::any_of(
                 diff_permission_modal,
                 [](std::string const& line) { return strip_sgr(line).find("diff:") != std::string::npos; }) &&
             std::ranges::any_of(
                 diff_permission_modal,
                 [](std::string const& line) { return strip_sgr(line).find("-old line") != std::string::npos; }) &&
             std::ranges::any_of(
                 diff_permission_modal,
                 [](std::string const& line) { return strip_sgr(line).find("+new line") != std::string::npos; }) &&
             std::ranges::any_of(diff_permission_modal,
                                 [](std::string const& line) {
                                   return strip_sgr(line).find("[diff truncated]") != std::string::npos;
                                 }) &&
             std::ranges::any_of(diff_permission_modal,
                                 [](std::string const& line) {
                                   return strip_sgr(line).find("[Deny]") != std::string::npos &&
                                          strip_sgr(line).find("[Allow once]") != std::string::npos;
                                 }) &&
             std::ranges::any_of(
                 diff_permission_modal,
                 [](std::string const& line) { return strip_sgr(line).find("Esc deny") != std::string::npos; }),
         "tui permission dock renders backend-provided mutation diffs while preserving fail-closed controls");

  auto const narrow_diff_permission_modal = ava::tui::render_composer(ava::tui::ComposerSnapshot{
      .mode = "build",
      .provider = "openai",
      .model = "gpt-5.5",
      .session_id = "session_test",
      .input = "",
      .status = "permission required",
      .transcript = {},
      .permission_prompt =
          ava::tui::PermissionPromptView{.tool_name = "edit",
                                         .operation = "edit",
                                         .target = "/tmp/outside.txt",
                                         .command = "",
                                         .reason = "external mutation",
                                         .diff_preview = "--- old\n+++ new\n@@ -1,1 +1,1 @@\n-old\n+new\n"},
      .width = 28,
      .height = 10});
  expect(std::ranges::all_of(narrow_diff_permission_modal,
                             [](std::string const& line) {
                               return line.find('\n') == std::string::npos && visible_columns(line) <= 28;
                             }) &&
             std::ranges::any_of(
                 narrow_diff_permission_modal,
                 [](std::string const& line) { return strip_sgr(line).find("diff:") != std::string::npos; }) &&
             std::ranges::any_of(narrow_diff_permission_modal,
                                 [](std::string const& line) {
                                   auto const visible = strip_sgr(line);
                                   return visible.find("[Deny]") != std::string::npos &&
                                          visible.find("[Allow once]") != std::string::npos;
                                 }),
         "tui permission dock keeps diff previews bounded at narrow widths");

  auto const long_permission_modal = ava::tui::render_composer(
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
                             [](std::string const& line) {
                               return line.find('\n') == std::string::npos && visible_columns(line) <= 80;
                             }) &&
             std::ranges::any_of(long_permission_modal,
                                 [](std::string const& line) {
                                   auto const visible = strip_sgr(line);
                                   return visible.find("cccc") != std::string::npos &&
                                          visible.find("...") != std::string::npos;
                                 }),
         "tui permission dock truncates long detail text and handles an empty tool name");

  auto const tight_permission_modal = ava::tui::render_composer(ava::tui::ComposerSnapshot{
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
             [](std::string const& line) { return line.find("PERMISSION REQUIRED") != std::string::npos; }) &&
             tight_permission_modal.size() <= 8 &&
             std::ranges::all_of(tight_permission_modal,
                                 [](std::string const& line) {
                                   return line.find('\n') == std::string::npos && visible_columns(line) <= 36;
                                 }) &&
             std::ranges::any_of(tight_permission_modal,
                                 [](std::string const& line) {
                                   return line.find("[Deny]") != std::string::npos &&
                                          line.find("[Allow once]") != std::string::npos;
                                 }),
         "tui permission dock keeps header and controls visible in tight height");

  auto const ultra_tight_permission_modal = ava::tui::render_composer(ava::tui::ComposerSnapshot{
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
                             [](std::string const& line) {
                               return line.find('\n') == std::string::npos && visible_columns(line) <= 20;
                             }) &&
             ultra_tight_permission_modal.size() <= 8 &&
             std::ranges::any_of(ultra_tight_permission_modal,
                                 [](std::string const& line) {
                                   auto const visible = strip_sgr(line);
                                   return visible.find("PERMISSION") != std::string::npos;
                                 }) &&
             std::ranges::any_of(ultra_tight_permission_modal,
                                 [](std::string const& line) {
                                   auto const visible = strip_sgr(line);
                                   return visible.find("> [D]") != std::string::npos &&
                                          visible.find("sel") == std::string::npos &&
                                          visible.find("[A]") != std::string::npos;
                                 }) &&
             std::ranges::any_of(ultra_tight_permission_modal,
                                 [](std::string const& line) {
                                   auto const visible = strip_sgr(line);
                                   return visible.find("A=allow") != std::string::npos &&
                                          visible.find("D=deny") != std::string::npos;
                                 }),
         "tui permission dock preserves deny and allow choices at minimum width");

  std::vector<ava::tui::TranscriptItem> permission_overflow_items;
  for (int index = 0; index < 8; ++index) {
    permission_overflow_items.push_back(
        ava::tui::TranscriptItem{.label = "ava", .text = "permission item " + std::to_string(index)});
  }
  auto const permission_starved = ava::tui::render_composer(ava::tui::ComposerSnapshot{
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
                                 [](std::string const& line) { return visible_columns(line) <= 40; }) &&
             std::ranges::none_of(
                 permission_starved,
                 [](std::string const& line) { return strip_sgr(line).find("lines hidden") != std::string::npos; }) &&
             std::ranges::any_of(
                 permission_starved,
                 [](std::string const& line) { return strip_sgr(line).find("❯ hidden input") != std::string::npos; }),
         "tui permission prompt stays above the composer without hidden-line banners");

  auto const sanitized = ava::tui::render_composer(
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
                             [](std::string const& line) {
                               auto visible = strip_sgr(line);
                               return visible.find("?[31mred") != std::string::npos;
                             }),
         "tui render sanitizes transcript escape bytes in user content");
  auto const sanitized_input = ava::tui::render_composer(ava::tui::ComposerSnapshot{.mode = "build",
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
             [](std::string const& line) { return strip_sgr(line).find("❯ bad?[31mred") != std::string::npos; }),
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
  expect(ava::tui::sanitize_terminal_text(std::string("nul") + std::string(1, '\0') + "byte") == "nul?byte",
         "tui sanitizer replaces binary-like NUL bytes with a visible marker");
  expect(ava::tui::detail::terminal_text_columns("\xE7\x95\x8C") == 2 &&
             ava::tui::detail::terminal_text_columns(std::string("e") + "\xCC\x81") == 1 &&
             ava::tui::detail::terminal_text_columns(std::string("a") + "\xE2\x80\x8D" + "b") == 2 &&
             ava::tui::detail::terminal_text_columns(std::string("\xE2\x98\xBA") + "\xEF\xB8\x8F") >= 1,
         "tui width accounting handles CJK width and treats combining marks, zero-width joiners, and variation "
         "selectors as non-advancing");
  auto const cursor_prefix_columns = ava::tui::detail::terminal_text_columns(
      std::string(ava::tui::detail::kComposerBar) + "  " + std::string(ava::tui::detail::kComposerPrompt) + " ");
  auto const cursor_base = cursor_prefix_columns + 1;
  auto const cursor_for = [](std::string input, std::size_t cursor) {
    return ava::tui::detail::input_cursor_column(ava::tui::ComposerSnapshot{.mode = "build",
                                                                            .provider = "openai",
                                                                            .model = "gpt-5.5",
                                                                            .session_id = "session_test",
                                                                            .input = std::move(input),
                                                                            .status = "ready",
                                                                            .transcript = {},
                                                                            .input_cursor = cursor},
                                                 120);
  };
  auto const cursor_text = std::string("a") + "\xE7\x95\x8C" + "e" + "\xCC\x81";
  expect(cursor_for(cursor_text, 1) == cursor_base + 1 && cursor_for(cursor_text, 4) == cursor_base + 3 &&
             cursor_for(cursor_text, 5) == cursor_base + 4 &&
             cursor_for(cursor_text, cursor_text.size()) == cursor_base + 4 &&
             cursor_for(std::string("x") + std::string("\xC0\x80", 2), 3) == cursor_base + 3,
         "tui composer cursor placement uses sanitized display columns for CJK, combining marks, and invalid utf-8");

  auto const composer_frame = ava::tui::render_composer(ava::tui::ComposerSnapshot{.mode = "plan",
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
             [](std::string const& line) { return strip_sgr(line).find("▎  ❯ hello") != std::string::npos; }),
         "tui composer frame renders the input prompt content");
  auto const wide_frame = ava::tui::render_composer(
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
  expect(std::ranges::all_of(wide_frame, [](std::string const& line) { return visible_columns(line) <= 24; }),
         "tui treats CJK and emoji as wide cells when fitting rendered lines");
  ava::tui::clear_terminal_signal();
  expect(!ava::tui::terminal_signal_received(), "tui terminal signal state can be cleared before curses entry");

  auto const permission_frame = ava::tui::render_composer(ava::tui::ComposerSnapshot{
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
             std::ranges::any_of(permission_frame,
                                 [](std::string const& line) {
                                   return strip_sgr(line).find("❯ do not focus composer") != std::string::npos;
                                 }) &&
             std::ranges::any_of(permission_frame,
                                 [](std::string const& line) {
                                   return strip_sgr(line).find("PERMISSION REQUIRED") != std::string::npos;
                                 }),
         "tui composer frame renders permission dock above composer while active");

  auto const question_frame = ava::tui::render_composer(ava::tui::ComposerSnapshot{
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
             std::ranges::any_of(question_frame,
                                 [](std::string const& line) {
                                   return strip_sgr(line).find("❯ do not focus composer") != std::string::npos;
                                 }) &&
             std::ranges::any_of(question_frame,
                                 [](std::string const& line) {
                                   return strip_sgr(line).find("Choose tools (multi-select)") != std::string::npos;
                                 }) &&
             std::ranges::any_of(question_frame,
                                 [](std::string const& line) {
                                   return strip_sgr(line).find("2. [x] Search text") != std::string::npos;
                                 }) &&
             std::ranges::any_of(
                 question_frame,
                 [](std::string const& line) { return strip_sgr(line).find("Custom: explain") != std::string::npos; }),
         "tui composer frame renders multi-select question dock above composer while active");

  auto const secret_question_frame = ava::tui::render_composer(
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
             [](std::string const& line) { return strip_sgr(line).find("sk-visible-secret") != std::string::npos; }) &&
             std::ranges::any_of(secret_question_frame,
                                 [](std::string const& line) {
                                   return strip_sgr(line).find("Custom: *****************") != std::string::npos;
                                 }),
         "tui question dock masks secret custom input");

  auto const modal_question_frame = ava::tui::render_composer(ava::tui::ComposerSnapshot{
      .mode = "build",
      .provider = "",
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
                                 [](std::string const& line) {
                                   return strip_sgr(line).find("Connect a provider") != std::string::npos;
                                 }) &&
             std::ranges::any_of(
                 modal_question_frame,
                 [](std::string const& line) { return strip_sgr(line).find("Search: anth") != std::string::npos; }) &&
             std::ranges::any_of(
                 modal_question_frame,
                 [](std::string const& line) { return strip_sgr(line).find("Anthropic") != std::string::npos; }) &&
             std::ranges::none_of(
                 modal_question_frame,
                 [](std::string const& line) { return strip_sgr(line).find("OpenAI") != std::string::npos; }),
         "tui renders searchable provider questions as centered filtered modals");

  auto const oauth_modal_frame = ava::tui::render_composer(ava::tui::ComposerSnapshot{
      .mode = "build",
      .provider = "openai",
      .model = "gpt-5.5",
      .session_id = "session_test",
      .input = "",
      .status = "question required",
      .transcript = {},
      .question_prompt =
          ava::tui::QuestionPromptView{
              .header = "ChatGPT Pro/Plus (browser)",
              .question = "https://auth.openai.com/oauth/authorize?client_id="
                          "app_EMoamEEZ73f0CkXaXp7hrann&redirect_uri=http%3A%2F%2Flocalhost%3A1455"
                          "%2Fauth%2Fcallback&code_challenge=longchallengevalue&state=state\n"
                          "\nComplete authorization in your browser. This window will close automatically."
                          "\n\nWaiting for authorization...",
              .options = {ava::tui::QuestionPromptOptionView{.value = "copy:https://auth.openai.com/oauth/authorize",
                                                             .label = "C Copy"}},
              .multiple = false,
              .allow_custom = false,
              .secret = false,
              .modal = true,
              .searchable = false,
              .selected_option_index = 0,
              .custom_text = ""},
      .width = 80,
      .height = 20});
  expect(std::ranges::any_of(oauth_modal_frame,
                             [](std::string const& line) {
                               return strip_sgr(line).find("https://auth.openai.com") != std::string::npos;
                             }) &&
             std::ranges::any_of(
                 oauth_modal_frame,
                 [](std::string const& line) { return strip_sgr(line).find("C Copy") != std::string::npos; }) &&
             std::ranges::any_of(oauth_modal_frame,
                                 [](std::string const& line) {
                                   return strip_sgr(line).find("Waiting for authorization") != std::string::npos;
                                 }) &&
             std::ranges::any_of(
                 oauth_modal_frame,
                 [](std::string const& line) { return strip_sgr(line).find("C copy") != std::string::npos; }) &&
             std::ranges::none_of(
                 oauth_modal_frame,
                 [](std::string const& line) { return strip_sgr(line).find("Enter confirm") != std::string::npos; }),
         "tui renders OpenAI OAuth modal with opencode-style waiting and C copy shortcut");

  auto const sidebar_modal_frame = ava::tui::render_composer(ava::tui::ComposerSnapshot{
      .mode = "build",
      .provider = "openai",
      .model = "gpt-5.5",
      .session_id = "session_test",
      .input = "",
      .status = "question required",
      .transcript = {},
      .question_prompt = ava::tui::QuestionPromptView{.header = "Connect OpenAI",
                                                      .question = "Choose login method",
                                                      .options = {ava::tui::QuestionPromptOptionView{
                                                          .value = "api_key", .label = "A OpenAI API key"}},
                                                      .multiple = false,
                                                      .allow_custom = false,
                                                      .secret = false,
                                                      .modal = true,
                                                      .searchable = false,
                                                      .selected_option_index = 0,
                                                      .custom_text = ""},
      .width = 128,
      .height = 22,
      .sidebar = ava::tui::SidebarSnapshot{.session_id = "session_test",
                                           .mode = "build",
                                           .provider = "openai",
                                           .model = "gpt-5.5",
                                           .workspace = "/workspace",
                                           .git_branch = "develop",
                                           .version = "0.32",
                                           .context_source_count = 1}});
  expect(std::ranges::any_of(sidebar_modal_frame,
                             [](std::string const& line) {
                               auto const visible = strip_sgr(line);
                               return visible.find("Connect OpenAI") != std::string::npos &&
                                      visible.find("│") != std::string::npos;
                             }) &&
             std::ranges::any_of(
                 sidebar_modal_frame,
                 [](std::string const& line) { return strip_sgr(line).find("Modified Files") != std::string::npos; }) &&
             std::ranges::any_of(
                 sidebar_modal_frame,
                 [](std::string const& line) { return strip_sgr(line).find("Session") != std::string::npos; }),
         "tui modal overlays the main pane without hiding the sidebar");

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
  auto const custom_search_answer = ava::tui::question_answer_from_prompt_view(
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

  auto const multiline_input = ava::tui::render_composer(ava::tui::ComposerSnapshot{.mode = "build",
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
             [](std::string const& line) { return strip_sgr(line).find("▎  ❯ first") != std::string::npos; }) &&
             std::ranges::any_of(
                 multiline_input,
                 [](std::string const& line) { return strip_sgr(line).find("▎    second") != std::string::npos; }),
         "tui renders shift-enter newlines as multiline composer input");
  auto const empty_composer_height = ava::tui::render_composer(ava::tui::ComposerSnapshot{.mode = "build",
                                                                                          .provider = "openai",
                                                                                          .model = "gpt-5.5",
                                                                                          .session_id = "session_test",
                                                                                          .input = "",
                                                                                          .status = "ready",
                                                                                          .transcript = {},
                                                                                          .width = 50,
                                                                                          .height = 12});
  auto const grown_composer_height =
      ava::tui::render_composer(ava::tui::ComposerSnapshot{.mode = "build",
                                                           .provider = "openai",
                                                           .model = "gpt-5.5",
                                                           .session_id = "session_test",
                                                           .input = "one\ntwo\nthree\nfour\nfive",
                                                           .status = "ready",
                                                           .transcript = {},
                                                           .width = 50,
                                                           .height = 12});
  auto const composer_bg_rows = [](std::vector<std::string> const& rendered) {
    return static_cast<std::size_t>(std::ranges::count_if(
        rendered, [](std::string const& line) { return line.find("\x1b[48;2;26;31;46m") != std::string::npos; }));
  };
  expect(composer_bg_rows(grown_composer_height) > composer_bg_rows(empty_composer_height) &&
             std::ranges::any_of(
                 grown_composer_height,
                 [](std::string const& line) { return strip_sgr(line).find("▎    five") != std::string::npos; }),
         "tui composer grows with multiline input and keeps the latest line visible");
  auto const tall_draft = ava::tui::render_composer(
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
          tall_draft, [](std::string const& line) { return strip_sgr(line).find("draft +") != std::string::npos; }) &&
          std::ranges::any_of(
              tall_draft,
              [](std::string const& line) { return strip_sgr(line).find("▎    nine") != std::string::npos; }) &&
          std::ranges::none_of(
              tall_draft,
              [](std::string const& line) { return strip_sgr(line).find("▎  ❯ one") != std::string::npos; }),
      "tui composer hides draft overflow text while keeping the latest draft line visible");
  auto const scrolled_draft = ava::tui::render_composer(
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
             [](std::string const& line) { return strip_sgr(line).find("▎  ❯ one") != std::string::npos; }) &&
             std::ranges::none_of(
                 scrolled_draft,
                 [](std::string const& line) { return strip_sgr(line).find("▎    nine") != std::string::npos; }) &&
             std::ranges::none_of(
                 scrolled_draft,
                 [](std::string const& line) { return strip_sgr(line).find("draft +") != std::string::npos; }),
         "tui composer draft scroll offset shows older draft lines without footer overflow text");

  std::vector<ava::tui::TranscriptItem> many_items;
  for (int index = 0; index < 20; ++index) {
    many_items.push_back(ava::tui::TranscriptItem{.label = "line", .text = "item " + std::to_string(index)});
  }
  auto const scrolled = ava::tui::render_composer(ava::tui::ComposerSnapshot{.mode = "build",
                                                                             .provider = "openai",
                                                                             .model = "gpt-5.5",
                                                                             .session_id = "session_test",
                                                                             .input = "",
                                                                             .status = "ready",
                                                                             .transcript = many_items,
                                                                             .width = 40,
                                                                             .height = 12});
  expect(std::ranges::any_of(scrolled,
                             [](std::string const& line) { return line.find("item 19") != std::string::npos; }) &&
             std::ranges::none_of(
                 scrolled, [](std::string const& line) { return line.find("lines hidden") != std::string::npos; }) &&
             std::ranges::none_of(scrolled,
                                  [](std::string const& line) { return line.find("item 0") != std::string::npos; }),
         "tui transcript viewport keeps newest lines without hidden-line banners");

  auto const scrolled_up = ava::tui::render_composer(ava::tui::ComposerSnapshot{.mode = "build",
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
                              [](std::string const& line) { return line.find("lines hidden") != std::string::npos; }) &&
             std::ranges::any_of(scrolled_up,
                                 [](std::string const& line) { return line.find("item 15") != std::string::npos; }) &&
             std::ranges::none_of(scrolled_up,
                                  [](std::string const& line) { return line.find("item 19") != std::string::npos; }),
         "tui transcript viewport supports an explicit scroll offset");

  auto const wrapped_transcript = ava::tui::render_composer(ava::tui::ComposerSnapshot{
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
  auto const wrapped_latest = ava::tui::render_composer(ava::tui::ComposerSnapshot{
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
          [](std::string const& line) { return strip_sgr(line).find("lines hidden") != std::string::npos; }) &&
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
  auto const mixed_scrolled = ava::tui::render_composer(ava::tui::ComposerSnapshot{.mode = "build",
                                                                                   .provider = "openai",
                                                                                   .model = "gpt-5.5",
                                                                                   .session_id = "session_test",
                                                                                   .input = "",
                                                                                   .status = "ready",
                                                                                   .transcript = mixed_items,
                                                                                   .width = 60,
                                                                                   .height = 12});
  std::string mixed_visible;
  for (auto const& line : mixed_scrolled) {
    mixed_visible += strip_sgr(line);
    mixed_visible += '\n';
  }
  expect(mixed_visible.find("lines hidden") == std::string::npos && mixed_visible.find("[+]") != std::string::npos &&
             mixed_visible.find("2 matches") != std::string::npos && mixed_visible.find("done") != std::string::npos &&
             mixed_visible.find("AVA") == std::string::npos && mixed_visible.find("old 0") == std::string::npos,
         "tui transcript viewport scrolls mixed text and tool-card lines together without hidden-line banners");

  auto const multiline = ava::tui::render_composer(
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
                             [](std::string const& line) {
                               auto visible = strip_sgr(line);
                               return visible.find("one") != std::string::npos;
                             }) &&
             std::ranges::any_of(multiline,
                                 [](std::string const& line) {
                                   auto visible = strip_sgr(line);
                                   return visible.find("two") != std::string::npos;
                                 }),
         "tui renders multiline assistant transcript content inside the message block");

  auto const tool_card = ava::tui::render_composer(
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
                                                                        .result_summary = "read lines 1-12/12"}}},
                                 .width = 80,
                                 .height = 10});
  expect(std::ranges::any_of(tool_card,
                             [](std::string const& line) {
                               auto visible = strip_sgr(line);
                               return visible.find("[+]") != std::string::npos &&
                                      visible.find("read_file") != std::string::npos &&
                                      visible.find("path=note.txt") != std::string::npos;
                             }) &&
             std::ranges::any_of(tool_card,
                                 [](std::string const& line) {
                                   auto visible = strip_sgr(line);
                                   return visible.find("read lines 1-12/12") != std::string::npos;
                                 }) &&
             std::ranges::any_of(tool_card,
                                 [](std::string const& line) {
                                   return line.find("\x1b[38;2;52;211;153m[+]") != std::string::npos &&
                                          line.find("\x1b[1m\x1b[38;2;77;158;246mread_file") != std::string::npos &&
                                          line.find("\x1b[38;2;139;149;165mpath=note.txt") != std::string::npos;
                                 }),
         "tui renders compact styled tool timeline cards");
  expect(std::ranges::none_of(tool_card,
                              [](std::string const& line) { return line.find("\x1b[31m") != std::string::npos; }),
         "tui tool card rendering removes untrusted raw sgr escape sequences");

  auto const empty_tool_card = ava::tui::render_composer(
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
                             [](std::string const& line) {
                               auto const visible = strip_sgr(line);
                               return visible.find("[+]") != std::string::npos &&
                                      visible.find("unknown") != std::string::npos;
                             }) &&
             std::ranges::all_of(empty_tool_card, [](std::string const& line) { return visible_columns(line) <= 40; }),
         "tui renders empty tool-card fields with a safe fallback name");

  auto const running_error_cards = ava::tui::render_composer(ava::tui::ComposerSnapshot{
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
                          [](std::string const& line) {
                            auto visible = strip_sgr(line);
                            return visible.find("[~]") != std::string::npos &&
                                   visible.find("bash") != std::string::npos &&
                                   visible.find("command=build?") != std::string::npos;
                          }) &&
          std::ranges::any_of(running_error_cards,
                              [](std::string const& line) {
                                auto visible = strip_sgr(line);
                                return visible.find("[x]") != std::string::npos &&
                                       visible.find("write_file") != std::string::npos;
                              }) &&
          std::ranges::all_of(running_error_cards, [](std::string const& line) { return visible_columns(line) <= 60; }),
      "tui renders running/error tool cards with sanitized truncated summaries");
  expect(std::ranges::none_of(running_error_cards,
                              [](std::string const& line) { return line.find("\x1b[31m") != std::string::npos; }),
         "tui running/error tool cards remove untrusted raw sgr escape sequences");
  expect(std::ranges::any_of(
             running_error_cards,
             [](std::string const& line) { return line.find("\x1b[38;2;251;191;36m[~]") != std::string::npos; }) &&
             std::ranges::any_of(
                 running_error_cards,
                 [](std::string const& line) { return line.find("\x1b[38;2;248;113;113m[x]") != std::string::npos; }),
         "tui emits trusted sgr status colors for running and error tool cards");

  auto const detailed_tool_card = ava::tui::render_composer(ava::tui::ComposerSnapshot{
      .mode = "build",
      .provider = "openai",
      .model = "gpt-5.5",
      .session_id = "session_test",
      .input = "",
      .status = "ready",
      .transcript = {ava::tui::TranscriptItem{
          .tool = ava::tui::ToolTimelineItem{.status = ava::tui::ToolTimelineStatus::Success,
                                             .name = "bash",
                                             .argument_summary = "command=cmake --build build",
                                             .result_summary = "line one line two line three line four"}}},
      .width = 48,
      .height = 12,
      .tool_details_visible = true});
  expect(
      std::ranges::any_of(
          detailed_tool_card,
          [](std::string const& line) { return strip_sgr(line).find("args: command=cmake") != std::string::npos; }) &&
          std::ranges::any_of(
              detailed_tool_card,
              [](std::string const& line) { return strip_sgr(line).find("result: line one") != std::string::npos; }) &&
          std::ranges::all_of(detailed_tool_card, [](std::string const& line) { return visible_columns(line) <= 48; }),
      "tui expands tool cards into sanitized argument and result detail rows when details are enabled");

  auto const collapsed_override_card = ava::tui::render_composer(
      ava::tui::ComposerSnapshot{.mode = "build",
                                 .provider = "openai",
                                 .model = "gpt-5.5",
                                 .session_id = "session_test",
                                 .input = "",
                                 .status = "ready",
                                 .transcript = {ava::tui::TranscriptItem{
                                     .tool = ava::tui::ToolTimelineItem{.status = ava::tui::ToolTimelineStatus::Success,
                                                                        .name = "bash",
                                                                        .argument_summary = "command=ctest",
                                                                        .result_summary = "ok",
                                                                        .details_visible = false}}},
                                 .width = 48,
                                 .height = 10,
                                 .tool_details_visible = true});
  expect(std::ranges::none_of(
             collapsed_override_card,
             [](std::string const& line) { return strip_sgr(line).find("args: command=ctest") != std::string::npos; }),
         "tui supports per-tool detail collapse even when the global details toggle is enabled");

  auto const expanded_override_card = ava::tui::render_composer(
      ava::tui::ComposerSnapshot{.mode = "build",
                                 .provider = "openai",
                                 .model = "gpt-5.5",
                                 .session_id = "session_test",
                                 .input = "",
                                 .status = "ready",
                                 .transcript = {ava::tui::TranscriptItem{
                                     .tool = ava::tui::ToolTimelineItem{.status = ava::tui::ToolTimelineStatus::Success,
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
                             [](std::string const& line) {
                               return strip_sgr(line).find("truncation: truncated 2/10 matches") != std::string::npos;
                             }) &&
             std::ranges::any_of(expanded_override_card,
                                 [](std::string const& line) {
                                   return strip_sgr(line).find("spill: /tmp/ava-spill/grep.txt") != std::string::npos;
                                 }),
         "tui renders backend-provided truncation counts and spill paths only when present");

  auto const wide_diff_card = ava::tui::render_composer(ava::tui::ComposerSnapshot{
      .mode = "build",
      .provider = "openai",
      .model = "gpt-5.5",
      .session_id = "session_test",
      .input = "",
      .status = "ready",
      .transcript = {ava::tui::TranscriptItem{
          .tool = ava::tui::ToolTimelineItem{.status = ava::tui::ToolTimelineStatus::Success,
                                             .name = "edit_file",
                                             .argument_summary = "path=note.txt",
                                             .result_summary = "wrote 9 bytes",
                                             .details_visible = true,
                                             .diff = "--- note.txt\n+++ note.txt\n-old\n+new",
                                             .diff_truncated = true}}},
      .width = 88,
      .height = 14});
  expect(
      std::ranges::any_of(wide_diff_card,
                          [](std::string const& line) { return strip_sgr(line).find("diff:") != std::string::npos; }) &&
          std::ranges::any_of(wide_diff_card,
                              [](std::string const& line) {
                                return strip_sgr(line).find("+new") != std::string::npos &&
                                       line.find("\x1b[38;2;52;211;153m") != std::string::npos;
                              }) &&
          std::ranges::any_of(
              wide_diff_card,
              [](std::string const& line) { return strip_sgr(line).find("[diff truncated]") != std::string::npos; }),
      "tui renders backend-provided unified diff previews with mutation colors and truncation markers");

  auto const narrow_diff_card = ava::tui::render_composer(ava::tui::ComposerSnapshot{
      .mode = "build",
      .provider = "openai",
      .model = "gpt-5.5",
      .session_id = "session_test",
      .input = "",
      .status = "ready",
      .transcript = {ava::tui::TranscriptItem{
          .tool = ava::tui::ToolTimelineItem{.status = ava::tui::ToolTimelineStatus::Success,
                                             .name = "edit_file",
                                             .argument_summary = "path=very/long/path/to/note.txt",
                                             .result_summary = "wrote 9 bytes",
                                             .details_visible = true,
                                             .diff = "--- very/long/path/to/note.txt\n+++ very/long/path/to/"
                                                     "note.txt\n-old value\n+new value"}}},
      .width = 36,
      .height = 14});
  expect(
      std::ranges::any_of(narrow_diff_card,
                          [](std::string const& line) { return strip_sgr(line).find("diff:") != std::string::npos; }) &&
          std::ranges::all_of(narrow_diff_card, [](std::string const& line) { return visible_columns(line) <= 36; }),
      "tui keeps backend-provided diff previews width-safe on narrow terminals");

  auto const sidebar_frame = ava::tui::render_composer(ava::tui::ComposerSnapshot{
      .mode = "build",
      .provider = "openai",
      .model = "gpt-5.5",
      .session_id = "session_test",
      .input = "",
      .status = "ready",
      .transcript = {ava::tui::TranscriptItem{.label = "you", .text = "hello"}},
      .width = 128,
      .height = 22,
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
          .version = "0.32",
          .token_status = "1.2k (4.0%)",
          .reasoning_status = "low\x1b[31m",
          .context_source_count = 2}});
  expect(std::ranges::any_of(sidebar_frame,
                             [](std::string const& line) {
                               auto const visible = strip_sgr(line);
                               return visible.find("Activity") != std::string::npos &&
                                      visible.find("Modified Files") == std::string::npos;
                             }) &&
             std::ranges::any_of(sidebar_frame,
                                 [](std::string const& line) {
                                   auto const visible = strip_sgr(line);
                                   return visible.find("bash") != std::string::npos &&
                                          visible.find("running tests") != std::string::npos;
                                 }) &&
             std::ranges::any_of(sidebar_frame,
                                 [](std::string const& line) {
                                   auto const visible = strip_sgr(line);
                                   return visible.find("src/ava/tui/runtime.cpp") != std::string::npos &&
                                          visible.find("+12") != std::string::npos &&
                                          visible.find("-3") != std::string::npos;
                                 }) &&
             std::ranges::any_of(sidebar_frame,
                                 [](std::string const& line) {
                                   auto const visible = strip_sgr(line);
                                   return visible.find("branch develop") != std::string::npos ||
                                          visible.find("AVA 0.32") != std::string::npos;
                                 }) &&
             std::ranges::any_of(sidebar_frame,
                                 [](std::string const& line) {
                                   return strip_sgr(line).find("usage 1.2k (4.0%)") != std::string::npos;
                                 }) &&
             std::ranges::any_of(
                 sidebar_frame,
                 [](std::string const& line) { return strip_sgr(line).find("reasoning low") != std::string::npos; }) &&
             std::ranges::any_of(sidebar_frame,
                                 [](std::string const& line) {
                                   return strip_sgr(line).find("context sources 2") != std::string::npos;
                                 }) &&
             std::ranges::none_of(sidebar_frame,
                                  [](std::string const& line) { return line.find("\x1b[31m") != std::string::npos; }) &&
             std::ranges::all_of(sidebar_frame, [](std::string const& line) { return visible_columns(line) <= 128; }),
         "tui renders a sidebar with activity, modified files, session metadata, and version");
  expect(std::ranges::any_of(sidebar_frame,
                             [](std::string const& line) {
                               auto const visible = strip_sgr(line);
                               auto const activity = visible.find("Activity");
                               auto const separator = visible.find("│");
                               return activity != std::string::npos && separator != std::string::npos &&
                                      separator < activity && activity >= 90;
                             }),
         "tui pads blank main rows so sidebar content stays in the right column");

  auto const idle_after_completed_activity_frame = ava::tui::render_composer(ava::tui::ComposerSnapshot{
      .mode = "build",
      .provider = "openai",
      .model = "gpt-5.5",
      .session_id = "session_test",
      .input = "",
      .status = "ready",
      .transcript = {},
      .width = 128,
      .height = 18,
      .sidebar = ava::tui::SidebarSnapshot{
          .activity = {ava::tui::SidebarActivityItem{.id = "responding",
                                                     .label = "responding",
                                                     .detail = "assistant responded",
                                                     .status = ava::tui::ToolTimelineStatus::Success}}}});
  expect(
      std::ranges::any_of(idle_after_completed_activity_frame,
                          [](std::string const& line) { return strip_sgr(line).find("idle") != std::string::npos; }) &&
          std::ranges::none_of(
              idle_after_completed_activity_frame,
              [](std::string const& line) { return strip_sgr(line).find("assistant responded") != std::string::npos; }),
      "tui sidebar treats completed assistant activity as idle instead of persistent history");

  auto const unknown_sidebar_frame = ava::tui::render_composer(ava::tui::ComposerSnapshot{
      .mode = "build",
      .provider = "openai",
      .model = "gpt-5.5",
      .session_id = "session_test",
      .input = "",
      .status = "ready",
      .transcript = {},
      .width = 120,
      .height = 18,
      .sidebar = ava::tui::SidebarSnapshot{.session_id = "session_test", .mode = "build", .provider = "openai"}});
  expect(std::ranges::any_of(unknown_sidebar_frame,
                             [](std::string const& line) {
                               return strip_sgr(line).find("usage tokens unknown") != std::string::npos;
                             }) &&
             std::ranges::any_of(unknown_sidebar_frame,
                                 [](std::string const& line) {
                                   return strip_sgr(line).find("context sources unknown") != std::string::npos;
                                 }),
         "tui sidebar labels missing usage and context values as unknown instead of inventing numbers");

  auto const zero_context_sidebar_frame = ava::tui::render_composer(ava::tui::ComposerSnapshot{
      .mode = "build",
      .provider = "openai",
      .model = "gpt-5.5",
      .session_id = "session_test",
      .input = "",
      .status = "ready",
      .transcript = {},
      .width = 120,
      .height = 18,
      .sidebar = ava::tui::SidebarSnapshot{
          .session_id = "session_test", .mode = "build", .provider = "openai", .context_source_count = 0}});
  expect(std::ranges::any_of(
             zero_context_sidebar_frame,
             [](std::string const& line) { return strip_sgr(line).find("context sources 0") != std::string::npos; }),
         "tui sidebar distinguishes a known zero context source count from unknown context data");

  auto const narrow_no_sidebar = ava::tui::render_composer(ava::tui::ComposerSnapshot{
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
             [](std::string const& line) { return strip_sgr(line).find("sidebar-only") != std::string::npos; }),
         "tui hides the sidebar on narrow terminals");

  auto const tabbed = ava::tui::render_composer(
      ava::tui::ComposerSnapshot{.mode = "build",
                                 .provider = "openai",
                                 .model = "gpt-5.5",
                                 .session_id = "session_test",
                                 .input = "",
                                 .status = "tab\tstatus",
                                 .transcript = {ava::tui::TranscriptItem{.label = "ava", .text = "tab\ttext"}},
                                 .width = 30,
                                 .height = 8});
  expect(std::ranges::none_of(tabbed, [](std::string const& line) { return line.find('\t') != std::string::npos; }) &&
             std::ranges::all_of(tabbed, [](std::string const& line) { return visible_columns(line) <= 30; }),
         "tui expands tabs before rendering width-bounded lines");

  auto const assistant_meta = ava::tui::render_composer(ava::tui::ComposerSnapshot{
      .mode = "build",
      .provider = "openai",
      .model = "gpt-5.5",
      .session_id = "session_test",
      .input = "",
      .status = "ready",
      .transcript = {ava::tui::TranscriptItem{.label = "ava", .text = "answer", .meta = "Build - GPT-5.5"}},
      .width = 48,
      .height = 10});
  expect(std::ranges::any_of(
             assistant_meta,
             [](std::string const& line) { return strip_sgr(line).find("* Build - GPT-5.5") != std::string::npos; }) &&
             std::ranges::all_of(assistant_meta, [](std::string const& line) { return visible_columns(line) <= 48; }),
         "tui renders assistant mode/model metadata under AVA messages with ASCII markers");

  std::string exact_width_utf8_status;
  for (int index = 0; index < 12; ++index) {
    exact_width_utf8_status += "\xC3\xA9";
  }
  auto const exact_width_utf8 = ava::tui::render_composer(ava::tui::ComposerSnapshot{.mode = "build",
                                                                                     .provider = "openai",
                                                                                     .model = "gpt-5.5",
                                                                                     .session_id = "session_test",
                                                                                     .input = "",
                                                                                     .status = exact_width_utf8_status,
                                                                                     .transcript = {},
                                                                                     .width = 20,
                                                                                     .height = 8});
  expect(std::ranges::all_of(exact_width_utf8, [](std::string const& line) { return visible_columns(line) <= 20; }) &&
             std::ranges::any_of(
                 exact_width_utf8,
                 [](std::string const& line) { return strip_sgr(line).find("▎  Build") != std::string::npos; }),
         "tui width fitting preserves the AVA composer surface at minimum width");

  auto const utf8 = ava::tui::render_composer(ava::tui::ComposerSnapshot{
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
                              [](std::string const& line) {
                                return !line.empty() && (static_cast<unsigned char>(line.back()) & 0xC0U) == 0xC0U;
                              }),
         "tui truncation does not leave a trailing utf-8 starter byte");

  std::vector<ava::tui::TranscriptItem> stress_transcript;
  for (int index = 0; index < 36; ++index) {
    stress_transcript.push_back(
        ava::tui::TranscriptItem{.label = "you",
                                 .text = "resize stress user line " + std::to_string(index) +
                                         " with a very-long-token-that-must-not-overflow-or-resize-the-layout"});
    stress_transcript.push_back(ava::tui::TranscriptItem{.label = "ava",
                                                         .text = "assistant answer " + std::to_string(index) +
                                                                 " keeps CJK \xE7\x95\x8C and emoji \xF0\x9F\x98\x80 "
                                                                 "inside the measured viewport",
                                                         .meta = "Build - GPT-5.5",
                                                         .thinking = "checked resize path " + std::to_string(index)});
    if (index % 5 == 0) {
      stress_transcript.push_back(ava::tui::TranscriptItem{
          .tool = ava::tui::ToolTimelineItem{
              .status = index % 10 == 0 ? ava::tui::ToolTimelineStatus::Error : ava::tui::ToolTimelineStatus::Success,
              .name = "grep",
              .argument_summary = "pattern=needle path=src",
              .result_summary = "returned " + std::to_string(index) + " matches",
              .call_id = "call_resize_" + std::to_string(index),
              .lifecycle =
                  index % 10 == 0 ? ava::tui::ToolLifecycleState::Error : ava::tui::ToolLifecycleState::Complete,
              .truncated = true,
              .visible_matches = 2,
              .total_matches = 12,
              .spill_path = "/tmp/ava-spill/resize.txt"}});
    }
    if (index % 7 == 0) {
      stress_transcript.push_back(ava::tui::TranscriptItem{
          .label = "audit", .text = "permission replied after resize boundary " + std::to_string(index)});
    }
  }

  ava::tui::SidebarSnapshot const stress_sidebar{
      .activity = {ava::tui::SidebarActivityItem{.id = "running",
                                                 .label = "compaction",
                                                 .detail = "compaction started tokens~9000/8000",
                                                 .status = ava::tui::ToolTimelineStatus::Running},
                   ava::tui::SidebarActivityItem{.id = "done",
                                                 .label = "read_file",
                                                 .detail = "assistant responded",
                                                 .status = ava::tui::ToolTimelineStatus::Success}},
      .modified_files = {ava::tui::SidebarModifiedFile{.path = "src/ava/tui/composer.cpp", .added = 3, .removed = 1}},
      .session_id = "session_resize_stress",
      .mode = "build",
      .provider = "openai",
      .model = "gpt-5.5",
      .workspace = "/workspace",
      .git_branch = "develop",
      .version = "test",
      .token_status = "tokens unknown",
      .context_source_count = 2};
  std::vector<std::size_t> const stress_widths = {1, 20, 28, 40, 72, 111, 112, 160};
  std::vector<std::size_t> const stress_heights = {1, 8, 10, 18, 32};
  for (auto const width : stress_widths) {
    for (auto const height : stress_heights) {
      auto frame = ava::tui::render_composer(
          ava::tui::ComposerSnapshot{.mode = "build",
                                     .provider = "openai",
                                     .model = "gpt-5.5",
                                     .session_id = "session_resize_stress",
                                     .input = "draft line one\nsecond draft line with \xE7\x95\x8C",
                                     .status = "ready",
                                     .processing = true,
                                     .token_status = "tokens unknown",
                                     .reasoning_status = "thinking visible",
                                     .transcript = stress_transcript,
                                     .transcript_scroll_offset = 50,
                                     .width = width,
                                     .height = height,
                                     .input_cursor = std::string::npos,
                                     .sidebar = stress_sidebar,
                                     .tool_details_visible = true,
                                     .thinking_visible = true});
      auto const effective_width = std::max<std::size_t>(ava::tui::detail::kMinWidth, width);
      auto const effective_height = std::max<std::size_t>(ava::tui::detail::kMinHeight, height);
      expect(frame.size() == effective_height && std::ranges::all_of(frame,
                                                                     [&](std::string const& line) {
                                                                       return line.find('\n') == std::string::npos &&
                                                                              visible_columns(line) <= effective_width;
                                                                     }),
             "tui resize stress render keeps long mixed transcripts bounded at every tested viewport");
    }
  }
}

void test_tui_text_model_conversions()
{
  auto const plain = ava::tui::text_from_plain("first\r\nsecond\nthird\rfour");
  expect(ava::tui::to_plain_text(plain) == "first\nsecond\nthird\nfour" && ava::tui::validate_text(plain).has_value(),
         "tui Text plain conversion normalizes CR/LF boundaries into explicit newline runs");

  auto appended = ava::tui::Text{};
  auto ok_string = ava::tui::append_string(appended, "plain");
  ava::tui::append_newline(appended);
  auto ok_span = ava::tui::append_span(appended, "code", ava::tui::Rendition{.code = true});
  auto bad_string = ava::tui::append_string(appended, "bad\nrun");
  auto bad_span = ava::tui::append_span(appended, "bad\rrun", ava::tui::Rendition{.bold = true});
  expect(ok_string.has_value() && ok_span.has_value() && !bad_string.has_value() && !bad_span.has_value() &&
             ava::tui::to_plain_text(appended) == "plain\ncode",
         "tui Text builders reject embedded newlines inside string and span runs");

  auto invalid = ava::tui::Text{};
  invalid.runs.push_back(ava::tui::String{.text = "broken\nrun"});
  expect(!ava::tui::validate_text(invalid).has_value(), "tui Text validation catches invalid hand-built string runs");

  auto const markdown = ava::tui::text_from_markdown(
      "# Title\nSee [docs](https://example.test) and *note*.\nUse `ava` and **bold**.\n```cpp\nint main() "
      "{}\n```\n**open");
  bool saw_code = false;
  bool saw_bold = false;
  bool saw_italic = false;
  bool saw_link = false;
  for (auto const& run : markdown.runs) {
    if (auto const* span = std::get_if<ava::tui::TextSpan>(&run)) {
      saw_code = saw_code || span->rendition.code;
      saw_bold = saw_bold || span->rendition.bold;
      saw_italic = saw_italic || span->rendition.italic;
      saw_link = saw_link || (span->rendition.underline && span->rendition.color == ava::tui::TextColorRole::Accent);
    }
  }
  expect(ava::tui::validate_text(markdown).has_value() && saw_code && saw_bold && saw_italic && saw_link &&
             ava::tui::to_plain_text(markdown).find("Title") != std::string::npos &&
             ava::tui::to_plain_text(markdown).find("docs (https://example.test)") != std::string::npos &&
             ava::tui::to_plain_text(markdown).find("Use ava and bold.") != std::string::npos &&
             ava::tui::to_plain_text(markdown).find("**open") != std::string::npos,
         "tui Markdown conversion supports basic heading/link/emphasis/code/fences and leaves unsupported Markdown "
         "readable");

  auto const rendered_from_model = ava::tui::render_composer(ava::tui::ComposerSnapshot{
      .mode = "build",
      .provider = "openai",
      .model = "gpt-5.5",
      .session_id = "session_text_model",
      .input = "",
      .status = "ready",
      .transcript = {ava::tui::TranscriptItem{.label = "you",
                                              .text = "raw-user-hidden",
                                              .text_model = ava::tui::text_from_plain("model user visible")},
                     ava::tui::TranscriptItem{.label = "ava",
                                              .text = "",
                                              .text_model = ava::tui::text_from_plain("model assistant visible")},
                     ava::tui::TranscriptItem{.label = "ava",
                                              .text = "answer",
                                              .thinking = "raw-thinking-hidden",
                                              .thinking_model = ava::tui::text_from_plain("model thinking visible")},
                     ava::tui::TranscriptItem{.label = "error",
                                              .text = "raw-error-hidden",
                                              .text_model = ava::tui::text_from_plain("model error visible")}},
      .width = 80,
      .height = 22});
  std::string visible_model_text;
  for (auto const& line : rendered_from_model) {
    visible_model_text += strip_sgr(line);
    visible_model_text += '\n';
  }
  expect(visible_model_text.find("model user visible") != std::string::npos &&
             visible_model_text.find("model assistant visible") != std::string::npos &&
             visible_model_text.find("model thinking visible") != std::string::npos &&
             visible_model_text.find("model error visible") != std::string::npos &&
             visible_model_text.find("raw-user-hidden") == std::string::npos &&
             visible_model_text.find("raw-thinking-hidden") == std::string::npos &&
             visible_model_text.find("raw-error-hidden") == std::string::npos,
         "tui transcript renderer consumes Text models for plain transcript, thinking, and fallback assistant paths");
}

void test_tui_event_state_reduces_runtime_events()
{
  ava::tui::TuiEventState state;

  ava::app::RuntimeEvent user;
  user.type = ava::app::RuntimeEventType::UserMessage;
  user.text = "hello";
  ava::tui::apply_runtime_event(state, user);
  expect(state.run_status == ava::tui::TuiEventRunStatus::Running && state.transcript.size() == 1 &&
             state.transcript[0].label == "you" && state.transcript[0].text == "hello" &&
             ava::tui::to_plain_text(state.transcript[0].text_model) == "hello",
         "tui event state records user messages as completed transcript items");

  ava::app::RuntimeEvent delta;
  delta.type = ava::app::RuntimeEventType::MessageUpdate;
  delta.model_id = "gpt-5.5";
  delta.text = "hel";
  ava::tui::apply_runtime_event(state, delta);
  delta.text = "lo";
  ava::tui::apply_runtime_event(state, delta);
  auto streaming_snapshot = ava::tui::event_state_transcript_snapshot(state);
  expect(state.pending_assistant_text == "hello" && streaming_snapshot.size() == 2 &&
             streaming_snapshot[1].label == "ava" && streaming_snapshot[1].text == "hello" &&
             streaming_snapshot[1].meta == "Build - GPT-5.5" &&
             ava::tui::to_plain_text(streaming_snapshot[1].text_model) == "hello",
         "tui event state exposes pending assistant deltas and mode/model metadata in snapshots");

  ava::app::RuntimeEvent end;
  end.type = ava::app::RuntimeEventType::MessageEnd;
  ava::tui::apply_runtime_event(state, end);
  expect(state.run_status == ava::tui::TuiEventRunStatus::Completed && state.pending_assistant_text.empty() &&
             state.transcript.size() == 2 && state.transcript[1].label == "ava" &&
             state.transcript[1].text == "hello" && state.transcript[1].meta == "Build - GPT-5.5" &&
             ava::tui::to_plain_text(state.transcript[1].text_model) == "hello",
         "tui event state commits assistant deltas on message end");
  expect(!state.activity.empty() && state.activity.back().id == "responding" &&
             state.activity.back().status == ava::tui::ToolTimelineStatus::Success &&
             state.activity.back().detail == "assistant responded",
         "tui event state settles responding activity when assistant streaming ends");

  ava::tui::TuiEventState non_gpt_state;
  ava::app::RuntimeEvent non_gpt_delta;
  non_gpt_delta.type = ava::app::RuntimeEventType::MessageUpdate;
  non_gpt_delta.provider_id = "anthropic";
  non_gpt_delta.model_id = "claude-sonnet-4-5";
  non_gpt_delta.text = "hi";
  ava::tui::apply_runtime_event(non_gpt_state, non_gpt_delta);
  auto const non_gpt_snapshot = ava::tui::event_state_transcript_snapshot(non_gpt_state);
  expect(non_gpt_snapshot.size() == 1 && non_gpt_snapshot[0].meta == "Build - Claude Sonnet 4.5",
         "tui event state uses centralized model profile display labels for non-GPT assistant metadata");

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

  ava::tui::TuiEventState reasoning_state;
  ava::app::RuntimeEvent reasoning_start;
  reasoning_start.type = ava::app::RuntimeEventType::ReasoningStart;
  reasoning_start.reasoning_format = "summary";
  ava::tui::apply_runtime_event(reasoning_state, reasoning_start);
  ava::app::RuntimeEvent reasoning_delta;
  reasoning_delta.type = ava::app::RuntimeEventType::ReasoningDelta;
  reasoning_delta.text = "checking";
  ava::tui::apply_runtime_event(reasoning_state, reasoning_delta);
  reasoning_delta.text = " options";
  ava::tui::apply_runtime_event(reasoning_state, reasoning_delta);
  auto reasoning_snapshot = ava::tui::event_state_transcript_snapshot(reasoning_state);
  expect(reasoning_state.pending_reasoning_text == "checking options" && reasoning_snapshot.size() == 1 &&
             reasoning_snapshot[0].label == "ava" && reasoning_snapshot[0].thinking == "checking options" &&
             reasoning_snapshot[0].text.empty() &&
             ava::tui::to_plain_text(reasoning_snapshot[0].thinking_model) == "checking options",
         "tui event state exposes pending reasoning as part of the assistant turn");
  ava::app::RuntimeEvent reasoning_end;
  reasoning_end.type = ava::app::RuntimeEventType::ReasoningEnd;
  ava::tui::apply_runtime_event(reasoning_state, reasoning_end);
  expect(reasoning_state.pending_reasoning_text == "checking options" && reasoning_state.transcript.empty() &&
             reasoning_state.activity.size() == 1 && reasoning_state.activity[0].label == "reasoning" &&
             reasoning_state.activity[0].status == ava::tui::ToolTimelineStatus::Success,
         "tui event state keeps completed reasoning attached to the pending assistant turn");

  ava::app::RuntimeEvent reasoning_answer;
  reasoning_answer.type = ava::app::RuntimeEventType::MessageUpdate;
  reasoning_answer.model_id = "gpt-5.5";
  reasoning_answer.text = "answer";
  ava::tui::apply_runtime_event(reasoning_state, reasoning_answer);
  ava::app::RuntimeEvent reasoning_answer_end;
  reasoning_answer_end.type = ava::app::RuntimeEventType::MessageEnd;
  reasoning_answer_end.model_id = "gpt-5.5";
  ava::tui::apply_runtime_event(reasoning_state, reasoning_answer_end);
  expect(reasoning_state.pending_reasoning_text.empty() && reasoning_state.transcript.size() == 1 &&
             reasoning_state.transcript[0].label == "ava" && reasoning_state.transcript[0].text == "answer" &&
             reasoning_state.transcript[0].thinking == "checking options" &&
             ava::tui::to_plain_text(reasoning_state.transcript[0].text_model) == "answer" &&
             ava::tui::to_plain_text(reasoning_state.transcript[0].thinking_model) == "checking options",
         "tui event state commits reasoning and answer as one assistant transcript item");

  auto const thinking_render =
      ava::tui::render_composer(ava::tui::ComposerSnapshot{.mode = "build",
                                                           .provider = "openai",
                                                           .model = "gpt-5.5",
                                                           .session_id = "session_test",
                                                           .input = "",
                                                           .status = "ready",
                                                           .transcript = reasoning_state.transcript,
                                                           .width = 60,
                                                           .height = 10});
  expect(std::ranges::any_of(thinking_render,
                             [](std::string const& line) {
                               return strip_sgr(line).find("Thinking: checking options") != std::string::npos;
                             }),
         "tui renders reasoning content as an inline thinking transcript block with a stable prefix");
  expect(std::ranges::any_of(thinking_render,
                             [](std::string const& line) {
                               return strip_sgr(line).find("Thinking:") != std::string::npos &&
                                      line.find("\x1b[38;2;88;96;112m") != std::string::npos;
                             }),
         "tui renders thinking text with dim grey styling");
  expect(std::ranges::none_of(thinking_render,
                              [](std::string const& line) {
                                auto const visible = strip_sgr(line);
                                return visible.find("╭─ AVA") != std::string::npos ||
                                       visible.find("AVA:") != std::string::npos ||
                                       visible.find("╭─ You") != std::string::npos ||
                                       visible.find("You:") != std::string::npos;
                              }),
         "tui transcript role headers stay hidden for compact chat rendering");
  expect(std::ranges::none_of(
             thinking_render,
             [](std::string const& line) { return strip_sgr(line).find("╭─ Thinking") != std::string::npos; }),
         "tui thinking transcript block avoids the normal boxed message header");
  auto const hidden_thinking_render =
      ava::tui::render_composer(ava::tui::ComposerSnapshot{.mode = "build",
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
                              [](std::string const& line) {
                                return strip_sgr(line).find("Thinking: checking options") != std::string::npos;
                              }) &&
             std::ranges::any_of(
                 hidden_thinking_render,
                 [](std::string const& line) { return strip_sgr(line).find("answer") != std::string::npos; }),
         "tui thinking visibility hides inline thinking blocks without hiding assistant text");

  ava::tui::TuiEventState redacted_reasoning_state;
  ava::app::RuntimeEvent redacted_reasoning;
  redacted_reasoning.type = ava::app::RuntimeEventType::ReasoningDelta;
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
  expect(redacted_snapshot.size() == 1 && redacted_snapshot[0].thinking == "[reasoning redacted]" &&
             std::ranges::none_of(redacted_render,
                                  [](std::string const& line) {
                                    return strip_sgr(line).find("provider-private-secret") != std::string::npos;
                                  }),
         "tui event state never renders text from redacted reasoning deltas");

  ava::tui::TuiEventState audit_state;
  ava::app::RuntimeEvent permission_audit;
  permission_audit.type = ava::app::RuntimeEventType::ProviderEvent;
  permission_audit.status = "tui:permission_request";
  permission_audit.text = "permission requested: bash pwd";
  ava::tui::apply_runtime_event(audit_state, permission_audit);
  ava::app::RuntimeEvent question_audit;
  question_audit.type = ava::app::RuntimeEventType::ProviderEvent;
  question_audit.status = "tui:question_answer";
  question_audit.text = "question answered: yes";
  ava::tui::apply_runtime_event(audit_state, question_audit);
  expect(audit_state.transcript.size() == 2 && audit_state.transcript[0].label == "audit" &&
             audit_state.transcript[0].text == "permission requested: bash pwd" &&
             audit_state.transcript[1].text == "question answered: yes" && !audit_state.activity.empty(),
         "tui event state records permission and question audit markers from resolver events");

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
  assistant_final.text = "Use `ava` and **bold**";
  ava::tui::apply_runtime_event(final_state, assistant_final);
  expect(final_state.transcript.size() == 2 &&
             ava::tui::to_plain_text(final_state.transcript.back().text_model) == "Use ava and bold",
         "tui event state stores assistant Markdown as frontend-owned semantic Text");

  ava::tui::TuiEventState provider_state;
  ava::app::RuntimeEvent provider_start;
  provider_start.type = ava::app::RuntimeEventType::ProviderEvent;
  provider_start.status = "tool_call_start";
  provider_start.call_id = "provider_call_1";
  provider_start.tool_name = "read_file";
  provider_start.text = R"({"path": "README.md"})";
  ava::tui::apply_runtime_event(provider_state, provider_start);
  auto provider_snapshot = ava::tui::event_state_transcript_snapshot(provider_state);
  auto const provider_activity_id = provider_state.activity.empty() ? std::string{} : provider_state.activity[0].id;
  expect(provider_state.activity.size() == 1 && !provider_activity_id.empty() &&
             provider_state.activity[0].label == "read_file" &&
             provider_state.activity[0].detail == "provider is preparing tool call" &&
             provider_state.activity[0].status == ava::tui::ToolTimelineStatus::Running &&
             provider_state.transcript.empty() && provider_state.pending_tools.size() == 1 &&
             provider_snapshot.size() == 1 && provider_snapshot.back().tool &&
             provider_snapshot.back().tool->lifecycle == ava::tui::ToolLifecycleState::ProviderAnnounced,
         "tui event state shows provider tool-call starts as pending announced tool cards");

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
             provider_state.transcript.empty() && provider_state.pending_tools.size() == 1 &&
             provider_state.pending_tools[0].item.lifecycle == ava::tui::ToolLifecycleState::ArgumentsStreaming &&
             provider_state.pending_tools[0].item.argument_summary.find("\"partial\": true") != std::string::npos &&
             provider_snapshot.size() == 1 && provider_snapshot.back().tool,
         "tui event state keeps provider tool-call deltas on the pending tool card and preserves labels by call id");

  ava::app::RuntimeEvent provider_end = provider_delta;
  provider_end.status = "tool_call_end";
  provider_end.text = R"({"path": "README.md", "complete": true})";
  ava::tui::apply_runtime_event(provider_state, provider_end);
  provider_snapshot = ava::tui::event_state_transcript_snapshot(provider_state);
  expect(provider_state.activity.size() == 1 && provider_state.activity[0].id == provider_activity_id &&
             provider_state.activity[0].label == "read_file" &&
             provider_state.activity[0].detail == "tool call ready" &&
             provider_state.activity[0].status == ava::tui::ToolTimelineStatus::Success &&
             provider_state.transcript.empty() && provider_state.pending_tools.size() == 1 &&
             provider_state.pending_tools[0].item.lifecycle == ava::tui::ToolLifecycleState::ArgumentsComplete &&
             provider_snapshot.size() == 1 && provider_snapshot.back().tool,
         "tui event state marks provider tool-call arguments complete without settling completed transcript history");

  ava::app::RuntimeEvent provider_execution_start;
  provider_execution_start.type = ava::app::RuntimeEventType::ToolStart;
  provider_execution_start.call_id = "provider_call_1";
  provider_execution_start.tool_name = "read_file";
  provider_execution_start.text = "path=README.md";
  ava::tui::apply_runtime_event(provider_state, provider_execution_start);
  expect(provider_state.pending_tools.size() == 1 &&
             provider_state.pending_tools[0].item.lifecycle == ava::tui::ToolLifecycleState::ExecutionStarted &&
             provider_state.pending_tools[0].item.argument_summary == "path=README.md",
         "tui event state advances an announced provider tool card into execution by call id");

  ava::app::RuntimeEvent provider_execution_progress;
  provider_execution_progress.type = ava::app::RuntimeEventType::ToolProgress;
  provider_execution_progress.call_id = "provider_call_1";
  provider_execution_progress.tool_name = "read_file";
  provider_execution_progress.text = "reading file";
  ava::tui::apply_runtime_event(provider_state, provider_execution_progress);
  expect(provider_state.pending_tools.size() == 1 &&
             provider_state.pending_tools[0].item.lifecycle == ava::tui::ToolLifecycleState::Progress &&
             provider_state.pending_tools[0].item.result_summary == "reading file",
         "tui event state records partial tool progress on the pending card");

  ava::app::RuntimeEvent provider_execution_result;
  provider_execution_result.type = ava::app::RuntimeEventType::ToolResult;
  provider_execution_result.call_id = "provider_call_1";
  provider_execution_result.tool_name = "read_file";
  provider_execution_result.status = "success";
  provider_execution_result.text = "read lines 1-10/10";
  ava::tui::apply_runtime_event(provider_state, provider_execution_result);
  expect(provider_state.pending_tools.empty() && !provider_state.transcript.empty() &&
             provider_state.transcript.back().tool &&
             provider_state.transcript.back().tool->lifecycle == ava::tui::ToolLifecycleState::Complete &&
             provider_state.transcript.back().tool->argument_summary == "path=README.md",
         "tui event state settles completed tools into immutable transcript history");

  ava::tui::TuiEventState provider_without_id_state;
  ava::app::RuntimeEvent provider_without_id;
  provider_without_id.type = ava::app::RuntimeEventType::ProviderEvent;
  provider_without_id.status = "tool_call_start";
  provider_without_id.tool_name = "grep";
  ava::tui::apply_runtime_event(provider_without_id_state, provider_without_id);
  auto const provider_without_id_activity_id =
      provider_without_id_state.activity.empty() ? std::string{} : provider_without_id_state.activity[0].id;
  provider_without_id.status = "tool_call_delta";
  ava::tui::apply_runtime_event(provider_without_id_state, provider_without_id);
  provider_without_id.status = "tool_call_end";
  ava::tui::apply_runtime_event(provider_without_id_state, provider_without_id);
  expect(
      provider_without_id_state.activity.size() == 1 && !provider_without_id_activity_id.empty() &&
          provider_without_id_state.activity[0].id == provider_without_id_activity_id &&
          provider_without_id_state.activity[0].label == "grep" &&
          provider_without_id_state.activity[0].detail == "tool call ready" &&
          provider_without_id_state.activity[0].status == ava::tui::ToolTimelineStatus::Success &&
          provider_without_id_state.pending_tools.size() == 1 &&
          provider_without_id_state.pending_tools[0].item.lifecycle == ava::tui::ToolLifecycleState::ArgumentsComplete,
      "tui event state coalesces provider tool-call activity and pending cards when provider events omit call ids");

  ava::app::RuntimeEvent tool_start;
  tool_start.type = ava::app::RuntimeEventType::ToolStart;
  tool_start.call_id = "call_1";
  tool_start.tool_name = "bash";
  tool_start.text = "pwd";
  tool_start.tool_arguments_json = "{\"command\":\"pwd\"}";
  ava::tui::apply_runtime_event(state, tool_start);
  expect(state.pending_tools.size() == 1 && state.pending_tools[0].call_id == "call_1" &&
             state.pending_tools[0].item.status == ava::tui::ToolTimelineStatus::Running &&
             state.pending_tools[0].item.name == "bash" && state.pending_tools[0].item.argument_summary == "pwd" &&
             state.pending_tools[0].item.arguments_json == "{\"command\":\"pwd\"}",
         "tui event state tracks started tools by call id");

  ava::app::RuntimeEvent tool_progress;
  tool_progress.type = ava::app::RuntimeEventType::ToolProgress;
  tool_progress.call_id = "call_1";
  tool_progress.tool_name = "bash";
  tool_progress.text = "running pwd";
  tool_progress.tool_result_json = "{\"partial\":true}";
  ava::tui::apply_runtime_event(state, tool_progress);
  auto tool_snapshot = ava::tui::event_state_transcript_snapshot(state);
  expect(state.pending_tools.size() == 1 && state.pending_tools[0].item.result_summary == "running pwd" &&
             state.pending_tools[0].item.result_json == "{\"partial\":true}" && !tool_snapshot.empty() &&
             tool_snapshot.back().tool && tool_snapshot.back().tool->status == ava::tui::ToolTimelineStatus::Running,
         "tui event state updates pending tool progress and includes it in snapshots");

  ava::app::RuntimeEvent tool_result;
  tool_result.type = ava::app::RuntimeEventType::ToolResult;
  tool_result.call_id = "call_1";
  tool_result.tool_name = "bash";
  tool_result.status = "success";
  tool_result.text = "ok";
  tool_result.tool_result_json = "{\"ok\":true}";
  ava::tui::apply_runtime_event(state, tool_result);
  expect(state.pending_tools.empty() && !state.transcript.empty() && state.transcript.back().tool &&
             state.transcript.back().tool->status == ava::tui::ToolTimelineStatus::Success &&
             state.transcript.back().tool->argument_summary == "pwd" &&
             state.transcript.back().tool->result_summary == "ok" &&
             state.transcript.back().tool->arguments_json == "{\"command\":\"pwd\"}" &&
             state.transcript.back().tool->result_json == "{\"ok\":true}",
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

  ava::app::RuntimeEvent semantic_write;
  semantic_write.type = ava::app::RuntimeEventType::ToolResult;
  semantic_write.call_id = "call_semantic_write";
  semantic_write.tool_name = "edit_file";
  semantic_write.status = "success";
  semantic_write.text = "edited file";
  semantic_write.changed_paths = {"src/semantic.cpp"};
  ava::tui::apply_runtime_event(state, semantic_write);
  expect(std::ranges::any_of(state.modified_files,
                             [](ava::tui::SidebarModifiedFile const& file) { return file.path == "src/semantic.cpp"; }),
         "tui event state prefers semantic changed paths over parsing mutating tool summaries");

  ava::app::RuntimeEvent tool_error;
  tool_error.type = ava::app::RuntimeEventType::ToolResult;
  tool_error.call_id = "call_2";
  tool_error.tool_name = "read";
  tool_error.status = "error";
  tool_error.text = "denied";
  ava::tui::apply_runtime_event(state, tool_error);
  expect(state.transcript.back().tool && state.transcript.back().tool->status == ava::tui::ToolTimelineStatus::Error &&
             state.transcript.back().tool->lifecycle == ava::tui::ToolLifecycleState::Error &&
             state.transcript.back().tool->result_summary == "denied",
         "tui event state records errored tool results as error tool cards");

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
  expect(correlated_tool_state.pending_tools.size() == 1 &&
             correlated_tool_state.pending_tools[0].call_id == "corr_tool" &&
             correlated_tool_state.pending_tools[0].request_id == "request_tool" &&
             correlated_tool_state.pending_tools[0].correlation_id == "corr_tool" &&
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
  auto const correlated_tool_render = ava::tui::render_composer(
      ava::tui::ComposerSnapshot{.mode = "build",
                                 .provider = "openai",
                                 .model = "gpt-5.5",
                                 .session_id = "session_test",
                                 .input = "",
                                 .status = "ready",
                                 .transcript = ava::tui::event_state_transcript_snapshot(correlated_tool_state),
                                 .width = 120,
                                 .height = 14,
                                 .tool_details_visible = false});
  expect(
      correlated_tool_state.pending_tools.empty() && correlated_tool_state.transcript.size() == 1 &&
          correlated_tool_state.transcript[0].tool &&
          correlated_tool_state.transcript[0].tool->lifecycle == ava::tui::ToolLifecycleState::Complete &&
          correlated_tool_state.transcript[0].tool->truncated &&
          correlated_tool_state.transcript[0].tool->details_visible == true &&
          correlated_tool_state.transcript[0].tool->arguments_json == "{\"pattern\":\"needle\"}" &&
          correlated_tool_state.transcript[0].tool->result_json == "{\"ok\":true,\"matches\":2}" &&
          correlated_tool_state.transcript[0].tool->changed_paths.size() == 1 &&
          correlated_tool_state.transcript[0].tool->changed_paths[0] == "logs/output.txt" &&
          correlated_tool_state.transcript[0].tool->spill_path == "/tmp/ava-spill/grep.txt" &&
          std::ranges::any_of(correlated_tool_render,
                              [](std::string const& line) {
                                auto const visible = strip_sgr(line);
                                return visible.find("truncation: truncated lines 5-8/20; next offset 9") !=
                                           std::string::npos &&
                                       visible.find("omitted 768 bytes, 12 lines") != std::string::npos;
                              }) &&
          std::ranges::any_of(
              correlated_tool_render,
              [](std::string const& line) { return strip_sgr(line).find("[diff truncated]") != std::string::npos; }),
      "tui EventEnvelope reducer settles completed tools with backend-provided truncation, spill, diff, and per-tool "
      "detail metadata");

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

  ava::tui::TuiEventState explicit_canceled_state;
  ava::app::RuntimeEvent explicit_canceled;
  explicit_canceled.type = ava::app::RuntimeEventType::Canceled;
  explicit_canceled.text = "stopped by user";
  explicit_canceled.reason = "cancel_requested";
  ava::tui::apply_runtime_event(explicit_canceled_state, explicit_canceled);
  expect(explicit_canceled_state.run_status == ava::tui::TuiEventRunStatus::Canceled &&
             explicit_canceled_state.transcript.size() == 1 &&
             explicit_canceled_state.transcript[0].text == "stopped by user" &&
             explicit_canceled_state.activity.back().detail == "cancel_requested",
         "tui event state accepts explicit backend canceled lifecycle events");

  ava::tui::TuiEventState lifecycle_state;
  ava::app::RuntimeEvent compaction_start;
  compaction_start.type = ava::app::RuntimeEventType::CompactionStart;
  compaction_start.trigger = "auto";
  compaction_start.estimated_tokens = 9000;
  compaction_start.threshold_tokens = 8000;
  ava::tui::apply_runtime_event(lifecycle_state, compaction_start);
  expect(lifecycle_state.transcript.empty() && lifecycle_state.activity.size() == 1 &&
             lifecycle_state.activity[0].label == "compaction" &&
             lifecycle_state.activity[0].detail.find("tokens~9000/8000") != std::string::npos,
         "tui event state keeps compaction starts in status activity without inventing transcript content");
  ava::app::RuntimeEvent retry;
  retry.type = ava::app::RuntimeEventType::Retry;
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
  ava::app::RuntimeEvent retry_tick;
  retry_tick.type = ava::app::RuntimeEventType::RetryTick;
  retry_tick.reason = "context_overflow";
  retry_tick.trigger = "context_overflow";
  retry_tick.attempt = 1;
  retry_tick.max_attempts = 1;
  retry_tick.delay_ms = 250;
  retry_tick.remaining_ms = 125;
  ava::tui::apply_runtime_event(lifecycle_state, retry_tick);
  ava::app::RuntimeEvent compaction_end;
  compaction_end.type = ava::app::RuntimeEventType::CompactionEnd;
  compaction_end.trigger = "context_overflow";
  compaction_end.attempt = 1;
  compaction_end.max_attempts = 2;
  compaction_end.summary_bytes = 1234;
  ava::tui::apply_runtime_event(lifecycle_state, compaction_end);
  expect(lifecycle_state.transcript.size() == 2 && lifecycle_state.transcript[0].label == "audit" &&
             lifecycle_state.transcript[0].text.find("retrying after context_overflow") != std::string::npos &&
             lifecycle_state.transcript[0].text.find("attempt 1/1") != std::string::npos &&
             lifecycle_state.transcript[0].text.find("delay=250ms") != std::string::npos &&
             lifecycle_state.transcript[0].text.find("tokens~9000/8000") != std::string::npos &&
             lifecycle_state.transcript[0].text.find("entries=3/4") != std::string::npos &&
             lifecycle_state.transcript[1].text.find("compaction completed") != std::string::npos &&
             lifecycle_state.transcript[1].text.find("attempt 1/2") != std::string::npos &&
             lifecycle_state.transcript[1].text.find("summary=1234 bytes") != std::string::npos &&
             std::ranges::any_of(lifecycle_state.activity,
                                 [](ava::tui::SidebarActivityItem const& activity) {
                                   return activity.label == "retry" &&
                                          activity.detail.find("remaining=125ms") != std::string::npos;
                                 }),
         "tui event state renders backend retry, retry countdown, and compaction markers with backend-provided detail");

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

  std::vector<ava::app::RuntimeEvent> live_events;
  ava::app::RuntimeEvent parity_session;
  parity_session.type = ava::app::RuntimeEventType::SessionStart;
  parity_session.provider_id = "openai";
  parity_session.model_id = "gpt-5.5";
  live_events.push_back(parity_session);
  ava::app::RuntimeEvent parity_user;
  parity_user.type = ava::app::RuntimeEventType::UserMessage;
  parity_user.text = "inspect";
  live_events.push_back(parity_user);
  ava::app::RuntimeEvent parity_retry;
  parity_retry.type = ava::app::RuntimeEventType::Retry;
  parity_retry.reason = "context_overflow";
  parity_retry.trigger = "context_overflow";
  parity_retry.attempt = 1;
  parity_retry.max_attempts = 1;
  live_events.push_back(parity_retry);
  ava::app::RuntimeEvent parity_retry_tick;
  parity_retry_tick.type = ava::app::RuntimeEventType::RetryTick;
  parity_retry_tick.reason = "context_overflow";
  parity_retry_tick.trigger = "context_overflow";
  parity_retry_tick.attempt = 1;
  parity_retry_tick.max_attempts = 1;
  parity_retry_tick.delay_ms = 250;
  parity_retry_tick.remaining_ms = 0;
  live_events.push_back(parity_retry_tick);
  ava::app::RuntimeEvent parity_compaction_start;
  parity_compaction_start.type = ava::app::RuntimeEventType::CompactionStart;
  parity_compaction_start.trigger = "context_overflow";
  parity_compaction_start.attempt = 1;
  parity_compaction_start.max_attempts = 2;
  live_events.push_back(parity_compaction_start);
  ava::app::RuntimeEvent parity_compaction_end;
  parity_compaction_end.type = ava::app::RuntimeEventType::CompactionEnd;
  parity_compaction_end.trigger = "context_overflow";
  parity_compaction_end.attempt = 1;
  parity_compaction_end.max_attempts = 2;
  parity_compaction_end.summary_bytes = 512;
  live_events.push_back(parity_compaction_end);
  ava::app::RuntimeEvent parity_reasoning;
  parity_reasoning.type = ava::app::RuntimeEventType::ReasoningDelta;
  parity_reasoning.text = "checking";
  live_events.push_back(parity_reasoning);
  ava::app::RuntimeEvent parity_delta;
  parity_delta.type = ava::app::RuntimeEventType::MessageUpdate;
  parity_delta.model_id = "gpt-5.5";
  parity_delta.text = "answer";
  live_events.push_back(parity_delta);
  ava::app::RuntimeEvent parity_end;
  parity_end.type = ava::app::RuntimeEventType::MessageEnd;
  parity_end.model_id = "gpt-5.5";
  live_events.push_back(parity_end);
  ava::app::RuntimeEvent parity_tool_start;
  parity_tool_start.type = ava::app::RuntimeEventType::ToolStart;
  parity_tool_start.call_id = "call_parity";
  parity_tool_start.tool_name = "read_file";
  parity_tool_start.text = "path=README.md";
  live_events.push_back(parity_tool_start);
  ava::app::RuntimeEvent parity_tool_progress;
  parity_tool_progress.type = ava::app::RuntimeEventType::ToolProgress;
  parity_tool_progress.call_id = "call_parity";
  parity_tool_progress.tool_name = "read_file";
  parity_tool_progress.text = "reading";
  parity_tool_progress.status = "running";
  live_events.push_back(parity_tool_progress);
  ava::app::RuntimeEvent parity_tool_result;
  parity_tool_result.type = ava::app::RuntimeEventType::ToolResult;
  parity_tool_result.call_id = "call_parity";
  parity_tool_result.tool_name = "read_file";
  parity_tool_result.text = "12 bytes";
  parity_tool_result.status = "success";
  live_events.push_back(parity_tool_result);
  ava::app::RuntimeEvent parity_audit;
  parity_audit.type = ava::app::RuntimeEventType::ProviderEvent;
  parity_audit.status = "tui:question_answer";
  parity_audit.text = "question answered: yes";
  live_events.push_back(parity_audit);
  ava::app::RuntimeEvent parity_done;
  parity_done.type = ava::app::RuntimeEventType::Done;
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
  for (auto const& event : live_events) {
    ava::tui::apply_runtime_event(live_state, event);
    ava::tui::apply_event_envelope(replayed_state, ava::app::to_event_envelope(event, parity_context));
  }
  auto visible_lines = [](std::vector<std::string> const& rendered) {
    std::vector<std::string> visible;
    visible.reserve(rendered.size());
    for (auto const& line : rendered) visible.push_back(strip_sgr(line));
    return visible;
  };
  auto const live_render = visible_lines(ava::tui::render_composer(
      ava::tui::ComposerSnapshot{.mode = "build",
                                 .provider = "openai",
                                 .model = "gpt-5.5",
                                 .session_id = "session_test",
                                 .input = "",
                                 .status = "ready",
                                 .transcript = ava::tui::event_state_transcript_snapshot(live_state),
                                 .width = 72,
                                 .height = 20}));
  auto const replayed_render = visible_lines(ava::tui::render_composer(
      ava::tui::ComposerSnapshot{.mode = "build",
                                 .provider = "openai",
                                 .model = "gpt-5.5",
                                 .session_id = "session_test",
                                 .input = "",
                                 .status = "ready",
                                 .transcript = ava::tui::event_state_transcript_snapshot(replayed_state),
                                 .width = 72,
                                 .height = 20}));
  expect(live_render == replayed_render && replayed_state.active_run_id == "run_1" &&
             replayed_state.active_turn_id == "turn_1" && replayed_state.active_message_id == "message_1" &&
             replayed_state.active_request_id == "request_1" && replayed_state.active_correlation_id == "correlation_1",
         "tui EventEnvelope replay renders the same visible transcript story as live RuntimeEvent reduction and tracks "
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
             resolver_state.transcript[0].text.find("permission requested: bash pwd") != std::string::npos &&
             !resolver_state.activity.empty() && resolver_state.activity[0].label == "permission" &&
             resolver_state.active_run_id == "run_prompt",
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
  expect(resolver_state.transcript.size() == 2 && resolver_state.transcript[1].label == "audit" &&
             resolver_state.transcript[1].text == "permission replied" &&
             std::ranges::any_of(resolver_state.activity,
                                 [](ava::tui::SidebarActivityItem const& item) {
                                   return item.label == "permission" && item.detail == "permission replied";
                                 }),
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
             std::ranges::any_of(resolver_state.activity,
                                 [](ava::tui::SidebarActivityItem const& item) {
                                   return item.label == "question" &&
                                          item.detail.find("Pick an option") != std::string::npos;
                                 }),
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
  expect(resolver_state.transcript.size() == 4 && resolver_state.transcript[3].label == "audit" &&
             resolver_state.transcript[3].text == "question replied" &&
             std::ranges::any_of(resolver_state.activity,
                                 [](ava::tui::SidebarActivityItem const& item) {
                                   return item.label == "question" && item.detail == "question replied";
                                 }),
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
             resolver_state.transcript.back().text.find("Use smaller patch groups") != std::string::npos &&
             resolver_state.queued_messages.size() == 1 && resolver_state.queued_messages.back().kind == "steer" &&
             std::ranges::any_of(resolver_state.activity,
                                 [](ava::tui::SidebarActivityItem const& item) {
                                   return item.label == "steer" &&
                                          item.detail.find("Use smaller patch groups") != std::string::npos &&
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
  expect(resolver_state.transcript.size() == 6 &&
             resolver_state.transcript.back().text.find("follow-up queued") != std::string::npos &&
             resolver_state.queued_messages.size() == 2 && resolver_state.queued_messages.back().kind == "follow-up" &&
             std::ranges::any_of(resolver_state.activity,
                                 [](ava::tui::SidebarActivityItem const& item) {
                                   return item.label == "follow-up" &&
                                          item.status == ava::tui::ToolTimelineStatus::Running &&
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
  expect(resolver_state.transcript.size() == 7 &&
             resolver_state.transcript.back().text.find("follow-up started") != std::string::npos &&
             resolver_state.queued_messages.size() == 1 &&
             std::ranges::any_of(resolver_state.activity,
                                 [](ava::tui::SidebarActivityItem const& item) {
                                   return item.label == "follow-up" &&
                                          item.status == ava::tui::ToolTimelineStatus::Running &&
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
             resolver_state.transcript.back().text.find("follow-up skipped: canceled") != std::string::npos &&
             resolver_state.transcript.back().text.find("message truncated from 4096 bytes") != std::string::npos &&
             std::ranges::any_of(resolver_state.activity,
                                 [](ava::tui::SidebarActivityItem const& item) {
                                   return item.label == "follow-up" &&
                                          item.status == ava::tui::ToolTimelineStatus::Error &&
                                          item.detail.find("Continue after tests") != std::string::npos;
                                 }),
         "tui EventEnvelope reducer records backend skipped follow-up events with truncation metadata");

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
             resolver_state.transcript.back().text.find("steer=1 follow-up=2") != std::string::npos &&
             resolver_state.activity.back().label == "cancel",
         "tui EventEnvelope reducer surfaces backend cancel requests without pretending the run has finished");
}

void test_ncurses_newterm_smoke_without_real_tty()
{
  static_cast<void>(setenv("TERM", "xterm-256color", 1));
  char const* previous_locale_value = std::setlocale(LC_ALL, nullptr);
  std::string const previous_locale = previous_locale_value == nullptr ? "C" : previous_locale_value;
  static_cast<void>(std::setlocale(LC_ALL, ""));
  FILE* input = std::tmpfile();
  FILE* output = std::tmpfile();
  expect(input != nullptr && output != nullptr, "ncurses smoke test can create temporary input/output streams");
  if (!input || !output) {
    if (input) static_cast<void>(std::fclose(input));
    if (output) static_cast<void>(std::fclose(output));
    static_cast<void>(std::setlocale(LC_ALL, previous_locale.c_str()));
    return;
  }

  SCREEN* screen = newterm(nullptr, output, input);
  expect(screen != nullptr, "ncurses smoke test creates a screen without a real terminal");
  if (screen) {
    SCREEN* previous = set_term(screen);
    int rows = 0;
    int columns = 0;
    getmaxyx(stdscr, rows, columns);
    expect(rows > 0 && columns > 0, "ncurses smoke test reports a usable virtual screen size");
    static_cast<void>(resizeterm(12, 48));
    auto const ime_sensitive_input = std::string("a") + "\xE7\x95\x8C" + "e" + "\xCC\x81";
    auto const snapshot =
        ava::tui::ComposerSnapshot{.mode = "build",
                                   .provider = "openai",
                                   .model = "gpt-5.5",
                                   .session_id = "session_term",
                                   .input = ime_sensitive_input,
                                   .status = "ready",
                                   .transcript = {ava::tui::TranscriptItem{
                                       .label = "ava", .text = "virtual terminal draw keeps \xE7\x95\x8C bounded"}},
                                   .width = 48,
                                   .height = 12,
                                   .input_cursor = ime_sensitive_input.size()};
    auto const expected_column =
        ava::tui::detail::input_cursor_column(snapshot, ava::tui::composer_main_width(snapshot));
    expect(ava::tui::draw_screen(snapshot), "ncurses smoke test draws a unicode composer frame to a virtual screen");
    int cursor_y = 0;
    int cursor_x = 0;
    getyx(stdscr, cursor_y, cursor_x);
    expect(cursor_x == static_cast<int>(expected_column - 1) && cursor_y >= 0,
           "ncurses smoke test places the hardware cursor using CJK and combining-mark display columns");
    static_cast<void>(endwin());
    if (previous) static_cast<void>(set_term(previous));
    delscreen(screen);
  }

  static_cast<void>(std::fclose(input));
  static_cast<void>(std::fclose(output));
  static_cast<void>(std::setlocale(LC_ALL, previous_locale.c_str()));
}

void test_tui_large_render_performance_budget()
{
  std::vector<ava::tui::TranscriptItem> transcript;
  transcript.reserve(900);
  for (int index = 0; index < 300; ++index) {
    transcript.push_back(ava::tui::TranscriptItem{
        .label = "you",
        .text = "performance input " + std::to_string(index) +
                " with CJK \xE7\x95\x8C combining e\xCC\x81 and a very-long-token-that-must-wrap-safely"});
    transcript.push_back(ava::tui::TranscriptItem{.label = "ava",
                                                  .text = "performance answer " + std::to_string(index) +
                                                          " keeps rendered rows bounded while the transcript is large",
                                                  .meta = "Build - GPT-5.5",
                                                  .thinking = "reasoning summary " + std::to_string(index)});
    transcript.push_back(ava::tui::TranscriptItem{
        .tool = ava::tui::ToolTimelineItem{.status = ava::tui::ToolTimelineStatus::Success,
                                           .name = "read_file",
                                           .argument_summary = "path=src/ava/tui/composer.cpp",
                                           .result_summary = "read 1024 bytes",
                                           .call_id = "perf_" + std::to_string(index),
                                           .lifecycle = ava::tui::ToolLifecycleState::Complete}});
  }

  auto const start = std::chrono::steady_clock::now();
  std::vector<std::string> frame;
  for (int pass = 0; pass < 4; ++pass) {
    frame = ava::tui::render_composer(
        ava::tui::ComposerSnapshot{.mode = "build",
                                   .provider = "openai",
                                   .model = "gpt-5.5",
                                   .session_id = "session_perf",
                                   .input = "draft \xE7\x95\x8C",
                                   .status = "ready",
                                   .processing = true,
                                   .transcript = transcript,
                                   .transcript_scroll_offset = static_cast<std::size_t>(pass * 20),
                                   .width = 120,
                                   .height = 36,
                                   .input_cursor = std::string::npos,
                                   .tool_details_visible = true,
                                   .thinking_visible = true});
  }
  auto const elapsed = std::chrono::steady_clock::now() - start;
  std::size_t max_columns = 0;
  std::string widest_line;
  for (auto const& line : frame) {
    auto const columns = visible_columns(line);
    if (columns > max_columns) {
      max_columns = columns;
      widest_line = strip_sgr(line);
    }
  }
  if (max_columns > 120) {
    std::cerr << "tui large render widest line has " << max_columns << " columns: " << widest_line << '\n';
  }
  expect(frame.size() == 36, "tui large render performance frame keeps the requested height");
  expect(std::ranges::all_of(frame,
                             [](std::string const& line) {
                               return line.find('\n') == std::string::npos && visible_columns(line) <= 120;
                             }),
         "tui large render performance frame keeps every rendered line inside the requested width");
  expect(elapsed < std::chrono::seconds(5),
         "tui large render performance budget catches pathological redraw slowdowns without a real terminal");
}

}  // namespace

void run_tui_composer_tests()
{
  test_tui_composer_rendering_and_input();
  test_tui_text_model_conversions();
  test_tui_event_state_reduces_runtime_events();
  test_ncurses_newterm_smoke_without_real_tty();
  test_tui_large_render_performance_budget();
}
