/*=****************************************************************************
 * Convenience marocs                                                         *
 ******************************************************************************/

#ifndef NAMESPACE_DEBUG_START
// Only include debug.h if it wasn't already included before.
#include "debug.h"
#endif
#include "print_reference.h"

// The idea behind passing a prefix to print_members is so that you can call
// print_members(os, prefix) from within another print_members to append the
// members (start with an optional leading ", " if anything was already printed
// before that point).
#define AVA_PRINT_ON_MEMBERS \
  void print_on(std::ostream& os) const \
  { \
    os << '{'; \
    print_members(os, ""); \
    os << '}'; \
  } \
  void print_members(std::ostream& os, char const* prefix) const;

#define AVA_PURE_VIRTUAL_PRINT_ON_MEMBERS \
  void print_on(std::ostream& os) const \
  { \
    os << libcwd::type_info_of(*this).demangled_name() << '@' << (void*)this; \
  }

#ifndef CWDEBUG
// AVA_DEBUG_MEMBERS only declares print_on in Debug mode.
#define AVA_DEBUG_PRINT_MEMBERS_ON
#define AVA_DEBUG_PURE_VIRTUAL_PRINT_MEMBERS
#else
#define AVA_DEBUG_PRINT_MEMBERS_ON AVA_PRINT_ON_MEMBERS
#define AVA_DEBUG_PURE_VIRTUAL_PRINT_MEMBERS AVA_PURE_VIRTUAL_PRINT_ON_MEMBERS

NAMESPACE_DEBUG_CHANNELS_START
extern Channel ava;
extern Channel agent;
extern Channel app;
extern Channel rpc;
extern Channel runtime;
extern Channel config;
extern Channel context;
extern Channel core;
extern Channel json;
extern Channel lsp;
extern Channel mcp;
extern Channel permissions;
extern Channel plugin;
extern Channel provider;
extern Channel session;
extern Channel tools;
extern Channel tui;
NAMESPACE_DEBUG_CHANNELS_END

#endif // CWDEBUG
