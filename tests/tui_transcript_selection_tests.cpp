#include "sys.h"
#include "tests/support/test_harness.h"
#include "tests/support/tui_test_support.h"
#include "ava/tui/runtime_transcript_internal.h"
#include "ava/tui/runtime_transcript_selection_internal.h"
#include "ava/tui/theme.h"

#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#include <unistd.h>

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

ava::tui::TranscriptItem private_launch_selection_task()
{
  ava::tui::ToolTimelineItem tool{
      .status = ava::tui::ToolTimelineStatus::Success,
      .name = "task",
      .argument_summary = "arguments provided",
      .result_summary = "ok",
      .arguments_json = R"({"description":"ordinary selectable header","subagent_type":"explore","mode":"foreground"})",
      .result_json = R"({"tool":"task","ok":true,"subagent_type":"explore","description":"ordinary selectable header","state":"completed","tool_calls":1})",
      .call_id = "call_private_launch_selection",
      .lifecycle = ava::tui::ToolLifecycleState::Complete};
  tool.subagent_launch_display = ava::agent::SubagentLaunchDisplay::normalized("PRIVATE MODEL SELECTION TOKEN", std::string_view("private-selection-thinking"));
  return ava::tui::TranscriptItem{.tool = std::move(tool)};
}

template <typename ToggleTool, typename ToggleThinking>
ava::tui::TranscriptSelectionMouseResult handle(ava::tui::RuntimeTranscriptSelectionState& state, ava::tui::InputEvent const& event,
                                                ava::tui::ComposerSnapshot& snapshot, ava::tui::detail::TranscriptLayoutCache const& cache,
                                                ava::tui::RuntimeDraftState* draft, std::size_t& scroll, ToggleTool const& toggle_tool,
                                                ToggleThinking const& toggle_thinking, std::ptrdiff_t frozen_to_live_shift = 0)
{
  return state.handle_mouse(event, snapshot, cache, draft, scroll, frozen_to_live_shift, toggle_tool, toggle_thinking);
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

  auto private_snapshot = ava::tui::ComposerSnapshot{};
  private_snapshot.transcript = {private_launch_selection_task(), ava::tui::TranscriptItem{.label = "ava", .text = "ordinary selectable following content"}};
  auto const private_layout = ava::tui::detail::render_transcript_layout(private_snapshot.transcript, 32, ava::tui::ToolPresentation::Compact, true, true);
  auto const first_private = std::ranges::find(private_layout.presentation_private_rows, true);
  auto const private_count = static_cast<std::size_t>(std::ranges::count(private_layout.presentation_private_rows, true));
  auto const following_position = std::ranges::find(private_layout.message_item_indices, std::size_t{1});
  auto const following_layout_position = static_cast<std::size_t>(following_position - private_layout.message_item_indices.begin());
  auto const following_line = private_layout.message_starts[following_layout_position];
  auto const header_line = private_layout.message_starts.front();
  auto const header_plain = ava::tui::transcript_selection_plain_row(private_layout.lines[header_line]);
  auto const following_plain = ava::tui::transcript_selection_plain_row(private_layout.lines[following_line]);
  auto const private_span = ava::tui::extract_transcript_selection_text(
      private_layout,
      TranscriptSelectionRange{
          .anchor = *ava::tui::endpoint_for_absolute_line(private_layout, header_line, 0),
          .focus = *ava::tui::endpoint_for_absolute_line(private_layout, following_line, ava::tui::transcript_selection_plain_columns(following_plain))},
      64 * 1024);
  auto const private_partial = ava::tui::extract_transcript_selection_text(
      private_layout,
      TranscriptSelectionRange{
          .anchor = *ava::tui::endpoint_for_absolute_line(private_layout,
                                                          static_cast<std::size_t>(first_private - private_layout.presentation_private_rows.begin()), 5),
          .focus = *ava::tui::endpoint_for_absolute_line(private_layout, following_line, ava::tui::transcript_selection_plain_columns(following_plain))},
      64 * 1024);
  auto private_visible_compact = tui_test_support::join_visible_lines(private_layout.lines);
  std::erase_if(private_visible_compact, [](unsigned char ch) { return ch == ' ' || ch == '\n'; });
  expect(private_layout.presentation_private_rows.size() == private_layout.lines.size() && private_count >= 2 &&
             private_span.text == header_plain + "\n" + following_plain && private_partial.text == following_plain &&
             private_span.text.find("PRIVATE MODEL SELECTION TOKEN") == std::string::npos &&
             private_span.text.find("private-selection-thinking") == std::string::npos &&
             private_visible_compact.find("PRIVATEMODELSELECTIONTOKEN") != std::string::npos,
         "rendered transcript selection traverses wrapped private launch rows but excludes their full and partial bytes without blank lines or accidental "
         "joining while ordinary header and following content stay selectable");

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
  std::size_t last_toggled = std::string::npos;
  auto toggle_tool = [&](std::size_t item) {
    ++toggles;
    last_toggled = item;
    return true;
  };
  auto never_toggle = [](std::size_t) { return false; };

  static_cast<void>(handle(state, mouse_event(ava::tui::Key::MouseLeftPress, 1, 2), snapshot, cache, &draft, scroll, toggle_tool, never_toggle));
  static_cast<void>(handle(state, mouse_event(ava::tui::Key::MouseLeftRelease, 1, 2), snapshot, cache, &draft, scroll, toggle_tool, never_toggle));
  expect(toggles == 1 && last_toggled == 0 && state.empty(), "transcript header press/release without movement preserves click-to-toggle fallback");

  static_cast<void>(handle(state, mouse_event(ava::tui::Key::MouseLeftPress, 1, 2), snapshot, cache, &draft, scroll, toggle_tool, never_toggle));
  static_cast<void>(handle(state, mouse_event(ava::tui::Key::MouseLeftRelease, 2, 5), snapshot, cache, &draft, scroll, toggle_tool, never_toggle));
  auto header_drag = state.range();
  expect(toggles == 1 && header_drag && header_drag->anchor.item_index == 0 && header_drag->anchor.line_offset == 0 && header_drag->focus.line_offset == 1 &&
             !draft.selection_bounds(),
         "transcript header drag anchors at the original press endpoint, does not toggle, and clears draft selection");

  state.clear();
  static_cast<void>(handle(state, mouse_event(ava::tui::Key::MouseLeftDrag, 2, 5), snapshot, cache, &draft, scroll, toggle_tool, never_toggle));
  expect(state.empty(), "transcript hover or drag without an owned press never starts selection");

  static_cast<void>(handle(state, mouse_event(ava::tui::Key::MouseLeftPress, 2, 2), snapshot, cache, &draft, scroll, toggle_tool, never_toggle));
  static_cast<void>(handle(state, mouse_event(ava::tui::Key::MouseLeftDrag, 2, 6), snapshot, cache, &draft, scroll, toggle_tool, never_toggle));
  static_cast<void>(handle(state, mouse_event(ava::tui::Key::MouseLeftRelease, 2, 6), snapshot, cache, &draft, scroll, toggle_tool, never_toggle));
  expect(state.range().has_value() && !state.dragging(), "transcript body drag remains selected after release");
  snapshot.status = "copied selection to clipboard";
  auto const copied_frame = ava::tui::render_composer(snapshot);
  expect(std::ranges::any_of(copied_frame, [](std::string const& line) { return strip_sgr(line).find("copied selection to clipboard") != std::string::npos; }),
         "successful transcript copy status is visible in the terminal frame");

  // SEL-004: allowlisted clipboard failure and oversize statuses render in-frame.
  snapshot.status = "clipboard copy failed";
  auto const fail_frame = ava::tui::render_composer(snapshot);
  expect(std::ranges::any_of(fail_frame, [](std::string const& line) { return strip_sgr(line).find("clipboard copy failed") != std::string::npos; }),
         "clipboard copy failed status is visible in the terminal frame");
  snapshot.status = "selection too large to copy";
  auto const oversize_frame = ava::tui::render_composer(snapshot);
  expect(std::ranges::any_of(oversize_frame, [](std::string const& line) { return strip_sgr(line).find("selection too large to copy") != std::string::npos; }),
         "selection too large to copy status is visible in the terminal frame");

  auto scrolling_layout = ava::tui::detail::TranscriptLayout{};
  for (std::size_t line = 0; line < 20; ++line) scrolling_layout.lines.push_back("line " + std::to_string(line));
  scrolling_layout.block_boundaries = {0, 20};
  scrolling_layout.message_starts = {0};
  scrolling_layout.content_starts = {0};
  scrolling_layout.message_item_indices = {0};
  auto scrolling_cache = selection_cache(scrolling_layout, snapshot.transcript_generation);
  ava::tui::RuntimeTranscriptSelectionState autoscroll_state;
  snapshot.status.clear();
  scroll = 0;
  static_cast<void>(
      handle(autoscroll_state, mouse_event(ava::tui::Key::MouseLeftPress, 2, 2), snapshot, scrolling_cache, &draft, scroll, never_toggle, never_toggle));
  static_cast<void>(
      handle(autoscroll_state, mouse_event(ava::tui::Key::MouseLeftDrag, 1, 2), snapshot, scrolling_cache, &draft, scroll, never_toggle, never_toggle));
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
  static_cast<void>(handle(state, mouse_event(ava::tui::Key::MouseLeftPress, 2, 2), snapshot, duplicate_cache, &draft, scroll, never_toggle, never_toggle));
  static_cast<void>(handle(state, mouse_event(ava::tui::Key::MouseLeftDrag, 2, 5), snapshot, duplicate_cache, &draft, scroll, never_toggle, never_toggle));
  static_cast<void>(handle(state, mouse_event(ava::tui::Key::MouseLeftRelease, 2, 5), snapshot, duplicate_cache, &draft, scroll, never_toggle, never_toggle));
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

  // SEL-001: frozen detached layout + deferred leading-eviction shift maps header toggle to the
  // same live source item (tool and thinking), and fails closed when the source is gone.
  {
    ava::tui::ComposerSnapshot frozen_snapshot;
    frozen_snapshot.width = 40;
    frozen_snapshot.height = 10;
    frozen_snapshot.thinking_visible = true;
    frozen_snapshot.transcript.resize(3);
    frozen_snapshot.transcript[0].text = "old leading";
    frozen_snapshot.transcript[1].text = "body";
    frozen_snapshot.transcript[1].tool.emplace();
    frozen_snapshot.transcript[1].tool->name = "bash";
    frozen_snapshot.transcript[1].tool->call_id = "call-tool";
    std::string long_think;
    for (int line = 0; line < 20; ++line)
    {
      long_think += "thinking line ";
      long_think += std::to_string(line);
      long_think.push_back('\n');
    }
    frozen_snapshot.transcript[2].label = "thinking";
    frozen_snapshot.transcript[2].text = long_think;
    frozen_snapshot.transcript[2].thinking_expanded = false;

    // Live transcript after leading eviction: former indices 1/2 are now 0/1.
    frozen_snapshot.transcript.erase(frozen_snapshot.transcript.begin());

    auto frozen_tool_layout = ava::tui::detail::TranscriptLayout{.lines = {"tool header", "body text"},
                                                                 .block_boundaries = {0, 2},
                                                                 .message_starts = {0},
                                                                 .content_starts = {0},
                                                                 // Frozen authority still names pre-shift item 1.
                                                                 .message_item_indices = {1}};
    auto frozen_tool_cache = selection_cache(frozen_tool_layout, 10);
    ava::tui::RuntimeTranscriptSelectionState frozen_state;
    ava::tui::RuntimeDraftState frozen_draft;
    std::size_t frozen_scroll = 0;
    std::size_t tool_toggle_index = std::string::npos;
    std::size_t tool_toggles = 0;
    auto toggle_live_tool = [&](std::size_t item) {
      ++tool_toggles;
      tool_toggle_index = item;
      return item < frozen_snapshot.transcript.size() && frozen_snapshot.transcript[item].tool.has_value();
    };

    static_cast<void>(handle(frozen_state, mouse_event(ava::tui::Key::MouseLeftPress, 1, 2), frozen_snapshot, frozen_tool_cache, &frozen_draft, frozen_scroll,
                             toggle_live_tool, never_toggle, /*frozen_to_live_shift=*/-1));
    expect(frozen_state.dragging(), "frozen tool header press arms without forcing a live layout rebuild");
    static_cast<void>(handle(frozen_state, mouse_event(ava::tui::Key::MouseLeftRelease, 1, 2), frozen_snapshot, frozen_tool_cache, &frozen_draft, frozen_scroll,
                             toggle_live_tool, never_toggle, /*frozen_to_live_shift=*/-1));
    expect(tool_toggles == 1 && tool_toggle_index == 0 && !frozen_state.dragging(),
           "frozen detached tool header release maps through deferred item_index_shift onto the same live source");

    // Body drag under the same frozen authority keeps frozen indices (no live rebuild).
    tool_toggles = 0;
    static_cast<void>(handle(frozen_state, mouse_event(ava::tui::Key::MouseLeftPress, 2, 2), frozen_snapshot, frozen_tool_cache, &frozen_draft, frozen_scroll,
                             toggle_live_tool, never_toggle, /*frozen_to_live_shift=*/-1));
    static_cast<void>(handle(frozen_state, mouse_event(ava::tui::Key::MouseLeftDrag, 2, 6), frozen_snapshot, frozen_tool_cache, &frozen_draft, frozen_scroll,
                             toggle_live_tool, never_toggle, /*frozen_to_live_shift=*/-1));
    static_cast<void>(handle(frozen_state, mouse_event(ava::tui::Key::MouseLeftRelease, 2, 6), frozen_snapshot, frozen_tool_cache, &frozen_draft, frozen_scroll,
                             toggle_live_tool, never_toggle, /*frozen_to_live_shift=*/-1));
    expect(tool_toggles == 0 && frozen_state.range() && frozen_state.range()->anchor.item_index == 1 && frozen_state.range()->focus.item_index == 1,
           "frozen body selection retains frozen item indices and does not force live rebuild or toggle");
    frozen_state.clear();

    // Thinking header under freeze + shift.
    auto frozen_think_layout = ava::tui::detail::TranscriptLayout{
        .lines = {"thinking header", "preview"}, .block_boundaries = {0, 2}, .message_starts = {0}, .content_starts = {0}, .message_item_indices = {2}};
    auto frozen_think_cache = selection_cache(frozen_think_layout, 11);
    std::size_t think_toggle_index = std::string::npos;
    std::size_t think_toggles = 0;
    auto toggle_live_think = [&](std::size_t item) {
      ++think_toggles;
      think_toggle_index = item;
      return item < frozen_snapshot.transcript.size() && !frozen_snapshot.transcript[item].tool.has_value();
    };
    static_cast<void>(handle(frozen_state, mouse_event(ava::tui::Key::MouseLeftPress, 1, 2), frozen_snapshot, frozen_think_cache, &frozen_draft, frozen_scroll,
                             never_toggle, toggle_live_think, /*frozen_to_live_shift=*/-1));
    static_cast<void>(handle(frozen_state, mouse_event(ava::tui::Key::MouseLeftRelease, 1, 2), frozen_snapshot, frozen_think_cache, &frozen_draft,
                             frozen_scroll, never_toggle, toggle_live_think, /*frozen_to_live_shift=*/-1));
    expect(think_toggles == 1 && think_toggle_index == 1,
           "frozen detached thinking header release maps through deferred item_index_shift onto the same live source");

    // Source replacement after arm fails closed (same mapped index, incompatible identity).
    tool_toggles = 0;
    tool_toggle_index = std::string::npos;
    static_cast<void>(handle(frozen_state, mouse_event(ava::tui::Key::MouseLeftPress, 1, 2), frozen_snapshot, frozen_tool_cache, &frozen_draft, frozen_scroll,
                             toggle_live_tool, never_toggle, /*frozen_to_live_shift=*/-1));
    expect(frozen_state.dragging(), "tool header arms before source replacement");
    frozen_snapshot.transcript[0] = ava::tui::TranscriptItem{.label = "ava", .text = "replaced"};
    static_cast<void>(handle(frozen_state, mouse_event(ava::tui::Key::MouseLeftRelease, 1, 2), frozen_snapshot, frozen_tool_cache, &frozen_draft, frozen_scroll,
                             toggle_live_tool, never_toggle, /*frozen_to_live_shift=*/-1));
    expect(tool_toggles == 0 && !frozen_state.dragging(), "header toggle fails closed when the mapped live source was replaced");

    // Leading-eviction of the frozen index itself fails closed on toggle mapping.
    expect(!ava::tui::shift_transcript_selection_item_index(0, -1), "frozen item 0 cannot survive a leading-eviction item_index_shift of -1");

    // Direct click path also maps through the deferred shift.
    frozen_snapshot.transcript[0] = ava::tui::TranscriptItem{.text = "body"};
    frozen_snapshot.transcript[0].tool.emplace();
    frozen_snapshot.transcript[0].tool->name = "bash";
    frozen_snapshot.transcript[0].tool->call_id = "call-tool";
    tool_toggles = 0;
    tool_toggle_index = std::string::npos;
    static_cast<void>(handle(frozen_state, mouse_event(ava::tui::Key::MouseLeftClick, 1, 2), frozen_snapshot, frozen_tool_cache, &frozen_draft, frozen_scroll,
                             toggle_live_tool, never_toggle, /*frozen_to_live_shift=*/-1));
    expect(tool_toggles == 1 && tool_toggle_index == 0, "frozen tool header click maps through deferred item_index_shift");
  }

  // SEL-002: cancel pointer interaction ends Selecting/HeaderArmed without discarding a committed range.
  {
    ava::tui::ComposerSnapshot cancel_snapshot;
    cancel_snapshot.width = 40;
    cancel_snapshot.height = 10;
    cancel_snapshot.transcript.resize(1);
    cancel_snapshot.transcript[0].text = "body";
    cancel_snapshot.transcript[0].tool.emplace();
    auto cancel_layout = ava::tui::detail::TranscriptLayout{
        .lines = {"tool header", "body text"}, .block_boundaries = {0, 2}, .message_starts = {0}, .content_starts = {0}, .message_item_indices = {0}};
    auto cancel_cache = selection_cache(cancel_layout, 20);
    ava::tui::RuntimeTranscriptSelectionState cancel_state;
    ava::tui::RuntimeDraftState cancel_draft;
    std::size_t cancel_scroll = 0;
    std::size_t cancel_toggles = 0;
    auto toggle_cancel = [&](std::size_t) {
      ++cancel_toggles;
      return true;
    };

    static_cast<void>(handle(cancel_state, mouse_event(ava::tui::Key::MouseLeftPress, 1, 2), cancel_snapshot, cancel_cache, &cancel_draft, cancel_scroll,
                             toggle_cancel, never_toggle));
    expect(cancel_state.dragging(), "header press arms before cancel");
    auto const cancel_armed = handle(cancel_state, mouse_event(ava::tui::Key::MousePointerCancel, 1, 2), cancel_snapshot, cancel_cache, &cancel_draft,
                                     cancel_scroll, toggle_cancel, never_toggle);
    expect(cancel_armed == ava::tui::TranscriptSelectionMouseResult::HandledNeedsRender && !cancel_state.dragging() && cancel_toggles == 0,
           "MousePointerCancel clears HeaderArmed without toggling");
    static_cast<void>(handle(cancel_state, mouse_event(ava::tui::Key::MouseLeftRelease, 1, 2), cancel_snapshot, cancel_cache, &cancel_draft, cancel_scroll,
                             toggle_cancel, never_toggle));
    expect(cancel_toggles == 0, "release after pointer cancel cannot toggle an armed header");

    static_cast<void>(handle(cancel_state, mouse_event(ava::tui::Key::MouseLeftPress, 2, 2), cancel_snapshot, cancel_cache, &cancel_draft, cancel_scroll,
                             toggle_cancel, never_toggle));
    static_cast<void>(handle(cancel_state, mouse_event(ava::tui::Key::MouseLeftDrag, 2, 6), cancel_snapshot, cancel_cache, &cancel_draft, cancel_scroll,
                             toggle_cancel, never_toggle));
    expect(cancel_state.dragging() && cancel_state.range(), "body drag establishes an in-flight selection");
    auto const focus_before = cancel_state.range()->focus.display_column;
    cancel_state.cancel_pointer_interaction();
    expect(!cancel_state.dragging() && cancel_state.range() && cancel_state.range()->focus.display_column == focus_before,
           "cancel_pointer_interaction ends Selecting while preserving the range");
    // Production terminals cannot emit Drag after cancel without a new press (g_left_mouse_down
    // is cleared). Release/hover alone must not toggle or mutate the preserved range.
    static_cast<void>(handle(cancel_state, mouse_event(ava::tui::Key::MouseLeftRelease, 2, 10), cancel_snapshot, cancel_cache, &cancel_draft, cancel_scroll,
                             toggle_cancel, never_toggle));
    expect(!cancel_state.dragging() && cancel_toggles == 0 && cancel_state.range() && cancel_state.range()->focus.display_column == focus_before,
           "release after cancel cannot toggle or extend a previously cancelled interaction");
    static_cast<void>(handle(cancel_state, mouse_event(ava::tui::Key::MousePointerCancel, 2, 10), cancel_snapshot, cancel_cache, &cancel_draft, cancel_scroll,
                             toggle_cancel, never_toggle));
    expect(cancel_state.range() && cancel_state.range()->focus.display_column == focus_before, "repeated pointer cancel preserves the committed range");
  }

  // SEL-003: endpoint hit-test uses direct upper_bound on const message_starts (large layout).
  {
    constexpr std::size_t kItems = 4096;
    ava::tui::detail::TranscriptLayout large;
    large.lines.reserve(kItems);
    large.message_starts.reserve(kItems);
    large.content_starts.reserve(kItems);
    large.message_item_indices.reserve(kItems);
    large.block_boundaries.reserve(kItems + 1);
    large.block_boundaries.push_back(0);
    for (std::size_t index = 0; index < kItems; ++index)
    {
      large.lines.push_back("row-" + std::to_string(index));
      large.message_starts.push_back(index);
      large.content_starts.push_back(index);
      large.message_item_indices.push_back(index);
      large.block_boundaries.push_back(index + 1);
    }
    auto const mid = ava::tui::endpoint_for_absolute_line(large, kItems / 2, 1);
    auto const last = ava::tui::endpoint_for_absolute_line(large, kItems - 1, 0);
    expect(mid && mid->item_index == kItems / 2 && mid->line_offset == 0 && last && last->item_index == kItems - 1,
           "endpoint hit-test upper_bounds directly on large const message_starts without copying");
    expect(ava::tui::shift_transcript_selection_item_index(5, -1) == std::size_t{4} && !ava::tui::shift_transcript_selection_item_index(0, -1) &&
               ava::tui::shift_transcript_selection_item_index(2, 3) == std::size_t{5},
           "selection item index shift maps forward and fails closed on leading eviction");
  }

  // SEL-004 production path: oversize copy_selection sets the allowlisted status string.
  // Establish a multi-row selection on short rows (stable geometry), then grow those rows
  // under the same item ownership so middle full rows push the extract past 64 KiB.
  {
    ava::tui::ComposerSnapshot copy_snapshot;
    copy_snapshot.width = 40;
    copy_snapshot.height = 10;
    copy_snapshot.transcript.resize(1);
    copy_snapshot.transcript[0].text = "body";
    constexpr std::size_t kRows = 5;
    ava::tui::detail::TranscriptLayout copy_layout;
    copy_layout.lines.assign(kRows, "short-row");
    copy_layout.block_boundaries = {0, kRows};
    copy_layout.message_starts = {0};
    copy_layout.content_starts = {0};
    copy_layout.message_item_indices = {0};
    auto copy_cache = selection_cache(copy_layout, 30);
    ava::tui::RuntimeTranscriptSelectionState copy_state;
    ava::tui::RuntimeDraftState copy_draft;
    std::size_t copy_scroll = 0;
    static_cast<void>(
        handle(copy_state, mouse_event(ava::tui::Key::MouseLeftPress, 1, 2), copy_snapshot, copy_cache, &copy_draft, copy_scroll, never_toggle, never_toggle));
    static_cast<void>(
        handle(copy_state, mouse_event(ava::tui::Key::MouseLeftDrag, 5, 8), copy_snapshot, copy_cache, &copy_draft, copy_scroll, never_toggle, never_toggle));
    static_cast<void>(handle(copy_state, mouse_event(ava::tui::Key::MouseLeftRelease, 5, 8), copy_snapshot, copy_cache, &copy_draft, copy_scroll, never_toggle,
                             never_toggle));
    expect(copy_state.range().has_value(), "multi-row selection range is established before oversize growth");
    copy_cache.layout.lines.assign(kRows, std::string(20 * 1024, 'x'));
    copy_cache.transcript_generation = 31;
    expect(copy_state.ensure_authority(copy_cache, &copy_snapshot) && copy_state.range().has_value(),
           "selection survives same-item layout payload growth used for oversize copy");
    expect(!copy_state.copy_selection(copy_snapshot, copy_cache) && copy_snapshot.status == "selection too large to copy",
           "copy_selection reports selection too large to copy for oversize payloads");
    auto const copy_frame = ava::tui::render_composer(copy_snapshot);
    expect(std::ranges::any_of(copy_frame, [](std::string const& line) { return strip_sgr(line).find("selection too large to copy") != std::string::npos; }),
           "oversize copy failure status is visible in the terminal frame");
  }

  // Production copy path over a frozen detached authority: geometry may cross the
  // private row, but the OSC52 payload must be built only from ordinary rows.
  {
    ava::tui::ComposerSnapshot copy_snapshot;
    copy_snapshot.width = 40;
    copy_snapshot.height = 10;
    copy_snapshot.transcript = {private_launch_selection_task()};
    auto frozen_private_layout = ava::tui::detail::TranscriptLayout{.lines = {"ordinary header", "PRIVATE FROZEN LAUNCH", "ordinary following"},
                                                                    .presentation_private_rows = {false, true, false},
                                                                    .block_boundaries = {0, 3},
                                                                    .message_starts = {0},
                                                                    .content_starts = {0},
                                                                    .message_item_indices = {1}};
    auto frozen_private_cache = selection_cache(std::move(frozen_private_layout), 40);
    ava::tui::RuntimeTranscriptSelectionState copy_state;
    ava::tui::RuntimeDraftState copy_draft;
    std::size_t copy_scroll = 0;
    static_cast<void>(handle(copy_state, mouse_event(ava::tui::Key::MouseLeftPress, 1, 2), copy_snapshot, frozen_private_cache, &copy_draft, copy_scroll,
                             never_toggle, never_toggle, /*frozen_to_live_shift=*/-1));
    static_cast<void>(handle(copy_state, mouse_event(ava::tui::Key::MouseLeftDrag, 3, 20), copy_snapshot, frozen_private_cache, &copy_draft, copy_scroll,
                             never_toggle, never_toggle, /*frozen_to_live_shift=*/-1));
    static_cast<void>(handle(copy_state, mouse_event(ava::tui::Key::MouseLeftRelease, 3, 20), copy_snapshot, frozen_private_cache, &copy_draft, copy_scroll,
                             never_toggle, never_toggle, /*frozen_to_live_shift=*/-1));
    auto const copy_range = copy_state.range();
    auto const expected_text =
        copy_range ? ava::tui::extract_transcript_selection_text(frozen_private_cache.layout, *copy_range, 64 * 1024).text : std::string{};
    auto const expected_sequence = ava::tui::runtime_transcript::try_build_osc52_clipboard_sequence(expected_text);

    auto* output = std::tmpfile();
    auto const saved_stdout = output ? dup(STDOUT_FILENO) : -1;
    auto redirected = saved_stdout >= 0 && std::fflush(stdout) == 0 && dup2(fileno(output), STDOUT_FILENO) >= 0;
    auto copied = false;
    if (redirected)
      copied = copy_state.copy_selection(copy_snapshot, frozen_private_cache);
    if (saved_stdout >= 0)
    {
      static_cast<void>(std::fflush(stdout));
      static_cast<void>(dup2(saved_stdout, STDOUT_FILENO));
      static_cast<void>(close(saved_stdout));
    }
    std::string captured;
    if (output)
    {
      if (std::fflush(output) == 0 && std::fseek(output, 0, SEEK_END) == 0)
      {
        auto const size = std::ftell(output);
        if (size >= 0 && std::fseek(output, 0, SEEK_SET) == 0)
        {
          captured.resize(static_cast<std::size_t>(size));
          if (!captured.empty() && std::fread(captured.data(), 1, captured.size(), output) != captured.size())
            captured.clear();
        }
      }
      static_cast<void>(std::fclose(output));
    }

    expect(copy_range && expected_sequence && expected_text.find("rdinary header") != std::string::npos &&
               expected_text.find("ordinary following") != std::string::npos && expected_text.find("PRIVATE FROZEN LAUNCH") == std::string::npos && copied &&
               copy_snapshot.status == "copied selection to clipboard" && captured == *expected_sequence,
           "frozen detached selection copy emits an OSC52 payload containing ordinary cross-row text but no private launch bytes");
  }
}
