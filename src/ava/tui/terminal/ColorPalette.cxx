#include "sys.h"
#include "ColorPalette.h"
#include "Context.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
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

//static
std::unique_ptr<ColorPalette> ColorPalette::create(Context& context, int number_of_colors)
{
  DoutEntering(dc::notice, "ColorPalette::create(" << context << ")");

  auto color_palette = std::make_unique<ColorPalette>();

  // Temporarily undo some of the initialization of `default_window_initialization`.

  // Make sure ncurses passes all esc sequences through without delay.
  bool const is_keypad = context.stdscr().is_keypad();
  if (is_keypad)
    context.stdscr().keypad(false);

  // Set a delay of 50 milliseconds because a terminals that do not support OSC 4 might simply not reply at all.
  int const delay_ms = context.stdscr().getdelay();
  context.stdscr().timeout(50);

  for (int color_index = 0; color_index < number_of_colors; ++color_index)
  {
    // This doesn't work... how to write these OSC 4 escape sequences to the terminal?
    std::cout << "\e[4;" << color_index << ";?\e\\" << std::flush;
  }

  for (;;)
  {
    int wc = context.get_wch();         // This times out after 50 ms if there is nothing to read and then returns 0.
    if (wc == 0)
      break;
    Dout(dc::notice, "wc = " << wc);
  }

  // Restore initial initialization.
  context.stdscr().timeout(delay_ms);
  if (is_keypad)
    context.stdscr().keypad(true);

  return std::move(color_palette);
}

} // namespace ava::tui::terminal
