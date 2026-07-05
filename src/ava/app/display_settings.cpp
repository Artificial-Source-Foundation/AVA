#include "sys.h"
#include "ava/app/display_settings.h"

#include "ava/core/json.h"
#include "ava/tui/theme.h"

#include <cctype>
#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace ava::app {
namespace {

std::string lower_ascii(std::string_view text)
{
  std::string lowered;
  lowered.reserve(text.size());
  for (char const ch : text)
    lowered.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
  return lowered;
}

std::string trim_ascii(std::string_view text)
{
  while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front())) != 0) text.remove_prefix(1);
  while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back())) != 0) text.remove_suffix(1);
  return std::string(text);
}

ava::core::Error io_error(std::string message, std::filesystem::path const& path, std::error_code const& error)
{
  auto result = ava::core::Error(ava::core::ErrorCategory::Io, std::move(message)).with_context("path", path.string());
  if (error)
    result.with_context("cause", error.message());
  return result;
}

std::string serialize_tui_display_settings(std::optional<std::string> const& theme)
{
  if (!theme)
    return "{\n}\n";
  return std::string("{\n  \"theme\": \"") + ava::core::json::escape(*theme) + "\"\n}\n";
}

ava::core::Error invalid_theme_error(std::string message, std::filesystem::path const& path)
{
  return ava::core::Error(ava::core::ErrorCategory::InvalidArgument, std::move(message)).with_context("path", path.string());
}

bool valid_custom_theme_name(std::string_view name)
{
  return !name.empty() && name.find('/') == std::string_view::npos && name.find('\\') == std::string_view::npos &&
         name.find_first_of(" \t\r\n") == std::string_view::npos && name != "." && name != "..";
}

int hex_digit(char ch)
{
  if (ch >= '0' && ch <= '9')
    return ch - '0';
  if (ch >= 'a' && ch <= 'f')
    return ch - 'a' + 10;
  if (ch >= 'A' && ch <= 'F')
    return ch - 'A' + 10;
  return -1;
}

std::optional<int> parse_hex_byte(std::string_view value, std::size_t offset)
{
  if (offset + 1 >= value.size())
    return std::nullopt;
  auto const high = hex_digit(value[offset]);
  auto const low = hex_digit(value[offset + 1]);
  if (high < 0 || low < 0)
    return std::nullopt;
  return (high * 16) + low;
}

int squared_distance(int left_red, int left_green, int left_blue, int right_red, int right_green, int right_blue)
{
  auto const red = left_red - right_red;
  auto const green = left_green - right_green;
  auto const blue = left_blue - right_blue;
  return (red * red) + (green * green) + (blue * blue);
}

int xterm_level(int index)
{
  return index == 0 ? 0 : 55 + (index * 40);
}

int nearest_xterm_256(int red, int green, int blue)
{
  auto const cube_index = [](int channel) {
    if (channel < 48)
      return 0;
    if (channel < 115)
      return 1;
    return std::clamp((channel - 35) / 40, 1, 5);
  };
  auto const red_index = cube_index(red);
  auto const green_index = cube_index(green);
  auto const blue_index = cube_index(blue);
  auto const cube_red = xterm_level(red_index);
  auto const cube_green = xterm_level(green_index);
  auto const cube_blue = xterm_level(blue_index);
  auto const cube_color = 16 + (36 * red_index) + (6 * green_index) + blue_index;
  auto const cube_distance = squared_distance(red, green, blue, cube_red, cube_green, cube_blue);

  auto const luminance = ((red * 299) + (green * 587) + (blue * 114)) / 1000;
  auto const gray_index = std::clamp((luminance - 8 + 5) / 10, 0, 23);
  auto const gray_level = 8 + (gray_index * 10);
  auto const gray_color = 232 + gray_index;
  auto const gray_distance = squared_distance(red, green, blue, gray_level, gray_level, gray_level);
  return gray_distance < cube_distance ? gray_color : cube_color;
}

