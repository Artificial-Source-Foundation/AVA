#include "sys.h"
#include "ava/core/thread.h"

#include "debug.h"

namespace ava::core {

// The sole place in the codebase that expands the Debug(...) /
// NAMESPACE_DEBUG::init_thread(...) pair. Every AVA thread is started through
// ava::core::JoinThread::create / make_thread / make_async, which call this before the
// thread body runs. No-op when libcwd (CWDEBUG) is not enabled, because Debug(x)
// then expands to nothing and never references the libcwd symbol.
void init_thread(std::string const& label)
{
  Debug(NAMESPACE_DEBUG::init_thread(label));
  Dout(dc::notice, "Started new thread \"" << label << "\" with id " << std::hex << std::this_thread::get_id());
}

}  // namespace ava::core
