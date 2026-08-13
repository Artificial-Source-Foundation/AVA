#include "sys.h"
#include "terminal/Context.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>
#include "debug.h"

namespace terminal = ava::tui::terminal;

std::array<char const*, 5> lorem_ipsum_paragraphs = {
    "Lorem ipsum dolor sit amet, consectetur adipiscing elit. Etiam lacinia, augue quis iaculis hendrerit, risus dui pellentesque augue, nec sodales ipsum "
    "felis at massa. Cras at leo elementum, egestas odio non, mattis dolor. Pellentesque elementum vestibulum diam, nec scelerisque eros hendrerit sed. Morbi "
    "quis lorem neque. Aliquam sollicitudin ante ipsum, id tempor lectus eleifend vitae. Fusce vel pulvinar metus. Sed sed blandit tellus. Etiam et lacinia "
    "lectus. Nunc posuere sit amet ante sit amet porttitor. Pellentesque quis auctor neque. Vivamus sollicitudin eleifend erat, ac sollicitudin augue bibendum "
    "sit amet. Praesent elementum fringilla elit eu hendrerit. Ut id consequat nisi. Vestibulum sapien dolor, ornare vitae dui ac, eleifend ultrices mauris.",
    "Mauris hendrerit dolor id velit gravida fringilla. Mauris porttitor venenatis odio, porttitor elementum nulla semper at. Morbi in massa fringilla, "
    "elementum mauris ut, aliquam urna. Aenean dapibus lobortis arcu ac hendrerit. Curabitur ac tellus nec ligula gravida gravida. In quis odio a erat cursus "
    "imperdiet. In rutrum lacinia nibh et bibendum. Vivamus eget lectus elementum, facilisis augue vel, luctus augue. Curabitur eget libero justo. Aliquam "
    "erat volutpat.",
    "Nullam rhoncus, sem nec congue vulputate, purus nulla tempus ipsum, at bibendum neque sapien cursus felis. Phasellus eu vulputate mi, congue pellentesque "
    "mi. In libero nunc, placerat quis maximus quis, volutpat ut magna. Nulla porttitor felis auctor mi posuere, sed congue sapien ultricies. In lobortis quam "
    "quis quam vestibulum, in egestas mi maximus. Mauris tristique felis lacinia risus tincidunt egestas. Ut nec erat lacinia, auctor leo mattis, malesuada "
    "nulla. Donec molestie id enim et facilisis. Sed tortor diam, ornare id egestas id, luctus in metus. Maecenas id tincidunt mi. Donec tempor viverra "
    "fringilla. Duis sollicitudin porta tellus vel cursus. Cras in imperdiet urna. Vestibulum interdum elementum hendrerit.",
    "Phasellus ac magna nec orci blandit tincidunt. Aenean finibus massa sed quam rutrum euismod. Morbi sollicitudin egestas malesuada. Vestibulum imperdiet, "
    "neque at iaculis congue, sem libero efficitur metus, in dapibus lectus ex consectetur nulla. Suspendisse volutpat, felis vel euismod rhoncus, ipsum massa "
    "sagittis quam, a efficitur mauris metus ut tellus. Donec lectus neque, commodo in elit et, consequat sodales sapien. Curabitur id odio ullamcorper, "
    "scelerisque lorem eget, porta leo. Quisque feugiat arcu quis sem vestibulum, eget bibendum sem dapibus. Sed eu ultricies urna. Morbi luctus mattis "
    "tellus, vitae vulputate ante scelerisque non. Aliquam erat volutpat. Nulla libero augue, ultrices sit amet justo volutpat, finibus maximus urna. Nam "
    "faucibus sed libero aliquam porta. Interdum et malesuada fames ac ante ipsum primis in faucibus. Cras pulvinar blandit urna, eget finibus lacus posuere "
    "non.",
    "Pellentesque fringilla velit vitae justo faucibus blandit. In pulvinar lectus ipsum, quis finibus odio convallis ut. Cras feugiat enim eget maximus "
    "auctor. Aliquam in tempus lectus, sed pharetra magna. Proin accumsan venenatis faucibus. Maecenas eget metus tempus, porta nulla at, rhoncus massa. Nam "
    "semper mollis mattis. Etiam lacinia odio odio, eu ullamcorper neque suscipit a. Donec vitae dui erat. Proin urna ligula, tincidunt nec orci eget, ornare "
    "blandit nisl. In mi velit, mattis non sapien vel, porttitor mollis lorem."};

