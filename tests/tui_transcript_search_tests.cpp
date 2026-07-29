#include "sys.h"
#include "tests/support/test_harness.h"
#include "ava/tui/composer.h"
#include "ava/tui/composer_internal.h"
#include "ava/tui/runtime_transcript_internal.h"
#include "ava/tui/runtime_transcript_search_internal.h"
#include "ava/tui/tool_cards.h"

#include <algorithm>
#include <string>
#include <string_view>
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

void test_transcript_search_is_stable_across_render_widths()
{
  constexpr std::string_view kLongPath = "/workspace/src/components/transcript_search_boundary_stability.cpp";
  constexpr std::string_view kToolToken = "/workspace/generated/very_long_unspaced_tool_card_path/abcdefghijklmnopqrstuvwxyz0123456789/result.json";
  ava::tui::ComposerSnapshot snapshot;
  snapshot.transcript = {
      ava::tui::TranscriptItem{.label = "ava", .text = "prefix alpha beta suffix " + std::string(kLongPath) + " exact Äpfel"},
      ava::tui::TranscriptItem{.tool = ava::tui::ToolTimelineItem{.status = ava::tui::ToolTimelineStatus::Success,
                                                                  .name = "custom_tool",
                                                                  .argument_summary = "inspect",
                                                                  .result_summary = "done",
                                                                  .result_json = "{\"output\":\"" + std::string(kToolToken) + "\"}",
                                                                  .lifecycle = ava::tui::ToolLifecycleState::Complete}},
  };
  for (auto const presentation : {ava::tui::ToolPresentation::Rich, ava::tui::ToolPresentation::Expanded})
  {
    auto const wide_layout = ava::tui::detail::render_transcript_layout(snapshot.transcript, 120, presentation, snapshot.thinking_visible, false);
    auto const narrow_layout = ava::tui::detail::render_transcript_layout(snapshot.transcript, 40, presentation, snapshot.thinking_visible, false);
    auto const wide_phrase = ava::tui::detail::build_transcript_search_matches(snapshot, wide_layout, "alpha beta");
    auto const narrow_phrase = ava::tui::detail::build_transcript_search_matches(snapshot, narrow_layout, "alpha beta");
    auto const wide_path = ava::tui::detail::build_transcript_search_matches(snapshot, wide_layout, kLongPath);
    auto const narrow_path = ava::tui::detail::build_transcript_search_matches(snapshot, narrow_layout, kLongPath);
    auto const wide_tool_token = ava::tui::detail::build_transcript_search_matches(snapshot, wide_layout, kToolToken);
    auto const narrow_tool_token = ava::tui::detail::build_transcript_search_matches(snapshot, narrow_layout, kToolToken);
    auto const wide_non_ascii = ava::tui::detail::build_transcript_search_matches(snapshot, wide_layout, "Äpfel");
    auto const narrow_non_ascii = ava::tui::detail::build_transcript_search_matches(snapshot, narrow_layout, "Äpfel");
    auto const folded_non_ascii = ava::tui::detail::build_transcript_search_matches(snapshot, narrow_layout, "äpfel");
    expect(narrow_layout.lines.size() > wide_layout.lines.size() && wide_phrase.size() == 1 && narrow_phrase.size() == 1 && wide_path.size() == 1 &&
               narrow_path.size() == 1 && wide_tool_token.size() == 1 && narrow_tool_token.size() == 1 && wide_tool_token.front().item_index == 1 &&
               narrow_tool_token.front().item_index == 1 && wide_non_ascii.size() == 1 && narrow_non_ascii.size() == 1 && folded_non_ascii.empty() &&
               !narrow_path.front().detail.empty() && narrow_path.front().detail.size() <= ava::tui::detail::kMaxTranscriptSearchDetailBytes,
           "transcript search preserves prose and actual Rich/Expanded tool-card hard-wrapped unspaced content across wide and narrow renders: presentation=" +
               std::string(ava::tui::to_string(presentation)) + " wide-lines=" + std::to_string(wide_layout.lines.size()) +
               " narrow-lines=" + std::to_string(narrow_layout.lines.size()) + " wide-tool=" + std::to_string(wide_tool_token.size()) +
               " narrow-tool=" + std::to_string(narrow_tool_token.size()));
  }
}

