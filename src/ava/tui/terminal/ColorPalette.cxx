#include "sys.h"
#include "ColorPalette.h"
#include "Context.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <string>
#include <string_view>
#include "debug.h"

namespace ava::tui::terminal {
namespace {

using ColorMatrix = std::array<std::array<double, 3>, 3>;

// IEC 61966-2-1:1999 matrices for converting between linear sRGB and D65-relative CIE XYZ.
static constexpr ColorMatrix linear_srgb_to_xyz = {{{0.412390799265960, 0.357584339383878, 0.180480788401834},
                                                    {0.212639005871510, 0.715168678767756, 0.072192315360734},
                                                    {0.019330818715592, 0.119194779794626, 0.950532152249661}}};
static constexpr ColorMatrix xyz_to_linear_srgb = {{{3.240969941904522, -1.537383177570094, -0.498610760293003},
                                                    {-0.969243636280880, 1.875967501507720, 0.041555057407176},
                                                    {0.055630079696994, -0.203976958888977, 1.056971514242878}}};

// Derive the reference white from the forward matrix so white maps exactly between sRGB and CIELAB in both directions.
static constexpr std::array<double, 3> d65_white_point = []() {
  std::array<double, 3> white_point{};
  for (int xyz = 0; xyz != 3; ++xyz)
  {
    for (int rgb = 0; rgb != 3; ++rgb)
      white_point[xyz] += linear_srgb_to_xyz[xyz][rgb];
  }
  return white_point;
}();

static constexpr double cielab_delta = 6.0 / 29.0;
static constexpr int palette_probe_timeout_ms = 100;
static constexpr std::size_t maximum_osc4_response_bytes = 96;

// Parse a one-to-four-digit hexadecimal channel and reduce it to one sRGB byte.
//
// Two or more digits place their most significant byte directly in the result and discard any lower precision. A single digit
// is expanded to both nibbles because its full intensity range still denotes black through maximum intensity.
std::optional<std::uint32_t> parse_osc4_channel(std::string_view text)
{
  if (text.empty() || text.size() > 4)
    return std::nullopt;

  std::uint32_t value = 0;
  auto const [end, error] = std::from_chars(text.data(), text.data() + text.size(), value, 16);
  if (error != std::errc{} || end != text.data() + text.size())
    return std::nullopt;

  if (text.size() == 1)
    return value * 0x11;
  return value >> (4 * (text.size() - 2));
}

// Return the end offset past the first BEL, seven-bit ST, or eight-bit ST terminating an OSC response at `start`.
std::optional<std::size_t> osc_response_end(std::string_view bytes, std::size_t start)
{
  for (std::size_t pos = start; pos < bytes.size(); ++pos)
  {
    unsigned char const byte = static_cast<unsigned char>(bytes[pos]);
    if (byte == '\a' || byte == 0x9c)
      return pos + 1;
    if (byte == 0x1b && pos + 1 < bytes.size() && bytes[pos + 1] == '\\')
      return pos + 2;
  }
  return std::nullopt;
}

} // namespace

// Convert a concrete packed-sRGB color into D65-relative CIELAB through linear sRGB and CIE XYZ.
//static
CIEDE2000::LAB ColorPalette::rgb_to_lab(Color rgb)
{
  // Pass a concrete RGB color; the terminal-default color has no known RGB value.
  ASSERT(!rgb.is_default());

  // Because 8-bit RGB is stored with a gamma curve, we must first undo this curve to get the true linear light values before doing any color science.

  // First normalize the sRGB values.
  // Unpack `rgb` and convert the three colors from their [0, 256) range to a [0, 1] range.
  int const packed_rgb = rgb.as_int();
  std::array<double, 3> colors = {((packed_rgb >> 16) & 0xff) / 255.0, ((packed_rgb >> 8) & 0xff) / 255.0, (packed_rgb & 0xff) / 255.0};
  // Now {0, 0, 0} is black and {1, 1, 1} is white, {1, 0, 0} is red, {0, 1, 0} is green and {0, 0, 1} is blue.

  // Then convert to linearized sRGB color space.
  // Undo the gamma curve.
  for (int i = 0; i < colors.size(); ++i)
  {
    if (colors[i] <= 0.04045)
      colors[i] /= 12.92;
    else
      colors[i] = std::pow((colors[i] + 0.055) / 1.055, 2.4);
  }

  // Convert Linear RGB to CIE XYZ.
  std::array<double, 3> XYZ = {0.0, 0.0, 0.0};
  for (int xyz = 0; xyz < 3; ++xyz)
    for (int i = 0; i < 3; ++i)
      XYZ[xyz] += linear_srgb_to_xyz[xyz][i] * colors[i];

  // Convert CIE XYZ to CIELAB.
  auto const f = [](double t) {
    constexpr double epsilon = cielab_delta * cielab_delta * cielab_delta;
    return t > epsilon ? std::cbrt(t) : t / (3.0 * cielab_delta * cielab_delta) + 4.0 / 29.0;
  };
  std::array<double, 3> fs;
  for (int xyz = 0; xyz < 3; ++xyz)
    fs[xyz] = f(XYZ[xyz] / d65_white_point[xyz]);

  return {116.0 * fs[1] - 16.0, 500.0 * (fs[0] - fs[1]), 200.0 * (fs[1] - fs[2])};
}

// Convert finite D65-relative CIELAB to packed sRGB, clipping CIELAB colors that lie outside the sRGB gamut.
//static
Color ColorPalette::lab_to_rgb(CIEDE2000::LAB const& lab)
{
  // Pass finite CIELAB components; sanitize or reject invalid calculations before converting them to an integer Color.
  ASSERT(std::isfinite(lab.l) && std::isfinite(lab.a) && std::isfinite(lab.b));

  // Recover the nonlinear XYZ ratios used by the forward CIELAB transform.
  double const fy = (lab.l + 16.0) / 116.0;
  std::array<double, 3> fs = {fy + lab.a / 500.0, fy, fy - lab.b / 200.0};
  auto const inverse_f = [](double value) { return value > cielab_delta ? value * value * value : 3.0 * cielab_delta * cielab_delta * (value - 4.0 / 29.0); };

  std::array<double, 3> xyz;
  for (int component = 0; component != 3; ++component)
    xyz[component] = d65_white_point[component] * inverse_f(fs[component]);

  // Transform XYZ into linear sRGB in red, green, blue order.
  std::array<double, 3> colors{};
  for (int rgb = 0; rgb != 3; ++rgb)
  {
    for (int component = 0; component != 3; ++component)
      colors[rgb] += xyz_to_linear_srgb[rgb][component] * xyz[component];
  }

  // Apply the sRGB encoding transfer function, clip to the displayable cube, and quantize to 8-bit channels.
  std::array<std::uint32_t, 3> channels;
  for (int rgb = 0; rgb != 3; ++rgb)
  {
    double const linear = colors[rgb];
    double const encoded = linear <= 0.0031308 ? 12.92 * linear : 1.055 * std::pow(linear, 1.0 / 2.4) - 0.055;
    channels[rgb] = static_cast<std::uint32_t>(std::lround(std::clamp(encoded, 0.0, 1.0) * 255.0));
  }

  return Color{(channels[0] << 16) | (channels[1] << 8) | channels[2]};
}

// Build one independently terminated OSC 4 palette query for the requested index.
//static
std::string ColorPalette::osc4_query(int color_index)
{
  if (color_index < 0)
    return {};

  return "\x1b]4;" + std::to_string(color_index) + ";?\x1b\\";
}

// Build one output string containing a consecutive range of independently terminated OSC 4 palette queries.
//static
std::string ColorPalette::osc4_queries(int first_color_index, int number_of_colors)
{
  if (first_color_index < 0 || number_of_colors <= 0 || first_color_index > std::numeric_limits<int>::max() - (number_of_colors - 1))
    return {};

  std::string queries;
  for (int offset = 0; offset < number_of_colors; ++offset)
    queries += osc4_query(first_color_index + offset);
  return queries;
}

// Parse one xterm-compatible OSC 4 response, accepting BEL and seven- or eight-bit string terminators.
//static
std::optional<std::pair<int, Color>> ColorPalette::parse_osc4_response(std::string_view response)
{
  if (response.starts_with("\x1b]"))
    response.remove_prefix(2);
  else if (!response.empty() && static_cast<unsigned char>(response.front()) == 0x9d)
    response.remove_prefix(1);
  else
    return std::nullopt;

  if (response.ends_with("\x1b\\"))
    response.remove_suffix(2);
  else if (!response.empty() && (response.back() == '\a' || static_cast<unsigned char>(response.back()) == 0x9c))
    response.remove_suffix(1);
  else
    return std::nullopt;

  if (!response.starts_with("4;"))
    return std::nullopt;
  response.remove_prefix(2);

  std::size_t const index_end = response.find(';');
  if (index_end == std::string_view::npos)
    return std::nullopt;
  int color_index = -1;
  std::string_view const index_text = response.substr(0, index_end);
  auto const [index_parse_end, index_error] = std::from_chars(index_text.data(), index_text.data() + index_text.size(), color_index);
  if (index_text.empty() || index_error != std::errc{} || index_parse_end != index_text.data() + index_text.size() || color_index < 0)
    return std::nullopt;
  response.remove_prefix(index_end + 1);

  if (!response.starts_with("rgb:"))
    return std::nullopt;
  response.remove_prefix(4);
  std::array<std::uint32_t, 3> channels;
  for (int channel = 0; channel != 3; ++channel)
  {
    std::size_t const separator = response.find('/');
    bool const last_channel = channel == 2;
    if ((separator == std::string_view::npos) != last_channel)
      return std::nullopt;
    std::string_view const channel_text = last_channel ? response : response.substr(0, separator);
    std::optional<std::uint32_t> const value = parse_osc4_channel(channel_text);
    if (!value)
      return std::nullopt;
    channels[channel] = *value;
    if (!last_channel)
      response.remove_prefix(separator + 1);
  }

  return std::pair{color_index, Color{(channels[0] << 16) | (channels[1] << 8) | channels[2]}};
}

// Probe one indexed color through the shared batched implementation.
//static
std::optional<Color> ColorPalette::probe_color(Context& context, int color_index)
{
  std::vector<Color> colors = probe_colors(context, color_index, 1);
  if (colors.empty())
    return std::nullopt;
  return colors.front();
}

// Emit a range of indexed-color queries together and collect their replies without waiting between individual colors.
//static
std::vector<Color> ColorPalette::probe_colors(Context& context, int first_color_index, int number_of_colors)
{
  if (context.output_file_ == nullptr)
    return {};

  std::string const queries = osc4_queries(first_color_index, number_of_colors);
  if (queries.empty() || std::fwrite(queries.data(), 1, queries.size(), context.output_file_) != queries.size() || std::fflush(context.output_file_) != 0)
    return {};

  std::vector<Color> colors(static_cast<std::size_t>(number_of_colors));
  std::vector<bool> seen(static_cast<std::size_t>(number_of_colors), false);
  int colors_received = 0;
  std::string response_bytes;
  std::size_t const maximum_response_bytes = static_cast<std::size_t>(number_of_colors) * maximum_osc4_response_bytes;
  while (response_bytes.size() < maximum_response_bytes)
  {
    std::optional<int> const input = context.try_get_wch();
    if (!input || *input < 0 || *input > 0xff)
      return {};
    response_bytes.push_back(static_cast<char>(*input));

    std::size_t const seven_bit_start = response_bytes.find("\x1b]4;");
    std::string const eight_bit_prefix{
        "\x9d"
        "4;",
        3};
    std::size_t const eight_bit_start = response_bytes.find(eight_bit_prefix);
    std::size_t start = seven_bit_start;
    if (start == std::string_view::npos || (eight_bit_start != std::string_view::npos && eight_bit_start < start))
      start = eight_bit_start;
    if (start == std::string_view::npos)
      continue;

    std::optional<std::size_t> const past_end = osc_response_end(response_bytes, start + 3);
    if (!past_end)
      continue;

    std::optional<std::pair<int, Color>> const entry = parse_osc4_response(std::string_view(response_bytes).substr(start, *past_end - start));
    if (!entry || entry->first < first_color_index)
      return {};
    int const integer_offset = entry->first - first_color_index;
    if (integer_offset >= number_of_colors)
      return {};
    std::size_t const offset = static_cast<std::size_t>(integer_offset);
    if (seen[offset])
      return {};
    seen[offset] = true;
    colors[offset] = entry->second;
    ++colors_received;
    if (colors_received == number_of_colors)
      return colors;

    response_bytes.erase(0, *past_end);
  }

  return {};
}

// Probe the terminal output stream and collect a complete, ordered CIELAB palette without relying on ncurses' synthetic color table.
//static
std::unique_ptr<ColorPalette> ColorPalette::create(Context& context, int number_of_colors)
{
  DoutEntering(dc::notice, "ColorPalette::create(" << context << ")");

  if (number_of_colors <= 0 || context.output_file_ == nullptr)
    return {};

  // Temporarily undo some of the initialization of `default_window_initialization`.
  bool const is_keypad = context.stdscr().is_keypad();
  if (is_keypad)
    context.stdscr().keypad(false);
  int const delay_ms = context.stdscr().getdelay();
  context.stdscr().timeout(palette_probe_timeout_ms);

  std::vector<Color> const colors = probe_colors(context, 0, number_of_colors);
  if (colors.empty())
    return {};

  std::vector<CIEDE2000::LAB> palette;
  palette.reserve(number_of_colors);
  for (Color color : colors)
    palette.push_back(rgb_to_lab(color));

  // Restore initial initialization.
  context.stdscr().timeout(delay_ms);
  if (is_keypad)
    context.stdscr().keypad(true);

  return std::unique_ptr<ColorPalette>(new ColorPalette(std::move(palette)));
}

} // namespace ava::tui::terminal
