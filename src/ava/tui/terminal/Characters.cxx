#include "sys.h"
#include "Characters.h"
#ifdef CWDEBUG
//#include "ava/debug/AVA_USING_OSTREAM_PRELUDE.h"
#endif

namespace ava::tui::terminal {

#ifdef CWDEBUG
void Characters::print_on(std::ostream& os) const
{
  LIBCWD_USING_OSTREAM_PRELUDE;
  os << "{text_span:" << print_pointer(text_span_) << '}';
}
#endif

} // namespace ava::tui::terminal
