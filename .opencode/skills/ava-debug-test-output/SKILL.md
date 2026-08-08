---
name: ava-debug-test-output
description: Capture and interpret libcwd debug output for a specific AVA test. Use when an AVA test aborts under CWDEBUG, reports a session-mutex or blocking-operation assertion, or needs per-suite runtime diagnostics.
---

# Getting libcwd Debug Output for a Specific AVA Test

AVAs debug builds link with libcwd (`CWDEBUG`), which provides detailed runtime
tracing of session mutex operations, thread lifecycles, and blocking-operation
assertions. This skill describes how to capture that output for a single test.

## Prerequisites

The project must be configured with libcwd enabled. Running configuration should
print that `Option EnableLibcwd` is `ON`.

Debug mode, `-DCMAKE_BUILD_TYPE=Debug`, has that on my default; otherwise add
`-DEnableDebug=ON` to the configuration. Note that that does not work with
`-DCMAKE_BUILD_TYPE=Release` which forces debug code out of the executable.
Instead, use for example `-DCMAKE_BUILD_TYPE=BetaTest -DEnableDebug=ON`.

Without `CWDEBUG` defined, the macros `Debug`, `Dout`, `ASSERT`, `SessionDebugMutex`,
`AVA_ASSERT_SESSION_UNLOCKED`, `AVA_ASSERT_NO_SESSION_LOCK_HELD`,
`AVA_DEBUG_PRINT_MESSAGE`, etc. are compiled out entirely. You should not
attempt to debug without them - *certainly* not a dead-lock.

## Running a Single Test With Debug Output

A readable file that specifies which debug channels to turn on must exist.
If `$WORKSPACE_ROOT/libcwdrc_ava` exists then you can use that. Otherwise
create a file with contents like:

```
# This is an override file; just define the debug channels that we need.

# libcwd default debug channels.
channels_on = warning,debug,notice,system

# AVA
channels_on = rpc,runtime,session,core
```
Available debug channels can be obtained with `grep '^Channel' src/ava/debug/print_members_on.cpp`,
where the uppercase names are what is listed after `channels_on` (case insensitive). Then
point `LIBCWD_RCFILE_OVERRIDE_NAME` to this file.

```sh
LIBCWD_RCFILE_OVERRIDE_NAME=$WORKSPACE_ROOT/libcwdrc_ava \
AVA_DEBUG_OUTPUT_DIR=/tmp/debug_output \
aap-test -R '^ava_tests\.<suite_name>$'
```

- `AVA_DEBUG_OUTPUT_DIR` sets the directory where libcwd writes one
  `<test_name>.libcwd.log` file per test process.

## Reading the Debug Log

```sh
tail -n 10 /tmp/debug_output/ava_tests.<suite_name>.libcwd.log
```

This file can potentially be quite large. Check the size before
reading it all with, for example, a `cat`.

Key markers to look for:

- `NOTICE : Entering SessionDebugMutex::lock() [0xADDRESS]` — a session mutex
  is being acquired. The address identifies which `session_ts` instance owns it.
- `NOTICE : Created session_r/w from ... (scoped) from FILE:LINE` — emitted by
  the `CRITICAL_AREA_*` and `SCOPED_CRITICAL_AREA_*` macros. If a lock has no
  corresponding "Created" message, it was created by a direct `rat`/`wat`/`crat`
  constructor that bypassed the macros.
- `COREDUMP : Potentially blocking operation while holding a session mutex` —
  a `Session` destructor, thread join, or other blocking call ran while the
  current thread still owned a session mutex. The message includes the operation,
  the source location, and/or the mutex address(es) held.

## Common Failure: Session Lock Leaked Past Destruction

When a `session_ts` (or `Result<session_ts>`) goes out of scope while a guard
created from it is still locked, the `Session` destructor's
`AVA_ASSERT_NO_SESSION_LOCK_HELD` fires. Match the leaked mutex address in the
log to find which guard was never released. Remember that the assertion checks
whether the current thread holds *any* session mutex, not just the one belonging
to the `Session` being destroyed.