void test_transcript_search_projection_match_and_modal_updates_are_suffix_bounded()
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
  static_cast<void>(cache.update_query("retained item"));
  auto initial_update = cache.rebuild_all(snapshot, layout);
  ava::tui::SelectListView view;
  std::size_t modal_row_build_count = 0;
  ava::tui::detail::update_transcript_search_select_list_rows(view, cache.matches(), cache.query(), initial_update.first_changed_match_row,
                                                              modal_row_build_count);
  auto const initial_builds = cache.projection_build_count();
  auto const initial_scans = cache.match_scan_count();

  snapshot.transcript.back().text = "retained item tail replacement";
  layout.lines.back() = snapshot.transcript.back().text;
  auto tail_update = cache.refresh_after_transcript_mutation(snapshot, layout, 0, ava::tui::kMaxTranscriptItems - 1);
  ava::tui::detail::update_transcript_search_select_list_rows(view, cache.matches(), cache.query(), tail_update.first_changed_match_row, modal_row_build_count);
  auto const query_builds = cache.projection_build_count();
  auto query_update = cache.update_query("RETAINED ITEM");
  ava::tui::detail::update_transcript_search_select_list_rows(view, cache.matches(), cache.query(), query_update.first_changed_match_row,
                                                              modal_row_build_count);
  auto const query_scans = cache.match_scan_count();
  auto const same_query_update = cache.update_query("RETAINED ITEM");
  auto const same_query_scans = cache.match_scan_count();

  snapshot.transcript.back().text = "retained item tail replacement twice";
  layout.lines.back() = snapshot.transcript.back().text;
  auto repeated_tail_update = cache.refresh_after_transcript_mutation(snapshot, layout, 0, ava::tui::kMaxTranscriptItems - 1);
  ava::tui::detail::update_transcript_search_select_list_rows(view, cache.matches(), cache.query(), repeated_tail_update.first_changed_match_row,
                                                              modal_row_build_count);
  auto const empty_update = cache.update_query("");
  ava::tui::detail::update_transcript_search_select_list_rows(view, cache.matches(), cache.query(), empty_update.first_changed_match_row,
                                                              modal_row_build_count);
  snapshot.transcript.back().text = "empty query incremental tail";
  layout.lines.back() = snapshot.transcript.back().text;
  auto empty_tail_update = cache.refresh_after_transcript_mutation(snapshot, layout, 0, ava::tui::kMaxTranscriptItems - 1);
  ava::tui::detail::update_transcript_search_select_list_rows(view, cache.matches(), cache.query(), empty_tail_update.first_changed_match_row,
                                                              modal_row_build_count);

  expect(
      initial_builds == ava::tui::kMaxTranscriptItems && initial_scans == ava::tui::kMaxTranscriptItems && cache.matches().size() == 1000 &&
          tail_update.first_changed_match_row == 999 && query_builds == initial_builds + 1 && query_update.first_changed_match_row == 0 &&
          query_scans == initial_scans + 1 + ava::tui::kMaxTranscriptItems && same_query_update.first_changed_match_row == 1000 &&
          same_query_scans == query_scans && repeated_tail_update.first_changed_match_row == 999 && empty_tail_update.first_changed_match_row == 999 &&
          cache.projection_build_count() == initial_builds + 3 && cache.match_scan_count() == initial_scans + 3 + (2 * ava::tui::kMaxTranscriptItems) &&
          modal_row_build_count == 3003,
      "1,000 matching rows project and scan once, query edits rescan without projection, and repeated or empty-query tail updates rebuild, scan, and recreate "
      "only one suffix row");
}

