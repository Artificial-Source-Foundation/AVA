#pragma once

#ifdef CWDEBUG
namespace ava::app {
// Initialize process-wide libcwd routing for the ava executable.
//
// When AVA_TEST_NAME identifies a test and AVA_DEBUG_OUTPUT_DIR is configured,
// the private <test-name>.libcwd.log destination overrides AVA_NO_DEBUG_OUTPUT.
// Outside a test this preserves the ordinary debug_init() behavior.
// The owned test stream remains alive until process teardown.
void initialize_debug();

// Initialize libcwd unless the environment suppresses output. Tests pass true
// only after installing a validated private output stream.
void debug_init(bool have_private_output_stream = false);
}  // namespace ava::app
#endif
