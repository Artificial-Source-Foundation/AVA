#include "sys.h"
#include "ava/tui/terminal_image.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

namespace ava::tui {
namespace {

constexpr std::size_t kKittyChunkSize = 4096;
constexpr std::string_view kBase64Alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

std::string lower_ascii(std::string_view text)
{
  std::string lowered;
  lowered.reserve(text.size());
  for (char const ch : text) {
    lowered.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
  }
  return lowered;
}

std::string env_value(char const* name)
{
  auto const* value = std::getenv(name);
  return value == nullptr ? std::string{} : lower_ascii(value);
}

bool env_present(char const* name)
{
  auto const* value = std::getenv(name);
  return value != nullptr && value[0] != '\0';
}

bool env_flag_enabled(char const* name)
{
  auto const value = env_value(name);
  return value == "1" || value == "true" || value == "yes" || value == "on" || value == "enabled";
}

bool starts_with(std::string_view text, std::string_view prefix)
{
  return text.size() >= prefix.size() && text.substr(0, prefix.size()) == prefix;
}

bool true_color_hint(std::string_view color_term)
{
  return color_term == "truecolor" || color_term == "24bit";
}

std::string tmux_text_fallback_detail(bool hyperlinks)
{
  if (hyperlinks) {
    return "image protocols disabled under tmux; OSC 8 hyperlinks enabled by explicit tmux forwarding hint";
  }
  return "image protocols disabled under tmux; set AVA_TUI_TMUX_HYPERLINKS=1 after configuring tmux hyperlinks to enable OSC 8";
}

TerminalImageCapabilities text_only(std::string detail, bool true_color = false, bool hyperlinks = false,
                                    std::string badge = "text-only")
{
  return TerminalImageCapabilities{.images = TerminalImageProtocol::None,
                                   .true_color = true_color,
                                   .hyperlinks = hyperlinks,
                                   .detail = std::move(detail),
                                   .badge = std::move(badge)};
}

TerminalImageCapabilities image_capable(TerminalImageProtocol protocol, std::string detail)
{
  return TerminalImageCapabilities{.images = protocol,
                                   .true_color = true,
                                   .hyperlinks = true,
                                   .detail = std::move(detail),
                                   .badge = terminal_image_protocol_name(protocol)};
}

std::string base64_encode(std::string_view text)
{
  std::string out;
  out.reserve(((text.size() + 2) / 3) * 4);
  for (std::size_t index = 0; index < text.size(); index += 3) {
    auto const first = static_cast<unsigned char>(text[index]);
    auto const second = index + 1 < text.size() ? static_cast<unsigned char>(text[index + 1]) : 0;
    auto const third = index + 2 < text.size() ? static_cast<unsigned char>(text[index + 2]) : 0;
    auto const block = (static_cast<unsigned int>(first) << 16U) | (static_cast<unsigned int>(second) << 8U) |
                       static_cast<unsigned int>(third);
    out.push_back(kBase64Alphabet[(block >> 18U) & 0x3FU]);
    out.push_back(kBase64Alphabet[(block >> 12U) & 0x3FU]);
    out.push_back(index + 1 < text.size() ? kBase64Alphabet[(block >> 6U) & 0x3FU] : '=');
    out.push_back(index + 2 < text.size() ? kBase64Alphabet[block & 0x3FU] : '=');
  }
  return out;
}

std::uint16_t read_u16_be(std::string_view bytes, std::size_t offset)
{
  return static_cast<std::uint16_t>((static_cast<unsigned char>(bytes[offset]) << 8) |
                                    static_cast<unsigned char>(bytes[offset + 1]));
}

std::uint16_t read_u16_le(std::string_view bytes, std::size_t offset)
{
  return static_cast<std::uint16_t>(static_cast<unsigned char>(bytes[offset]) |
                                    (static_cast<unsigned char>(bytes[offset + 1]) << 8));
}

std::uint32_t read_u24_le(std::string_view bytes, std::size_t offset)
{
  return static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[offset])) |
         (static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[offset + 1])) << 8) |
         (static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[offset + 2])) << 16);
}

std::uint32_t read_u32_be(std::string_view bytes, std::size_t offset)
{
  return (static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[offset])) << 24) |
         (static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[offset + 1])) << 16) |
         (static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[offset + 2])) << 8) |
         static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[offset + 3]));
}

