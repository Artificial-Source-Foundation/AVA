#include "sys.h"
#include "ava/debug/libcwd_output_sink.h"
#include "ava/app/ava_debug.h"

#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#ifdef DEBUGGLOBAL
#include "utils/GlobalObjectManager.h"
#endif
#include "debug.h"

namespace ava::app {

// Install a process-lifetime private sink before libcwd initialization, then
// balance initialization when no requested test output file could be enabled.
// Ordinary, non-test processes follow debug_init(): libcwd stays off unless
// the operator opts in with AVA_DEBUG_OUTPUT=1.
void initialize_debug()
{
  char const* test_name = std::getenv("AVA_TEST_NAME");
  if (test_name == nullptr || test_name[0] == '\0')
  {
    debug_init();
    return;
  }

  static std::string const log_stem(test_name);
  static ava::debug::LibcwdOutputSink output(log_stem);
  debug_init(output.enabled());
  if (!output.setup_succeeded())
    std::cerr << "failed to configure libcwd output: " << output.setup_error() << '\n';
  Dout(dc::notice, "AVA libcwd routing marker: test=" << log_stem);
}

namespace {

// Run the GlobalObjectManager and iostream preamble that must execute on every
// debug_init path, including the paths that leave libcwd output off.
void debug_init_preamble()
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
}

}  // namespace

// Initialize libcwd for the process only when debug output was explicitly
// requested. AVA_DEBUG_OUTPUT=1 opts an ordinary process into developer
// diagnostics; libcwd output otherwise stays off by default, even in CWDEBUG
// builds. AVA_NO_DEBUG_OUTPUT is a hard suppression control that wins when
// both are set. The test executable may override suppression only after it
// has installed a validated private output stream; initialization diagnostics
// then cannot leak to its stdout or stderr protocol. When initialization is
// skipped, the usual GlobalObjectManager and iostream preamble still runs.
//
// This does not silence LIBCWD_ASSERT / the dc::core ("COREDUMP") channel,
// which libcwd configures in static initializers.
void debug_init(bool have_private_output_stream)
{
  char const* debug_output = std::getenv("AVA_DEBUG_OUTPUT");
  bool const opted_in = debug_output != nullptr && std::strcmp(debug_output, "1") == 0;
  if (!have_private_output_stream && (!opted_in || std::getenv("AVA_NO_DEBUG_OUTPUT") != nullptr))
  {
    debug_init_preamble();

    // Leave libcw_do in its default off state.
    return;
  }

  Debug(::NAMESPACE_DEBUG::init());
  Debug(libcw_do.always_flush_on());
  Dout(dc::notice, "Debug output turned on.");
}

}  // namespace ava::app
