#include "sys.h"
#include "tests/support/test_harness.h"
#include "ava/tui/composer.h"
#include "ava/tui/composer_internal.h"
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
  test_transcript_search_details_and_queries_are_bounded();
}
