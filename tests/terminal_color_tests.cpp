#include "sys.h"
#include "terminal/ColorPalette.h"
#include "terminal/Context.h"
#include "tests/support/test_harness.h"

#include <array>
#include <cstdio>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace terminal = ava::tui::terminal;

namespace ava::tui::terminal {

// Expose ColorPalette's pure OSC 4 seams to focused tests without widening the production API.
struct ColorPaletteTestAccess
{
  static std::string queries(int first_index, int past_last_index) { return ColorPalette::osc4_queries(first_index, past_last_index); }
  static std::optional<std::pair<int, Color>> parse_response(std::string_view response) { return ColorPalette::parse_osc4_response(response); }
  static std::optional<std::vector<CIEDE2000::LAB>> parse_responses(std::string_view responses, int number_of_colors)
  {
    return ColorPalette::parse_osc4_responses(responses, number_of_colors);
  }
};

} // namespace ava::tui::terminal

namespace {

// Verify known CIELAB endpoints and exact 8-bit sRGB round trips through the forward and inverse conversions.
void test_srgb_cielab_round_trip()
{
  expect(terminal::ColorPalette::lab_to_rgb({0.0, 0.0, 0.0}).as_int() == 0x000000, "CIELAB black must convert to sRGB black");
  expect(terminal::ColorPalette::lab_to_rgb({100.0, 0.0, 0.0}).as_int() == 0xffffff, "CIELAB D65 white must convert to sRGB white");
  expect(terminal::ColorPalette::lab_to_rgb({53.2371155954293, 80.0882453236802, 67.1996262211358}).as_int() == 0xff0000,
         "the reference CIELAB red must convert to sRGB red");

  constexpr std::array<int, 11> colors = {0x000000, 0xffffff, 0xff0000, 0x00ff00, 0x0000ff, 0x808080, 0x301838, 0xa8e050, 0x102850, 0xe8c8f0, 0x50d8a8};
  for (int const packed_rgb : colors)
  {
    terminal::Color const original{static_cast<uint32_t>(packed_rgb)};
    terminal::Color const round_trip = terminal::ColorPalette::lab_to_rgb(terminal::ColorPalette::rgb_to_lab(original));
    expect(round_trip.as_int() == original.as_int(),
           "sRGB color " + std::to_string(packed_rgb) + " must survive an RGB-to-CIELAB-to-RGB round trip, got " + std::to_string(round_trip.as_int()));
  }
}

// Verify exact OSC 4 query framing and strict parsing of live palette replies with common channel widths and terminators.
void test_osc4_palette_protocol()
{
  using Access = terminal::ColorPaletteTestAccess;
  expect(Access::queries(0, 2) == std::string("\x1b]4;0;?\x1b\\\x1b]4;1;?\x1b\\"),
         "OSC 4 palette queries must use the OSC introducer and terminate each requested index with ST");

  auto const red = Access::parse_response("\x1b]4;7;rgb:ffff/0000/0000\x1b\\");
  expect(red && red->first == 7 && red->second.as_int() == 0xff0000, "OSC 4 must parse a four-digit red response terminated by ST");
  auto const green = Access::parse_response("\x1b]4;3;rgb:00/ff/00\a");
  expect(green && green->first == 3 && green->second.as_int() == 0x00ff00, "OSC 4 must parse a two-digit green response terminated by BEL");
  auto const truncated = Access::parse_response("\x1b]4;4;rgb:00ff/0100/01ff\a");
  expect(truncated && truncated->second.as_int() == 0x000101,
         "four-digit OSC 4 channels must discard the low byte so each consecutive 256-value range maps to one sRGB byte");
  auto const mixed_widths = Access::parse_response("\x1b]4;5;rgb:f/abc/12\a");
  expect(mixed_widths && mixed_widths->second.as_int() == 0xffab12,
         "short OSC 4 channels must expand one hex digit and retain the most significant two digits of longer values");
  expect(!Access::parse_response("\x1b[4;3;rgb:00/ff/00\x1b\\"), "a CSI sequence must not be accepted as an OSC 4 palette response");

  std::string const out_of_order = std::string("ignored") + "\x1b]4;1;rgb:0000/0000/ffff\x1b\\" + "input" + "\x1b]4;0;rgb:ffff/0000/0000\a";
  auto const palette = Access::parse_responses(out_of_order, 2);
  expect(palette && palette->size() == 2 && terminal::ColorPalette::lab_to_rgb((*palette)[0]).as_int() == 0xff0000 &&
             terminal::ColorPalette::lab_to_rgb((*palette)[1]).as_int() == 0x0000ff,
         "OSC 4 responses must produce an index-ordered CIELAB palette while ignoring unrelated bytes");
  expect(!Access::parse_responses("\x1b]4;0;rgb:ffff/0000/0000\a", 2), "an incomplete OSC 4 palette must fail instead of returning partial colors");
}

// Read all bytes emitted to `file`, rewinding it first and leaving it at end-of-file.
//
// The caller must flush any writers before calling this helper. Embedded terminal control bytes are retained.
std::string read_all(FILE* file)
{
  std::rewind(file);
  std::string result;
  std::array<char, 1024> buffer;
  while (std::size_t const count = std::fread(buffer.data(), 1, buffer.size(), file))
    result.append(buffer.data(), count);
  return result;
}

// Verify that xterm-compatible indexed colors use the standardized color cube instead of ncurses' synthetic palette report.
//
// Runs ncurses against temporary files with TERM=xterm-256color and inspects the SGR sequences emitted for a rendered cell. The
// requested green and blue must map to their nearby cube colors rather than basic yellow and black or unmodified grayscale slots.
void test_xterm_indexed_colors_use_standard_palette()
{
  FILE* input = std::tmpfile();
  FILE* output = std::tmpfile();
  if (!input || !output)
  {
    if (input)
      static_cast<void>(std::fclose(input));
    if (output)
      static_cast<void>(std::fclose(output));
    expect(false, "tmpfile must be available for terminal indexed-color tests");
    return;
  }

  {
    ScopedEnvVar term_guard("TERM", "xterm-256color");
    terminal::Context terminal_context(output, input);
    expect(!terminal::ColorPalette::create(terminal_context, 16), "an input stream with no OSC 4 replies must produce no live ColorPalette");
    terminal::ColorPair const pair = terminal_context.create_color_pair({0xa8e050}, {0x102850});
    terminal::BasicWindow window({2, 2}, {0, 0});
    window.attr_set(terminal::Rendition{pair});
    window.addstr("X");
    window.refresh();
  }

  std::fflush(output);
  std::string const emitted = read_all(output);
  expect(emitted.find("38;5;149") != std::string::npos, "xterm-256color must map green 0xa8e050 to nearby green cube entry 149");
  expect(emitted.find("48;5;17") != std::string::npos, "xterm-256color must map blue 0x102850 to nearby blue cube entry 17");
  expect(emitted.find(";rgb:") == std::string::npos, "indexed-color rendering must not attempt to reprogram an emulator's palette");

  static_cast<void>(std::fclose(input));
  static_cast<void>(std::fclose(output));
}

} // namespace

void run_terminal_color_tests()
{
  test_srgb_cielab_round_trip();
  test_osc4_palette_protocol();
  test_xterm_indexed_colors_use_standard_palette();
}
