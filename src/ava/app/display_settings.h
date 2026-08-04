#pragma once

#include "ava/config/xdg_paths.h"
#include "ava/core/result.h"
#include "ava/tui/theme.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ava::app {

inline constexpr std::size_t kDefaultTuiImageWidthCells = 60;
inline constexpr std::size_t kMinTuiImageWidthCells = 8;
inline constexpr std::size_t kMaxTuiImageWidthCells = 160;
inline constexpr std::size_t kMaxTuiDisplaySettingsBytes = 64 * 1024;

struct TuiCustomThemeSummary
{
  std::string name;
  std::filesystem::path path;

  AVA_DEBUG_PRINT_MEMBERS_ON
};

// Validated display.json document with field-preserving unknown top-level members.
struct DisplaySettingsDocument
{
  std::optional<std::string> theme;
  std::optional<bool> show_images;
  std::optional<std::size_t> image_width_cells;
  // Unknown top-level fields retained as raw JSON values for forward-compatible updates.
  std::vector<std::pair<std::string, std::string>> unknown_fields;
  std::filesystem::path path;

  AVA_DEBUG_PRINT_MEMBERS_ON
};

struct TuiDisplaySettings
{
  std::optional<std::string> theme;
  std::optional<ava::tui::TuiCustomTheme> custom_theme;
  bool show_images = true;
  std::size_t image_width_cells = kDefaultTuiImageWidthCells;
  bool show_images_configured = false;
  bool image_width_configured = false;
  std::filesystem::path path;

  AVA_DEBUG_PRINT_MEMBERS_ON
};

struct TuiDisplaySettingsWatchState
{
  std::string display_revision;
  std::optional<std::string> theme;
  std::optional<std::filesystem::path> custom_theme_path;
  std::optional<std::string> custom_theme_revision;
  bool show_images = true;
  std::size_t image_width_cells = kDefaultTuiImageWidthCells;

  AVA_DEBUG_PRINT_MEMBERS_ON
};

[[nodiscard]] std::filesystem::path tui_display_settings_file(ava::config::XdgPaths const& paths);
[[nodiscard]] std::filesystem::path tui_theme_directory(ava::config::XdgPaths const& paths);
[[nodiscard]] std::optional<std::string> normalize_tui_theme_setting(std::string_view value);
[[nodiscard]] bool is_tui_theme_reset_value(std::string_view value);
[[nodiscard]] std::string tui_theme_setting_usage();
[[nodiscard]] std::optional<bool> normalize_tui_show_images_setting(std::string_view value);
[[nodiscard]] bool is_tui_show_images_reset_value(std::string_view value);
[[nodiscard]] std::string tui_show_images_setting_usage();
[[nodiscard]] std::optional<std::size_t> normalize_tui_image_width_setting(std::string_view value);
[[nodiscard]] bool is_tui_image_width_reset_value(std::string_view value);
[[nodiscard]] std::string tui_image_width_setting_usage();
[[nodiscard]] std::string active_tui_theme_summary();
[[nodiscard]] ava::core::Result<ava::tui::TuiCustomTheme> load_tui_custom_theme_file(std::filesystem::path const& path);
[[nodiscard]] ava::core::Result<ava::tui::TuiCustomTheme> load_tui_custom_theme(ava::config::XdgPaths const& paths, std::string_view name);
[[nodiscard]] std::vector<TuiCustomThemeSummary> available_tui_custom_themes(ava::config::XdgPaths const& paths);
[[nodiscard]] ava::core::Result<DisplaySettingsDocument> load_display_settings_document(ava::config::XdgPaths const& paths);
[[nodiscard]] ava::core::Result<TuiDisplaySettings> load_tui_display_settings(ava::config::XdgPaths const& paths);
[[nodiscard]] ava::core::Result<TuiDisplaySettings> apply_tui_display_settings(ava::config::XdgPaths const& paths);
[[nodiscard]] ava::core::Result<TuiDisplaySettingsWatchState> load_tui_display_settings_watch_state(ava::config::XdgPaths const& paths);
[[nodiscard]] bool tui_display_settings_watch_state_changed(TuiDisplaySettingsWatchState const& previous, TuiDisplaySettingsWatchState const& current);
// Field-specific setters update or erase only the owned key and preserve every other field.
[[nodiscard]] ava::core::VoidResult store_tui_theme_setting(ava::config::XdgPaths const& paths, std::optional<std::string> theme);
[[nodiscard]] ava::core::VoidResult store_tui_show_images_setting(ava::config::XdgPaths const& paths, std::optional<bool> show_images);
[[nodiscard]] ava::core::VoidResult store_tui_image_width_setting(ava::config::XdgPaths const& paths, std::optional<std::size_t> image_width_cells);

}  // namespace ava::app
