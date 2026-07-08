#include "sys.h"
#include "ava/tui/theme.h"

#include <charconv>
#include <cctype>
#include <cstdlib>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>

namespace ava::tui {
namespace {

std::mutex& config_theme_mutex()
{
  static std::mutex mutex;
  return mutex;
}

std::optional<std::string>& config_theme_storage()
{
  static std::optional<std::string> theme;
  return theme;
}

std::optional<TuiCustomTheme>& config_custom_theme_storage()
{
  static std::optional<TuiCustomTheme> theme;
  return theme;
}

bool env_enabled(char const* name)
{
  auto const* value = std::getenv(name);
  return value != nullptr && value[0] != '\0';
}

std::string lower_ascii(std::string_view text)
{
  std::string lowered;
  lowered.reserve(text.size());
  for (char const ch : text)
    lowered.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
  return lowered;
}

std::string theme_env_value()
{
  auto const* value = std::getenv("AVA_TUI_THEME");
  return value == nullptr ? std::string{} : lower_ascii(value);
}

std::string_view trim_ascii(std::string_view text)
{
  while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front())) != 0)
    text.remove_prefix(1);
  while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back())) != 0)
    text.remove_suffix(1);
  return text;
}

std::optional<int> parse_nonnegative_int(std::string_view text)
{
  text = trim_ascii(text);
  if (text.empty())
    return std::nullopt;

  int value = 0;
  auto const* begin = text.data();
  auto const* end = begin + text.size();
  auto [ptr, error] = std::from_chars(begin, end, value);
  if (error != std::errc{} || ptr != end || value < 0)
    return std::nullopt;
  return value;
}

std::optional<int> colorfgbg_background_index()
{
  auto const* value = std::getenv("COLORFGBG");
  if (value == nullptr || value[0] == '\0')
    return std::nullopt;

  std::string_view const text(value);
  auto const separator = text.find_last_of(";:");
  auto const background = separator == std::string_view::npos ? text : text.substr(separator + 1);
  return parse_nonnegative_int(background);
}

std::optional<int> xterm_color_luminance(int index)
{
  static constexpr int ansi16_luminance[] = {0,   76,  150, 114, 29,  105, 178, 229,
                                             127, 127, 200, 226, 105, 170, 221, 255};
  if (index >= 0 && index < 16)
    return ansi16_luminance[index];

  if (index >= 16 && index <= 231)
  {
    auto const offset = index - 16;
    auto const red_index = offset / 36;
    auto const green_index = (offset / 6) % 6;
    auto const blue_index = offset % 6;
    auto const level = [](int value) { return value == 0 ? 0 : 55 + (value * 40); };
    auto const red = level(red_index);
    auto const green = level(green_index);
    auto const blue = level(blue_index);
    return ((red * 299) + (green * 587) + (blue * 114)) / 1000;
  }

  if (index >= 232 && index <= 255)
    return 8 + ((index - 232) * 10);

  return std::nullopt;
}

std::optional<std::string> configured_theme_value()
{
  std::lock_guard lock(config_theme_mutex());
  return config_theme_storage();
}

std::optional<TuiCustomTheme> configured_custom_theme_value()
{
  std::lock_guard lock(config_theme_mutex());
  return config_custom_theme_storage();
}

std::optional<TuiThemeInfo> theme_info_for_request(std::string_view requested, std::string badge)
{
  auto const value = lower_ascii(requested);
  if (value == "plain" || value == "none" || value == "no-color" || value == "no_color")
  {
    return TuiThemeInfo{.kind = TuiThemeKind::Plain,
                        .name = "plain",
                        .detail = badge + "=plain disables ANSI styling",
                        .badge = std::move(badge),
                        .palette = std::nullopt,
                        .revision = "plain"};
  }
  if (value == "light" || value == "ava-light")
  {
    return TuiThemeInfo{.kind = TuiThemeKind::Light,
                        .name = "ava-light",
                        .detail = "built-in light ncurses token palette",
                        .badge = std::move(badge),
                        .palette = std::nullopt,
                        .revision = "built-in-light"};
  }
  if (value == "dark" || value == "ava-dark")
  {
    return TuiThemeInfo{.kind = TuiThemeKind::Dark,
                        .name = "ava-dark",
                        .detail = "built-in dark ncurses token palette",
                        .badge = std::move(badge),
                        .palette = std::nullopt,
                        .revision = "built-in-dark"};
  }
  return std::nullopt;
}

TuiThemeInfo theme_info_for_custom(TuiCustomTheme const& theme)
{
  return TuiThemeInfo{.kind = TuiThemeKind::Custom,
                      .name = theme.name,
                      .detail = "custom theme file: " + theme.path.string(),
                      .badge = "display.json",
                      .palette = theme.palette,
                      .revision = theme.revision};
}

std::optional<TuiThemeInfo> theme_info_for_terminal_background()
{
  auto const background = colorfgbg_background_index();
  if (!background)
    return std::nullopt;

  auto const luminance = xterm_color_luminance(*background);
  if (!luminance)
    return std::nullopt;

  if (*luminance >= 180)
  {
    return TuiThemeInfo{.kind = TuiThemeKind::Light,
                        .name = "ava-light",
                        .detail = "terminal background appears light from COLORFGBG",
                        .badge = "COLORFGBG",
                        .palette = std::nullopt,
                        .revision = "colorfgbg-light"};
  }

  return TuiThemeInfo{.kind = TuiThemeKind::Dark,
                      .name = "ava-dark",
                      .detail = "terminal background appears dark from COLORFGBG",
                      .badge = "COLORFGBG",
                      .palette = std::nullopt,
                      .revision = "colorfgbg-dark"};
}

}  // namespace

void set_tui_config_theme(std::optional<std::string> theme, std::optional<TuiCustomTheme> custom_theme)
{
  if (theme && !custom_theme)
    *theme = lower_ascii(*theme);
  if (custom_theme)
    theme = custom_theme->name;
  std::lock_guard lock(config_theme_mutex());
  config_theme_storage() = std::move(theme);
  config_custom_theme_storage() = std::move(custom_theme);
}

TuiThemeInfo active_tui_theme()
{
  if (env_enabled("NO_COLOR"))
  {
    return TuiThemeInfo{.kind = TuiThemeKind::Plain,
                        .name = "plain",
                        .detail = "NO_COLOR disables ANSI styling",
                        .badge = "NO_COLOR"};
  }

  auto const requested = theme_env_value();
  if (auto env_theme = theme_info_for_request(requested, "AVA_TUI_THEME"))
  {
    return *env_theme;
  }

  auto const configured = configured_theme_value();
  if (configured)
  {
    if (auto config_theme = theme_info_for_request(*configured, "display.json"))
      return *config_theme;
    auto const custom_theme = configured_custom_theme_value();
    if (custom_theme && custom_theme->name == *configured)
      return theme_info_for_custom(*custom_theme);
  }

  if (auto terminal_theme = theme_info_for_terminal_background())
    return *terminal_theme;

  return TuiThemeInfo{.kind = TuiThemeKind::Dark,
                      .name = "ava-dark",
                      .detail = requested.empty() ? std::string("built-in dark ncurses token palette")
                                                  : std::string("unknown AVA_TUI_THEME ignored"),
                      .badge = requested.empty() ? std::string("built-in") : std::string("built-in fallback"),
                      .palette = std::nullopt,
                      .revision = requested.empty() ? std::string("built-in-dark") : std::string("built-in-dark-fallback")};
}

bool tui_plain_output()
{
  return active_tui_theme().kind == TuiThemeKind::Plain;
}

}  // namespace ava::tui