std::optional<int> parse_hex_color(std::string_view value)
{
  if (value.size() != 7 || value.front() != '#')
    return std::nullopt;
  auto const red = parse_hex_byte(value, 1);
  auto const green = parse_hex_byte(value, 3);
  auto const blue = parse_hex_byte(value, 5);
  if (!red || !green || !blue)
    return std::nullopt;
  return nearest_xterm_256(*red, *green, *blue);
}

std::string revision_for_text(std::string_view text)
{
  std::uint64_t hash = 1469598103934665603ULL;
  for (unsigned char const ch : text)
  {
    hash ^= ch;
    hash *= 1099511628211ULL;
  }
  std::ostringstream out;
  out << text.size() << ':' << hash;
  return out.str();
}

ava::core::Result<int> parse_theme_color_field(std::string_view object, std::string_view key, std::string_view vars,
                                               std::filesystem::path const& path, int depth);

ava::core::Result<int> parse_theme_color_string(std::string_view value, std::string_view vars,
                                                std::filesystem::path const& path, int depth)
{
  if (value.empty())
    return -1;
  if (auto const hex = parse_hex_color(value))
    return *hex;
  if (depth > 8)
  {
    return std::unexpected(invalid_theme_error("custom TUI theme color variable cycle", path)
                               .with_context("variable", std::string(value)));
  }
  if (vars.empty())
  {
    return std::unexpected(invalid_theme_error("custom TUI theme color variable is not defined", path)
                               .with_context("variable", std::string(value)));
  }
  auto resolved = parse_theme_color_field(vars, value, vars, path, depth + 1);
  if (!resolved)
  {
    return std::unexpected(invalid_theme_error("custom TUI theme color variable is not defined", path)
                               .with_context("variable", std::string(value)));
  }
  return resolved;
}

ava::core::Result<int> parse_theme_color_field(std::string_view object, std::string_view key, std::string_view vars,
                                               std::filesystem::path const& path, int depth)
{
  auto const start = ava::core::json::field_value_start(object, key);
  if (!start)
  {
    return std::unexpected(invalid_theme_error("custom TUI theme is missing a required color", path)
                               .with_context("color", std::string(key)));
  }
  if (*start < object.size() && object[*start] == '"')
  {
    auto value = ava::core::json::string_field(object, key);
    if (!value)
    {
      return std::unexpected(invalid_theme_error("custom TUI theme color must be a string or 0-255 integer", path)
                                 .with_context("color", std::string(key)));
    }
    return parse_theme_color_string(*value, vars, path, depth);
  }
  auto integer = ava::core::json::integer_field(object, key);
  if (!integer || *integer < 0 || *integer > 255)
  {
    return std::unexpected(invalid_theme_error("custom TUI theme color must be a string or 0-255 integer", path)
                               .with_context("color", std::string(key)));
  }
  return static_cast<int>(*integer);
}

ava::core::Result<int> parse_required_theme_color(std::string_view colors, std::string_view key, std::string_view vars,
                                                  std::filesystem::path const& path)
{
  auto parsed = parse_theme_color_field(colors, key, vars, path, 0);
  if (!parsed)
    return std::unexpected(std::move(parsed.error()));
  return *parsed;
}

