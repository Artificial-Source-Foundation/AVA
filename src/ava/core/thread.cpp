#include "sys.h"

#include "ava/core/thread.h"

#include "debug.h"

namespace ava::core {

// The sole place in the codebase that expands the Debug(...) /
// NAMESPACE_DEBUG::init_thread(...) pair. Every AVA thread is started through
// ava::core::make_jthread / make_thread / make_async, which call this before the
// thread body runs. No-op when libcwd (CWDEBUG) is not enabled, because Debug(x)
// then expands to nothing and never references the libcwd symbol.
void init_thread(std::string const& label)
{
  Debug(NAMESPACE_DEBUG::init_thread(label));
}

}  // namespace ava::core
