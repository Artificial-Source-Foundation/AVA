#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace ava::tui {

enum class TuiThemeKind
{
  Dark,
  Light,
  Plain,
  Custom,
};

struct TuiThemePalette
{
  int text = -1;
  int muted = 6;
  int success = 2;
  int warning = 3;
  int error = 1;
  int accent = 6;
  int screen_bg = -1;
  int composer_bg = -1;
};

struct TuiCustomTheme
{
  std::string name;
  std::filesystem::path path;
  TuiThemePalette palette;
  std::string revision;
};

struct TuiThemeInfo
{
  TuiThemeKind kind = TuiThemeKind::Dark;
  std::string name = "ava-dark";
  std::string detail = "built-in dark ncurses token palette";
  std::string badge = "built-in";
  std::optional<TuiThemePalette> palette;
  std::string revision = "built-in-dark";
};

void set_tui_config_theme(std::optional<std::string> theme,
                          std::optional<TuiCustomTheme> custom_theme = std::nullopt);
[[nodiscard]] TuiThemeInfo active_tui_theme();
[[nodiscard]] bool tui_plain_output();

}  // namespace ava::tui
