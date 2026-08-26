#include "sys.h"
#include "terminal/ColorPalette.h"
#include "terminal/Context.h"
#include "tests/support/test_harness.h"

#include <array>
#include <cstdio>
#include <string>

namespace terminal = ava::tui::terminal;

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
  expect(emitted.find("]4;") == std::string::npos, "indexed-color rendering must not attempt to reprogram an emulator's palette");

  static_cast<void>(std::fclose(input));
  static_cast<void>(std::fclose(output));
}

} // namespace

void run_terminal_color_tests()
{
  test_srgb_cielab_round_trip();
  test_xterm_indexed_colors_use_standard_palette();
}
