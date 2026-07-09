#pragma once

// Currently these are just the libcwd defaults.
// Defined here because sometimes we need them before we can include "debug.h".

#define NAMESPACE_DEBUG debug
#define NAMESPACE_DEBUG_START namespace NAMESPACE_DEBUG {
#define NAMESPACE_DEBUG_END }
#define NAMESPACE_CHANNELS channels
#define LIBCWD_DEBUG_CHANNELS NAMESPACE_DEBUG::NAMESPACE_CHANNELS
