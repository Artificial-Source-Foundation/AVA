#pragma once

// If one of the functions that LIBCWD_USING_OSTREAM_PRELUDE has a using for doesn't exist
// because its header wasn't included, then these dummy objects will be used instead of
// producing a compile error.
namespace debug::hidden_dummies {
  struct Dummy;
  [[maybe_unused]] inline constexpr void operator<<(Dummy&, int) { }
  [[maybe_unused]] inline constexpr int print_pointer = 0;
}
namespace libcwd::ostream_operators { using namespace debug::hidden_dummies; }
namespace debug::ostream_operators { using namespace debug::hidden_dummies; }
namespace ava_utils { using namespace debug::hidden_dummies; }

// Make sure we find appropriate functions when printing debug output.
#define LIBCWD_USING_OSTREAM_PRELUDE \
  using ::libcwd::ostream_operators::operator<<; \
  using ::debug::ostream_operators::operator<<; \
  using ::ava_utils::print_pointer;

#include "cwds/debug.h"

// define AVA_DEBUG_PRINT_MEMBERS_ON and declare all debug channels.
#include "src/ava/debug/print_members_on.h"

#ifdef CWDEBUG
// Make print_on members visible.
#include "src/ava/debug/ava_print_on.h"
#endif