ava::core::Result<ava::tui::TuiThemePalette> parse_custom_theme_palette(std::string_view json,
                                                                        std::filesystem::path const& path)
{
  auto const colors = ava::core::json::object_field(json, "colors");
  if (!colors)
    return std::unexpected(invalid_theme_error("custom TUI theme is missing colors", path));
  auto const vars = ava::core::json::object_field(json, "vars").value_or(std::string{});

  ava::tui::TuiThemePalette palette;
  auto text = parse_required_theme_color(*colors, "text", vars, path);
  auto muted = parse_required_theme_color(*colors, "muted", vars, path);
  auto success = parse_required_theme_color(*colors, "success", vars, path);
  auto warning = parse_required_theme_color(*colors, "warning", vars, path);
  auto error = parse_required_theme_color(*colors, "error", vars, path);
  auto accent = parse_required_theme_color(*colors, "accent", vars, path);
  auto screen_bg = parse_required_theme_color(*colors, "screenBg", vars, path);
  auto composer_bg = parse_required_theme_color(*colors, "composerBg", vars, path);
  if (!text)
    return std::unexpected(std::move(text.error()));
  if (!muted)
    return std::unexpected(std::move(muted.error()));
  if (!success)
    return std::unexpected(std::move(success.error()));
  if (!warning)
    return std::unexpected(std::move(warning.error()));
  if (!error)
    return std::unexpected(std::move(error.error()));
  if (!accent)
    return std::unexpected(std::move(accent.error()));
  if (!screen_bg)
    return std::unexpected(std::move(screen_bg.error()));
  if (!composer_bg)
    return std::unexpected(std::move(composer_bg.error()));
  palette.text = *text;
  palette.muted = *muted;
  palette.success = *success;
  palette.warning = *warning;
  palette.error = *error;
  palette.accent = *accent;
  palette.screen_bg = *screen_bg;
  palette.composer_bg = *composer_bg;
  return palette;
}

ava::core::Result<TuiDisplaySettings> resolve_tui_theme_setting(ava::config::XdgPaths const& paths,
                                                                std::string_view theme,
                                                                std::filesystem::path path)
{
  if (auto normalized = normalize_tui_theme_setting(theme))
    return TuiDisplaySettings{.theme = std::move(normalized), .custom_theme = std::nullopt, .path = std::move(path)};
  auto custom = load_tui_custom_theme(paths, theme);
  if (!custom)
  {
    return std::unexpected(ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "invalid TUI display theme")
                               .with_context("path", path.string())
                               .with_context("theme", std::string(theme))
                               .with_context("supported", "dark, light, plain, or a valid theme name under themes/*.json"));
  }
  auto name = custom->name;
  return TuiDisplaySettings{.theme = std::move(name), .custom_theme = std::move(*custom), .path = std::move(path)};
}

}  // namespace

std::filesystem::path tui_display_settings_file(ava::config::XdgPaths const& paths)
{
  return paths.ava_config_dir / "display.json";
}

std::filesystem::path tui_theme_directory(ava::config::XdgPaths const& paths)
{
  return paths.ava_config_dir / "themes";
}

std::optional<std::string> normalize_tui_theme_setting(std::string_view value)
{
  auto const normalized = lower_ascii(trim_ascii(value));
  if (normalized == "dark" || normalized == "ava-dark")
    return "dark";
  if (normalized == "light" || normalized == "ava-light")
    return "light";
  if (normalized == "plain" || normalized == "none" || normalized == "no-color" || normalized == "no_color")
    return "plain";
  return std::nullopt;
}

bool is_tui_theme_reset_value(std::string_view value)
{
  auto const normalized = lower_ascii(trim_ascii(value));
  return normalized == "reset" || normalized == "default" || normalized == "auto";
}

std::string tui_theme_setting_usage()
{
  return "usage: /theme [dark|light|plain|custom-name|reset]";
}