std::uint32_t read_u32_le(std::string_view bytes, std::size_t offset)
{
  return static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[offset])) |
         (static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[offset + 1])) << 8) |
         (static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[offset + 2])) << 16) |
         (static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[offset + 3])) << 24);
}

std::optional<ImageDimensions> png_dimensions(std::string_view bytes)
{
  if (bytes.size() < 24 || static_cast<unsigned char>(bytes[0]) != 0x89 || bytes.substr(1, 3) != "PNG" ||
      bytes.substr(4, 4) != std::string_view("\r\n\x1A\n", 4) || bytes.substr(12, 4) != "IHDR") {
    return std::nullopt;
  }
  return ImageDimensions{.width_px = read_u32_be(bytes, 16), .height_px = read_u32_be(bytes, 20)};
}

std::optional<ImageDimensions> gif_dimensions(std::string_view bytes)
{
  if (bytes.size() < 10 || (bytes.substr(0, 6) != "GIF87a" && bytes.substr(0, 6) != "GIF89a")) {
    return std::nullopt;
  }
  return ImageDimensions{.width_px = read_u16_le(bytes, 6), .height_px = read_u16_le(bytes, 8)};
}

std::optional<ImageDimensions> jpeg_dimensions(std::string_view bytes)
{
  if (bytes.size() < 2 || static_cast<unsigned char>(bytes[0]) != 0xFF ||
      static_cast<unsigned char>(bytes[1]) != 0xD8) {
    return std::nullopt;
  }
  std::size_t offset = 2;
  while (offset + 9 < bytes.size()) {
    if (static_cast<unsigned char>(bytes[offset]) != 0xFF) {
      ++offset;
      continue;
    }
    auto const marker = static_cast<unsigned char>(bytes[offset + 1]);
    if (marker >= 0xC0 && marker <= 0xC2) {
      return ImageDimensions{.width_px = read_u16_be(bytes, offset + 7), .height_px = read_u16_be(bytes, offset + 5)};
    }
    if (offset + 3 >= bytes.size()) return std::nullopt;
    auto const length = read_u16_be(bytes, offset + 2);
    if (length < 2) return std::nullopt;
    offset += 2 + length;
  }
  return std::nullopt;
}

std::optional<ImageDimensions> webp_dimensions(std::string_view bytes)
{
  if (bytes.size() < 30 || bytes.substr(0, 4) != "RIFF" || bytes.substr(8, 4) != "WEBP") {
    return std::nullopt;
  }
  auto const chunk = bytes.substr(12, 4);
  if (chunk == "VP8 ") {
    return ImageDimensions{.width_px = static_cast<std::size_t>(read_u16_le(bytes, 26) & 0x3FFFU),
                           .height_px = static_cast<std::size_t>(read_u16_le(bytes, 28) & 0x3FFFU)};
  }
  if (chunk == "VP8L") {
    if (bytes.size() < 25) return std::nullopt;
    auto const bits = read_u32_le(bytes, 21);
    return ImageDimensions{.width_px = (bits & 0x3FFF) + 1, .height_px = ((bits >> 14) & 0x3FFF) + 1};
  }
  if (chunk == "VP8X") {
    return ImageDimensions{.width_px = read_u24_le(bytes, 24) + 1, .height_px = read_u24_le(bytes, 27) + 1};
  }
  return std::nullopt;
}

}  // namespace

TerminalEnvironment current_terminal_environment()
{
  return TerminalEnvironment{.term_program = env_value("TERM_PROGRAM"),
                             .terminal_emulator = env_value("TERMINAL_EMULATOR"),
                             .term = env_value("TERM"),
                             .color_term = env_value("COLORTERM"),
                             .tmux = env_present("TMUX"),
                             .tmux_forwards_hyperlinks = env_flag_enabled("AVA_TUI_TMUX_HYPERLINKS"),
                             .kitty_window_id = env_present("KITTY_WINDOW_ID"),
                             .ghostty_resources_dir = env_present("GHOSTTY_RESOURCES_DIR"),
                             .wezterm_pane = env_present("WEZTERM_PANE"),
                             .warp_session_id = env_present("WARP_SESSION_ID"),
                             .warp_terminal_session_uuid = env_present("WARP_TERMINAL_SESSION_UUID"),
                             .iterm_session_id = env_present("ITERM_SESSION_ID"),
                             .wt_session = env_present("WT_SESSION")};
}

