#include "sys.h"
#include "terminal/Attributes.h"
#include "terminal/BasicWindow.h"
#include "terminal/ColorPair.h"
#include "terminal/ComplexChar.h"
#include "terminal/GraphemeRun.h"
#include "terminal/Pad.h"
#include "terminal/Paragraph.h"
#include "terminal/Rendition.h"
#include "terminal/GraphemeSpan.h"
#include "terminal/TextSpan.h"
#include "tests/support/test_harness.h"

#include <array>
#include <clocale>
#include <cstddef>
#include <cstdio>
#include <string>
#include <vector>

// Disable the ncurses convenience macros (e.g. instr(str)): they collide with
// calls to the like-named terminal::Window member functions below.
#define NCURSES_NOMACROS
#include <curses.h>

namespace terminal = ava::tui::terminal;

namespace {

// Build the Paragraph of the worked example in the comment at the top of src/ava/tui/terminal/Paragraph.cxx.
//
// The TextSpan's whose text contains uppercase (including their spaces) are StyledTextSpan's with `styled_rendition`;
// the other TextSpan's were created without a rendition and must be rendered with the default rendition using
// terminals default fore- and background.
std::unique_ptr<terminal::Paragraph> make_comment_paragraph(terminal::Rendition const& styled_rendition)
{
  auto paragraph = terminal::Paragraph::create({.minimum_width = 16});
  paragraph->append(terminal::TextSpan::create(u8"AAAAAAAAAAA ", styled_rendition));
  paragraph->append(terminal::TextSpan::create(u8" bbb ccc ddd "));
  paragraph->append(terminal::TextSpan::create(u8"EEEEEEEE FFFFF  GGGGG", styled_rendition));
  paragraph->append(terminal::TextSpan::create(u8" hhhhh iii j kkkk lll  "));
  paragraph->append(terminal::TextSpan::create(u8"MMMMM N OOO", styled_rendition));
  paragraph->append(terminal::TextSpan::create(u8" ppp "));
  paragraph->append(terminal::TextSpan::create(u8"Q ", styled_rendition));
  paragraph->append(terminal::TextSpan::create(u8" rrr  sss ttt uuuu vvvv wwww xxx yyy "));
  paragraph->append(terminal::TextSpan::create(u8" ZZZ", styled_rendition));
  return paragraph;
}

// One expected GraphemeRun of a wrapped GraphemeSpan: its wide characters, and whether it must be a run into a
// StyledTextSpan (`styled`) rather than into a TextSpan using the Paragraph default rendition.
struct ExpectedRun
{
  wchar_t const* text;
  bool styled;
};

// One expected GraphemeSpan of the comment example after wrapping at 9 cells: its occupied columns
// (including trailing white-space) and its list of GraphemeRun's, exactly as listed in the comment.
struct ExpectedRow
{
  std::size_t columns;
  std::vector<ExpectedRun> grapheme_runs;
};

// The expected result of Paragraph::wrap(9) for the comment example, row by row.
//
// Note which span the white-space-only runs belong to: the " " of row 6 and of row 11 are leading white-space of
// the following plain TextSpan, while the " " of row 15 is leading white-space of the styled TextSpan " ZZZ".
std::vector<ExpectedRow> const& expected_wrapped_rows()
{
  static std::vector<ExpectedRow> const rows{
      ExpectedRow{9, {{L"AAAAAAAAA", true}}},
      ExpectedRow{8, {{L"AA ", true}, {L" bbb ", false}}},
      ExpectedRow{8, {{L"ccc ddd ", false}}},
      ExpectedRow{9, {{L"EEEEEEEE ", true}}},
      ExpectedRow{7, {{L"FFFFF  ", true}}},
      ExpectedRow{6, {{L"GGGGG", true}, {L" ", false}}},
      ExpectedRow{10, {{L"hhhhh iii ", false}}},
      ExpectedRow{7, {{L"j kkkk ", false}}},
      ExpectedRow{5, {{L"lll  ", false}}},
      ExpectedRow{8, {{L"MMMMM N ", true}}},
      ExpectedRow{11, {{L"OOO", true}, {L" ppp ", false}, {L"Q ", true}, {L" ", false}}},
      ExpectedRow{9, {{L"rrr  sss ", false}}},
      ExpectedRow{9, {{L"ttt uuuu ", false}}},
      ExpectedRow{10, {{L"vvvv wwww ", false}}},
      ExpectedRow{9, {{L"xxx yyy ", false}, {L" ", true}}},
      ExpectedRow{3, {{L"ZZZ", true}}},
  };
  return rows;
}

// Check Paragraph::wrap(9) against the worked example in the comment at the top of Paragraph.cxx.
void test_paragraph_wrap_comment_example()
{
  auto paragraph = make_comment_paragraph(terminal::Rendition{terminal::ColorPair{}});

  std::vector<terminal::GraphemeSpan> rows = paragraph->create_grapheme_spans(9);

  std::vector<ExpectedRow> const& expected = expected_wrapped_rows();
  expect(rows.size() == expected.size(),
         "wrap(9) of the comment example must produce " + std::to_string(expected.size()) + " rows, got " + std::to_string(rows.size()));

  std::wstring concatenated;
  for (std::size_t row = 0; row < rows.size() && row < expected.size(); ++row)
  {
    std::vector<terminal::GraphemeRun> const& grapheme_runs = rows[row].grapheme_runs();

    expect(rows[row].columns() == expected[row].columns, "row " + std::to_string(row) + " must occupy " + std::to_string(expected[row].columns) +
                                                             " terminal columns, got " + std::to_string(rows[row].columns()));
    expect(grapheme_runs.size() == expected[row].grapheme_runs.size(), "row " + std::to_string(row) + " must contain " +
                                                                           std::to_string(expected[row].grapheme_runs.size()) + " GraphemeRun's, got " +
                                                                           std::to_string(grapheme_runs.size()));

    for (std::size_t run = 0; run < grapheme_runs.size() && run < expected[row].grapheme_runs.size(); ++run)
    {
      expect(grapheme_runs[run].str() == expected[row].grapheme_runs[run].text,
             "row " + std::to_string(row) + " run " + std::to_string(run) + " must cover the expected text");
      expect(grapheme_runs[run].text_span()->use_default_rendition() != expected[row].grapheme_runs[run].styled,
             "row " + std::to_string(row) + " run " + std::to_string(run) + " must be a view into " +
                 (expected[row].grapheme_runs[run].styled ? "a StyledTextSpan" : "a TextSpan using the Paragraph default rendition"));
      concatenated += grapheme_runs[run].str();
    }
  }

  // The catenation of all GraphemeRun's of all rows must give again the original string.
  expect(concatenated == L"AAAAAAAAAAA  bbb ccc ddd EEEEEEEE FFFFF  GGGGG hhhhh iii j kkkk lll  MMMMM N OOO ppp Q  rrr  sss ttt uuuu vvvv wwww xxx yyy  ZZZ",
         "the catenation of all GraphemeRun's of all rows must reproduce the original text");
}

// One expected row of the ncurses pad generated by Pad::generate(9) from the comment example: the full 9 cells
// of the row (content plus the spaces that Pad appends to fill the row, which carry the Paragraph default
// rendition) and, for every cell, whether it must have the styled rendition or the Paragraph default rendition.
struct ExpectedPadRow
{
  wchar_t const* cells;
  unsigned styled_mask;      // Bit N is set when cell N must have the styled rendition instead of the default rendition.
};

// The expected pad content, row by row.
//
// Rows whose wrapped width exceeded 9 cells (rows 6, 10, 13 and 11 with widths 10, 9 + clipped white-space, etc.)
// are clipped at 9 cells: only trailing white-space may be dropped. The last row exercises writing up till the
// bottom-right corner of the pad.
std::array<ExpectedPadRow, 16> const& expected_pad_rows()
{
  static std::array<ExpectedPadRow, 16> const rows{{
      {L"AAAAAAAAA", 0x1ff},
      {L"AA  bbb  ", 0x007},
      {L"ccc ddd  ", 0x000},
      {L"EEEEEEEE ", 0x1ff},
      {L"FFFFF    ", 0x07f},
      {L"GGGGG    ", 0x01f},
      {L"hhhhh iii", 0x000},
      {L"j kkkk   ", 0x000},
      {L"lll      ", 0x000},
      {L"MMMMM N  ", 0x0ff},
      {L"OOO ppp Q", 0x107},
      {L"rrr  sss ", 0x000},
      {L"ttt uuuu ", 0x000},
      {L"vvvv wwww", 0x000},
      {L"xxx yyy  ", 0x100},
      {L"ZZZ      ", 0x007},
  }};
  return rows;
}

// Check Pad::generate(9) for the comment example: the ncurses pad must contain, for every cell, the expected
// character and the expected rendition (styled, or Paragraph default).
//
// Runs ncurses against temporary files (never a real terminal) with TERM=xterm-256color; no refresh is performed,
// the pad is inspected through Window::instr.
void test_pad_generate_comment_example()
{
  ScopedEnvVar term_guard("TERM", "xterm-256color");
  FILE* input = std::tmpfile();
  FILE* output = std::tmpfile();
  if (!input || !output)
  {
    if (input)
      static_cast<void>(std::fclose(input));
    if (output)
      static_cast<void>(std::fclose(output));
    expect(false, "tmpfile must be available for the terminal::Pad test");
    return;
  }

  SCREEN* screen = newterm(nullptr, output, input);
  if (!screen)
  {
    static_cast<void>(std::fclose(input));
    static_cast<void>(std::fclose(output));
    expect(false, "newterm must initialize ncurses for the terminal::Pad test");
    return;
  }
  static_cast<void>(set_term(screen));

  bool const color_support = has_colors();
  expect(color_support, "TERM=xterm-256color must provide colors for the terminal::Pad test");
  if (color_support)
  {
    static_cast<void>(start_color());
    // Register two color pairs; only their (distinct) indices matter, the actual colors are never inspected.
    static_cast<void>(::init_extended_pair(1, 2, 0));
    static_cast<void>(::init_extended_pair(2, 4, 0));
  }

  if (color_support)
  {
    terminal::ColorPair styled_pair;
    styled_pair.index() = 1;

    terminal::Rendition const styled_rendition{styled_pair, terminal::Attribute::bold};

    terminal::Pad pad;
    pad.append(make_comment_paragraph(styled_rendition));
    pad.generate(9);

    std::array<ExpectedPadRow, 16> const& expected = expected_pad_rows();

    expect(pad.dimension().width() == 9 && pad.dimension().height() == expected.size(),
           "Pad::generate(9) must create a 9 x 16 pad, got " + std::to_string(pad.dimension().width()) + " x " + std::to_string(pad.dimension().height()));

    std::array<terminal::ComplexChar, 9> cells;
    for (std::size_t row = 0; row < expected.size(); ++row)
    {
      pad.basic_window().instr(terminal::Position{static_cast<uint32_t>(row), 0}, cells.data(), static_cast<int>(cells.size()));

      for (std::size_t col = 0; col < cells.size(); ++col)
      {
        bool const styled = (expected[row].styled_mask >> col) & 1U;
        std::string const where = "pad cell (row " + std::to_string(row) + ", col " + std::to_string(col) + ")";

        expect(cells[col].cell_character().data()[0] == expected[row].cells[col], where + " must contain the expected character");
        expect(cells[col].rendition().color_pair().index() == (styled ? styled_pair.index() : 0),
               where + " must use the " + (styled ? "styled" : "paragraph default") + " color pair, got color pair " +
                   std::to_string(cells[col].rendition().color_pair().index()));
        expect(cells[col].rendition().attributes().mask() ==
                   (styled ? static_cast<terminal::Attributes::attr_t>(terminal::Attribute::bold) : static_cast<terminal::Attributes::attr_t>(0)),
               where + " must use the " + (styled ? "bold" : "normal") + " attributes");
      }
    }
  }

  static_cast<void>(endwin());
  delscreen(screen);
  static_cast<void>(std::fclose(input));
  static_cast<void>(std::fclose(output));
}

// Check compact grapheme metadata for combining marks, flags, emoji modifiers, variation selectors, and ZWJ sequences.
//
// The first wide character of each compact cluster must remain unmarked. Every subsequent wide character that must stay
// attached to that first character is marked combining, including positive-width emoji joined by a zero-width joiner.
void test_text_span_compact_cluster_metadata()
{
  char const* previous_locale_value = std::setlocale(LC_CTYPE, nullptr);
  std::string const previous_locale = previous_locale_value == nullptr ? std::string{} : std::string{previous_locale_value};
  if (std::setlocale(LC_CTYPE, "") == nullptr)
  {
    expect(false, "the environment locale must support UTF-8 for TextSpan compact-cluster metadata coverage");
    return;
  }

  auto text_span = terminal::TextSpan::create(u8"e\u0301\U0001F1E8\U0001F1F3\U0001F469\u200D\U0001F4BB\U0001F44D\U0001F3FD\u2764\uFE0F");
  terminal::GraphemeRun const grapheme_run{*text_span};
  auto const& metadata = grapheme_run.metadata();
  std::array<bool, 11> const expected_combining{
      false, true,       // e + COMBINING ACUTE ACCENT.
      false, true,       // Regional indicators C + N.
      false, true, true, // WOMAN + ZWJ + LAPTOP.
      false, true,       // THUMBS UP + MEDIUM SKIN TONE.
      false, true        // HEART + VARIATION SELECTOR-16.
  };

  expect(metadata.size() == expected_combining.size(),
         "compact-cluster TextSpan must decode to " + std::to_string(expected_combining.size()) + " wide characters, got " + std::to_string(metadata.size()));
  for (std::size_t index = 0; index < metadata.size() && index < expected_combining.size(); ++index)
  {
    expect(metadata[index].combining == expected_combining[index], "compact-cluster wide character " + std::to_string(index) + " must have combining=" +
                                                                       (expected_combining[index] ? std::string{"true"} : std::string{"false"}));
  }

  if (!previous_locale.empty())
    static_cast<void>(std::setlocale(LC_CTYPE, previous_locale.c_str()));
}

// Verify that wrapping `text` to `columns` never starts a GraphemeRun with a continuation of the preceding compact cluster.
//
// Also checks the first row width against `expected_first_row_columns` so a cluster that straddles the boundary cannot merely
// be split into two fragments that happen to remain on the same logical row.
void expect_compact_clusters_stay_whole(std::u8string const& text, uint32_t columns, std::size_t expected_first_row_columns, std::string_view name)
{
  auto paragraph = terminal::Paragraph::create({.minimum_width = 1});
  paragraph->append(terminal::TextSpan::create(text));
  paragraph->initialize_cached_natural_width();

  std::vector<terminal::GraphemeSpan> const rows = paragraph->create_grapheme_spans(columns);
  expect(!rows.empty(), std::string{name} + " must produce at least one wrapped row");
  if (!rows.empty())
    expect(rows.front().columns() == expected_first_row_columns, std::string{name} + " first row must occupy " + std::to_string(expected_first_row_columns) +
                                                                     " columns, got " + std::to_string(rows.front().columns()));

  for (std::size_t row = 0; row < rows.size(); ++row)
  {
    for (terminal::GraphemeRun const& grapheme_run : rows[row].grapheme_runs())
    {
      auto const& metadata = grapheme_run.metadata();
      expect(metadata.empty() || !metadata.front().combining,
             std::string{name} + " row " + std::to_string(row) + " must not begin a GraphemeRun inside a compact grapheme cluster");
    }
  }
}

// Check that Paragraph wrapping treats every Character marked as combining as part of its preceding compact cluster.
void test_paragraph_wrap_compact_clusters()
{
  char const* previous_locale_value = std::setlocale(LC_CTYPE, nullptr);
  std::string const previous_locale = previous_locale_value == nullptr ? std::string{} : std::string{previous_locale_value};
  if (std::setlocale(LC_CTYPE, "") == nullptr)
  {
    expect(false, "the environment locale must support UTF-8 for Paragraph compact-cluster wrapping coverage");
    return;
  }

  expect_compact_clusters_stay_whole(u8"aaaa\U0001F469\u200D\U0001F4BBbbbb", 6, 4, "ZWJ emoji wrapping");
  expect_compact_clusters_stay_whole(u8"aaaa\U0001F44D\U0001F3FDbbbb", 6, 4, "emoji-modifier wrapping");
  expect_compact_clusters_stay_whole(u8"aaaa\U0001F1E8\U0001F1F3bbbb", 5, 4, "regional-indicator wrapping");
  expect_compact_clusters_stay_whole(u8"\U0001F469\u200D\U0001F4BB", 1, 4, "oversized compact-cluster wrapping");

  if (!previous_locale.empty())
    static_cast<void>(std::setlocale(LC_CTYPE, previous_locale.c_str()));
}

} // namespace

void run_terminal_paragraph_tests()
{
  test_paragraph_wrap_comment_example();
  test_pad_generate_comment_example();
  test_text_span_compact_cluster_metadata();
  test_paragraph_wrap_compact_clusters();
}
