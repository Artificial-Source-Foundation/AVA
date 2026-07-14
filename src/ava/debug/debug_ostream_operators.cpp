#include "sys.h"
#include "debug_ostream_operators.h"
#include <termios.h>

namespace debug::ostream_operators {

std::ostream& operator<<(std::ostream& os, struct termios const& UNUSED_ARG(te))
{
  //FIXME
  return os << "$termios$";
}

//=============================================================================

} // namespace debug::ostream_operators
