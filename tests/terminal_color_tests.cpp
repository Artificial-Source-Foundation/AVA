#include "sys.h"
#include "terminal/Context.h"
#include "tests/support/test_harness.h"

#include <array>
#include <cstdio>
#include <string>

namespace terminal = ava::tui::terminal;

namespace {

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
  test_xterm_indexed_colors_use_standard_palette();
}