ava::core::Result<ava::tui::TuiCustomTheme> load_tui_custom_theme_file(std::filesystem::path const& path)
{
  std::ifstream input(path, std::ios::binary);
  if (!input)
    return std::unexpected(ava::core::Error(ava::core::ErrorCategory::Io, "failed to read TUI custom theme")
                               .with_context("path", path.string()));
  std::string json((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
  if (!ava::core::json::is_valid_object(json))
    return std::unexpected(invalid_theme_error("invalid TUI custom theme JSON", path));

  auto name = ava::core::json::string_field(json, "name");
  if (!name || !valid_custom_theme_name(*name))
  {
    return std::unexpected(
        invalid_theme_error("custom TUI theme name must be non-empty and cannot contain whitespace or path separators", path));
  }
  if (normalize_tui_theme_setting(*name) || is_tui_theme_reset_value(*name))
  {
    return std::unexpected(invalid_theme_error("custom TUI theme name conflicts with a built-in theme", path)
                               .with_context("theme", *name));
  }

  auto palette = parse_custom_theme_palette(json, path);
  if (!palette)
    return std::unexpected(std::move(palette.error()));
  return ava::tui::TuiCustomTheme{.name = std::move(*name),
                                  .path = path,
                                  .palette = std::move(*palette),
                                  .revision = revision_for_text(json)};
}

ava::core::Result<ava::tui::TuiCustomTheme> load_tui_custom_theme(ava::config::XdgPaths const& paths,
                                                                  std::string_view name)
{
  if (!valid_custom_theme_name(name))
  {
    return std::unexpected(ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "invalid TUI custom theme name")
                               .with_context("theme", std::string(name)));
  }

  auto const dir = tui_theme_directory(paths);
  std::error_code exists_error;
  if (!std::filesystem::exists(dir, exists_error))
  {
    return std::unexpected(ava::core::Error(ava::core::ErrorCategory::NotFound, "TUI custom theme was not found")
                               .with_context("theme", std::string(name))
                               .with_context("directory", dir.string()));
  }

  std::error_code iter_error;
  std::optional<ava::tui::TuiCustomTheme> match;
  for (std::filesystem::directory_iterator it(dir, iter_error), end; !iter_error && it != end; it.increment(iter_error))
  {
    if (!it->is_regular_file() || it->path().extension() != ".json")
      continue;
    auto theme = load_tui_custom_theme_file(it->path());
    if (!theme)
      continue;
    if (theme->name != name)
      continue;
    if (match)
    {
      return std::unexpected(ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "duplicate TUI custom theme name")
                                 .with_context("theme", std::string(name))
                                 .with_context("path", match->path.string())
                                 .with_context("path", theme->path.string()));
    }
    match = std::move(*theme);
  }
  if (iter_error)
    return std::unexpected(io_error("failed to inspect TUI custom themes", dir, iter_error));
  if (!match)
  {
    return std::unexpected(ava::core::Error(ava::core::ErrorCategory::NotFound, "TUI custom theme was not found")
                               .with_context("theme", std::string(name))
                               .with_context("directory", dir.string()));
  }
  return std::move(*match);
}

std::vector<TuiCustomThemeSummary> available_tui_custom_themes(ava::config::XdgPaths const& paths)
{
  std::vector<TuiCustomThemeSummary> themes;
  auto const dir = tui_theme_directory(paths);
  std::error_code exists_error;
  if (!std::filesystem::exists(dir, exists_error))
    return themes;
  std::error_code iter_error;
  for (std::filesystem::directory_iterator it(dir, iter_error), end; !iter_error && it != end; it.increment(iter_error))
  {
    if (!it->is_regular_file() || it->path().extension() != ".json")
      continue;
    auto theme = load_tui_custom_theme_file(it->path());
    if (!theme)
      continue;
    themes.push_back(TuiCustomThemeSummary{.name = theme->name, .path = theme->path});
  }
  std::ranges::sort(themes, {}, &TuiCustomThemeSummary::name);
  return themes;
}

