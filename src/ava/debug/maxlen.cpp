#include "sys.h"
#include "maxlen.h"
#include "NAMESPACE_DEBUG.h"
#include <libcwd/buf2str.h>
#include <string>      // std::string, std::to_string

NAMESPACE_DEBUG_START

//static
utils::iomanip::Index iomanip::MaxLen::s_index;

namespace detail {

// Number of decimal digits needed to represent n (num_digits(0) == 1).
std::size_t num_digits(std::size_t n)
{
  std::size_t d = 0;
  do { ++d; n /= 10; } while (n != 0);
  return d;
}

} // namespace detail

// Render the content of a string for debug output, honoring an active maxlen
// limit. max_length is the value stored by maxlen() (0 means no limit).
//
// When the string exceeds max_length characters, the middle is replaced by the
// marker " ...(K chars)... " where K is the number of removed characters; the
// kept prefix and suffix are chosen so that prefix + marker + suffix fits
// max_length. K is solved together with the marker length (which depends on the
// digit count of K) by iterating to a fixed point, which converges in a couple
// of steps. If fewer than 20 characters would be removed, the full string is
// returned unchanged. The returned string never includes the surrounding double
// quotes that the std::string inserter adds.
std::string render_maxlen_string(std::string const& str, long max_length)
{
  std::size_t const len = str.size();

  // No limit, or the string already fits: render verbatim.
  if (max_length <= 0 || len <= static_cast<std::size_t>(max_length))
    return str;

  std::size_t const M = static_cast<std::size_t>(max_length);

  // Marker text is " ...(K chars)... "; its length is 16 + digits(K).
  // K (removed) = len - kept, and kept = M - marker_len(K) when M is large
  // enough to hold the marker, else 0. Solve by fixed-point iteration.
  std::size_t kept = M;            // first guess: keep everything that fits.
  std::size_t K = len - kept;      // characters removed so far.
  for (int i = 0; i < 8; ++i)
  {
    std::size_t const marker_len = 16 + detail::num_digits(K);
    kept = (M > marker_len) ? (M - marker_len) : 0;
    std::size_t const K_next = len - kept;
    if (K_next == K)
      break;
    K = K_next;
  }

  // Not worth truncating: the marker would dwarf the savings.
  if (K < 20)
    return str;

  std::size_t const marker_len = 16 + detail::num_digits(K);
  std::size_t const keep = (M > marker_len) ? (M - marker_len) : 0;
  std::size_t const prefix_len = keep / 2;
  std::size_t const suffix_len = keep - prefix_len;

  std::string out;
  out.reserve(prefix_len + suffix_len + marker_len);
  out.append(str, 0, prefix_len);
  out += "\"...(";
  out += std::to_string(K);
  out += " chars)...\"";
  out.append(str, len - suffix_len, suffix_len);
  return out;
}

namespace {

[[gnu::always_inline]] inline void write_maxlen_string(std::ostream& os, std::string const& str)
{
  // Put double quotes around strings, cutting the middle out of overly long
  // strings when a maxlen() limit is active on the stream.
  os << '"';
  long const maxlen = iomanip::MaxLen::get_value(os);
  size_t const max_length = maxlen ? maxlen : config::ava_debug_maxlen_c;
  if (str.length() < max_length + 2)
    os.write(str.data(), str.size());
  else
  {
    std::string const rendered = debug::render_maxlen_string(str, max_length);
    os << libcwd::buf2str(rendered.data(), rendered.size());
  }
  os << '"';
}

} // namespace

namespace ostream_operators {

std::ostream& operator<<(std::ostream& os, std::string const& str)
{
  write_maxlen_string(os, str);
  return os;
}

} // namespace ostream_operators

NAMESPACE_DEBUG_END
