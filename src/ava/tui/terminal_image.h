#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

namespace ava::tui {

enum class TerminalImageProtocol
{
  None,
  Kitty,
  Iterm2,
};

struct TerminalImageCapabilities
{
  TerminalImageProtocol images = TerminalImageProtocol::None;
  bool true_color = false;
  bool hyperlinks = false;
  std::string detail = "unknown terminal";
  std::string badge = "text-only";
};

struct TerminalEnvironment
{
  std::string term_program = {};
  std::string terminal_emulator = {};
  std::string term = {};
  std::string color_term = {};
  bool tmux = false;
  bool tmux_forwards_hyperlinks = false;
  bool kitty_window_id = false;
  bool ghostty_resources_dir = false;
  bool wezterm_pane = false;
  bool warp_session_id = false;
  bool warp_terminal_session_uuid = false;
  bool iterm_session_id = false;
  bool wt_session = false;
};

struct TerminalCellDimensions
{
  std::size_t width_px = 9;
  std::size_t height_px = 18;
};

struct ImageDimensions
{
  std::size_t width_px = 0;
  std::size_t height_px = 0;
};

struct ImageCellSize
{
  std::size_t columns = 1;
  std::size_t rows = 1;
};

struct KittyImageOptions
{
  std::optional<std::size_t> columns = std::nullopt;
  std::optional<std::size_t> rows = std::nullopt;
  std::optional<std::size_t> image_id = std::nullopt;
  bool move_cursor = true;
};

struct Iterm2ImageOptions
{
  std::optional<std::string> width = std::nullopt;
  std::optional<std::string> height = std::nullopt;
  std::optional<std::string> name = std::nullopt;
  bool preserve_aspect_ratio = true;
  bool inline_image = true;
};

[[nodiscard]] TerminalEnvironment current_terminal_environment();
[[nodiscard]] TerminalImageCapabilities detect_terminal_image_capabilities(TerminalEnvironment const& environment,
                                                                           bool tmux_forwards_hyperlinks = false);
[[nodiscard]] TerminalImageCapabilities active_terminal_image_capabilities();
[[nodiscard]] std::string terminal_image_protocol_name(TerminalImageProtocol protocol);
[[nodiscard]] std::string terminal_image_settings_description(TerminalImageCapabilities const& capabilities);
[[nodiscard]] std::string terminal_image_settings_detail(TerminalImageCapabilities const& capabilities);
[[nodiscard]] bool terminal_line_contains_image_sequence(std::string_view line);
[[nodiscard]] std::string encode_kitty_image(std::string_view base64_data, KittyImageOptions const& options = {});
[[nodiscard]] std::string delete_kitty_image(std::size_t image_id);
[[nodiscard]] std::string delete_all_kitty_images();
[[nodiscard]] std::string encode_iterm2_image(std::string_view base64_data, Iterm2ImageOptions const& options = {});
[[nodiscard]] ImageCellSize calculate_image_cell_size(ImageDimensions image_dimensions, std::size_t max_width_cells,
                                                       std::optional<std::size_t> max_height_cells = std::nullopt,
                                                       TerminalCellDimensions cell_dimensions = {});
[[nodiscard]] std::optional<ImageDimensions> image_dimensions_from_bytes(std::string_view bytes, std::string_view mime_type);

}  // namespace ava::tui
