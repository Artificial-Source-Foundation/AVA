#include "sys.h"
// clang-format off
#include "ava/debug/debug_ostream_operators.h"  // Must be included immediately after "sys.h".
#include "ava/tui/terminal/Border.h"
#include "ava/tui/terminal/GraphemeCluster.h"
// clang-format on
#include <codecvt>
#include <locale>

namespace ava::tui::terminal {

void Border::print_members(std::ostream& os, char const* prefix) const
{
  std::wstring_convert<std::codecvt_utf8<wchar_t>> conv;

  os << prefix;
  AVA_USING_OSTREAM_PRELUDE(os)
     << __write__("ls:") << ls
     << __write__(", rs:") << rs
     << __write__(", ts:") << ts
     << __write__(", bs:") << bs
     << __write__(", tl:") << tl
     << __write__(", tr:") << tr
     << __write__(", bl:") << bl
     << __write__(", br:") << br
     << __write__(", index_to_pos:") << index_to_pos
     << __write__(", default_box")                              // We are not showing `default_box`.
     << __write__(", box_characters:") << conv.to_bytes(box_characters_)
     ;
}

void GraphemeCluster::print_members(std::ostream& os, char const* prefix) const
{
  std::wstring_convert<std::codecvt_utf8<wchar_t>> conv;
  std::string storage;
  for (wchar_t wc : storage_)
  {
    if (!wc)
      break;
    storage += conv.to_bytes(wc);
  }

  os << prefix;
  AVA_USING_OSTREAM_PRELUDE(os)
     << __write__("space")                                      // We are not printing `space_`.
     << __write__(", capacity:") << capacity
     << __write__(", storage:") << storage
     ;
}

} // namespace ava::tui::terminal
