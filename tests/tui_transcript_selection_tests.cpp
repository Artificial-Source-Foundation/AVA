#include "sys.h"
#include "tests/support/test_harness.h"
#include "ava/tui/runtime_transcript_selection_internal.h"
#include "ava/tui/theme.h"

#include <cstddef>
#include <string>
#include <vector>

namespace {

ava::tui::detail::TranscriptLayout selection_layout()
{
  return ava::tui::detail::TranscriptLayout{.lines = {"leading spacer", "\x1b[1mheading\x1b[0m",
                                                      "ab\xE4\xBD\xA0"
                                                      "c",
                                                      "e\xCC\x81x", "", "same"},
                                            .block_boundaries = {1, 4, 6},
                                            .message_starts = {1, 5},
                                            .content_starts = {2, 5},
                                            .message_item_indices = {2, 5}};
}

ava::tui::detail::TranscriptLayoutCache selection_cache(ava::tui::detail::TranscriptLayout layout, std::size_t generation = 1)
{
  ava::tui::detail::TranscriptLayoutCache cache;
  cache.transcript_generation = generation;
  cache.width = 40;
  cache.valid = true;
  cache.layout = std::move(layout);
  return cache;
}

ava::tui::InputEvent mouse_event(ava::tui::Key key, std::size_t row, std::size_t column)
{
  return ava::tui::InputEvent{.key = key, .mouse_column = column, .mouse_row = row};
}

}  // namespace

