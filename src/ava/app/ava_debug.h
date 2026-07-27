#pragma once

#ifdef CWDEBUG
namespace ava::app {
// Initialize libcwd unless the environment suppresses output. Tests pass true
// only after installing a validated private output stream.
void debug_init(bool private_output_ready = false);
}  // namespace ava::app
#endif
