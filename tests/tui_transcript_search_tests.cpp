#include "sys.h"
#include "tests/support/test_harness.h"
#include "ava/tui/composer.h"
#include "ava/tui/composer_internal.h"
#include "ava/tui/runtime_transcript_internal.h"
#include "ava/tui/runtime_transcript_search_internal.h"
#include "ava/tui/tool_cards.h"

#include <algorithm>
#include <string>
#include <vector>

namespace {

void test_transcript_search_literal_and_sanitized_rendered_scope()
{
  ava::tui::ComposerSnapshot snapshot;
  snapshot.transcript = {
      ava::tui::TranscriptItem{.label = "you", .text = "unused source"},
      ava::tui::TranscriptItem{.label = "ava", .text = "unused duplicate"},
      ava::tui::TranscriptItem{.tool = ava::tui::ToolTimelineItem{.status = ava::tui::ToolTimelineStatus::Success, .name = "read"}},
  };
  ava::tui::detail::TranscriptLayout layout{
      .lines = {"\x1b[31mALPHA first\x1b[0m", "", "\x1b]8;;https://example.invalid\x1b\\alpha duplicate\x1b]8;;\x1b\\",
                std::string("tool result ") + char{1} + " Äpfel"},
      .message_starts = {0, 2, 3},
      .content_starts = {0, 2, 3},
      .message_item_indices = {0, 1, 2},
  };

  auto alpha = ava::tui::detail::build_transcript_search_matches(snapshot, layout, "alpha");
  auto upper_non_ascii = ava::tui::detail::build_transcript_search_matches(snapshot, layout, "Ä");
  auto lower_non_ascii = ava::tui::detail::build_transcript_search_matches(snapshot, layout, "ä");
  auto all = ava::tui::detail::build_transcript_search_matches(snapshot, layout, "");
  expect(alpha.size() == 2 && alpha[0].item_index == 0 && alpha[1].item_index == 1 && alpha[0].identity == "user" && alpha[1].identity == "assistant" &&
             alpha[0].detail == "ALPHA first" && alpha[1].detail == "alpha duplicate" &&
             ava::tui::detail::build_transcript_search_matches(snapshot, layout, "example.invalid").empty(),
         "transcript search is ASCII-case-insensitive, strips SGR/OSC, and retains duplicate rendered items in chronological order");
  expect(upper_non_ascii.size() == 1 && upper_non_ascii[0].item_index == 2 && lower_non_ascii.empty(),
         "transcript search keeps non-ASCII UTF-8 matching byte-exact");
  expect(all.size() == 3 && all[2].identity == "tool · read" && all[2].detail == "tool result ? Äpfel",
         "empty transcript search lists every rendered block with sanitized safe identities and first nonblank details");
}

void test_transcript_search_uses_current_rendered_tool_and_thinking_presentation()
{
  ava::tui::ComposerSnapshot snapshot;
  snapshot.tool_presentation = ava::tui::ToolPresentation::Rich;
  snapshot.thinking_visible = false;
  snapshot.transcript = {
      ava::tui::TranscriptItem{.label = "ava", .text = "visible assistant answer", .thinking = "SECRET HIDDEN THINKING"},
      ava::tui::TranscriptItem{.tool = ava::tui::ToolTimelineItem{.status = ava::tui::ToolTimelineStatus::Success,
                                                                  .name = "custom_tool",
                                                                  .argument_summary = "needle=alpha",
                                                                  .result_summary = "done",
                                                                  .result_json = R"({"output":"rendered rich result alpha"})",
                                                                  .lifecycle = ava::tui::ToolLifecycleState::Complete}},
  };
  auto rich_layout = ava::tui::detail::render_transcript_layout(snapshot.transcript, 80, snapshot.tool_presentation, snapshot.thinking_visible, false);
  auto hidden = ava::tui::detail::build_transcript_search_matches(snapshot, rich_layout, "SECRET");
  auto rich = ava::tui::detail::build_transcript_search_matches(snapshot, rich_layout, "rich result");

  snapshot.tool_presentation = ava::tui::ToolPresentation::Compact;
  auto compact_layout = ava::tui::detail::render_transcript_layout(snapshot.transcript, 80, snapshot.tool_presentation, snapshot.thinking_visible, false);
  auto compact = ava::tui::detail::build_transcript_search_matches(snapshot, compact_layout, "rich result");
  expect(hidden.empty() && rich.size() == 1 && rich.front().item_index == 1 && compact.empty(),
         "transcript search includes only current rendered thinking and Rich/Compact tool-card content");

  snapshot.thinking_visible = true;
  auto thinking_layout = ava::tui::detail::render_transcript_layout(snapshot.transcript, 80, snapshot.tool_presentation, snapshot.thinking_visible, false);
  auto visible_thinking = ava::tui::detail::build_transcript_search_matches(snapshot, thinking_layout, "secret hidden thinking");
  expect(visible_thinking.size() == 1 && visible_thinking.front().item_index == 0,
         "transcript search includes thinking only when the current transcript layout renders it");
}

void test_transcript_search_is_stable_across_soft_wraps()
{
  ava::tui::ComposerSnapshot snapshot;
  snapshot.transcript = {ava::tui::TranscriptItem{.label = "ava", .text = "prefix alpha beta suffix Äpfel"}};
  auto const wide_layout = ava::tui::detail::render_transcript_layout(snapshot.transcript, 80, snapshot.tool_presentation, snapshot.thinking_visible, false);
  auto const narrow_layout = ava::tui::detail::render_transcript_layout(snapshot.transcript, 14, snapshot.tool_presentation, snapshot.thinking_visible, false);
  ava::tui::detail::TranscriptSearchProjectionCache cache;
  cache.rebuild_all(snapshot, wide_layout);
  auto const wide_phrase = cache.matches("alpha beta");
  auto const wide_builds = cache.projection_build_count();
  cache.rebuild_all(snapshot, narrow_layout);
  auto const narrow_phrase = cache.matches("alpha beta");
  auto const exact_non_ascii = cache.matches("Äpfel");
  auto const folded_non_ascii = cache.matches("äpfel");
  expect(narrow_layout.lines.size() > wide_layout.lines.size() && wide_phrase.size() == 1 && narrow_phrase.size() == 1 &&
             wide_phrase.front().item_index == narrow_phrase.front().item_index && wide_builds == 1 && cache.projection_build_count() == 2 &&
             exact_non_ascii.size() == 1 && folded_non_ascii.empty(),
         "transcript search preserves literal phrase identity across soft wrapping while keeping non-ASCII matching exact");
}

void test_transcript_search_projection_cache_rebuilds_only_changed_suffix()
{
  ava::tui::ComposerSnapshot snapshot;
  ava::tui::detail::TranscriptLayout layout;
  snapshot.transcript.reserve(ava::tui::kMaxTranscriptItems);
  layout.lines.reserve(ava::tui::kMaxTranscriptItems);
  layout.message_starts.reserve(ava::tui::kMaxTranscriptItems);
  layout.content_starts.reserve(ava::tui::kMaxTranscriptItems);
  layout.message_item_indices.reserve(ava::tui::kMaxTranscriptItems);
  for (std::size_t index = 0; index < ava::tui::kMaxTranscriptItems; ++index)
  {
    auto text = "retained item [" + std::to_string(index) + "]";
    snapshot.transcript.push_back(ava::tui::TranscriptItem{.label = "ava", .text = text});
    layout.lines.push_back(text);
    layout.message_starts.push_back(index);
    layout.content_starts.push_back(index);
    layout.message_item_indices.push_back(index);
  }

  ava::tui::detail::TranscriptSearchProjectionCache cache;
  cache.rebuild_all(snapshot, layout);
  auto const initial_builds = cache.projection_build_count();
  snapshot.transcript.back().text = "active tail replacement";
  layout.lines.back() = "active tail replacement";
  cache.refresh_after_transcript_mutation(snapshot, layout, 0, ava::tui::kMaxTranscriptItems - 1);
  auto const tail_builds = cache.projection_build_count();
  auto const active_tail = cache.matches("active tail replacement");
  auto const builds_before_queries = cache.projection_build_count();
  static_cast<void>(cache.matches("retained item [500]"));
  static_cast<void>(cache.matches("ACTIVE TAIL"));

  auto const old_layout = layout;
  auto const old_max_scroll = old_layout.lines.size() - 20;
  auto const old_anchor = ava::tui::detail::capture_transcript_viewport_anchor(old_layout, old_max_scroll, old_max_scroll - 500);
  snapshot.transcript.erase(snapshot.transcript.begin());
  snapshot.transcript.push_back(ava::tui::TranscriptItem{.label = "ava", .text = "new capped tail"});
  layout.lines.erase(layout.lines.begin());
  layout.lines.push_back("new capped tail");
  cache.refresh_after_transcript_mutation(snapshot, layout, -1, ava::tui::kMaxTranscriptItems - 1);
  auto const builds_after_eviction = cache.projection_build_count();
  auto const retained_after_eviction = cache.matches("retained item [1]");
  auto const new_tail = cache.matches("new capped tail");
  auto const new_max_scroll = layout.lines.size() - 20;
  auto const restored_scroll = ava::tui::detail::restore_transcript_viewport_anchor(old_anchor, layout, new_max_scroll, -1);
  auto const restored_start = new_max_scroll - std::min(restored_scroll, new_max_scroll);
  auto const shifted_selection = ava::tui::detail::shift_transcript_search_item_index(std::size_t{500}, -1);

  expect(initial_builds == ava::tui::kMaxTranscriptItems && tail_builds == initial_builds + 1 && active_tail.size() == 1 &&
             builds_before_queries == tail_builds && builds_after_eviction == tail_builds + 1 && cache.projection_build_count() == builds_after_eviction &&
             retained_after_eviction.size() == 1 && retained_after_eviction.front().item_index == 0 && new_tail.size() == 1 &&
             new_tail.front().item_index == ava::tui::kMaxTranscriptItems - 1 && shifted_selection == std::optional<std::size_t>{499} && restored_start == 499,
         "transcript search caches 1,000 retained projections, rebuilds only changed tails, shifts cache/selection/anchor on eviction, and does no query "
         "rebuilds");
}

void test_transcript_search_details_and_queries_are_bounded()
{
  ava::tui::ComposerSnapshot snapshot;
  snapshot.transcript = {ava::tui::TranscriptItem{.label = "ava", .text = "unused"}};
  ava::tui::detail::TranscriptLayout layout{
      .lines = {std::string(400, 'x') + " MATCH"}, .message_starts = {0}, .content_starts = {0}, .message_item_indices = {0}};
  auto matches = ava::tui::detail::build_transcript_search_matches(snapshot, layout, "match");
  std::string too_long(ava::tui::detail::kMaxTranscriptSearchQueryBytes + 1, 'q');
  expect(matches.size() == 1 && matches.front().detail.size() <= ava::tui::detail::kMaxTranscriptSearchDetailBytes &&
             !ava::tui::detail::transcript_search_query_valid(too_long) && !ava::tui::detail::transcript_search_query_valid("line\nbreak") &&
             ava::tui::detail::transcript_search_query_valid("spaces are literal"),
         "transcript search bounds rendered details and rejects oversized or control-bearing modal queries");
}

}  // namespace

void run_tui_transcript_search_tests()
{
  test_transcript_search_literal_and_sanitized_rendered_scope();
  test_transcript_search_uses_current_rendered_tool_and_thinking_presentation();
  test_transcript_search_is_stable_across_soft_wraps();
  test_transcript_search_projection_cache_rebuilds_only_changed_suffix();
  test_transcript_search_details_and_queries_are_bounded();
}
