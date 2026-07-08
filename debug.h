#pragma once

// If one of the functions that LIBCWD_USING_OSTREAM_PRELUDE has a using for doesn't exist
// because its header wasn't included, then these dummy objects will be used instead of
// producing a compile error.
namespace debug::hidden_dummies::ostream_operators {
  struct Dummy;
  [[maybe_unused]] inline constexpr void operator<<(Dummy&, int) { }    // Defined in libcwd::ostream_operators and debug::ostream_operators,
                                                                        // by "cwds/debug_ostream_operators.h" and "ava/debug/debug_ostream_operators.h".
} // namespace debug::hidden_dummies::ostream_operators
namespace debug::hidden_dummies::utils {
  [[maybe_unused]] inline constexpr int print_pointer = 0;              // Defined in namespace utils by "utils/print_pointer.h".
} // namespace debug::hidden_dummies::utils {
namespace debug::hidden_dummies::debug {
  [[maybe_unused]] inline constexpr int print_string = 0;               // Defined in NAMESPACE_DEBUG by "cwds/debug_ostream_operators.h".
  [[maybe_unused]] inline constexpr int __write__ = 0;                  // Defined in NAMESPACE_DEBUG by "ava/debug/debug_ostream_operators.h".
} // namespace debug::hidden_dummies::debug

namespace libcwd::ostream_operators { using namespace debug::hidden_dummies::ostream_operators; }
namespace debug::ostream_operators { using namespace debug::hidden_dummies::ostream_operators; }
namespace utils { using namespace debug::hidden_dummies::utils; }
namespace debug { using namespace debug::hidden_dummies::debug; }

// Make sure we find appropriate functions when printing debug output.
#define LIBCWD_USING_OSTREAM_PRELUDE \
  using ::libcwd::ostream_operators::operator<<; \
  using ::debug::ostream_operators::operator<<; \
  using ::utils::print_pointer; \
  using ::debug::print_string; \
  using ::debug::__write__;

#include "cwds/debug.h"

// define AVA_DEBUG_PRINT_MEMBERS_ON and declare all debug channels.
#include "src/ava/debug/print_members_on.h"

#ifdef CWDEBUG
// Make print_on members visible.
#include "src/ava/debug/ava_print_on.h"
#endif