void run_tui_transcript_selection_tests()
{
  using ava::tui::TranscriptSelectionEndpoint;
  using ava::tui::TranscriptSelectionRange;

  auto const layout = selection_layout();
  auto const heading = ava::tui::endpoint_for_absolute_line(layout, 1, 3);
  auto const wide_left = ava::tui::endpoint_for_absolute_line(layout, 2, 3);
  auto const wide_right = ava::tui::snap_display_column(
      "ab\xE4\xBD\xA0"
      "c",
      3, true);
  expect(!ava::tui::endpoint_for_absolute_line(layout, 0, 0) && !ava::tui::endpoint_for_absolute_line(layout, 4, 0) && heading && heading->item_index == 2 &&
             heading->line_offset == 0 && wide_left && wide_left->display_column == 2 && wide_right == 4,
         "transcript selection owns headings and bodies, rejects inter-item spacers, and snaps wide cells atomically");

  auto const styled_plain = ava::tui::transcript_selection_plain_row("\x1b[31mred\x1b[0m\x1b]8;;https://invalid.example\x1b\\link\x1b]8;;\x1b\\\x01");
  expect(styled_plain == "redlink", "transcript selection strips SGR, OSC, and controls from copied rows");

  auto const extracted = ava::tui::extract_transcript_selection_text(
      layout,
      TranscriptSelectionRange{.anchor = TranscriptSelectionEndpoint{.item_index = 2, .line_offset = 1, .display_column = 1},
                               .focus = TranscriptSelectionEndpoint{.item_index = 2, .line_offset = 2, .display_column = 1}},
      64 * 1024);
  expect(!extracted.oversize &&
             extracted.text ==
                 "b\xE4\xBD\xA0"
                 "c\ne\xCC\x81" &&
             extracted.examined_rows == 2,
         "transcript selection extracts stripped rendered rows with soft-wrap newlines and whole grapheme clusters");

  auto long_layout = ava::tui::detail::TranscriptLayout{
      .lines = {std::string(64 * 1024 + 1, 'x')}, .block_boundaries = {0, 1}, .message_starts = {0}, .content_starts = {0}, .message_item_indices = {0}};
  auto const oversized = ava::tui::extract_transcript_selection_text(
      long_layout,
      TranscriptSelectionRange{.anchor = TranscriptSelectionEndpoint{},
                               .focus = TranscriptSelectionEndpoint{.item_index = 0, .line_offset = 0, .display_column = 64 * 1024 + 1}},
      64 * 1024);
  expect(oversized.oversize && oversized.text.empty() && oversized.examined_rows == 1,
         "transcript selection observes the 64KiB+1 boundary without retaining a truncated clipboard payload");

  auto const highlighted = ava::tui::apply_transcript_selection_highlight("\x1b[31mred\x1b[0m", 1, 3, false);
  auto const no_color = ava::tui::apply_transcript_selection_highlight("\x1b[31mred\x1b[0m", 1, 3, true);
  expect(highlighted.find(ava::tui::detail::kReverseVideo) != std::string::npos && highlighted.find(ava::tui::detail::kReverseVideoOff) != std::string::npos &&
             highlighted.find("\x1b[0m") != std::string::npos && no_color == "red" && no_color.find('\x1b') == std::string::npos,
         "transcript selection highlight uses style-preserving reverse-off and emits no escapes in plain output");

  ava::tui::ComposerSnapshot snapshot;
  snapshot.width = 40;
  snapshot.height = 10;
  snapshot.transcript.resize(1);
  snapshot.transcript[0].text = "body";
  snapshot.transcript[0].tool.emplace();
  auto header_layout = ava::tui::detail::TranscriptLayout{
      .lines = {"tool header", "body text"}, .block_boundaries = {0, 2}, .message_starts = {0}, .content_starts = {0}, .message_item_indices = {0}};
  auto cache = selection_cache(header_layout);
  ava::tui::RuntimeTranscriptSelectionState state;
  ava::tui::RuntimeDraftState draft;
  draft.draft.text = "draft";
  draft.draft_selection_anchor = 0;
  draft.draft_selection_cursor = 3;
  std::size_t scroll = 0;
  std::size_t toggles = 0;
  auto toggle_tool = [&](std::size_t item) {
    ++toggles;
    return item == 0;
  };
  auto never_toggle = [](std::size_t) { return false; };

  static_cast<void>(state.handle_mouse(mouse_event(ava::tui::Key::MouseLeftPress, 1, 2), snapshot, cache, &draft, scroll, toggle_tool, never_toggle));
  static_cast<void>(state.handle_mouse(mouse_event(ava::tui::Key::MouseLeftRelease, 1, 2), snapshot, cache, &draft, scroll, toggle_tool, never_toggle));
  expect(toggles == 1 && state.empty(), "transcript header press/release without movement preserves click-to-toggle fallback");

  static_cast<void>(state.handle_mouse(mouse_event(ava::tui::Key::MouseLeftPress, 1, 2), snapshot, cache, &draft, scroll, toggle_tool, never_toggle));
  static_cast<void>(state.handle_mouse(mouse_event(ava::tui::Key::MouseLeftRelease, 2, 5), snapshot, cache, &draft, scroll, toggle_tool, never_toggle));
  auto header_drag = state.range();
  expect(toggles == 1 && header_drag && header_drag->anchor.item_index == 0 && header_drag->anchor.line_offset == 0 && header_drag->focus.line_offset == 1 &&
             !draft.selection_bounds(),
         "transcript header drag anchors at the original press endpoint, does not toggle, and clears draft selection");

  state.clear();
  static_cast<void>(state.handle_mouse(mouse_event(ava::tui::Key::MouseLeftDrag, 2, 5), snapshot, cache, &draft, scroll, toggle_tool, never_toggle));
  expect(state.empty(), "transcript hover or drag without an owned press never starts selection");

  static_cast<void>(state.handle_mouse(mouse_event(ava::tui::Key::MouseLeftPress, 2, 2), snapshot, cache, &draft, scroll, toggle_tool, never_toggle));
  static_cast<void>(state.handle_mouse(mouse_event(ava::tui::Key::MouseLeftDrag, 2, 6), snapshot, cache, &draft, scroll, toggle_tool, never_toggle));
  static_cast<void>(state.handle_mouse(mouse_event(ava::tui::Key::MouseLeftRelease, 2, 6), snapshot, cache, &draft, scroll, toggle_tool, never_toggle));
  expect(state.range().has_value() && !state.dragging(), "transcript body drag remains selected after release");
  snapshot.status = "copied selection to clipboard";
  auto const copied_frame = ava::tui::render_composer(snapshot);
  expect(std::ranges::any_of(copied_frame, [](std::string const& line) { return strip_sgr(line).find("copied selection to clipboard") != std::string::npos; }),
         "successful transcript copy status is visible in the terminal frame");

  auto scrolling_layout = ava::tui::detail::TranscriptLayout{};
  for (std::size_t line = 0; line < 20; ++line)
    scrolling_layout.lines.push_back("line " + std::to_string(line));
  scrolling_layout.block_boundaries = {0, 20};
  scrolling_layout.message_starts = {0};
  scrolling_layout.content_starts = {0};
  scrolling_layout.message_item_indices = {0};
  auto scrolling_cache = selection_cache(scrolling_layout, snapshot.transcript_generation);
  ava::tui::RuntimeTranscriptSelectionState autoscroll_state;
  snapshot.status.clear();
  scroll = 0;
  static_cast<void>(autoscroll_state.handle_mouse(mouse_event(ava::tui::Key::MouseLeftPress, 2, 2), snapshot, scrolling_cache, &draft, scroll, never_toggle,
                                                  never_toggle));
  static_cast<void>(autoscroll_state.handle_mouse(mouse_event(ava::tui::Key::MouseLeftDrag, 1, 2), snapshot, scrolling_cache, &draft, scroll, never_toggle,
                                                  never_toggle));
  expect(scroll == 1 && autoscroll_state.range(), "transcript selection drag beyond the top edge autoscrolls while retaining selection authority");

  auto remapped_layout = header_layout;
  remapped_layout.lines = {"tool header"};
  remapped_layout.block_boundaries = {0, 1};
  state.rebind_authority(remapped_layout, 2, 40, ava::tui::ToolPresentation::Rich, true, false);
  expect(state.range() && state.range()->anchor.line_offset == 0 && state.range()->focus.line_offset == 0,
         "transcript selection clamps content-relative endpoints after resize or expansion reflow");

  auto shifted_layout = remapped_layout;
  shifted_layout.message_item_indices = {2};
  state.apply_item_index_shift(2, shifted_layout);
  expect(state.range() && state.range()->anchor.item_index == 2 && state.range()->focus.item_index == 2,
         "transcript selection remaps endpoints through positive transcript-cap item shifts");
  shifted_layout.message_item_indices.clear();
  shifted_layout.message_starts.clear();
  shifted_layout.content_starts.clear();
  shifted_layout.block_boundaries.clear();
  state.remap_or_clear(shifted_layout);
  expect(state.empty(), "transcript selection clears when selected content is suppressed or replaced");

  auto duplicate_layout = ava::tui::detail::TranscriptLayout{
      .lines = {"same", "same"}, .block_boundaries = {0, 1, 2}, .message_starts = {0, 1}, .content_starts = {0, 1}, .message_item_indices = {0, 1}};
  auto duplicate_cache = selection_cache(duplicate_layout, 3);
  snapshot.transcript.resize(2);
  snapshot.transcript[0].tool.reset();
  snapshot.transcript[1].text = "same";
  snapshot.transcript[1].stream_id = "stable-stream";
  snapshot.transcript[1].append_only_stream = true;
  static_cast<void>(
      state.handle_mouse(mouse_event(ava::tui::Key::MouseLeftPress, 2, 2), snapshot, duplicate_cache, &draft, scroll, never_toggle, never_toggle));
  static_cast<void>(state.handle_mouse(mouse_event(ava::tui::Key::MouseLeftDrag, 2, 5), snapshot, duplicate_cache, &draft, scroll, never_toggle, never_toggle));
  static_cast<void>(
      state.handle_mouse(mouse_event(ava::tui::Key::MouseLeftRelease, 2, 5), snapshot, duplicate_cache, &draft, scroll, never_toggle, never_toggle));
  expect(state.range() && state.range()->anchor.item_index == 1 && state.range()->focus.item_index == 1,
         "transcript selection ownership is item-indexed and independent of duplicate rendered text");

  snapshot.transcript[1].text = "same appended";
  auto streamed_layout = duplicate_layout;
  streamed_layout.lines[1] = "same appended";
  auto streamed_cache = selection_cache(streamed_layout, 4);
  expect(state.ensure_authority(streamed_cache, &snapshot) && state.range() && state.range()->anchor.item_index == 1,
         "transcript selection survives a proven append-only stream update");
  snapshot.transcript[1].append_only_stream = false;
  snapshot.transcript[1].text = "replacement";
  auto replacement_layout = duplicate_layout;
  replacement_layout.lines[1] = "replacement";
  auto replacement_cache = selection_cache(replacement_layout, 5);
  expect(state.ensure_authority(replacement_cache, &snapshot) && state.empty(), "transcript selection clears when selected source content is replaced");

  ava::tui::detail::TranscriptLayoutCache invalid_cache;
  expect(!state.ensure_authority(invalid_cache) && state.empty(), "transcript selection fails closed instead of rebuilding an invalid layout authority");
}