TerminalImageCapabilities detect_terminal_image_capabilities(TerminalEnvironment const& environment,
                                                             bool tmux_forwards_hyperlinks)
{
  auto const color_hint = true_color_hint(environment.color_term);
  if (environment.tmux || starts_with(environment.term, "tmux")) {
    auto const hyperlinks = tmux_forwards_hyperlinks || environment.tmux_forwards_hyperlinks;
    return text_only(tmux_text_fallback_detail(hyperlinks), color_hint, hyperlinks, "tmux");
  }
  if (starts_with(environment.term, "screen")) {
    return text_only("image protocols disabled under screen", color_hint, false, "screen");
  }
  if (environment.kitty_window_id || environment.term_program == "kitty") {
    return image_capable(TerminalImageProtocol::Kitty, "Kitty graphics protocol available");
  }
  if (environment.term_program == "ghostty" || environment.term.find("ghostty") != std::string::npos ||
      environment.ghostty_resources_dir) {
    return image_capable(TerminalImageProtocol::Kitty, "Ghostty Kitty-compatible graphics available");
  }
  if (environment.wezterm_pane || environment.term_program == "wezterm") {
    return image_capable(TerminalImageProtocol::Kitty, "WezTerm Kitty-compatible graphics available");
  }
  if (environment.term_program == "warpterminal" || environment.warp_session_id ||
      environment.warp_terminal_session_uuid) {
    return image_capable(TerminalImageProtocol::Kitty, "Warp Kitty-compatible graphics available");
  }
  if (environment.iterm_session_id || environment.term_program == "iterm.app") {
    return image_capable(TerminalImageProtocol::Iterm2, "iTerm2 inline image protocol available");
  }
  if (environment.wt_session) {
    return text_only("Windows Terminal does not advertise an inline image protocol", true, true, "OSC 8");
  }
  if (environment.term_program == "vscode") {
    return text_only("VS Code terminal uses textual image fallback", true, true, "OSC 8");
  }
  if (environment.term_program == "alacritty") {
    return text_only("Alacritty uses textual image fallback", true, true, "OSC 8");
  }
  if (environment.terminal_emulator == "jetbrains-jediterm") {
    return text_only("JetBrains terminal uses textual image fallback", true, false, "text-only");
  }
  return text_only("unknown terminal uses textual image fallback", color_hint, false, "text-only");
}

TerminalImageCapabilities active_terminal_image_capabilities()
{
  return detect_terminal_image_capabilities(current_terminal_environment());
}

std::string terminal_image_protocol_name(TerminalImageProtocol protocol)
{
  switch (protocol) {
    case TerminalImageProtocol::Kitty:
      return "kitty";
    case TerminalImageProtocol::Iterm2:
      return "iterm2";
    case TerminalImageProtocol::None:
      return "text-only";
  }
  return "text-only";
}

std::string terminal_image_settings_description(TerminalImageCapabilities const& capabilities)
{
  if (capabilities.images == TerminalImageProtocol::None) return "text-only";
  return terminal_image_protocol_name(capabilities.images);
}

std::string terminal_image_settings_detail(TerminalImageCapabilities const& capabilities)
{
  return capabilities.detail;
}

bool terminal_line_contains_image_sequence(std::string_view line)
{
  return line.find("\x1b_G") != std::string_view::npos || line.find("\x1b]1337;File=") != std::string_view::npos;
}