ava::tui::TranscriptItem context_tool(std::string name, std::string result)
{
  return ava::tui::TranscriptItem{.tool = ava::tui::ToolTimelineItem{.status = ava::tui::ToolTimelineStatus::Success,
                                                                     .name = std::move(name),
                                                                     .argument_summary = "path=src/context.cpp",
                                                                     .result_summary = std::move(result),
                                                                     .lifecycle = ava::tui::ToolLifecycleState::Complete}};
}

void test_transcript_search_rebuilds_negative_shift_render_boundaries()
{
  ava::tui::ComposerSnapshot snapshot;
  snapshot.tool_presentation = ava::tui::ToolPresentation::Compact;
  snapshot.transcript = {context_tool("read_file", "DUPLICATE ASSISTANT RESULT"),
                         ava::tui::TranscriptItem{.label = "ava", .text = "DUPLICATE ASSISTANT RESULT"}};
  auto layout = ava::tui::detail::render_transcript_layout(snapshot.transcript, 80, snapshot.tool_presentation, false, false);
  ava::tui::detail::TranscriptSearchProjectionCache cache;
  static_cast<void>(cache.update_query("duplicate assistant result"));
  static_cast<void>(cache.rebuild_all(snapshot, layout));
  auto const initial_builds = cache.projection_build_count();
  auto const initial_scans = cache.match_scan_count();
  auto initial_matches = cache.matches();

  snapshot.transcript.erase(snapshot.transcript.begin());
  layout = ava::tui::detail::render_transcript_layout(snapshot.transcript, 80, snapshot.tool_presentation, false, false);
  auto update = cache.refresh_after_transcript_mutation(snapshot, layout, -1, snapshot.transcript.size());
  auto visible_matches = cache.matches();
  expect(initial_matches.size() == 1 && initial_matches.front().identity == "tool · read_file" && visible_matches.size() == 1 &&
             visible_matches.front().item_index == 0 && visible_matches.front().identity == "assistant" && update.first_changed_match_row == 0 &&
             cache.projection_build_count() == initial_builds + 1 && cache.match_scan_count() == initial_scans + 1,
         "evicting a tool rebuilds and rescans the new leading assistant so an adjacency-suppressed duplicate becomes visible and searchable");

  snapshot.transcript = {context_tool("read_file", "first context"), context_tool("grep", "second context")};
  layout = ava::tui::detail::render_transcript_layout(snapshot.transcript, 80, snapshot.tool_presentation, false, false);
  static_cast<void>(cache.update_query("2 tools"));
  static_cast<void>(cache.rebuild_all(snapshot, layout));
  auto const pair_builds = cache.projection_build_count();
  auto const pair_scans = cache.match_scan_count();
  snapshot.transcript.push_back(context_tool("glob", "third context"));
  layout = ava::tui::detail::render_transcript_layout(snapshot.transcript, 80, snapshot.tool_presentation, false, false);
  static_cast<void>(cache.refresh_after_transcript_mutation(snapshot, layout, 0, 2));
  expect(cache.matches().empty() && cache.projection_build_count() == pair_builds + 2 && cache.match_scan_count() == pair_scans + 2,
         "a context-tool tail append rebuilds and filters only the changed suffix plus the earlier group-heading owner");

  snapshot.transcript = {context_tool("read_file", "first context"), context_tool("grep", "second context")};
  layout = ava::tui::detail::render_transcript_layout(snapshot.transcript, 80, snapshot.tool_presentation, false, false);
  static_cast<void>(cache.update_query("context gathering"));
  static_cast<void>(cache.rebuild_all(snapshot, layout));
  auto const grouped_builds = cache.projection_build_count();
  auto const grouped_scans = cache.match_scan_count();
  auto grouped = cache.matches();
  snapshot.transcript.erase(snapshot.transcript.begin());
  layout = ava::tui::detail::render_transcript_layout(snapshot.transcript, 80, snapshot.tool_presentation, false, false);
  static_cast<void>(cache.refresh_after_transcript_mutation(snapshot, layout, -1, snapshot.transcript.size()));
  expect(grouped.size() == 1 && grouped.front().item_index == 0 && cache.matches().empty() && cache.projection_build_count() == grouped_builds + 1 &&
             cache.match_scan_count() == grouped_scans + 1,
         "negative cap shift rebuilds the actual new leading context tool and removes its stale synthetic group-heading match");
}

