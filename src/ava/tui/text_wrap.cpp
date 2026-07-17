#include "sys.h"
#include "ava/tui/composer_internal.h"
#include "ava/tui/text_wrap.h"

#include <algorithm>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ava::tui::detail {
namespace {

struct SgrState
{
  bool bold = false;
  bool dim = false;
  bool italic = false;
  bool underline = false;
  bool strikethrough = false;
  bool reverse = false;
  std::optional<std::string> foreground;
  std::optional<std::string> background;

  void reset()
  {
    bold = false;
    dim = false;
    italic = false;
    underline = false;
    strikethrough = false;
    reverse = false;
    foreground.reset();
    background.reset();
  }

  [[nodiscard]] bool active() const
  {
    return bold || dim || italic || underline || strikethrough || reverse || foreground.has_value() || background.has_value();
  }

  [[nodiscard]] std::string reopen_sequence() const
  {
    std::string sequence;
    if (bold)
      sequence += "\x1b[1m";
    if (dim)
      sequence += "\x1b[2m";
    if (italic)
      sequence += "\x1b[3m";
    if (underline)
      sequence += "\x1b[4m";
    if (strikethrough)
      sequence += "\x1b[9m";
    if (reverse)
      sequence += "\x1b[7m";
    if (foreground)
      sequence += *foreground;
    if (background)
      sequence += *background;
    return sequence;
  }
};

inline constexpr std::string_view kOsc8Close = "\x1b]8;;\x1b\\";

[[nodiscard]] bool sgr_sequence_at(std::string_view text, std::size_t index, std::size_t& end)
{
  if (index + 1 >= text.size() || text[index] != '\x1b' || text[index + 1] != '[')
  {
    return false;
  }

  auto cursor = index + 2;
  while (cursor < text.size() && text[cursor] != 'm')
  {
    ++cursor;
  }
  if (cursor >= text.size())
  {
    return false;
  }
  end = cursor + 1;
  return true;
}

[[nodiscard]] std::optional<std::vector<int>> parse_sgr_parameters(std::string_view sequence)
{
  if (sequence.size() < 3 || sequence[0] != '\x1b' || sequence[1] != '[' || sequence.back() != 'm')
  {
    return std::nullopt;
  }

  auto const body = sequence.substr(2, sequence.size() - 3);
  if (body.empty())
  {
    return std::vector<int>{0};
  }

  std::vector<int> parameters;
  int value = 0;
  bool has_digit = false;
  for (char const ch : body)
  {
    if (ch >= '0' && ch <= '9')
    {
      has_digit = true;
      value = std::min(999999, (value * 10) + static_cast<int>(ch - '0'));
      continue;
    }
    if (ch != ';')
    {
      return std::nullopt;
    }
    parameters.push_back(has_digit ? value : 0);
    value = 0;
    has_digit = false;
  }
  parameters.push_back(has_digit ? value : 0);
  return parameters;
}

[[nodiscard]] std::string sgr_from_parameters(std::vector<int> const& parameters, std::size_t first, std::size_t last_exclusive)
{
  std::string sequence = "\x1b[";
  for (auto index = first; index < last_exclusive; ++index)
  {
    if (index > first)
      sequence.push_back(';');
    sequence += std::to_string(parameters[index]);
  }
  sequence.push_back('m');
  return sequence;
}

[[nodiscard]] bool store_extended_color(std::vector<int> const& parameters, std::size_t& index, std::optional<std::string>& target)
{
  if (index + 1 >= parameters.size())
  {
    return false;
  }

  auto const mode = parameters[index + 1];
  if (mode == 5 && index + 2 < parameters.size())
  {
    target = sgr_from_parameters(parameters, index, index + 3);
    index += 2;
    return true;
  }
  if (mode == 5)
  {
    index += 1;
    return false;
  }
  if (mode == 2 && index + 4 < parameters.size())
  {
    target = sgr_from_parameters(parameters, index, index + 5);
    index += 4;
    return true;
  }
  if (mode == 2)
  {
    index += parameters.size() - index - 1;
    return false;
  }
  return false;
}

[[nodiscard]] bool osc8_sequence_is_close(std::string_view sequence)
{
  if (!sequence.starts_with("\x1b]8;"))
    return false;
  auto const target_start = sequence.find(';', 4);
  if (target_start == std::string_view::npos)
    return false;
  auto cursor = target_start + 1;
  if (cursor >= sequence.size())
    return false;
  if (sequence[cursor] == '\a')
    return true;
  return sequence[cursor] == '\x1b' && cursor + 1 < sequence.size() && sequence[cursor + 1] == '\\';
}

[[nodiscard]] bool osc8_sequence_is_open(std::string_view sequence)
{
  return sequence.starts_with("\x1b]8;") && !osc8_sequence_is_close(sequence);
}

void apply_sgr_sequence(std::string_view sequence, SgrState& state)
{
  auto parameters = parse_sgr_parameters(sequence);
  if (!parameters)
  {
    return;
  }

  for (std::size_t index = 0; index < parameters->size(); ++index)
  {
    auto const parameter = (*parameters)[index];
    if (parameter == 0)
    {
      state.reset();
    }
    else if (parameter == 1)
    {
      state.bold = true;
    }
    else if (parameter == 2)
    {
      state.dim = true;
    }
    else if (parameter == 3)
    {
      state.italic = true;
    }
    else if (parameter == 4 || parameter == 21)
    {
      state.underline = true;
    }
    else if (parameter == 7)
    {
      state.reverse = true;
    }
    else if (parameter == 9)
    {
      state.strikethrough = true;
    }
    else if (parameter == 22)
    {
      state.bold = false;
      state.dim = false;
    }
    else if (parameter == 23)
    {
      state.italic = false;
    }
    else if (parameter == 24)
    {
      state.underline = false;
    }
    else if (parameter == 27)
    {
      state.reverse = false;
    }
    else if (parameter == 29)
    {
      state.strikethrough = false;
    }
    else if ((parameter >= 30 && parameter <= 37) || (parameter >= 90 && parameter <= 97))
    {
      state.foreground = sgr_from_parameters(*parameters, index, index + 1);
    }
    else if (parameter == 38)
    {
      static_cast<void>(store_extended_color(*parameters, index, state.foreground));
    }
    else if (parameter == 39)
    {
      state.foreground.reset();
    }
    else if ((parameter >= 40 && parameter <= 47) || (parameter >= 100 && parameter <= 107))
    {
      state.background = sgr_from_parameters(*parameters, index, index + 1);
    }
    else if (parameter == 48)
    {
      static_cast<void>(store_extended_color(*parameters, index, state.background));
    }
    else if (parameter == 49)
    {
      state.background.reset();
    }
  }
}

}  // namespace

