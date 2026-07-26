#include "sys.h"
#include "ava/app/ava_debug.h"

#include <cstdlib>
#ifdef DEBUGGLOBAL
#include "utils/GlobalObjectManager.h"
#endif
#include "debug.h"

namespace ava::app {

// Initialize libcwd for the process, unless AVA_NO_DEBUG_OUTPUT is set in the
// environment. The test harness sets AVA_NO_DEBUG_OUTPUT when no debug output is
// wanted (i.e. when AVA_DEBUG_OUTPUT_DIR is not configured). In that case
// NAMESPACE_DEBUG::init() is NOT called, so libcwd is left uninitialized and
// regular Dout(...) output is suppressed (the debug object stays in its
// default off state). Only the same preamble that debug::init() runs first is
// executed here: GlobalObjectManager main-entry bookkeeping (when DEBUGGLOBAL
// is configured) and the optional sync_with_stdio tuning, so callers that depend
// on those side effects still observe them.
//
// Note: this does NOT silence LIBCWD_ASSERT / the dc::core ("COREDUMP") channel,
// which libcwd sets up in static initializers and which therefore still writes
// to the configured ostream (std::cerr by default) even when init() was never
// called.
void debug_init()
{
  if (std::getenv("AVA_NO_DEBUG_OUTPUT") != nullptr)
  {
#ifdef DEBUGGLOBAL
    if (!Singleton<GlobalObjectManager>::instantiate().is_after_global_constructors())
      GlobalObjectManager::main_entered();
#endif

#ifdef NO_SYNC_WITH_STDIO_FALSE
#warning "NO_SYNC_WITH_STDIO_FALSE is now the default."
#endif
#ifdef SYNC_WITH_STDIO_FALSE
    std::ios::sync_with_stdio(false);
#endif

    // Do not call `NAMESPACE_DEBUG::init()` if `AVA_NO_DEBUG_OUTPUT` is set.
    // That keeps `libcw_do` off.
    return;
  }

  Debug(NAMESPACE_DEBUG::init());
}

}  // namespace ava::app
