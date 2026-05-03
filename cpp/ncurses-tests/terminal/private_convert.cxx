#include "private_convert.h"
#include "debug.h"

attr_t convert_to_attr(Attributes attributes)
{
  auto const mask = attributes.mask();
  attr_t result = A_NORMAL;
  if ((mask & static_cast<attr_t>(Attribute::bold)))
    result |= A_BOLD;
  if ((mask & static_cast<attr_t>(Attribute::underline)))
    result |= A_UNDERLINE;
  if ((mask & static_cast<attr_t>(Attribute::standout)))
    result |= A_STANDOUT;
  if ((mask & static_cast<attr_t>(Attribute::blink)))
    result |= A_BLINK;
  return result;
}

// Convert the subset of ncurses attributes represented by terminal::Attributes back to the public terminal bitmask.
// Attribute bits that terminal::Attributes cannot represent are intentionally ignored; this keeps conversions stable
// when ncurses returns extra rendition bits that this wrapper does not model.
static Attributes convert_to_Attributes(attr_t attributes)
{
  Attributes result;
  if ((attributes & A_BOLD))
    result |= Attribute::bold;
  if ((attributes & A_UNDERLINE))
    result |= Attribute::underline;
  if ((attributes & A_STANDOUT))
    result |= Attribute::standout;
  if ((attributes & A_BLINK))
    result |= Attribute::blink;
  return result;
}

cchar_t convert_to_cchar(ComplexChar const& complex_char)
{
  cchar_t result;
  terminal::GraphemeCluster const& grapheme_cluster = complex_char.cell_character();
  int color_pair_index = complex_char.rendition().color_pair().index();
  attr_t attributes = convert_to_attr(complex_char.rendition().attributes());
  if (grapheme_cluster.is_zero_terminated())
    ::setcchar(&result, complex_char.cell_character().data(), attributes, 0, &color_pair_index);
  else
  {
    // setcchar requires the grapheme to be zero terminated.
    std::array<wchar_t, CCHARW_MAX + 1> tmp;
    std::wcsncpy(tmp.data(), grapheme_cluster.data(), tmp.size());
    ::setcchar(&result, tmp.data(), attributes, 0, &color_pair_index);
  }
  return result;
}

ComplexChar convert_to_ComplexChar(cchar_t const& cchar)
{
  ComplexChar result;
  [[maybe_unused]] NCURSES_PAIRS_T color_pair = 0;
  // We can not write directly into the Storage of result.cell_character() because the grapheme_cluster
  // returned by getcchar is null-terminated, even if it contains CCHARW_MAX non-zero characters!
  std::array<wchar_t, CCHARW_MAX + 1> grapheme_cluster;
  int const status = ::getcchar(&cchar, grapheme_cluster.data(), &result.rendition().attributes().mask(), &color_pair, &result.rendition().color_pair().index());
  ASSERT(status == OK);
  // Copy at most CCHARW_MAX wide characters into the ComplexChar.
  std::wcsncpy(result.cell_character().data(), grapheme_cluster.data(), CCHARW_MAX);
  return result;
}