ava::core::Result<TuiDisplaySettings> load_tui_display_settings(ava::config::XdgPaths const& paths)
{
  auto const path = tui_display_settings_file(paths);
  std::error_code exists_error;
  auto const exists = std::filesystem::exists(path, exists_error);
  if (exists_error)
    return std::unexpected(io_error("failed to inspect TUI display settings", path, exists_error));
  if (!exists)
    return TuiDisplaySettings{.theme = std::nullopt, .custom_theme = std::nullopt, .path = path};

  std::ifstream input(path, std::ios::binary);
  if (!input)
    return std::unexpected(ava::core::Error(ava::core::ErrorCategory::Io, "failed to read TUI display settings")
                               .with_context("path", path.string()));
  std::string json((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
  if (!ava::core::json::is_valid_object(json))
    return std::unexpected(ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "invalid TUI display settings JSON")
                               .with_context("path", path.string()));

  auto theme = ava::core::json::string_field(json, "theme");
  if (!theme || theme->empty())
    return TuiDisplaySettings{.theme = std::nullopt, .custom_theme = std::nullopt, .path = path};
  return resolve_tui_theme_setting(paths, *theme, path);
}

ava::core::Result<TuiDisplaySettings> apply_tui_display_settings(ava::config::XdgPaths const& paths)
{
  auto settings = load_tui_display_settings(paths);
  if (!settings)
    return std::unexpected(std::move(settings.error()));
  ava::tui::set_tui_config_theme(settings->theme, settings->custom_theme);
  return settings;
}

ava::core::Result<TuiDisplaySettingsWatchState> load_tui_display_settings_watch_state(ava::config::XdgPaths const& paths)
{
  auto const path = tui_display_settings_file(paths);
  std::string display_revision = "missing";
  std::error_code exists_error;
  auto const exists = std::filesystem::exists(path, exists_error);
  if (exists_error)
    return std::unexpected(io_error("failed to inspect TUI display settings", path, exists_error));
  if (exists)
  {
    std::ifstream input(path, std::ios::binary);
    if (!input)
      return std::unexpected(ava::core::Error(ava::core::ErrorCategory::Io, "failed to read TUI display settings")
                                 .with_context("path", path.string()));
    std::string json((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    display_revision = revision_for_text(json);
  }

  auto settings = load_tui_display_settings(paths);
  if (!settings)
    return std::unexpected(std::move(settings.error()));

  TuiDisplaySettingsWatchState state;
  state.display_revision = std::move(display_revision);
  state.theme = settings->theme;
  if (settings->custom_theme)
  {
    state.custom_theme_path = settings->custom_theme->path;
    state.custom_theme_revision = settings->custom_theme->revision;
  }
  return state;
}

bool tui_display_settings_watch_state_changed(TuiDisplaySettingsWatchState const& previous,
                                              TuiDisplaySettingsWatchState const& current)
{
  return previous.display_revision != current.display_revision || previous.theme != current.theme ||
         previous.custom_theme_path != current.custom_theme_path ||
         previous.custom_theme_revision != current.custom_theme_revision;
}

ava::core::VoidResult store_tui_theme_setting(ava::config::XdgPaths const& paths, std::optional<std::string> theme)
{
  if (theme)
  {
    auto resolved = resolve_tui_theme_setting(paths, *theme, tui_display_settings_file(paths));
    if (!resolved)
      return std::unexpected(std::move(resolved.error()));
    theme = resolved->theme;
  }

  std::error_code create_error;
  std::filesystem::create_directories(paths.ava_config_dir, create_error);
  if (create_error)
    return std::unexpected(io_error("failed to create TUI display settings directory", paths.ava_config_dir, create_error));

  auto const path = tui_display_settings_file(paths);
  auto const temp_path = path.string() + ".tmp";
  {
    std::ofstream output(temp_path, std::ios::binary | std::ios::trunc);
    if (!output)
      return std::unexpected(ava::core::Error(ava::core::ErrorCategory::Io, "failed to write TUI display settings")
                                 .with_context("path", temp_path));
    output << serialize_tui_display_settings(theme);
    if (!output)
      return std::unexpected(ava::core::Error(ava::core::ErrorCategory::Io, "failed to finish TUI display settings write")
                                 .with_context("path", temp_path));
  }

  std::error_code permission_error;
  std::filesystem::permissions(temp_path, std::filesystem::perms::owner_read | std::filesystem::perms::owner_write,
                               std::filesystem::perm_options::replace, permission_error);

  std::error_code rename_error;
  std::filesystem::rename(temp_path, path, rename_error);
  if (rename_error)
  {
    std::error_code remove_error;
    std::filesystem::remove(temp_path, remove_error);
    return std::unexpected(io_error("failed to replace TUI display settings", path, rename_error));
  }
  return {};
}

}  // namespace ava::app