std::string encode_kitty_image(std::string_view base64_data, KittyImageOptions const& options)
{
  std::vector<std::string> params = {"a=T", "f=100", "q=2"};
  if (!options.move_cursor) params.push_back("C=1");
  if (options.columns) params.push_back("c=" + std::to_string(*options.columns));
  if (options.rows) params.push_back("r=" + std::to_string(*options.rows));
  if (options.image_id) params.push_back("i=" + std::to_string(*options.image_id));

  auto join_params = [&]() {
    std::string joined;
    for (std::size_t index = 0; index < params.size(); ++index) {
      if (index > 0) joined.push_back(',');
      joined += params[index];
    }
    return joined;
  };

  auto const param_text = join_params();
  if (base64_data.size() <= kKittyChunkSize) {
    return "\x1b_G" + param_text + ";" + std::string(base64_data) + "\x1b\\";
  }

  std::string out;
  for (std::size_t offset = 0, chunk_index = 0; offset < base64_data.size(); offset += kKittyChunkSize, ++chunk_index) {
    auto const chunk = base64_data.substr(offset, std::min(kKittyChunkSize, base64_data.size() - offset));
    auto const last = offset + kKittyChunkSize >= base64_data.size();
    if (chunk_index == 0) {
      out += "\x1b_G" + param_text + ",m=1;" + std::string(chunk) + "\x1b\\";
    } else {
      out += std::string("\x1b_Gm=") + (last ? "0;" : "1;") + std::string(chunk) + "\x1b\\";
    }
  }
  return out;
}

std::string delete_kitty_image(std::size_t image_id)
{
  return "\x1b_Ga=d,d=I,i=" + std::to_string(image_id) + ",q=2\x1b\\";
}

std::string delete_all_kitty_images()
{
  return "\x1b_Ga=d,d=A,q=2\x1b\\";
}

std::string encode_iterm2_image(std::string_view base64_data, Iterm2ImageOptions const& options)
{
  std::vector<std::string> params = {std::string("inline=") + (options.inline_image ? "1" : "0")};
  if (options.width) params.push_back("width=" + *options.width);
  if (options.height) params.push_back("height=" + *options.height);
  if (options.name) params.push_back("name=" + base64_encode(*options.name));
  if (!options.preserve_aspect_ratio) params.push_back("preserveAspectRatio=0");

  std::string joined;
  for (std::size_t index = 0; index < params.size(); ++index) {
    if (index > 0) joined.push_back(';');
    joined += params[index];
  }
  return "\x1b]1337;File=" + joined + ":" + std::string(base64_data) + "\a";
}

ImageCellSize calculate_image_cell_size(ImageDimensions image_dimensions, std::size_t max_width_cells,
                                        std::optional<std::size_t> max_height_cells,
                                        TerminalCellDimensions cell_dimensions)
{
  auto const max_width = std::max<std::size_t>(1, max_width_cells);
  auto const max_height = max_height_cells ? std::max<std::size_t>(1, *max_height_cells) : std::optional<std::size_t>{};
  auto const image_width = std::max<std::size_t>(1, image_dimensions.width_px);
  auto const image_height = std::max<std::size_t>(1, image_dimensions.height_px);
  auto const cell_width = std::max<std::size_t>(1, cell_dimensions.width_px);
  auto const cell_height = std::max<std::size_t>(1, cell_dimensions.height_px);

  auto const width_scale = static_cast<double>(max_width * cell_width) / static_cast<double>(image_width);
  auto const height_scale = max_height ? static_cast<double>(*max_height * cell_height) / static_cast<double>(image_height)
                                       : width_scale;
  auto const scale = std::min(width_scale, height_scale);
  auto const scaled_width = static_cast<double>(image_width) * scale;
  auto const scaled_height = static_cast<double>(image_height) * scale;
  auto columns = static_cast<std::size_t>(std::ceil(scaled_width / static_cast<double>(cell_width)));
  auto rows = static_cast<std::size_t>(std::ceil(scaled_height / static_cast<double>(cell_height)));
  columns = std::max<std::size_t>(1, std::min(max_width, columns));
  rows = std::max<std::size_t>(1, max_height ? std::min(*max_height, rows) : rows);
  return ImageCellSize{.columns = columns, .rows = rows};
}

std::optional<ImageDimensions> image_dimensions_from_bytes(std::string_view bytes, std::string_view mime_type)
{
  if (mime_type == "image/png") return png_dimensions(bytes);
  if (mime_type == "image/jpeg") return jpeg_dimensions(bytes);
  if (mime_type == "image/gif") return gif_dimensions(bytes);
  if (mime_type == "image/webp") return webp_dimensions(bytes);
  return std::nullopt;
}

}  // namespace ava::tui
