#pragma once

#include "ava/config/xdg_paths.h"
#include "ava/core/result.h"
#include "ava/tui/theme.h"

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ava::app {

struct TuiCustomThemeSummary
{
  std::string name;
  std::filesystem::path path;

  AVA_DEBUG_PRINT_MEMBERS_ON
};

struct TuiDisplaySettings
{
  std::optional<std::string> theme;
  std::optional<ava::tui::TuiCustomTheme> custom_theme;
  std::filesystem::path path;

  AVA_DEBUG_PRINT_MEMBERS_ON
};

struct TuiDisplaySettingsWatchState
{
  std::string display_revision;
  std::optional<std::string> theme;
  std::optional<std::filesystem::path> custom_theme_path;
  std::optional<std::string> custom_theme_revision;

  AVA_DEBUG_PRINT_MEMBERS_ON
};

[[nodiscard]] std::filesystem::path tui_display_settings_file(ava::config::XdgPaths const& paths);
[[nodiscard]] std::filesystem::path tui_theme_directory(ava::config::XdgPaths const& paths);
[[nodiscard]] std::optional<std::string> normalize_tui_theme_setting(std::string_view value);
[[nodiscard]] bool is_tui_theme_reset_value(std::string_view value);
[[nodiscard]] std::string tui_theme_setting_usage();
[[nodiscard]] ava::core::Result<ava::tui::TuiCustomTheme> load_tui_custom_theme_file(std::filesystem::path const& path);
[[nodiscard]] ava::core::Result<ava::tui::TuiCustomTheme> load_tui_custom_theme(ava::config::XdgPaths const& paths,
                                                                                std::string_view name);
[[nodiscard]] std::vector<TuiCustomThemeSummary> available_tui_custom_themes(ava::config::XdgPaths const& paths);
[[nodiscard]] ava::core::Result<TuiDisplaySettings> load_tui_display_settings(ava::config::XdgPaths const& paths);
[[nodiscard]] ava::core::Result<TuiDisplaySettings> apply_tui_display_settings(ava::config::XdgPaths const& paths);
[[nodiscard]] ava::core::Result<TuiDisplaySettingsWatchState> load_tui_display_settings_watch_state(
    ava::config::XdgPaths const& paths);
[[nodiscard]] bool tui_display_settings_watch_state_changed(TuiDisplaySettingsWatchState const& previous,
                                                            TuiDisplaySettingsWatchState const& current);
[[nodiscard]] ava::core::VoidResult store_tui_theme_setting(ava::config::XdgPaths const& paths,
                                                            std::optional<std::string> theme);

}  // namespace ava::app
