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
  int color_pair_index = complex_char.rendition().color_pair().index();
  setcchar(&result, complex_char.cell_character(), convert_to_attr(complex_char.rendition().attributes()), 0, &color_pair_index);
  return result;
}

ComplexChar convert_to_ComplexChar(cchar_t const& cchar)
{
  ComplexChar result;
  [[maybe_unused]] NCURSES_PAIRS_T color_pair = 0;

  // getcchar is the documented inverse of setcchar: it extracts the wide character string, attribute mask, and color-pair identifier from ncurses' opaque cchar_t.
  // convert_to_cchar uses ncurses' extended-color-pair opts argument, so pass an int pointer here as well and prefer that value over the possibly narrowed NCURSES_PAIRS_T result.
  // On success, characters is null-terminated and can be copied directly into ComplexChar.
  int const status = getcchar(&cchar, result.cell_character(), &result.rendition().attributes().mask(), &color_pair, &result.rendition().color_pair().index());
  ASSERT(status == OK);

  return result;
}
