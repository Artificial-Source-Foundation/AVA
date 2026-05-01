#include "private_convert.h"

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

cchar_t convert_to_cchar(ComplexChar const& complex_char)
{
  cchar_t result;
  int color_pair_int = complex_char.color_pair();
  setcchar(&result, complex_char.character(), convert_to_attr(complex_char.attributes()), 0, &color_pair_int);
  return result;
}
