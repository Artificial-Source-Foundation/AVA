#pragma once

// ava/debug/maxlen.h
//
// Defines the `maxlen` debug IO manipulator, used to cap the rendered width of
// std::string values written to a debug ostream. Typical usage inside a Dout()
// expression:
//
//   Dout(dc::notice, maxlen(100) << some_string);
//
// `maxlen(N)` stores N in a per-stream iword for the duration of the enclosing
// full expression only: it derives from utils::iomanip::Unsticky, so the
// previous iword value is restored when the temporary returned by maxlen() is
// destroyed at the end of the statement. The std::string inserter in
// ava/debug/debug_ostream_operators.h consults that iword and, when the string
// would exceed N characters, cuts out the middle, replacing it with
// " ...(K chars)... " where K is the number of removed characters. If fewer than
// 20 characters would be removed the string is printed in full (the marker would
// not earn its place).
//
// When no maxlen() manipulator is active, the configured default
// config::ava_debug_maxlen_c is used (set via -DAVA_DEBUG_MAXLEN=N,
// default 100). A value of 0 means "no limit".

#ifdef CWDEBUG

#include "NAMESPACE_DEBUG.h"    // NAMESPACE_DEBUG_START/END
#include "config.h"
#include "utils/iomanip.h"      // utils::iomanip::Unsticky, utils::iomanip::Index

#include <cstddef>              // std::size_t
#include <iosfwd>               // std::ostream

// This lives in the debug namespace (the NAMESPACE_DEBUG namespace, which
// resolves to `debug` in this project) so that LIBCWD_USING_OSTREAM_PRELUDE can
// bring it in unqualified inside a Dout() expression. The literal `namespace
// debug` is used here (rather than the NAMESPACE_DEBUG_START/END macros) for the
// same reason ava/debug/debug_ostream_operators.h does: this header is included
// before <debug.h> finishes, at which point the macros are not defined yet.
NAMESPACE_DEBUG_START

namespace iomanip {

// IO manipulator that limits the rendered width of std::string debug output.
//
// It derives from utils::iomanip::Unsticky rather than Sticky so that the limit
// is scoped to the single full expression in which the manipulator appears.
// Sticky would leave the limit set on the (persistent) debug ostream and
// silently truncate every subsequent string printed to it, which is not what
// `maxlen(100) << str` should do. Unsticky saves the previous iword value when
// the manipulator is streamed and restores it when the temporary is destroyed
// (i.e. at the end of the statement), so only the strings printed within the
// same expression are affected.
class MaxLen : public utils::iomanip::Unsticky<1>
{
 private:
  static utils::iomanip::Index s_index;

 public:
  // Store max_length in the stream's iword; the previous value is restored on
  // destruction. A value of 0 means "no limit".
  explicit MaxLen(long max_length) : Unsticky(s_index, max_length) { }

  // Returns the raw iword value for os, or 0 when maxlen() was not used.
  static long get_value(std::ostream& os) { return get_iword_from(os, s_index); }
};

} // namespace iomanip

// Factory used as `maxlen(N)` inside Dout() expressions. Returns a manipulator
// that limits std::string debug output to max_length characters.
// Pass nothing for no limit (well, a limit of 20000; if that isn't enough
// then pass a larger value).
inline iomanip::MaxLen maxlen(std::size_t max_length = 20000) { return iomanip::MaxLen(static_cast<long>(max_length)); }

// Returns the effective maxlen limit for os. When no maxlen() manipulator was
// used (the iword is 0), falls back to the configured default
// config::ava_debug_maxlen_c. A return value of 0 means "no limit".
//
// This is a thin wrapper around MaxLen::get_value exposed as a free function so
// that the std::string inserter in ava/debug/debug_ostream_operators.h can be
// defined before MaxLen's full definition is visible (it only needs the
// declaration, which is forward-declared in that header).
inline long get_maxlen_value(std::ostream& os)
{
  long const v = iomanip::MaxLen::get_value(os);
  return (v > 0) ? v : static_cast<long>(config::ava_debug_maxlen_c);   // "error: ‘config’ has not been declared" means that you forgot to #include "sys.h" at the top of the current .cpp file.
}

NAMESPACE_DEBUG_END

#endif // CWDEBUG