void test_transcript_search_rebuilds_positive_shift_join_boundary()
{
  ava::tui::ComposerSnapshot snapshot;
  snapshot.tool_presentation = ava::tui::ToolPresentation::Compact;
  snapshot.transcript = {ava::tui::TranscriptItem{.label = "ava", .text = "RESTORED PREFIX DUPLICATE"}};
  auto layout = ava::tui::detail::render_transcript_layout(snapshot.transcript, 80, snapshot.tool_presentation, false, false);
  ava::tui::detail::TranscriptSearchProjectionCache cache;
  static_cast<void>(cache.update_query("restored prefix duplicate"));
  static_cast<void>(cache.rebuild_all(snapshot, layout));
  auto const initial_builds = cache.projection_build_count();
  auto const initial_scans = cache.match_scan_count();

  snapshot.transcript.insert(snapshot.transcript.begin(), context_tool("read_file", "RESTORED PREFIX DUPLICATE"));
  layout = ava::tui::detail::render_transcript_layout(snapshot.transcript, 80, snapshot.tool_presentation, false, false);
  static_cast<void>(cache.refresh_after_transcript_mutation(snapshot, layout, 1, snapshot.transcript.size()));
  expect(cache.matches().size() == 1 && cache.matches().front().item_index == 0 && cache.matches().front().identity == "tool · read_file" &&
             cache.projection_build_count() == initial_builds + 1 && cache.match_scan_count() == initial_scans + 2,
         "positive restored-prefix shift filters both the restored projection and first retained join item, removing the newly suppressed assistant match");
}

void test_transcript_search_details_and_queries_are_bounded()
{
  ava::tui::ComposerSnapshot snapshot;
  snapshot.transcript = {ava::tui::TranscriptItem{.label = "ava", .text = "unused"}};
  ava::tui::detail::TranscriptLayout layout{
      .lines = {std::string(400, 'x') + " MATCH"}, .message_starts = {0}, .content_starts = {0}, .message_item_indices = {0}};
  auto matches = ava::tui::detail::build_transcript_search_matches(snapshot, layout, "match");
  std::string max_query(ava::tui::detail::kMaxTranscriptSearchQueryBytes - 1, 'a');
  max_query.push_back('b');
  auto repetitive_candidate = std::string(16 * 1024, 'a') + "b";
  std::string too_long(ava::tui::detail::kMaxTranscriptSearchQueryBytes + 1, 'q');
  expect(matches.size() == 1 && matches.front().detail.size() <= ava::tui::detail::kMaxTranscriptSearchDetailBytes &&
             ava::tui::detail::transcript_search_literal_match(repetitive_candidate, max_query) && !ava::tui::detail::transcript_search_query_valid(too_long) &&
             !ava::tui::detail::transcript_search_query_valid("line\nbreak") && ava::tui::detail::transcript_search_query_valid("spaces are literal"),
         "transcript search bounds rendered details, handles a maximum-length repetitive literal, and rejects oversized or control-bearing modal queries");
}

}  // namespace

void run_tui_transcript_search_tests()
{
  test_transcript_search_literal_and_sanitized_rendered_scope();
  test_transcript_search_uses_current_rendered_tool_and_thinking_presentation();
  test_transcript_search_is_stable_across_render_widths();
  test_transcript_search_projection_match_and_modal_updates_are_suffix_bounded();
  test_transcript_search_rebuilds_negative_shift_render_boundaries();
  test_transcript_search_rebuilds_positive_shift_join_boundary();
  test_transcript_search_details_and_queries_are_bounded();
}
