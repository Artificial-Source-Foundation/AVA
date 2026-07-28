#include "sys.h"
#include "ava/app/ava_debug.h"

#include <cstdlib>
#ifdef DEBUGGLOBAL
#include "utils/GlobalObjectManager.h"
#endif
#include "debug.h"

namespace ava::app {

// Initialize libcwd for the process unless AVA_NO_DEBUG_OUTPUT suppresses it.
// The test executable may override suppression only after it has installed a
// validated private output stream; initialization diagnostics then cannot leak
// to its stdout or stderr protocol. When initialization is skipped, the usual
// GlobalObjectManager and iostream preamble still runs.
//
// This does not silence LIBCWD_ASSERT / the dc::core ("COREDUMP") channel,
// which libcwd configures in static initializers.
void debug_init(bool private_output_ready)
{
  if (!private_output_ready && std::getenv("AVA_NO_DEBUG_OUTPUT") != nullptr)
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

    // Leave libcw_do in its default off state.
    return;
  }

  Debug(NAMESPACE_DEBUG::init());
}

}  // namespace ava::app
