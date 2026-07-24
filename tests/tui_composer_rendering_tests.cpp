#include "sys.h"
#include "tests/support/test_harness.h"
#include "ava/tui/composer.h"
#include "ava/tui/composer_internal.h"
#include "ava/tui/runtime_internal.h"
#include "ava/tui/terminal.h"
#include "ava/tui/terminal_image.h"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <iostream>
#include <iterator>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

void run_tui_composer_rendering_tests_part_1()
{
  auto const lines = ava::tui::render_composer(ava::tui::ComposerSnapshot{
      .mode = "build",
      .provider = "openai",
      .model = "gpt-5.5",
      .session_id = "session_test",
      .input = "/help",
      .status = "slash palette dismissed",
      .transcript = {ava::tui::TranscriptItem{.label = "you", .text = "hello"}, ava::tui::TranscriptItem{.label = "ava", .text = "world"}},
      .width = 80,
      .height = 14});
  {
    ScopedEnvVar no_color_guard("NO_COLOR", "1");
    auto const plain_lines = ava::tui::render_composer(ava::tui::ComposerSnapshot{
        .mode = "build",
        .provider = "openai",
        .model = "gpt-5.5",
        .session_id = "session_plain",
        .input = "/model",
        .status = "ready",
        .transcript = {ava::tui::TranscriptItem{.label = "ava", .text = "Plain output keeps **bold**, `code`, and colors out of the terminal"}},
        .select_list = ava::tui::SelectListView{.title = "Select model",
                                                .subtitle = "NO_COLOR smoke",
                                                .items = {ava::tui::SelectListItemView{.value = "openai/gpt-5.5",
                                                                                       .label = "GPT-5.5",
                                                                                       .description = "openai/gpt-5.5",
                                                                                       .group = "openai",
                                                                                       .detail = "plain terminal",
                                                                                       .badge = "current",
                                                                                       .current = true,
                                                                                       .enabled = true,
                                                                                       .disabled_reason = {}}},
                                                .selected_item_index = 0,
                                                .query = {},
                                                .placeholder = "Search models",
                                                .empty_text = "No models",
                                                .footer_hint = "Enter choose · Esc cancel"},
        .width = 88,
        .height = 18,
        .input_cursor = 6});
    expect(std::ranges::all_of(plain_lines, [](std::string const& line) { return line.find('\x1b') == std::string::npos && visible_columns(line) <= 88; }) &&
               std::ranges::any_of(plain_lines, [](std::string const& line) { return line.find("Select model") != std::string::npos; }) &&
               std::ranges::any_of(plain_lines,
                                   [](std::string const& line) { return line.find("Plain output keeps bold, code, and colors") != std::string::npos; }),
           "tui honors NO_COLOR by rendering the full frame without ANSI styling while preserving visible content and width");
  }
  expect(lines.size() == 14, "tui fills the viewport with transcript, spacer, and composer lines");
  expect(!lines.empty() && strip_sgr(lines.front()).find("hello") != std::string::npos, "tui starts short chats at the top of the transcript area");
  expect(lines.size() == 14 && strip_sgr(lines[12]).starts_with("│  /help") && strip_sgr(lines[13]).starts_with("│  GPT-5.5") &&
             lines[11].find("\x1b[48;2;26;31;46m") == std::string::npos &&
             std::ranges::none_of(lines, [](std::string const& line) { return line.find("/ commands") != std::string::npos; }),
         "tui keeps a one-line draft in exactly the bottom input and footer rows without composer-surface padding");
  expect(std::ranges::any_of(lines, [](std::string const& line) { return strip_sgr(line).find("│  /help") != std::string::npos; }),
         "tui renders the quiet composer input without a prompt glyph");
  expect(std::ranges::none_of(lines, [](std::string const& line) { return strip_sgr(line).find("slash palette dismissed") != std::string::npos; }),
         "tui keeps transient composer status text out of the footer");
  expect(std::ranges::any_of(lines,
                             [](std::string const& line) {
                               return line.find("\x1b[48;2;26;31;46m") != std::string::npos && line.find("\x1b[38;2;77;158;246m│") != std::string::npos &&
                                      strip_sgr(line).find("❯") == std::string::npos;
                             }),
         "tui preserves the elevated composer surface with one quiet accent boundary");
  expect(std::ranges::none_of(lines, [](std::string const& line) { return strip_sgr(line).find("╭─ You") != std::string::npos; }) &&
             std::ranges::none_of(lines, [](std::string const& line) { return strip_sgr(line).find("╭─ AVA") != std::string::npos; }) &&
             std::ranges::any_of(lines,
                                 [](std::string const& line) {
                                   return line.find("\x1b[38;2;77;158;246m›") != std::string::npos &&
                                          line.find("\x1b[1m\x1b[38;2;232;236;241mhello") != std::string::npos &&
                                          line.find("\x1b[48;2;26;31;46m") == std::string::npos;
                                 }) &&
             std::ranges::any_of(lines, [](std::string const& line) { return strip_sgr(line).find("world") != std::string::npos; }),
         "tui distinguishes user messages with a quiet blue chevron and bright text without a composer surface or role header");

  auto const wrapped_user_rows =
      ava::tui::detail::render_transcript_lines({ava::tui::TranscriptItem{.label = "you", .text = "one two three four five six seven eight\x1b[31m"}}, 16);
  auto const available_width_user_rows = ava::tui::detail::render_transcript_lines({ava::tui::TranscriptItem{.label = "you", .text = "abcdefghijkl"}}, 16);
  auto const empty_user_rows = ava::tui::detail::render_transcript_lines({ava::tui::TranscriptItem{.label = "you", .text = ""}}, 16);
  expect(
      wrapped_user_rows.size() > 1 &&
          std::ranges::all_of(wrapped_user_rows,
                              [](std::string const& line) { return line.find("\x1b[48;2;26;31;46m") == std::string::npos && visible_columns(line) <= 16; }) &&
          strip_sgr(wrapped_user_rows.front()).starts_with("  › ") && wrapped_user_rows.front().find("\x1b[31m") == std::string::npos &&
          std::ranges::all_of(std::next(wrapped_user_rows.begin()), wrapped_user_rows.end(),
                              [](std::string const& line) { return strip_sgr(line).starts_with("    "); }) &&
          available_width_user_rows.size() == 1 && strip_sgr(available_width_user_rows.front()) == "  › abcdefghijkl" && empty_user_rows.size() == 1 &&
          strip_sgr(empty_user_rows.front()).starts_with("  › "),
      "tui user chevron uses all available text width, sanitizes terminal escapes, preserves empty messages, aligns continuations, and never paints a composer "
      "background");
  {
    ScopedEnvVar no_color_guard("NO_COLOR", "1");
    auto const plain_user_rows = ava::tui::render_composer(
        ava::tui::ComposerSnapshot{.mode = "build",
                                   .provider = "openai",
                                   .model = "gpt-5.5",
                                   .session_id = "session_plain_user",
                                   .input = {},
                                   .status = "ready",
                                   .transcript = {ava::tui::TranscriptItem{.label = "you", .text = "plain user rendering remains identifiable"}},
                                   .width = 40,
                                   .height = 10});
    auto const plain_user_row = std::ranges::find_if(plain_user_rows, [](std::string const& line) { return line.find("plain user") != std::string::npos; });
    expect(plain_user_row != plain_user_rows.end() &&
               std::ranges::all_of(plain_user_rows,
                                   [](std::string const& line) { return line.find('\x1b') == std::string::npos && visible_columns(line) <= 40; }) &&
               plain_user_row->starts_with("  › plain user"),
           "tui user identity remains meaningful and width-bounded with NO_COLOR");
  }

  auto const idle_two_row_snapshot = ava::tui::ComposerSnapshot{.mode = "build",
                                                                .provider = "openai",
                                                                .model = "gpt-5.5",
                                                                .session_id = "session_test",
                                                                .input = "",
                                                                .status = "ready",
                                                                .context_source_count = 2,
                                                                .transcript = {},
                                                                .width = 80,
                                                                .height = 10};
  auto const idle_two_row_lines = ava::tui::render_composer(idle_two_row_snapshot);
  auto idle_input = strip_sgr(idle_two_row_lines[8]);
  auto idle_footer = strip_sgr(idle_two_row_lines[9]);
  while (!idle_input.empty() && idle_input.back() == ' ') idle_input.pop_back();
  while (!idle_footer.empty() && idle_footer.back() == ' ') idle_footer.pop_back();
  expect(idle_two_row_lines.size() == 10 && idle_input == "│  Type a message..." && idle_footer == "│  GPT-5.5 · ctx 2" &&
             idle_two_row_lines[7].find("\x1b[48;2;26;31;46m") == std::string::npos &&
             std::ranges::count_if(idle_two_row_lines, [](std::string const& line) { return line.find("\x1b[48;2;26;31;46m") != std::string::npos; }) == 2 &&
             std::ranges::none_of(idle_two_row_lines, [](std::string const& line) { return strip_sgr(line).find("❯") != std::string::npos; }),
         "tui empty composer is exactly two bottom rows with one boundary, quiet gutter, pure footer, and no prompt glyph");
  auto const composer_lines_for = [&](std::string input, std::size_t width = 80) {
    auto snapshot = idle_two_row_snapshot;
    snapshot.input = std::move(input);
    return ava::tui::detail::composer_block_line_count(snapshot, 100, width);
  };
  expect(composer_lines_for("") == 2 && composer_lines_for("one") == 2 && composer_lines_for("one\ntwo") == 3 &&
             composer_lines_for("abcdefghijklmnopqr", 20) == 3 && composer_lines_for("1\n2\n3\n4\n5\n6\n7") == 8 &&
             composer_lines_for("1\n2\n3\n4\n5\n6\n7\n8\n9") == 8,
         "tui composer desired height is visible input lines plus one footer bounded to two through eight rows");

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
  expect(processing_lines.size() == 10 && strip_sgr(processing_lines[7]).find("Esc stop · type a follow-up") != std::string::npos &&
             strip_sgr(processing_lines[8]).starts_with("│  Type a message...") && strip_sgr(processing_lines[9]).starts_with("│  GPT-5.5") &&
             strip_sgr(processing_lines[9]).find("▂") != std::string::npos && processing_lines[9].find("\x1b[38;2;77;158;246m▂") != std::string::npos &&
             processing_lines[7].find("\x1b[48;2;26;31;46m") != std::string::npos &&
             std::ranges::all_of(processing_lines,
                                 [](std::string const& line) {
                                   auto const visible = strip_sgr(line);
                                   return visible.find("thinking...") == std::string::npos && visible.find("working") == std::string::npos &&
                                          visible.find("1.3k (0.7%)") == std::string::npos && visible.find("❯") == std::string::npos;
                                 }),
         "tui processing composer adds only a contextual active-run row while the footer remains model metadata plus a calm blue indicator");
  expect(ava::tui::detail::kProcessingIndicatorFrameDelay == std::chrono::milliseconds(120) &&
             std::ranges::all_of(ava::tui::detail::kProcessingIndicatorFrames,
                                 [](std::string_view frame) {
                                   return ava::tui::detail::terminal_text_columns(frame) == 1 && frame.find("\xE2\xA0") == std::string_view::npos;
                                 }) &&
             ava::tui::detail::processing_indicator_frame(0) == "▁" && ava::tui::detail::processing_indicator_frame(6) == "▇" &&
             ava::tui::detail::processing_indicator_frame(12) == "▁" &&
             ava::tui::detail::processing_indicator_elapsed_frames(std::chrono::milliseconds(119)) == 0 &&
             ava::tui::detail::processing_indicator_elapsed_frames(std::chrono::milliseconds(120)) == 1 &&
             ava::tui::detail::processing_indicator_elapsed_frames(std::chrono::milliseconds(365)) == 3,
         "tui processing indicator has a shared 120ms one-cell lower-block sequence and advances by elapsed intervals independent of redraws");

  auto narrow_footer_snapshot = ava::tui::ComposerSnapshot{.mode = "build",
                                                           .provider = "openai",
                                                           .model = "gpt-5.6-terra",
                                                           .session_id = "session_test",
                                                           .input = "",
                                                           .status = "ready",
                                                           .context_source_count = 12,
                                                           .transcript = {},
                                                           .width = 20,
                                                           .height = 8};
  auto const narrow_footer_lines = ava::tui::render_composer(narrow_footer_snapshot);
  expect(std::ranges::any_of(narrow_footer_lines,
                             [](std::string const& line) {
                               auto const visible = strip_sgr(line);
                               return visible.find("GPT-") != std::string::npos && visible.find("ctx 12") != std::string::npos && visible_columns(line) == 20;
                             }),
         "tui shortens a long model label before dropping multi-digit context metadata at the supported minimum width");

  narrow_footer_snapshot.status = "thinking...";
  narrow_footer_snapshot.processing = true;
  narrow_footer_snapshot.spinner_frame = 1;
  auto const narrow_processing_lines = ava::tui::render_composer(narrow_footer_snapshot);
  expect(std::ranges::any_of(narrow_processing_lines,
                             [](std::string const& line) {
                               auto const visible = strip_sgr(line);
                               return visible.find("GPT-") != std::string::npos && visible.find("ctx 12") != std::string::npos &&
                                      visible.find("▂") != std::string::npos && line.find("\x1b[38;2;77;158;246m▂") != std::string::npos &&
                                      visible_columns(line) == 20;
                             }),
         "tui preserves multi-digit context metadata and the blue breathing indicator beside a shortened model at the supported minimum width");

  auto expect_direct_footer_matches_frame = [&](ava::tui::ComposerSnapshot footer_snapshot, std::string const& layout_name) {
    footer_snapshot.processing = true;
    auto const canvas = ava::tui::composer_canvas_layout(footer_snapshot);
    auto const composer_lines = ava::tui::detail::composer_block_line_count(footer_snapshot, footer_snapshot.height, canvas.content_width);
    auto const direct_footer = ava::tui::detail::render_composer_footer_line(footer_snapshot, canvas.content_width);
    auto const input_footer = ava::tui::detail::render_composer_block(footer_snapshot, canvas.content_width, composer_lines).back();
    auto const full_footer = ava::tui::render_composer_frame(footer_snapshot).lines.back();
    expect(direct_footer == input_footer && full_footer.find(direct_footer) != std::string::npos,
           "tui direct footer renderer matches the full composer footer for " + layout_name);
  };
  expect_direct_footer_matches_frame(idle_two_row_snapshot, "ordinary canvas");
  auto centered_footer_snapshot = idle_two_row_snapshot;
  centered_footer_snapshot.width = 160;
  centered_footer_snapshot.height = 14;
  expect_direct_footer_matches_frame(centered_footer_snapshot, "centered canvas");
  auto rail_footer_snapshot = centered_footer_snapshot;
  rail_footer_snapshot.width = 176;
  rail_footer_snapshot.height = 16;
  rail_footer_snapshot.sidebar = ava::tui::SidebarSnapshot{
      .activity = {ava::tui::SidebarActivityItem{.id = "running", .label = "run", .detail = "active", .status = ava::tui::ToolTimelineStatus::Running}}};
  expect_direct_footer_matches_frame(rail_footer_snapshot, "automatic sidebar rail");
  expect_direct_footer_matches_frame(narrow_footer_snapshot, "narrow canvas");

  auto const queued_lines = ava::tui::render_composer(
      ava::tui::ComposerSnapshot{.mode = "build",
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
                                          visible.find("/restore or dequeue latest") != std::string::npos;
                                 }),
         "tui renders active-run queued steering/follow-up messages in a compact pending region");

  auto const attachment_lines = ava::tui::render_composer(
      ava::tui::ComposerSnapshot{.mode = "build",
                                 .provider = "openai",
                                 .model = "gpt-5.5",
                                 .session_id = "session_test",
                                 .input = "",
                                 .status = "attached image for next prompt",
                                 .transcript = {ava::tui::TranscriptItem{.label = "status", .text = "attached image"}},
                                 .width = 80,
                                 .height = 12,
                                 .pending_attachments = {ava::tui::PendingAttachmentItem{.label = "screen.png", .detail = "(image/png, 68 bytes)"}}});
  expect(std::ranges::any_of(attachment_lines,
                             [](std::string const& line) {
                               auto const visible = strip_sgr(line);
                               return visible.find("attached image screen.png") != std::string::npos &&
                                      visible.find("(image/png, 68 bytes)") != std::string::npos && visible.find("(next prompt)") != std::string::npos;
                             }),
         "tui renders pending image attachments before the next prompt is submitted");
  auto const attachment_preview_snapshot = ava::tui::ComposerSnapshot{
      .mode = "build",
      .provider = "openai",
      .model = "gpt-5.5",
      .session_id = "session_test",
      .input = "",
      .status = "attached image for next prompt",
      .transcript = {ava::tui::TranscriptItem{.label = "status", .text = "attached image"}},
      .width = 80,
      .height = 14,
      .pending_attachments = {ava::tui::PendingAttachmentItem{
          .label = "screen.png",
          .detail = "(image/png, 68 bytes) preview kitty",
          .preview = ava::tui::PendingAttachmentItem::Preview{.protocol = ava::tui::TerminalImageProtocol::Kitty,
                                                              .base64_data = std::make_shared<std::string const>("AAAA"),
                                                              .dimensions = ava::tui::ImageDimensions{.width_px = 20, .height_px = 20},
                                                              .image_id = 42}}}};
  auto const attachment_preview_frame = ava::tui::render_composer_frame(attachment_preview_snapshot);
  auto const attachment_preview_lines = ava::tui::render_composer(attachment_preview_snapshot);
  expect(attachment_preview_frame.graphics.size() == 1 && attachment_preview_frame.graphics[0].protocol == ava::tui::TerminalImageProtocol::Kitty &&
             attachment_preview_frame.graphics[0].image_id == std::optional<std::size_t>{42} && attachment_preview_frame.graphics[0].rows > 1 &&
             attachment_preview_frame.graphics[0].sequence.starts_with("\x1b_G") &&
             attachment_preview_frame.graphics[0].sequence.find("C=1") != std::string::npos &&
             std::ranges::none_of(attachment_preview_lines, [](std::string const& line) { return ava::tui::terminal_line_contains_image_sequence(line); }),
         "tui render frame reserves rows and carries trusted Kitty image graphics outside the text-only line API");
  auto centered_attachment_preview_snapshot = attachment_preview_snapshot;
  centered_attachment_preview_snapshot.width = 160;
  auto const centered_attachment_preview_frame = ava::tui::render_composer_frame(centered_attachment_preview_snapshot);
  expect(centered_attachment_preview_frame.graphics.size() == 1 &&
             centered_attachment_preview_frame.graphics.front().column == attachment_preview_frame.graphics.front().column + 20 &&
             centered_attachment_preview_frame.graphics.front().row == attachment_preview_frame.graphics.front().row &&
             std::ranges::all_of(centered_attachment_preview_frame.lines, [](std::string const& line) { return visible_columns(line) == 160; }),
         "tui centered canvas shifts each terminal graphic overlay by the physical inset exactly once");
  {
    ScopedEnvVar no_color_preview_guard("NO_COLOR", "1");
    auto const plain_attachment_preview_frame = ava::tui::render_composer_frame(attachment_preview_snapshot);
    expect(plain_attachment_preview_frame.graphics.empty() && std::ranges::none_of(plain_attachment_preview_frame.lines,
                                                                                   [](std::string const& line) {
                                                                                     return line.find("\x1b[") != std::string::npos ||
                                                                                            ava::tui::terminal_line_contains_image_sequence(line);
                                                                                   }),
           "plain TUI output keeps image previews on the textual fallback path without ANSI or graphics escapes");
  }

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
                               return visible.find("GPT-5.5") != std::string::npos && visible.find("Build") == std::string::npos &&
                                      visible.find("OpenAI") == std::string::npos && visible.find("low") == std::string::npos;
                             }),
         "tui keeps mode, provider, and reasoning level out of the composer footer");

  auto const default_reasoning_lines = ava::tui::render_composer(ava::tui::ComposerSnapshot{.mode = "build",
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
                               return visible.find("GPT-5.5") != std::string::npos && visible.find("Build") == std::string::npos &&
                                      visible.find("OpenAI") == std::string::npos && visible.find("default") == std::string::npos;
                             }),
         "tui renders only the model when context metadata is unavailable");
  expect(
      std::ranges::none_of(default_reasoning_lines, [](std::string const& line) { return strip_sgr(line).find("session session_test") != std::string::npos; }),
      "tui keeps the session id out of the composer footer");

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
                               auto const visible = strip_sgr(line);
                               return visible.find("GPT-5.5") != std::string::npos && visible.find("Build") == std::string::npos;
                             }) &&
             std::ranges::any_of(plan_mode_lines,
                                 [](std::string const& line) {
                                   auto const visible = strip_sgr(line);
                                   return visible.find("GPT-5.5") != std::string::npos && visible.find("Plan") == std::string::npos;
                                 }),
         "tui keeps build and plan mode badges out of the composer footer");

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
  expect(std::ranges::none_of(token_margin_lines, [](std::string const& line) { return strip_sgr(line).find("1.3k (0.7%)") != std::string::npos; }),
         "tui keeps token-status text out of the composer footer");

  auto const compact_footer_lines = ava::tui::render_composer(ava::tui::ComposerSnapshot{.mode = "build",
                                                                                         .provider = "openai",
                                                                                         .model = "gpt-5.5",
                                                                                         .session_id = "session_test",
                                                                                         .input = "",
                                                                                         .status = "ready",
                                                                                         .token_status = "1.3k (0.7%)",
                                                                                         .transcript = {},
                                                                                         .width = 110,
                                                                                         .height = 10,
                                                                                         .sidebar = ava::tui::SidebarSnapshot{.session_id = "session_test",
                                                                                                                              .mode = "build",
                                                                                                                              .provider = "openai",
                                                                                                                              .model = "gpt-5.5",
                                                                                                                              .workspace = "/workspace/project",
                                                                                                                              .git_branch = "develop",
                                                                                                                              .token_status = "1.3k (0.7%)",
                                                                                                                              .context_source_count = 2,
                                                                                                                              .session_entry_count = 42}});
  expect(std::ranges::any_of(compact_footer_lines,
                             [](std::string const& line) {
                               auto const visible = strip_sgr(line);
                               return visible.find("GPT-5.5 · ctx 2") != std::string::npos && visible.find("Build") == std::string::npos &&
                                      visible.find("OpenAI") == std::string::npos && visible.find("cwd") == std::string::npos &&
                                      visible.find("git") == std::string::npos && visible.find("entries") == std::string::npos &&
                                      visible.find("1.3k (0.7%)") == std::string::npos;
                             }),
         "tui compact footer shows only the model name and context source count");

  auto const refreshed_context_lines = ava::tui::render_composer(ava::tui::ComposerSnapshot{.mode = "build",
                                                                                            .provider = "openai",
                                                                                            .model = "gpt-5.5",
                                                                                            .session_id = "session_test",
                                                                                            .input = "",
                                                                                            .status = "ready",
                                                                                            .context_source_count = 3,
                                                                                            .transcript = {},
                                                                                            .width = 110,
                                                                                            .height = 10,
                                                                                            .sidebar = ava::tui::SidebarSnapshot{.context_source_count = 2}});
  expect(std::ranges::any_of(refreshed_context_lines,
                             [](std::string const& line) {
                               auto const visible = strip_sgr(line);
                               return visible.find("GPT-5.5 · ctx 3") != std::string::npos && visible.find("ctx 2") == std::string::npos;
                             }),
         "tui composer footer prefers refreshed runtime context count over stale sidebar metadata");
}
void run_tui_composer_rendering_tests_part_2()
{
  auto const rows_transcript = ava::tui::render_composer(ava::tui::ComposerSnapshot{
      .mode = "build",
      .provider = "openai",
      .model = "gpt-5.5",
      .session_id = "session_test",
      .input = "",
      .status = "ready",
      .transcript = {ava::tui::TranscriptItem{.label = "error", .text = "bad command"}, ava::tui::TranscriptItem{.label = "command", .text = "/help"}},
      .width = 50,
      .height = 10});
  expect(std::ranges::any_of(rows_transcript, [](std::string const& line) { return strip_sgr(line).find("! bad command") != std::string::npos; }) &&
             std::ranges::any_of(rows_transcript, [](std::string const& line) { return strip_sgr(line).find("· /help") != std::string::npos; }),
         "tui keeps errors and generic command rows distinct from message blocks");
  auto const onboarding_transcript =
      ava::tui::render_composer(ava::tui::ComposerSnapshot{.mode = "build",
                                                           .provider = "openai",
                                                           .model = "gpt-5.5",
                                                           .session_id = "session_test",
                                                           .input = "",
                                                           .status = "ready",
                                                           .transcript = {ava::tui::TranscriptItem{.label = "setup",
                                                                                                   .text = "Provider auth is not configured for `openai`.\n"
                                                                                                           "Connect with /connect or /login in this TUI.\n"
                                                                                                           "Auth file: /tmp/ava/auth.json"}},
                                                           .width = 72,
                                                           .height = 12});
  expect(std::ranges::any_of(onboarding_transcript,
                             [](std::string const& line) { return strip_sgr(line).find("Provider auth is not configured") != std::string::npos; }) &&
             std::ranges::any_of(onboarding_transcript,
                                 [](std::string const& line) { return strip_sgr(line).find("Connect with /connect or /login") != std::string::npos; }) &&
             std::ranges::all_of(onboarding_transcript, [](std::string const& line) { return visible_columns(line) <= 72; }),
         "tui renders first-run setup transcript guidance without width overflow");
  auto const disconnected = std::vector<ava::tui::TranscriptItem>{ava::tui::TranscriptItem{.label = "setup", .text = "! OpenAI not connected · /connect"}};
  auto const disconnected_styled = ava::tui::detail::render_transcript_lines(disconnected, 72, false, true, false);
  std::vector<std::string> disconnected_plain;
  {
    ScopedEnvVar no_color_guard("NO_COLOR", "1");
    disconnected_plain = ava::tui::detail::render_transcript_lines(disconnected, 72, false, true, false);
  }
  auto const disconnected_visible = disconnected_styled.empty() ? std::string{} : strip_sgr(disconnected_styled.front());
  expect(disconnected_styled.size() == 1 && disconnected_plain.size() == 1 && disconnected_plain.front() == disconnected_visible &&
             disconnected_visible.find("· !") == std::string::npos && disconnected_visible.find("! OpenAI not connected · /connect") != std::string::npos,
         "tui disconnected startup guidance renders one logical warning marker with styled/plain parity");

  auto const compact_status = ava::tui::render_composer(
      ava::tui::ComposerSnapshot{.mode = "build",
                                 .provider = "openai",
                                 .model = "gpt-5.5",
                                 .session_id = "session_test",
                                 .input = "",
                                 .status = "Enter submits. Shift/Ctrl+Enter inserts newline. Alt+Enter queues follow-up. / opens commands.",
                                 .transcript = {},
                                 .width = 120,
                                 .height = 8});
  expect(std::ranges::none_of(compact_status, [](std::string const& line) { return strip_sgr(line).find("Alt+Enter queues follow-up") != std::string::npos; }),
         "tui keeps the composer status compact instead of rendering verbose help");
  auto const status_alert = ava::tui::render_composer(
      ava::tui::ComposerSnapshot{.mode = "build",
                                 .provider = "openai",
                                 .model = "gpt-5.5",
                                 .session_id = "session_test",
                                 .input = "",
                                 .status = "invalid_argument: conflicting TUI keybinding\n  key: Ctrl+P\n  action: model_cycle_forward\x1b[31m",
                                 .transcript = {},
                                 .width = 96,
                                 .height = 10});
  expect(
      std::ranges::any_of(
          status_alert, [](std::string const& line) { return strip_sgr(line).find("! invalid_argument: conflicting TUI keybinding") != std::string::npos; }) &&
          std::ranges::any_of(status_alert, [](std::string const& line) { return strip_sgr(line).find("key: Ctrl+P") != std::string::npos; }) &&
          std::ranges::none_of(status_alert, [](std::string const& line) { return line.find("\x1b[31m") != std::string::npos; }) &&
          std::ranges::all_of(status_alert, [](std::string const& line) { return visible_columns(line) <= 96; }),
      "tui renders error-category status strings as compact sanitized alerts above the composer");

  auto const disabled_statuses =
      std::vector<std::string>{"command disabled: cloud sharing is deferred", "reference disabled: outside workspace", "path disabled: outside workspace"};
  auto const disabled_status_alerts_visible = std::ranges::all_of(disabled_statuses, [](std::string const& status) {
    auto const frame = ava::tui::render_composer(ava::tui::ComposerSnapshot{.mode = "build",
                                                                            .provider = "openai",
                                                                            .model = "gpt-5.5",
                                                                            .session_id = "session_test",
                                                                            .input = "/share",
                                                                            .status = status,
                                                                            .transcript = {},
                                                                            .width = 96,
                                                                            .height = 10});
    return std::ranges::any_of(frame, [&status](std::string const& line) { return strip_sgr(line).find("! " + status) != std::string::npos; });
  });
  expect(disabled_status_alerts_visible, "tui renders command, reference, and path disabled statuses as compact dock-aware alerts");

  auto const prioritized_status_alert = ava::tui::render_composer(ava::tui::ComposerSnapshot{
      .mode = "build",
      .provider = "openai",
      .model = "gpt-5.5",
      .session_id = "session_test",
      .input = "",
      .status = "invalid_argument: first alert line\nsecond alert line\nthird alert line\nfourth alert line",
      .transcript = {},
      .width = 96,
      .height = 9,
      .queued_messages = {ava::tui::QueuedMessageItem{.id = "q1", .kind = "follow-up", .text = "queue one"},
                          ava::tui::QueuedMessageItem{.id = "q2", .kind = "follow-up", .text = "queue two"},
                          ava::tui::QueuedMessageItem{.id = "q3", .kind = "follow-up", .text = "queue three"}},
      .pending_attachments = {ava::tui::PendingAttachmentItem{.label = "first.png"}, ava::tui::PendingAttachmentItem{.label = "second.png"}}});
  expect(prioritized_status_alert.size() == 9 && strip_sgr(prioritized_status_alert[0]).find("queued follow-up queue one") != std::string::npos &&
             strip_sgr(prioritized_status_alert[2]).find("queued follow-up queue three") != std::string::npos &&
             strip_sgr(prioritized_status_alert[3]).find("attached +1 more images") != std::string::npos &&
             strip_sgr(prioritized_status_alert[4]).find("! invalid_argument: first alert line") != std::string::npos &&
             strip_sgr(prioritized_status_alert[5]).find("second alert line") != std::string::npos &&
             strip_sgr(prioritized_status_alert[6]).find("third alert line ...") != std::string::npos &&
             strip_sgr(prioritized_status_alert[7]).starts_with("│  Type a message...") && strip_sgr(prioritized_status_alert[8]).starts_with("│  GPT-5.5"),
         "tui reserves a three-line status alert before queue and attachment budgets and renders it immediately above the two-row composer");

  auto const minimum_width = ava::tui::render_composer(ava::tui::ComposerSnapshot{.mode = "build",
                                                                                  .provider = "openai",
                                                                                  .model = "gpt-5.5",
                                                                                  .session_id = "session_test",
                                                                                  .input = "hello",
                                                                                  .status = "ready",
                                                                                  .transcript = {},
                                                                                  .width = 1,
                                                                                  .height = 1});
  expect(std::ranges::all_of(minimum_width, [](std::string const& line) { return line.find('\n') == std::string::npos && visible_columns(line) <= 20; }) &&
             std::ranges::any_of(minimum_width, [](std::string const& line) { return strip_sgr(line).find("│  hello") != std::string::npos; }),
         "tui clamps normal composer rendering to the minimum viewport");
}
void run_tui_composer_rendering_tests_part_3()
{
  auto const sanitized =
      ava::tui::render_composer(ava::tui::ComposerSnapshot{.mode = "build",
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
  expect(std::ranges::any_of(sanitized_input, [](std::string const& line) { return strip_sgr(line).find("│  bad?[31mred") != std::string::npos; }),
         "tui render sanitizes composer input escape bytes");
  expect(ava::tui::sanitize_terminal_text(std::string("osc") + static_cast<char>(0x9D) + "payload") == "osc?payload",
         "tui sanitizes raw c1 terminal control bytes");
  expect(ava::tui::sanitize_terminal_text("a\tb") == "a  b", "tui expands tabs before width accounting");
  expect(ava::tui::sanitize_terminal_text(std::string("ok ") + "\xC3\xA9") == std::string("ok ") + "\xC3\xA9", "tui sanitizer preserves valid utf-8 text");
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
  expect(ava::tui::detail::terminal_text_columns("\xE7\x95\x8C") == 2 && ava::tui::detail::terminal_text_columns(std::string("e") + "\xCC\x81") == 1 &&
             ava::tui::detail::terminal_text_columns(std::string("a") + "\xE2\x80\x8D" + "b") == 2 &&
             ava::tui::detail::terminal_text_columns(std::string("\xE2\x98\xBA") + "\xEF\xB8\x8F") >= 1,
         "tui width accounting handles CJK width and treats combining marks, zero-width joiners, and variation "
         "selectors as non-advancing");
  auto const regional_c = std::string("\xF0\x9F\x87\xA8");
  auto const regional_n = std::string("\xF0\x9F\x87\xB3");
  auto const thumbs_up = std::string("\xF0\x9F\x91\x8D");
  auto const light_skin_tone = std::string("\xF0\x9F\x8F\xBB");
  auto const man = std::string("\xF0\x9F\x91\xA8");
  auto const laptop = std::string("\xF0\x9F\x92\xBB");
  auto const zwj = std::string("\xE2\x80\x8D");
  auto const check_mark = std::string("\xE2\x9C\x85");
  auto const lightning = std::string("\xE2\x9A\xA1");
  auto const variation_16 = std::string("\xEF\xB8\x8F");
  auto const white_flag = std::string("\xF0\x9F\x8F\xB3");
  auto const rainbow = std::string("\xF0\x9F\x8C\x88");
  expect(ava::tui::detail::terminal_text_columns(regional_c) == 2 && ava::tui::detail::terminal_text_columns(regional_c + regional_n) == 2 &&
             ava::tui::detail::terminal_text_columns("      - " + regional_c) == 10 &&
             ava::tui::detail::terminal_text_columns(thumbs_up + light_skin_tone) == 2 && ava::tui::detail::terminal_text_columns(man + zwj + laptop) == 2 &&
             ava::tui::detail::terminal_text_columns(check_mark) == 2 && ava::tui::detail::terminal_text_columns(lightning) == 2 &&
             ava::tui::detail::terminal_text_columns(lightning + variation_16) == 2 &&
             ava::tui::detail::terminal_text_columns(white_flag + variation_16 + zwj + rainbow) == 2,
         "tui width accounting treats Pi-style regional indicators and emoji modifier/ZWJ clusters as stable wide cells");
  auto const partial_flag_wrap = ava::tui::detail::wrap_transcript_text("      - " + regional_c, 13);
  expect(partial_flag_wrap.size() == 2 && ava::tui::detail::terminal_text_columns(partial_flag_wrap[0]) <= 9 &&
             ava::tui::detail::terminal_text_columns(partial_flag_wrap[1]) == 2,
         "tui transcript wrapping breaks Pi-style partial-flag list lines before terminal overflow");
  auto const clipped_regional_indicator = ava::tui::detail::fit_line("x" + regional_c + "y", 2);
  expect(ava::tui::detail::terminal_text_columns(clipped_regional_indicator) <= 2, "tui narrow fitting does not undercount singleton regional indicators");
  auto const clipped_zwj_cluster = ava::tui::detail::fit_line(man + zwj + laptop + "x", 2);
  expect(clipped_zwj_cluster == man + zwj + laptop, "tui narrow fitting keeps emoji ZWJ clusters intact when they fit exactly");
  expect(ava::tui::detail::composer_input_prefix_columns(true) == 3 && ava::tui::detail::composer_input_prefix_columns(false) == 3,
         "tui composer input and continuation rows share one three-column boundary and gutter");
  auto const cursor_base = ava::tui::detail::composer_input_prefix_columns(true) + 1;
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
  expect(cursor_for(cursor_text, 1) == cursor_base + 1 && cursor_for(cursor_text, 4) == cursor_base + 3 && cursor_for(cursor_text, 5) == cursor_base + 4 &&
             cursor_for(cursor_text, cursor_text.size()) == cursor_base + 4 && cursor_for(std::string("x") + std::string("\xC0\x80", 2), 3) == cursor_base + 3,
         "tui composer cursor placement uses sanitized display columns for CJK, combining marks, and invalid utf-8");
  auto const wrapped_input = ava::tui::detail::input_render_line_spans("alpha beta gamma delta", 20);
  expect(wrapped_input.size() == 2 && wrapped_input[0].text == "alpha beta gamma " && wrapped_input[0].start == 0 &&
             wrapped_input[0].end == std::string("alpha beta gamma ").size() && wrapped_input[0].first_line && wrapped_input[1].text == "delta" &&
             wrapped_input[1].start == std::string("alpha beta gamma ").size() && !wrapped_input[1].first_line,
         "tui composer wraps long input at word boundaries while preserving source offsets");
  auto const wrapped_long_word = ava::tui::detail::input_render_line_spans("abcdefghijklmnopqr", 20);
  expect(wrapped_long_word.size() == 2 && wrapped_long_word[0].text == "abcdefghijklmnopq" && wrapped_long_word[1].text == "r",
         "tui composer falls back to cell-level wrapping for long unbroken input tokens");
  auto const cjk_wrap_text = std::string("\xE7\x95\x8C\xE7\x95\x8C\xE7\x95\x8C\xE7\x95\x8C\xE7\x95\x8C\xE7\x95\x8C\xE7\x95\x8C\xE7\x95\x8C\xE7\x95\x8C");
  auto const wrapped_cjk_input = ava::tui::detail::input_render_line_spans(cjk_wrap_text, 20);
  expect(wrapped_cjk_input.size() == 2 && ava::tui::detail::terminal_text_columns(wrapped_cjk_input[0].text) == 16 &&
             ava::tui::detail::terminal_text_columns(wrapped_cjk_input[1].text) == 2,
         "tui composer wraps CJK input on full UTF-8 cell boundaries");
  auto const wrapped_cursor_snapshot = ava::tui::ComposerSnapshot{.mode = "build",
                                                                  .provider = "openai",
                                                                  .model = "gpt-5.5",
                                                                  .session_id = "session_test",
                                                                  .input = "alpha beta gamma delta",
                                                                  .status = "ready",
                                                                  .transcript = {},
                                                                  .width = 20,
                                                                  .height = 8,
                                                                  .input_cursor = std::string::npos};
  expect(ava::tui::detail::input_cursor_line(wrapped_cursor_snapshot, 20) == 1 &&
             ava::tui::detail::input_cursor_column(wrapped_cursor_snapshot, 20) ==
                 ava::tui::detail::composer_input_prefix_columns(false) + std::string("delta").size() + 1,
         "tui composer places the cursor on the wrapped continuation row");
  auto const wrapped_render = ava::tui::render_composer(wrapped_cursor_snapshot);
  expect(std::ranges::any_of(wrapped_render, [](std::string const& line) { return strip_sgr(line).find("│  alpha beta gamma ") != std::string::npos; }) &&
             std::ranges::any_of(wrapped_render, [](std::string const& line) { return strip_sgr(line).find("│  delta") != std::string::npos; }),
         "tui composer renders wrapped input as visible continuation rows");
  auto const wrapped_click =
      ava::tui::composer_input_cursor_for_screen_position(wrapped_cursor_snapshot, 7, ava::tui::detail::composer_input_prefix_columns(false) + 3);
  expect(wrapped_click && *wrapped_click == std::string("alpha beta gamma de").size(),
         "tui composer hit-tests wrapped input continuation rows to source cursor offsets");
  auto const click_cursor_snapshot = ava::tui::ComposerSnapshot{.mode = "build",
                                                                .provider = "openai",
                                                                .model = "gpt-5.5",
                                                                .session_id = "session_test",
                                                                .input = "alpha beta",
                                                                .status = "ready",
                                                                .transcript = {},
                                                                .width = 80,
                                                                .height = 8};
  auto const clicked_after_alpha = ava::tui::composer_input_cursor_for_screen_position(click_cursor_snapshot, 7, cursor_base + std::string("alpha ").size());
  expect(clicked_after_alpha && *clicked_after_alpha == std::string("alpha ").size() &&
             !ava::tui::composer_input_cursor_for_screen_position(click_cursor_snapshot, 1, cursor_base) &&
             !ava::tui::composer_input_cursor_for_screen_position(click_cursor_snapshot, 8, cursor_base),
         "tui composer hit-tests visible input rows to draft cursor byte offsets and ignores non-input rows");
  auto centered_click_snapshot = click_cursor_snapshot;
  centered_click_snapshot.width = 160;
  auto const centered_clicked_after_alpha =
      ava::tui::composer_input_cursor_for_screen_position(centered_click_snapshot, 7, 20 + cursor_base + std::string("alpha ").size());
  expect(centered_clicked_after_alpha && *centered_clicked_after_alpha == std::string("alpha ").size() &&
             !ava::tui::composer_input_cursor_for_screen_position(centered_click_snapshot, 7, 20) &&
             ava::tui::composer_input_cursor_for_screen_position(centered_click_snapshot, 7, 21).has_value() &&
             ava::tui::composer_input_cursor_for_screen_position(centered_click_snapshot, 7, 140).has_value() &&
             !ava::tui::composer_input_cursor_for_screen_position(centered_click_snapshot, 7, 141),
         "tui centered composer click maps physical columns locally, accepts both canvas edges, and rejects both exact gutters");
  auto const multiline_click_snapshot = ava::tui::ComposerSnapshot{.mode = "build",
                                                                   .provider = "openai",
                                                                   .model = "gpt-5.5",
                                                                   .session_id = "session_test",
                                                                   .input = "one\ntwo\nthree",
                                                                   .status = "ready",
                                                                   .transcript = {},
                                                                   .width = 80,
                                                                   .height = 8};
  auto const clicked_second_line = ava::tui::composer_input_cursor_for_screen_position(multiline_click_snapshot, 6, cursor_base + 1);
  expect(clicked_second_line && *clicked_second_line == std::string("one\nt").size(),
         "tui composer hit-tests multiline visible input rows to the matching logical line cursor");
  auto const wide_click_text = std::string("a") + "\xE7\x95\x8C" + "b";
  auto const wide_click_cursor = ava::tui::composer_input_cursor_for_screen_position(ava::tui::ComposerSnapshot{.mode = "build",
                                                                                                                .provider = "openai",
                                                                                                                .model = "gpt-5.5",
                                                                                                                .session_id = "session_test",
                                                                                                                .input = wide_click_text,
                                                                                                                .status = "ready",
                                                                                                                .transcript = {},
                                                                                                                .width = 80,
                                                                                                                .height = 8},
                                                                                     7, cursor_base + 3);
  expect(wide_click_cursor && *wide_click_cursor == std::string("a").size() + std::string("\xE7\x95\x8C").size(),
         "tui composer click-to-cursor clamps through wide utf-8 cells without landing inside a codepoint");
  auto const selected_input_frame = ava::tui::render_composer(ava::tui::ComposerSnapshot{.mode = "build",
                                                                                         .provider = "openai",
                                                                                         .model = "gpt-5.5",
                                                                                         .session_id = "session_test",
                                                                                         .input = "alpha beta",
                                                                                         .status = "ready",
                                                                                         .transcript = {},
                                                                                         .width = 80,
                                                                                         .height = 8,
                                                                                         .input_cursor = 10,
                                                                                         .input_selection_start = 6,
                                                                                         .input_selection_end = 10});
  expect(std::ranges::any_of(selected_input_frame,
                             [](std::string const& line) {
                               return line.find(std::string(ava::tui::detail::kReverseVideo) + "beta") != std::string::npos &&
                                      strip_sgr(line).find("│  alpha beta") != std::string::npos;
                             }),
         "tui composer renders selected input text with reverse video without changing visible draft text");

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
  expect(std::ranges::any_of(composer_frame, [](std::string const& line) { return strip_sgr(line).find("│  hello") != std::string::npos; }),
         "tui composer frame renders the input prompt content");
  auto const wide_frame = ava::tui::render_composer(
      ava::tui::ComposerSnapshot{.mode = "build",
                                 .provider = "openai",
                                 .model = "gpt-5.5",
                                 .session_id = "session_test",
                                 .input = std::string("wide ") + "\xE6\xBC\xA2\xE6\xBC\xA2\xF0\x9F\x98\x80",
                                 .status = "ready",
                                 .transcript = {ava::tui::TranscriptItem{.label = "ava", .text = "\xE6\xBC\xA2\xE6\xBC\xA2\xE6\xBC\xA2\xE6\xBC\xA2"}},
                                 .width = 24,
                                 .height = 10});
  expect(std::ranges::all_of(wide_frame, [](std::string const& line) { return visible_columns(line) <= 24; }),
         "tui treats CJK and emoji as wide cells when fitting rendered lines");
  ava::tui::clear_terminal_signal();
  expect(!ava::tui::terminal_signal_received(), "tui terminal signal state can be cleared before curses entry");
}
void run_tui_composer_rendering_tests_part_4()
{
  auto const sidebar_frame = ava::tui::render_composer(ava::tui::ComposerSnapshot{
      .mode = "build",
      .provider = "openai",
      .model = "gpt-5.5",
      .session_id = "session_test",
      .input = "",
      .status = "ready",
      .transcript = {ava::tui::TranscriptItem{.label = "you", .text = "hello"}},
      .width = 144,
      .height = 22,
      .sidebar = ava::tui::SidebarSnapshot{.activity = {ava::tui::SidebarActivityItem{
                                               .id = "call_1", .label = "bash", .detail = "running tests", .status = ava::tui::ToolTimelineStatus::Running}},
                                           .modified_files = {ava::tui::SidebarModifiedFile{.path = "src/ava/tui/runtime.cpp", .added = 12, .removed = 3}},
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
                               return visible.find("Activity") != std::string::npos && visible.find("Modified Files") == std::string::npos;
                             }) &&
             std::ranges::any_of(sidebar_frame,
                                 [](std::string const& line) {
                                   auto const visible = strip_sgr(line);
                                   return visible.find("bash") != std::string::npos && visible.find("running tests") != std::string::npos;
                                 }) &&
             std::ranges::any_of(sidebar_frame,
                                 [](std::string const& line) {
                                   auto const visible = strip_sgr(line);
                                   return visible.find("src/ava/tui/runtime.cpp") != std::string::npos && visible.find("+12") != std::string::npos &&
                                          visible.find("-3") != std::string::npos;
                                 }) &&
             std::ranges::any_of(sidebar_frame,
                                 [](std::string const& line) {
                                   auto const visible = strip_sgr(line);
                                   return visible.find("branch develop") != std::string::npos || visible.find("AVA 0.32") != std::string::npos;
                                 }) &&
             std::ranges::any_of(sidebar_frame, [](std::string const& line) { return strip_sgr(line).find("usage 1.2k (4.0%)") != std::string::npos; }) &&
             std::ranges::any_of(sidebar_frame, [](std::string const& line) { return strip_sgr(line).find("reasoning low") != std::string::npos; }) &&
             std::ranges::any_of(sidebar_frame, [](std::string const& line) { return strip_sgr(line).find("context sources 2") != std::string::npos; }) &&
             std::ranges::none_of(sidebar_frame, [](std::string const& line) { return line.find("\x1b[31m") != std::string::npos; }) &&
             std::ranges::all_of(sidebar_frame, [](std::string const& line) { return visible_columns(line) <= 144; }),
         "tui renders curated running activity, modified files, and known Context metadata");
  expect(std::ranges::any_of(sidebar_frame,
                             [](std::string const& line) {
                               auto const visible = strip_sgr(line);
                               auto const activity = visible.find("Activity");
                               auto const separator = visible.find("│");
                               return activity != std::string::npos && separator != std::string::npos && separator < activity && activity >= 106;
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
      .width = 176,
      .height = 18,
      .sidebar = ava::tui::SidebarSnapshot{
          .activity = {ava::tui::SidebarActivityItem{
              .id = "responding", .label = "responding", .detail = "assistant responded", .status = ava::tui::ToolTimelineStatus::Success}}}});
  expect(
      std::ranges::any_of(idle_after_completed_activity_frame, [](std::string const& line) { return strip_sgr(line).find("Session") != std::string::npos; }) &&
          std::ranges::none_of(idle_after_completed_activity_frame,
                               [](std::string const& line) { return strip_sgr(line).find("Activity") != std::string::npos; }) &&
          std::ranges::none_of(idle_after_completed_activity_frame,
                               [](std::string const& line) { return strip_sgr(line).find("assistant responded") != std::string::npos; }) &&
          std::ranges::none_of(idle_after_completed_activity_frame, [](std::string const& line) { return strip_sgr(line).find("idle") != std::string::npos; }),
      "tui automatic rail omits completed assistant activity history and idle placeholders");

  auto const unknown_sidebar_frame = ava::tui::render_composer(ava::tui::ComposerSnapshot{.mode = "build",
                                                                                          .provider = "openai",
                                                                                          .model = "gpt-5.5",
                                                                                          .session_id = "session_test",
                                                                                          .input = "",
                                                                                          .status = "ready",
                                                                                          .transcript = {},
                                                                                          .width = 176,
                                                                                          .height = 18,
                                                                                          .sidebar = ava::tui::SidebarSnapshot{.session_id = "session_test",
                                                                                                                               .mode = "build",
                                                                                                                               .provider = "openai",
                                                                                                                               .git_branch = "unknown",
                                                                                                                               .token_status = "tokens unknown",
                                                                                                                               .reasoning_status = "unknown"}});
  expect(std::ranges::none_of(unknown_sidebar_frame, [](std::string const& line) { return strip_sgr(line).find("Context") != std::string::npos; }) &&
             std::ranges::none_of(unknown_sidebar_frame, [](std::string const& line) { return strip_sgr(line).find("unknown") != std::string::npos; }),
         "tui automatic rail omits the Context group and placeholders when all context values are unknown");

  auto const legitimate_unknown_substring_frame =
      ava::tui::render_composer(ava::tui::ComposerSnapshot{.mode = "build",
                                                           .provider = "unknown-labs",
                                                           .model = "model-unknown-v2",
                                                           .session_id = "session_known_values",
                                                           .input = "",
                                                           .status = "ready",
                                                           .transcript = {},
                                                           .width = 176,
                                                           .height = 18,
                                                           .sidebar = ava::tui::SidebarSnapshot{.mode = "build",
                                                                                                .provider = "unknown-labs",
                                                                                                .model = "model-unknown-v2",
                                                                                                .git_branch = "fix/unknown-token-count",
                                                                                                .token_status = "tokens unknown",
                                                                                                .reasoning_status = "UnKnOwN"}});
  expect(std::ranges::any_of(legitimate_unknown_substring_frame,
                             [](std::string const& line) { return strip_sgr(line).find("build · unknown-labs/model-unknow") != std::string::npos; }) &&
             std::ranges::any_of(legitimate_unknown_substring_frame,
                                 [](std::string const& line) { return strip_sgr(line).find("branch fix/unknown-token-count") != std::string::npos; }) &&
             std::ranges::none_of(legitimate_unknown_substring_frame,
                                  [](std::string const& line) { return strip_sgr(line).find("usage tokens unknown") != std::string::npos; }) &&
             std::ranges::none_of(legitimate_unknown_substring_frame,
                                  [](std::string const& line) { return strip_sgr(line).find("reasoning UnKnOwN") != std::string::npos; }),
         "tui automatic rail preserves legitimate values containing unknown while omitting exact normalized unknown sentinels");

  auto const zero_context_sidebar_frame = ava::tui::render_composer(ava::tui::ComposerSnapshot{
      .mode = "build",
      .provider = "openai",
      .model = "gpt-5.5",
      .session_id = "session_test",
      .input = "",
      .status = "ready",
      .transcript = {},
      .width = 176,
      .height = 18,
      .sidebar = ava::tui::SidebarSnapshot{.session_id = "session_test", .mode = "build", .provider = "openai", .context_source_count = 0}});
  expect(
      std::ranges::any_of(zero_context_sidebar_frame, [](std::string const& line) { return strip_sgr(line).find("context sources 0") != std::string::npos; }),
      "tui automatic rail distinguishes a known zero context source count from unknown context data");

  auto const long_session_sidebar_frame =
      ava::tui::render_composer(ava::tui::ComposerSnapshot{.mode = "build",
                                                           .provider = "openai",
                                                           .model = "gpt-5.5",
                                                           .session_id = "session_test",
                                                           .input = "",
                                                           .status = "ready",
                                                           .transcript = {},
                                                           .width = 176,
                                                           .height = 20,
                                                           .sidebar = ava::tui::SidebarSnapshot{.session_id = "session_test",
                                                                                                .mode = "build",
                                                                                                .provider = "openai",
                                                                                                .model = "gpt-5.5",
                                                                                                .workspace = "/workspace/project",
                                                                                                .git_branch = "develop",
                                                                                                .version = "0.32",
                                                                                                .token_status = "180k (90.0%)",
                                                                                                .reasoning_status = std::nullopt,
                                                                                                .context_source_count = 3,
                                                                                                .session_path = "/tmp/ava/sessions/session_test.jsonl",
                                                                                                .session_entry_count = 42}});
  expect(
      std::ranges::none_of(long_session_sidebar_frame,
                           [](std::string const& line) { return strip_sgr(line).find("path /tmp/ava/sessions") != std::string::npos; }) &&
          std::ranges::none_of(long_session_sidebar_frame, [](std::string const& line) { return strip_sgr(line).find("entries 42") != std::string::npos; }) &&
          std::ranges::any_of(long_session_sidebar_frame,
                              [](std::string const& line) {
                                auto const visible = strip_sgr(line);
                                return visible.find("context pressure critical 90.0%") != std::string::npos;
                              }),
      "tui automatic rail shows critical context pressure while omitting raw session path and entry count");

  auto const curated_idle_sidebar = ava::tui::render_composer(ava::tui::ComposerSnapshot{
      .mode = "build",
      .provider = "openai",
      .model = "gpt-5.5",
      .session_id = "outer_session",
      .input = "",
      .status = "ready",
      .transcript = {},
      .width = 176,
      .height = 22,
      .sidebar = ava::tui::SidebarSnapshot{
          .activity = {ava::tui::SidebarActivityItem{
              .id = "completed", .label = "completed-history", .detail = "must stay hidden", .status = ava::tui::ToolTimelineStatus::Success}},
          .session_id = "raw-session-id-must-stay-hidden",
          .mode = "build",
          .provider = "openai",
          .model = "gpt-5.5",
          .workspace = "/raw/workspace/must/stay/hidden",
          .version = "9.9.9-must-stay-hidden",
          .token_status = std::nullopt,
          .reasoning_status = std::nullopt,
          .context_source_count = std::nullopt,
          .session_path = "/raw/session/path/must/stay/hidden.jsonl",
          .session_entry_count = 999}});
  auto const curated_idle_text = [&]() {
    std::string text;
    for (auto const& line : curated_idle_sidebar) text += strip_sgr(line) + "\n";
    return text;
  }();
  auto const curated_idle_title_line =
      std::ranges::find_if(curated_idle_sidebar, [](std::string const& line) { return strip_sgr(line).find("Session") != std::string::npos; });
  auto const curated_idle_title_visible = curated_idle_title_line == curated_idle_sidebar.end() ? std::string{} : strip_sgr(*curated_idle_title_line);
  auto const curated_idle_divider = curated_idle_title_visible.find("│");
  auto curated_idle_footer = strip_sgr(curated_idle_sidebar.back());
  auto const curated_idle_footer_divider = curated_idle_footer.rfind("│");
  if (curated_idle_footer_divider != std::string::npos)
    curated_idle_footer.erase(curated_idle_footer_divider);
  while (!curated_idle_footer.empty() && curated_idle_footer.back() == ' ') curated_idle_footer.pop_back();
  expect(curated_idle_title_line != curated_idle_sidebar.end() && curated_idle_divider != std::string::npos &&
             curated_idle_title_visible.find("│", curated_idle_divider + std::string_view("│").size()) == std::string::npos &&
             curated_idle_title_visible.substr(curated_idle_divider).starts_with("│  Session") &&
             curated_idle_text.find("build · openai/gpt-5.5") != std::string::npos && curated_idle_text.find("AVA") == std::string::npos &&
             curated_idle_text.find("live session") == std::string::npos && curated_idle_text.find("Activity") == std::string::npos &&
             curated_idle_text.find("Modified Files") == std::string::npos && curated_idle_text.find("idle") == std::string::npos &&
             curated_idle_text.find("no file changes") == std::string::npos && curated_idle_text.find("unknown") == std::string::npos &&
             curated_idle_text.find("raw-session-id") == std::string::npos && curated_idle_text.find("/raw/session/path") == std::string::npos &&
             curated_idle_text.find("/raw/workspace") == std::string::npos && curated_idle_text.find("999") == std::string::npos &&
             curated_idle_text.find("9.9.9") == std::string::npos && curated_idle_footer == "│  GPT-5.5" &&
             std::ranges::all_of(curated_idle_sidebar,
                                 [](std::string const& line) {
                                   auto const visible = strip_sgr(line);
                                   auto const divider = visible.rfind("│");
                                   return divider != std::string::npos && visible_columns(visible.substr(0, divider)) == 137 && visible_columns(line) <= 176;
                                 }),
         "tui idle automatic rail is a bounded two-cell-inset Session summary with the quiet footer and no placeholders or drawer-only metadata");

  auto const curated_populated_sidebar = ava::tui::render_composer(ava::tui::ComposerSnapshot{
      .mode = "build\x1b[31m",
      .provider = "openai\x1b[31m",
      .model = "gpt-5.5\x1b[31m",
      .session_id = "outer_session",
      .input = "",
      .status = "thinking...",
      .processing = true,
      .transcript = {},
      .width = 144,
      .height = 24,
      .sidebar = ava::tui::SidebarSnapshot{
          .activity = {ava::tui::SidebarActivityItem{.id = "running",
                                                     .label = "running-task\x1b[31m",
                                                     .detail = "bounded-detail-that-is-deliberately-long-for-the-rail\x1b[31m",
                                                     .status = ava::tui::ToolTimelineStatus::Running},
                       ava::tui::SidebarActivityItem{
                           .id = "completed", .label = "completed-history-must-stay-hidden", .status = ava::tui::ToolTimelineStatus::Success}},
          .modified_files = {ava::tui::SidebarModifiedFile{.path = "src/curated-file.cpp\x1b[31m", .added = 12, .removed = 3}},
          .session_id = "raw-populated-session-must-stay-hidden",
          .mode = "build\x1b[31m",
          .provider = "openai\x1b[31m",
          .model = "gpt-5.5\x1b[31m",
          .workspace = "/raw/populated/workspace/must/stay/hidden",
          .git_branch = "develop\x1b[31m",
          .version = "8.8.8-must-stay-hidden",
          .token_status = "180k (90.0%)\x1b[31m",
          .reasoning_status = "high\x1b[31m",
          .context_source_count = 7,
          .session_path = "/raw/populated/session/path.jsonl",
          .session_entry_count = 42}});
  auto const curated_populated_text = [&]() {
    std::string text;
    for (auto const& line : curated_populated_sidebar) text += strip_sgr(line) + "\n";
    return text;
  }();
  auto const curated_populated_rail_lines = [&]() {
    std::vector<std::string> lines;
    for (auto const& line : curated_populated_sidebar)
    {
      auto visible = strip_sgr(line);
      auto const divider = visible.rfind("│");
      lines.push_back(divider == std::string::npos ? std::string{} : visible.substr(divider + std::string_view("│").size()));
    }
    return lines;
  }();
  auto const rail_group_row = [&](std::string_view title) {
    return static_cast<std::size_t>(std::ranges::find_if(curated_populated_rail_lines,
                                                         [title](std::string const& line) {
                                                           auto const first = line.find_first_not_of(' ');
                                                           auto const last = line.find_last_not_of(' ');
                                                           return first != std::string::npos && line.substr(first, last - first + 1) == title;
                                                         }) -
                                    curated_populated_rail_lines.begin());
  };
  auto const session_group_row = rail_group_row("Session");
  auto const activity_group_row = rail_group_row("Activity");
  auto const modified_group_row = rail_group_row("Modified Files");
  auto const context_group_row = rail_group_row("Context");
  auto const blank_before = [&](std::size_t row) {
    return row > 0 && row < curated_populated_rail_lines.size() && curated_populated_rail_lines[row - 1].find_first_not_of(' ') == std::string::npos;
  };
  expect(curated_populated_text.find("Session") != std::string::npos && curated_populated_text.find("Activity") != std::string::npos &&
             curated_populated_text.find("[~] running-task") != std::string::npos && curated_populated_text.find("Modified Files") != std::string::npos &&
             curated_populated_text.find("src/curated-file.cpp") != std::string::npos && curated_populated_text.find("+12") != std::string::npos &&
             curated_populated_text.find("-3") != std::string::npos && curated_populated_text.find("Context") != std::string::npos &&
             curated_populated_text.find("branch develop") != std::string::npos && curated_populated_text.find("reasoning high") != std::string::npos &&
             curated_populated_text.find("usage 180k (90.0%)") != std::string::npos &&
             curated_populated_text.find("context pressure critical 90.0%") != std::string::npos &&
             curated_populated_text.find("context sources 7") != std::string::npos && curated_populated_text.find("completed-history") == std::string::npos &&
             curated_populated_text.find("raw-populated-session") == std::string::npos && curated_populated_text.find("/raw/populated") == std::string::npos &&
             curated_populated_text.find("entries 42") == std::string::npos && curated_populated_text.find("8.8.8") == std::string::npos &&
             session_group_row == 0 && activity_group_row < curated_populated_rail_lines.size() && blank_before(activity_group_row) &&
             modified_group_row < curated_populated_rail_lines.size() && blank_before(modified_group_row) &&
             context_group_row < curated_populated_rail_lines.size() && blank_before(context_group_row) &&
             std::ranges::none_of(
                 curated_populated_sidebar, [](std::string const& line) { return line.find("\x1b[31m") != std::string::npos; }) &&
             std::ranges::all_of(
                 curated_populated_sidebar, [](std::string const& line) { return visible_columns(line) <= 144; }),
         "tui populated automatic rail shows only running activity, modified files, and known sanitized Context values with one blank between groups");
  {
    ScopedEnvVar no_color("NO_COLOR", "1");
    auto const plain_curated_rail = ava::tui::render_composer(
        ava::tui::ComposerSnapshot{.mode = "build",
                                   .provider = "openai",
                                   .model = "gpt-5.5",
                                   .session_id = "plain",
                                   .input = "",
                                   .status = "ready",
                                   .transcript = {},
                                   .width = 176,
                                   .height = 18,
                                   .sidebar = ava::tui::SidebarSnapshot{.mode = "build", .provider = "openai", .model = "gpt-5.5", .context_source_count = 0}});
    expect(std::ranges::any_of(plain_curated_rail, [](std::string const& line) { return line.find("│  Session") != std::string::npos; }) &&
               std::ranges::any_of(plain_curated_rail, [](std::string const& line) { return line.find("context sources 0") != std::string::npos; }) &&
               std::ranges::none_of(plain_curated_rail, [](std::string const& line) { return line.find('\x1b') != std::string::npos; }),
           "tui automatic rail keeps its textual hierarchy and known-zero Context in NO_COLOR mode");
  }

  auto const narrow_no_sidebar = ava::tui::render_composer(
      ava::tui::ComposerSnapshot{.mode = "build",
                                 .provider = "openai",
                                 .model = "gpt-5.5",
                                 .session_id = "session_test",
                                 .input = "",
                                 .status = "ready",
                                 .transcript = {},
                                 .width = 90,
                                 .height = 10,
                                 .sidebar = ava::tui::SidebarSnapshot{.activity = {ava::tui::SidebarActivityItem{.id = "a", .label = "sidebar-only"}}}});
  expect(std::ranges::none_of(narrow_no_sidebar, [](std::string const& line) { return strip_sgr(line).find("sidebar-only") != std::string::npos; }),
         "tui hides the sidebar on narrow terminals");

  auto canvas_snapshot = ava::tui::ComposerSnapshot{.mode = "build",
                                                    .provider = "openai",
                                                    .model = "gpt-5.5",
                                                    .session_id = "session_canvas",
                                                    .input = "centered draft",
                                                    .status = "ready",
                                                    .transcript = {},
                                                    .width = 119,
                                                    .height = 16};
  auto canvas_119 = ava::tui::composer_canvas_layout(canvas_snapshot);
  canvas_snapshot.width = 120;
  auto canvas_120 = ava::tui::composer_canvas_layout(canvas_snapshot);
  canvas_snapshot.width = 121;
  auto canvas_121 = ava::tui::composer_canvas_layout(canvas_snapshot);
  canvas_snapshot.width = 160;
  auto canvas_160 = ava::tui::composer_canvas_layout(canvas_snapshot);
  auto const centered_frame = ava::tui::render_composer(canvas_snapshot);
  auto const centered_input =
      std::ranges::find_if(centered_frame, [](std::string const& line) { return strip_sgr(line).find("centered draft") != std::string::npos; });
  expect(canvas_119.content_width == 119 && canvas_119.left == 0 && !canvas_119.rail_visible && canvas_120.content_width == 120 && canvas_120.left == 0 &&
             !canvas_120.rail_visible && canvas_121.content_width == 120 && canvas_121.left == 0 && !canvas_121.rail_visible &&
             canvas_160.content_width == 120 && canvas_160.left == 20 && !canvas_160.rail_visible && centered_input != centered_frame.end() &&
             strip_sgr(*centered_input).find("│  centered draft") == 20 &&
             std::ranges::all_of(centered_frame, [](std::string const& line) { return visible_columns(line) == 160; }),
         "tui ordinary canvas stays full width through 120 columns and becomes one exact centered 120-column frame above it");

  auto boundary_snapshot = ava::tui::ComposerSnapshot{
      .mode = "build",
      .provider = "openai",
      .model = "gpt-5.5",
      .session_id = "session_boundary",
      .input = "",
      .status = "ready",
      .transcript = {},
      .width = 143,
      .height = 16,
      .sidebar = ava::tui::SidebarSnapshot{
          .activity = {ava::tui::SidebarActivityItem{.id = "boundary", .label = "boundary-activity", .status = ava::tui::ToolTimelineStatus::Running}}}};
  auto boundary_frame = ava::tui::render_composer(boundary_snapshot);
  expect(ava::tui::composer_canvas_layout(boundary_snapshot).content_width == 120 && ava::tui::composer_canvas_layout(boundary_snapshot).left == 11 &&
             !ava::tui::composer_canvas_layout(boundary_snapshot).rail_visible &&
             std::ranges::none_of(boundary_frame, [](std::string const& line) { return strip_sgr(line).find("boundary-activity") != std::string::npos; }),
         "tui actionable automatic sidebar stays hidden and centers the canvas at 143x16");
  boundary_snapshot.width = 144;
  boundary_snapshot.height = 15;
  boundary_frame = ava::tui::render_composer(boundary_snapshot);
  expect(ava::tui::composer_canvas_layout(boundary_snapshot).content_width == 120 && ava::tui::composer_canvas_layout(boundary_snapshot).left == 12 &&
             !ava::tui::composer_canvas_layout(boundary_snapshot).rail_visible &&
             std::ranges::none_of(boundary_frame, [](std::string const& line) { return strip_sgr(line).find("boundary-activity") != std::string::npos; }),
         "tui actionable automatic sidebar stays hidden and centers the canvas at 144x15");
  boundary_snapshot.height = 16;
  boundary_frame = ava::tui::render_composer(boundary_snapshot);
  expect(ava::tui::composer_canvas_layout(boundary_snapshot).content_width == 105 && ava::tui::composer_canvas_layout(boundary_snapshot).left == 0 &&
             ava::tui::composer_canvas_layout(boundary_snapshot).rail_visible &&
             std::ranges::any_of(boundary_frame,
                                 [](std::string const& line) {
                                   auto const visible = strip_sgr(line);
                                   auto const divider = visible.find("│");
                                   return divider != std::string::npos && visible_columns(visible.substr(0, divider)) == 105 &&
                                          visible.find("boundary-activity") != std::string::npos;
                                 }),
         "tui actionable automatic sidebar appears at 144x16 with a capped 38-column rail");

  auto modified_boundary_snapshot = boundary_snapshot;
  modified_boundary_snapshot.sidebar->activity.clear();
  modified_boundary_snapshot.sidebar->modified_files = {ava::tui::SidebarModifiedFile{.path = "boundary-file.cpp"}};
  modified_boundary_snapshot.width = 143;
  auto modified_boundary_frame = ava::tui::render_composer(modified_boundary_snapshot);
  expect(
      ava::tui::composer_canvas_layout(modified_boundary_snapshot).content_width == 120 &&
          ava::tui::composer_canvas_layout(modified_boundary_snapshot).left == 11 &&
          !ava::tui::composer_canvas_layout(modified_boundary_snapshot).rail_visible &&
          std::ranges::none_of(modified_boundary_frame, [](std::string const& line) { return strip_sgr(line).find("boundary-file.cpp") != std::string::npos; }),
      "tui automatic sidebar stays hidden and centers modified-file work at 143x16");
  modified_boundary_snapshot.width = 144;
  modified_boundary_frame = ava::tui::render_composer(modified_boundary_snapshot);
  expect(
      ava::tui::composer_main_width(modified_boundary_snapshot) == 105 &&
          std::ranges::any_of(modified_boundary_frame, [](std::string const& line) { return strip_sgr(line).find("boundary-file.cpp") != std::string::npos; }),
      "tui automatic sidebar appears for modified-file work at exactly 144x16");

  auto idle_boundary_snapshot = boundary_snapshot;
  idle_boundary_snapshot.sidebar->activity.clear();
  idle_boundary_snapshot.sidebar->mode = "build";
  idle_boundary_snapshot.sidebar->provider = "openai";
  idle_boundary_snapshot.sidebar->model = "gpt-5.5";
  idle_boundary_snapshot.width = 175;
  auto idle_boundary_frame = ava::tui::render_composer(idle_boundary_snapshot);
  expect(ava::tui::composer_canvas_layout(idle_boundary_snapshot).content_width == 120 && ava::tui::composer_canvas_layout(idle_boundary_snapshot).left == 27 &&
             !ava::tui::composer_canvas_layout(idle_boundary_snapshot).rail_visible &&
             std::ranges::none_of(idle_boundary_frame, [](std::string const& line) { return strip_sgr(line).find("Session") != std::string::npos; }),
         "tui idle automatic sidebar stays hidden and centers the canvas at 175x16");
  idle_boundary_snapshot.width = 176;
  idle_boundary_frame = ava::tui::render_composer(idle_boundary_snapshot);
  expect(ava::tui::composer_canvas_layout(idle_boundary_snapshot).content_width == 137 && ava::tui::composer_canvas_layout(idle_boundary_snapshot).left == 0 &&
             ava::tui::composer_canvas_layout(idle_boundary_snapshot).rail_visible &&
             std::ranges::any_of(idle_boundary_frame, [](std::string const& line) { return strip_sgr(line).find("Session") != std::string::npos; }),
         "tui idle automatic sidebar appears at 176x16");

  auto reasoning_feedback_snapshot = idle_boundary_snapshot;
  reasoning_feedback_snapshot.width = 160;
  reasoning_feedback_snapshot.reasoning_feedback = "reasoning low";
  auto const reasoning_feedback_frame = ava::tui::render_composer(reasoning_feedback_snapshot);
  expect(ava::tui::composer_canvas_layout(reasoning_feedback_snapshot).content_width == 120 &&
             ava::tui::composer_canvas_layout(reasoning_feedback_snapshot).left == 20 &&
             std::ranges::any_of(reasoning_feedback_frame, [](std::string const& line) { return strip_sgr(line).find("reasoning low") != std::string::npos; }),
         "tui renders subtle one-action reasoning feedback in the centered canvas when the automatic sidebar is hidden");

  auto permission_boundary_snapshot = boundary_snapshot;
  permission_boundary_snapshot.width = 176;
  permission_boundary_snapshot.permission_prompt.emplace();
  permission_boundary_snapshot.permission_prompt->tool_name = "bash";
  permission_boundary_snapshot.permission_prompt->operation = "run";
  permission_boundary_snapshot.permission_prompt->target = "tests";
  permission_boundary_snapshot.permission_prompt->reason = "boundary";
  auto question_boundary_snapshot = boundary_snapshot;
  question_boundary_snapshot.width = 176;
  question_boundary_snapshot.question_prompt.emplace();
  question_boundary_snapshot.question_prompt->header = "Boundary question";
  question_boundary_snapshot.question_prompt->question = "Choose";
  question_boundary_snapshot.question_prompt->options.push_back(ava::tui::QuestionPromptOptionView{.value = "a", .label = "A"});
  auto select_boundary_snapshot = boundary_snapshot;
  select_boundary_snapshot.width = 176;
  select_boundary_snapshot.select_list.emplace();
  select_boundary_snapshot.select_list->title = "Boundary select";
  select_boundary_snapshot.select_list->items.emplace_back();
  select_boundary_snapshot.select_list->items.back().value = "a";
  select_boundary_snapshot.select_list->items.back().label = "A";
  auto const permission_boundary_frame = ava::tui::render_composer(permission_boundary_snapshot);
  auto const question_boundary_frame = ava::tui::render_composer(question_boundary_snapshot);
  auto const select_boundary_frame = ava::tui::render_composer(select_boundary_snapshot);
  expect(
      ava::tui::composer_canvas_layout(permission_boundary_snapshot).content_width == 120 &&
          ava::tui::composer_canvas_layout(permission_boundary_snapshot).left == 28 &&
          ava::tui::composer_canvas_layout(question_boundary_snapshot).content_width == 120 &&
          ava::tui::composer_canvas_layout(question_boundary_snapshot).left == 28 &&
          ava::tui::composer_canvas_layout(select_boundary_snapshot).content_width == 120 &&
          ava::tui::composer_canvas_layout(select_boundary_snapshot).left == 28 &&
          std::ranges::any_of(permission_boundary_frame, [](std::string const& line) { return strip_sgr(line).find("! Permission required") == 30; }) &&
          std::ranges::any_of(question_boundary_frame, [](std::string const& line) { return strip_sgr(line).find("? Boundary question") == 30; }) &&
          std::ranges::none_of(permission_boundary_frame,
                               [](std::string const& line) { return strip_sgr(line).find("boundary-activity") != std::string::npos; }) &&
          std::ranges::none_of(question_boundary_frame,
                               [](std::string const& line) { return strip_sgr(line).find("boundary-activity") != std::string::npos; }) &&
          std::ranges::none_of(select_boundary_frame, [](std::string const& line) { return strip_sgr(line).find("boundary-activity") != std::string::npos; }),
      "tui permission, question, and select authority suppress the automatic sidebar and share centered canvas geometry");

  boundary_snapshot.width = 160;
  boundary_snapshot.height = 12;
  boundary_frame = ava::tui::render_composer(boundary_snapshot);
  expect(ava::tui::composer_canvas_layout(boundary_snapshot).content_width == 120 && ava::tui::composer_canvas_layout(boundary_snapshot).left == 20 &&
             !ava::tui::composer_canvas_layout(boundary_snapshot).rail_visible &&
             std::ranges::none_of(boundary_frame, [](std::string const& line) { return strip_sgr(line).find("boundary-activity") != std::string::npos; }),
         "tui automatic sidebar stays hidden and centers the canvas on a short 160x12 terminal");

  auto drawer_snapshot =
      ava::tui::ComposerSnapshot{
          .mode = "build",
          .provider = "openai",
          .model = "gpt-5.5",
          .session_id = "session_drawer",
          .input = "",
          .status = "ready",
          .transcript = {ava::tui::TranscriptItem{.label = "you", .text = "DRAWER MUST HIDE THIS TRANSCRIPT"}},
          .width = 80,
          .height = 24,
          .sidebar =
              ava::tui::SidebarSnapshot{
                  .activity = {ava::tui::SidebarActivityItem{.id = "call_drawer",
                                                             .label = "running-activity",
                                                             .detail = "running a deliberately detailed responsive sidebar validation task",
                                                             .status = ava::tui::ToolTimelineStatus::Running},
                               ava::tui::SidebarActivityItem{
                                   .id = "complete_drawer", .label = "completed-activity", .status = ava::tui::ToolTimelineStatus::Success},
                               ava::tui::SidebarActivityItem{
                                   .id = "cancel_drawer", .label = "canceled-activity", .status = ava::tui::ToolTimelineStatus::Canceled},
                               ava::tui::SidebarActivityItem{.id = "error_drawer", .label = "failed-activity", .status = ava::tui::ToolTimelineStatus::Error}},
                  .modified_files = {ava::tui::SidebarModifiedFile{.path = "src/ava/tui/a-very-long-modified-file-name-for-responsive-drawer.cpp",
                                                                   .added = 12,
                                                                   .removed = 3}},
                  .session_id = "session_drawer_with_a_deliberately_long_identifier_that_must_wrap_without_clipping",
                  .mode = "build",
                  .provider = "openai",
                  .model = "gpt-5.5",
                  .workspace = "/workspace/a/very/long/project/path/that/must/wrap/and/remain/reachable/in/the/session/overview",
                  .git_branch = "develop-responsive-sidebar-checkpoint",
                  .version = "1.0.0-responsive-sidebar",
                  .token_status = "180k (90.0%)",
                  .reasoning_status = "high",
                  .context_source_count = 7,
                  .session_path = "/tmp/ava/sessions/a/very/long/session/path/session_drawer.jsonl",
                  .session_entry_count = 42},
          .sidebar_drawer_visible = true};
  auto wide_drawer_snapshot = drawer_snapshot;
  wide_drawer_snapshot.width = 160;
  auto const wide_drawer_canvas = ava::tui::composer_canvas_layout(wide_drawer_snapshot);
  expect(wide_drawer_canvas.content_width == 160 && wide_drawer_canvas.left == 0 && !wide_drawer_canvas.rail_visible,
         "tui sidebar drawer retains the full terminal canvas instead of inheriting the ordinary width cap");
  auto drawer_frame = ava::tui::render_composer(drawer_snapshot);
  auto const drawer_max = ava::tui::sidebar_drawer_max_scroll_offset(drawer_snapshot);
  expect(drawer_frame.size() == 24 && ava::tui::composer_main_width(drawer_snapshot) == 80 && drawer_max > 0 &&
             std::ranges::any_of(drawer_frame, [](std::string const& line) { return strip_sgr(line).find("Session overview") != std::string::npos; }) &&
             std::ranges::any_of(drawer_frame, [](std::string const& line) { return strip_sgr(line).find("PgUp/PgDn") != std::string::npos; }) &&
             std::ranges::any_of(drawer_frame, [](std::string const& line) { return strip_sgr(line).find("very-long-modified") != std::string::npos; }) &&
             std::ranges::any_of(drawer_frame, [](std::string const& line) { return strip_sgr(line).find("[~] running-activity") != std::string::npos; }) &&
             std::ranges::any_of(drawer_frame, [](std::string const& line) { return strip_sgr(line).find("[+] completed-activity") != std::string::npos; }) &&
             std::ranges::any_of(drawer_frame, [](std::string const& line) { return strip_sgr(line).find("[-] canceled-activity") != std::string::npos; }) &&
             std::ranges::any_of(drawer_frame, [](std::string const& line) { return strip_sgr(line).find("[x] failed-activity") != std::string::npos; }) &&
             std::ranges::none_of(drawer_frame, [](std::string const& line) { return strip_sgr(line).find("DRAWER MUST HIDE") != std::string::npos; }) &&
             std::ranges::none_of(drawer_frame, [](std::string const& line) { return strip_sgr(line).find("live session") != std::string::npos; }) &&
             strip_sgr(drawer_frame[22]).starts_with("│  Type a message...") && strip_sgr(drawer_frame[23]).starts_with("│  GPT-5.5") &&
             std::ranges::all_of(drawer_frame, [](std::string const& line) { return visible_columns(line) <= 80; }),
         "tui narrow sidebar drawer replaces the transcript, wraps semantic data, stays bounded, and retains the full-width quiet composer");
  drawer_snapshot.sidebar_drawer_scroll_offset = drawer_max;
  auto const drawer_end_frame = ava::tui::render_composer(drawer_snapshot);
  expect(std::ranges::any_of(drawer_end_frame,
                             [](std::string const& line) { return strip_sgr(line).find("context pressure critical 90.0%") != std::string::npos; }) &&
             std::ranges::any_of(drawer_end_frame, [](std::string const& line) { return strip_sgr(line).find("context sources 7") != std::string::npos; }) &&
             std::ranges::any_of(drawer_end_frame,
                                 [](std::string const& line) { return strip_sgr(line).find("AVA 1.0.0-responsive-sidebar") != std::string::npos; }),
         "tui sidebar drawer maximum scroll reaches final context and version data");

  drawer_snapshot.width = 100;
  drawer_snapshot.height = 12;
  drawer_snapshot.sidebar_drawer_scroll_offset = ava::tui::sidebar_drawer_max_scroll_offset(drawer_snapshot);
  auto const short_drawer_frame = ava::tui::render_composer(drawer_snapshot);
  expect(short_drawer_frame.size() == 12 && ava::tui::composer_main_width(drawer_snapshot) == 100 &&
             strip_sgr(short_drawer_frame[10]).starts_with("│  Type a message...") && strip_sgr(short_drawer_frame[11]).starts_with("│  GPT-5.5") &&
             std::ranges::any_of(short_drawer_frame, [](std::string const& line) { return strip_sgr(line).find("context sources 7") != std::string::npos; }) &&
             std::ranges::all_of(short_drawer_frame, [](std::string const& line) { return visible_columns(line) <= 100; }),
         "tui short sidebar drawer remains scrollable and retains the bottom composer rows");

  auto drawer_conflict_hidden = [&](ava::tui::ComposerSnapshot conflict, std::string_view expected_view_text) {
    auto const frame = ava::tui::render_composer(conflict);
    return std::ranges::none_of(frame, [](std::string const& line) { return strip_sgr(line).find("Session overview") != std::string::npos; }) &&
           std::ranges::any_of(frame, [&](std::string const& line) { return strip_sgr(line).find(expected_view_text) != std::string::npos; });
  };
  auto permission_conflict = drawer_snapshot;
  permission_conflict.permission_prompt = ava::tui::PermissionPromptView{.tool_name = "bash", .operation = "run command", .target = "tests", .reason = "test"};
  auto docked_question_conflict = drawer_snapshot;
  docked_question_conflict.question_prompt = ava::tui::QuestionPromptView{};
  docked_question_conflict.question_prompt->header = "Question";
  docked_question_conflict.question_prompt->question = "Choose";
  docked_question_conflict.question_prompt->options = {{.value = "a", .label = "A"}};
  auto modal_question_conflict = docked_question_conflict;
  modal_question_conflict.question_prompt->modal = true;
  auto select_conflict = drawer_snapshot;
  select_conflict.select_list = ava::tui::SelectListView{};
  select_conflict.select_list->title = "Choose";
  select_conflict.select_list->items.emplace_back();
  select_conflict.select_list->items.back().value = "a";
  select_conflict.select_list->items.back().label = "A";
  expect(drawer_conflict_hidden(permission_conflict, "Permission required") && drawer_conflict_hidden(docked_question_conflict, "Choose") &&
             drawer_conflict_hidden(modal_question_conflict, "Choose") && drawer_conflict_hidden(select_conflict, "Choose"),
         "tui safety and choice views suppress the sidebar drawer");

  drawer_snapshot.width = 80;
  drawer_snapshot.height = 24;
  drawer_snapshot.sidebar_drawer_scroll_offset = 0;
  expect(!ava::tui::composer_input_cursor_for_screen_position(drawer_snapshot, 23, 4),
         "tui composer hit testing rejects clicks while the sidebar drawer owns focus");
  drawer_snapshot.sidebar_drawer_visible = false;
  expect(ava::tui::composer_input_cursor_for_screen_position(drawer_snapshot, 23, 4).has_value(),
         "tui composer hit testing resumes after the sidebar drawer closes");
  auto missing_drawer_data = drawer_snapshot;
  missing_drawer_data.sidebar_drawer_visible = true;
  missing_drawer_data.sidebar = std::nullopt;
  auto const missing_drawer_data_frame = ava::tui::render_composer(missing_drawer_data);
  expect(std::ranges::any_of(missing_drawer_data_frame,
                             [](std::string const& line) { return strip_sgr(line).find("DRAWER MUST HIDE THIS TRANSCRIPT") != std::string::npos; }) &&
             ava::tui::composer_input_cursor_for_screen_position(missing_drawer_data, 23, 4).has_value(),
         "tui sidebar drawer fails closed to the normal full-width composer when semantic sidebar data is absent");
  {
    ScopedEnvVar no_color("NO_COLOR", "1");
    auto plain_drawer_snapshot = drawer_snapshot;
    plain_drawer_snapshot.sidebar_drawer_visible = true;
    auto const plain_drawer = ava::tui::render_composer(plain_drawer_snapshot);
    expect(std::ranges::none_of(plain_drawer, [](std::string const& line) { return line.find('\x1b') != std::string::npos; }),
           "tui sidebar drawer plain mode emits no terminal escapes");
  }

  auto const tabbed = ava::tui::render_composer(ava::tui::ComposerSnapshot{.mode = "build",
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

  auto const assistant_meta = ava::tui::render_composer(
      ava::tui::ComposerSnapshot{.mode = "build",
                                 .provider = "openai",
                                 .model = "gpt-5.5",
                                 .session_id = "session_test",
                                 .input = "",
                                 .status = "ready",
                                 .transcript = {ava::tui::TranscriptItem{.label = "ava", .text = "answer", .meta = "Build · GPT-5.5 · 1.2s"}},
                                 .width = 48,
                                 .height = 10});
  expect(std::ranges::any_of(assistant_meta, [](std::string const& line) { return strip_sgr(line).find("* Build · GPT-5.5 · 1.2s") != std::string::npos; }) &&
             std::ranges::all_of(assistant_meta, [](std::string const& line) { return visible_columns(line) <= 48; }),
         "tui renders assistant mode/model/duration metadata under AVA messages with ASCII markers");
  auto assistant_meta_index = std::optional<std::size_t>{};
  auto composer_index = std::optional<std::size_t>{};
  for (std::size_t index = 0; index < assistant_meta.size(); ++index)
  {
    auto const visible = strip_sgr(assistant_meta[index]);
    if (!assistant_meta_index && visible.find("* Build · GPT-5.5 · 1.2s") != std::string::npos)
    {
      assistant_meta_index = index;
    }
    if (!composer_index && visible.find("│  Type a message...") != std::string::npos)
      composer_index = index;
  }
  expect(assistant_meta_index && composer_index && *composer_index > *assistant_meta_index + 1 && strip_sgr(assistant_meta[*assistant_meta_index + 1]).empty(),
         "tui leaves a vertical margin between the latest assistant metadata and the composer");

  std::string exact_width_utf8_status;
  for (int index = 0; index < 12; ++index)
  {
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
             std::ranges::any_of(exact_width_utf8, [](std::string const& line) { return strip_sgr(line).find("│  GPT-5.5") != std::string::npos; }),
         "tui width fitting preserves the AVA composer surface at minimum width");

  auto const utf8 = ava::tui::render_composer(
      ava::tui::ComposerSnapshot{.mode = "build",
                                 .provider = "openai",
                                 .model = "gpt-5.5",
                                 .session_id = "session_test",
                                 .input = "",
                                 .status = "ready",
                                 .transcript = {ava::tui::TranscriptItem{.label = "ava", .text = std::string(13, 'x') + "\xC3\xA9" + "zzz"}},
                                 .width = 20,
                                 .height = 8});
  expect(std::ranges::none_of(utf8, [](std::string const& line) { return !line.empty() && (static_cast<unsigned char>(line.back()) & 0xC0U) == 0xC0U; }),
         "tui truncation does not leave a trailing utf-8 starter byte");

  std::vector<ava::tui::TranscriptItem> stress_transcript;
  for (int index = 0; index < 36; ++index)
  {
    stress_transcript.push_back(ava::tui::TranscriptItem{
        .label = "you", .text = "resize stress user line " + std::to_string(index) + " with a very-long-token-that-must-not-overflow-or-resize-the-layout"});
    stress_transcript.push_back(ava::tui::TranscriptItem{.label = "ava",
                                                         .text = "assistant answer " + std::to_string(index) +
                                                                 " keeps CJK \xE7\x95\x8C and emoji \xF0\x9F\x98\x80 "
                                                                 "inside the measured viewport",
                                                         .meta = "Build · GPT-5.5",
                                                         .thinking = "checked resize path " + std::to_string(index)});
    if (index % 5 == 0)
    {
      stress_transcript.push_back(ava::tui::TranscriptItem{
          .tool = ava::tui::ToolTimelineItem{.status = index % 10 == 0 ? ava::tui::ToolTimelineStatus::Error : ava::tui::ToolTimelineStatus::Success,
                                             .name = "grep",
                                             .argument_summary = "pattern=needle path=src",
                                             .result_summary = "returned " + std::to_string(index) + " matches",
                                             .call_id = "call_resize_" + std::to_string(index),
                                             .lifecycle = index % 10 == 0 ? ava::tui::ToolLifecycleState::Error : ava::tui::ToolLifecycleState::Complete,
                                             .truncated = true,
                                             .visible_matches = 2,
                                             .total_matches = 12,
                                             .spill_path = "/tmp/ava-spill/resize.txt"}});
    }
    if (index % 7 == 0)
    {
      stress_transcript.push_back(ava::tui::TranscriptItem{.label = "audit", .text = "permission replied after resize boundary " + std::to_string(index)});
    }
  }

  ava::tui::SidebarSnapshot const stress_sidebar{
      .activity =
          {ava::tui::SidebarActivityItem{
               .id = "running", .label = "compaction", .detail = "compaction started tokens~9000/8000", .status = ava::tui::ToolTimelineStatus::Running},
           ava::tui::SidebarActivityItem{.id = "done", .label = "read_file", .detail = "assistant responded", .status = ava::tui::ToolTimelineStatus::Success}},
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
  for (auto const width : stress_widths)
  {
    for (auto const height : stress_heights)
    {
      auto frame = ava::tui::render_composer(ava::tui::ComposerSnapshot{.mode = "build",
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
      expect(frame.size() == effective_height &&
                 std::ranges::all_of(frame,
                                     [&](std::string const& line) { return line.find('\n') == std::string::npos && visible_columns(line) <= effective_width; }),
             "tui resize stress render keeps long mixed transcripts bounded at every tested viewport");
    }
  }
  auto cycle_feedback_snapshot = ava::tui::ComposerSnapshot{.mode = "build",
                                                            .provider = "openai",
                                                            .model = "gpt-5.5",
                                                            .session_id = "session_test",
                                                            .input = "",
                                                            .status = "",
                                                            .reasoning_feedback = "reasoning changed to high",
                                                            .transcript = {ava::tui::TranscriptItem{.label = "ava", .text = std::string(1200, 'x')}},
                                                            .width = 120,
                                                            .height = 18,
                                                            .sidebar = ava::tui::SidebarSnapshot{.session_id = "session_test"}};
  auto const hidden_rail_feedback_frame = ava::tui::render_composer(cycle_feedback_snapshot);
  auto rail_feedback_snapshot = cycle_feedback_snapshot;
  rail_feedback_snapshot.width = 176;
  auto const idle_rail_feedback_frame = ava::tui::render_composer(rail_feedback_snapshot);
  auto no_feedback_rail_snapshot = rail_feedback_snapshot;
  no_feedback_rail_snapshot.reasoning_feedback.reset();
  auto const rail_feedback_scroll =
      ava::tui::composer_max_transcript_scroll_offset(rail_feedback_snapshot, rail_feedback_snapshot.width, rail_feedback_snapshot.height);
  auto const no_feedback_rail_scroll =
      ava::tui::composer_max_transcript_scroll_offset(no_feedback_rail_snapshot, no_feedback_rail_snapshot.width, no_feedback_rail_snapshot.height);
  auto ordinary_reasoning_status_snapshot = cycle_feedback_snapshot;
  ordinary_reasoning_status_snapshot.reasoning_feedback.reset();
  ordinary_reasoning_status_snapshot.status = "reasoning ordinary status";
  auto const ordinary_reasoning_status_frame = ava::tui::render_composer(ordinary_reasoning_status_snapshot);
  auto runtime_reasoning_snapshot = cycle_feedback_snapshot;
  runtime_reasoning_snapshot.status = "stale status";
  ava::tui::apply_reasoning_cycle_success(runtime_reasoning_snapshot, "reasoning changed to high");
  auto const runtime_success_is_presentation_only =
      runtime_reasoning_snapshot.status.empty() && runtime_reasoning_snapshot.reasoning_feedback == "reasoning changed to high";
  ava::tui::clear_reasoning_feedback_for_user_input(runtime_reasoning_snapshot);
  expect(std::ranges::any_of(hidden_rail_feedback_frame,
                             [](std::string const& line) { return strip_sgr(line).find("reasoning changed to high") != std::string::npos; }) &&
             std::ranges::none_of(idle_rail_feedback_frame,
                                  [](std::string const& line) { return strip_sgr(line).find("reasoning changed to high") != std::string::npos; }) &&
             rail_feedback_scroll == no_feedback_rail_scroll &&
             std::ranges::none_of(ordinary_reasoning_status_frame,
                                  [](std::string const& line) { return strip_sgr(line).find("reasoning ordinary status") != std::string::npos; }) &&
             runtime_success_is_presentation_only && !runtime_reasoning_snapshot.reasoning_feedback,
         "tui reasoning-cycle feedback is one-action presentation state: visible without a rail, suppressed without rail geometry drift, and cleared by user "
         "input");
}