namespace {

constexpr uint32_t pad_line_count = 2000;
constexpr uint32_t pad_line_width = 200;

// Wrap one space-delimited paragraph into lines no wider than max_width terminal cells.
//
// The test text is ASCII, so byte counts equal terminal-cell counts. Words longer than
// max_width are kept intact because the supplied lorem ipsum contains no such words.
std::vector<std::string> wrap_paragraph(std::string_view paragraph, std::size_t max_width)
{
  std::vector<std::string> lines;
  std::string line;

  for (std::size_t begin = 0; begin < paragraph.size();)
  {
    std::size_t const end = paragraph.find(' ', begin);
    std::string_view const word = paragraph.substr(begin, end == std::string_view::npos ? paragraph.size() - begin : end - begin);
    if (!line.empty() && line.size() + 1 + word.size() > max_width)
    {
      lines.push_back(std::move(line));
      line.clear();
    }
    if (!line.empty())
      line += ' ';
    line += word;
    if (end == std::string_view::npos)
      break;
    begin = end + 1;
  }

  if (!line.empty())
    lines.push_back(std::move(line));
  return lines;
}

// Build exactly pad_line_count lines by cycling through the source paragraphs.
// An empty line follows every complete paragraph, including between repetitions.
std::vector<std::string> make_pad_lines()
{
  std::vector<std::string> lines;
  lines.reserve(pad_line_count);

  for (int paragraph_index = 0; lines.size() < pad_line_count; ++paragraph_index)
  {
    for (std::string& line : wrap_paragraph(lorem_ipsum_paragraphs[paragraph_index % lorem_ipsum_paragraphs.size()], pad_line_width))
    {
      if (lines.size() == pad_line_count)
        break;
      lines.push_back(std::move(line));
    }
    if (lines.size() < pad_line_count)
      lines.emplace_back();
  }
  return lines;
}

// Write one line using its base color and recolor one selected word with highlight_color.
// Empty paragraph separators remain blank and therefore have no word to highlight.
void write_colored_line(terminal::Window& pad, std::size_t row, std::string const& line, terminal::ColorPair base_color, terminal::ColorPair highlight_color)
{
  if (line.empty())
    return;

  std::vector<std::size_t> word_starts{0};
  for (std::size_t pos = line.find(' '); pos != std::string::npos; pos = line.find(' ', pos + 1))
    word_starts.push_back(pos + 1);

  std::size_t const word_begin = word_starts[row % word_starts.size()];
  std::size_t const word_end = line.find(' ', word_begin);
  std::size_t const highlight_end = word_end == std::string::npos ? line.size() : word_end;

  pad.move({static_cast<uint32_t>(row), 0});
  pad.color_set(base_color);
  pad.addstr(line.c_str(), static_cast<int>(word_begin));
  pad.color_set(highlight_color);
  pad.addstr(line.c_str() + word_begin, static_cast<int>(highlight_end - word_begin));
  pad.color_set(base_color);
  pad.addstr(line.c_str() + highlight_end);
}

} // namespace

int main()
{
  Debug(NAMESPACE_DEBUG::init());
  Debug(libcw_do.always_flush_on());

  std::mutex log_mutex;
  std::ofstream log("debug.out");
  Debug(libcw_do.set_ostream(&log, &log_mutex));

  terminal::Context terminal_context;
  terminal::Window const& stdscr = terminal_context.stdscr();

  Dout(dc::notice, "stdscr = " << stdscr.getmaxyx());

  // Create three pads of `pad_line_count` lines that are `pad_line_width` wide.
  terminal::Dimension const pad_dimension{pad_line_count, pad_line_width};
  std::array<terminal::Window, 3> pads = {
    stdscr.newpad(pad_dimension),
    stdscr.newpad(pad_dimension),
    stdscr.newpad(pad_dimension)
  };

  std::array const line_colors = {
      terminal_context.create_color_pair({0xe8e8e8}, {0x182030}),
      terminal_context.create_color_pair({0x201408}, {0xf0d8a8}),
      terminal_context.create_color_pair({0xd8f0e0}, {0x183828}),
      terminal_context.create_color_pair({0x301838}, {0xe8c8f0}),
  };
  std::array const word_colors = {
      terminal_context.create_color_pair({0xfff060}, {0x804020}),
      terminal_context.create_color_pair({0x102850}, {0x70e0f0}),
      terminal_context.create_color_pair({0xffd8f0}, {0x903060}),
      terminal_context.create_color_pair({0x182008}, {0xa8e050}),
  };

  std::vector<std::string> const lines = make_pad_lines();
  for (std::size_t row = 0; row < lines.size(); ++row)
    for (int p = 0; p < pads.size(); ++p)
      write_colored_line(pads[p], row, lines[row], line_colors[row % line_colors.size()], word_colors[(row * 3 + 1) % word_colors.size()]);

  uint32_t const pad_view_height = 17;
  terminal::Position const top_left_first_pad_view{1, 5};
  std::array<terminal::Position, 3> pad_view_pos;
  for (int p = 0; p < pads.size(); ++p)
  {
    terminal::Margin const pad_view_offset{static_cast<uint8_t>(p * pad_view_height), 0};
    pad_view_pos[p] = top_left_first_pad_view + pad_view_offset;
  }
  terminal::Dimension const view_size{pad_view_height, std::min(pad_line_width, stdscr.getmaxyx().width() - top_left_first_pad_view.col())};

  int first_row = 0;
  for (;;)
  {
    for (int p = 0; p < pads.size(); ++p)
      pads[p].prefresh({static_cast<uint32_t>(first_row + p * pad_view_height), 0}, pad_view_pos[p], view_size);

    bool saw_esc = false;
    bool saw_bracket_open = false;
    int count = 0;
    for (;;)
    {
      wint_t key;
      // Input is screen-global, but reading it through stdscr can implicitly refresh
      // stdscr over the pad. Reading through the pad retains ncurses key decoding;
      // pads are deliberately not refreshed as a side effect of input operations.
      pads[0].get_wch(key);

      Dout(dc::notice, "key = " << key);
      if (key == 'q')
        return EXIT_SUCCESS;

      bool saw_scroll_up, saw_scroll_down;
      if (saw_esc && key == '[')
        saw_bracket_open = true;
      else
      {
        saw_scroll_up = false;
        saw_scroll_down = false;
        if (saw_bracket_open && (key == 'A' || key == 'B'))
        {
          saw_scroll_up = key == 'A';
          saw_scroll_down = key == 'B';
        }
        saw_bracket_open = false;
      }

      if (saw_scroll_up || saw_scroll_down)
      {
        if ((++count % 5) == 0)
        {
          first_row = std::clamp(first_row + (saw_scroll_up ? 1 : -1), 0, static_cast<int>(pad_line_count - pads.size() * view_size.height()));
          Dout(dc::notice, "first_row = " << first_row);
          break;
        }
      }

      saw_esc = key == 27;
    }
  }
}