std::vector<std::string> wrap_ansi_text(std::string_view text, std::size_t width)
{
  auto const content_width = std::max<std::size_t>(1, width);
  std::vector<std::string> lines;
  SgrState sgr_state;
  std::optional<std::string> active_osc8;
  std::string current;
  std::size_t columns = 0;

  auto finish_current_line = [&] {
    if (active_osc8)
      current += std::string(kOsc8Close);
    if (sgr_state.active())
      current += std::string(kSgrReset);
    lines.push_back(std::move(current));
    current = active_osc8.value_or(std::string{}) + sgr_state.reopen_sequence();
    columns = 0;
  };

  for (std::size_t index = 0; index < text.size();)
  {
    if (text[index] == '\r' || text[index] == '\n')
    {
      auto const newline = text[index];
      ++index;
      if (newline == '\r' && index < text.size() && text[index] == '\n')
        ++index;
      finish_current_line();
      continue;
    }

    std::size_t sequence_end = 0;
    if (sgr_sequence_at(text, index, sequence_end))
    {
      auto const sequence = text.substr(index, sequence_end - index);
      current.append(sequence);
      apply_sgr_sequence(sequence, sgr_state);
      index = sequence_end;
      continue;
    }

    auto const before_osc = index;
    if (skip_osc_sequence(text, index))
    {
      auto const sequence = text.substr(before_osc, index - before_osc);
      current.append(sequence);
      if (osc8_sequence_is_close(sequence))
      {
        active_osc8.reset();
      }
      else if (osc8_sequence_is_open(sequence))
      {
        active_osc8 = std::string(sequence);
      }
      continue;
    }

    auto const cell = terminal_text_cell(text, index);
    auto const chunk_length = cell.valid ? cell.bytes : std::size_t{1};
    auto const chunk_columns = cell.columns;
    if (columns + chunk_columns > content_width && columns > 0)
    {
      finish_current_line();
      continue;
    }

    current.append(text.substr(index, chunk_length));
    columns += chunk_columns;
    index += chunk_length;
  }

  if (sgr_state.active())
    current += std::string(kSgrReset);
  if (active_osc8)
    current += std::string(kOsc8Close);
  lines.push_back(std::move(current));
  return lines;
}

}  // namespace ava::tui::detail
