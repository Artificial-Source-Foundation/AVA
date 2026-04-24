# C++ Milestone 2 Boundaries (Bootstrap Green-Fix Pass)

This note documents the scoped Milestone 2 bootstrap quality fixes that were applied after the original M2 landing.

## Implemented in this M2 green-fix pass

1. Added explicit CMake presets in `cpp/CMakePresets.json` for predictable local configure/build/test flows (`cpp-debug`, `cpp-release`).
2. Added repository-level `just` helpers for C++ bootstrap ergonomics:
   - `just cpp-presets`
   - `just cpp-configure [PRESET]`
   - `just cpp-build [PRESET]`
   - `just cpp-test [PRESET]`
   - `just cpp-clean [PRESET]`
3. Added minimal per-target C++20/toolchain hygiene wiring via a dedicated helper (`cpp/cmake/ToolchainHygiene.cmake`) and applied it across current C++ libraries/apps/tests.
4. Updated contributor and architecture docs so the C++ bootstrap lane and M2 boundary artifact are discoverable.

## Explicitly Deferred

1. Any runtime behavior changes for headless/TUI/orchestration flows.
2. Broad CMake restructuring beyond bootstrap ergonomics and low-risk target hygiene.
3. New C++ feature surface expansion outside the existing milestone tree.
4. Full parity claims with the Rust runtime stack.

## Verification Scope

The green-fix scope is verified through bootstrap-oriented checks only:

1. `cmake --list-presets=all` in `cpp/` to validate preset discovery.
2. `just cpp-configure` to validate configure through the shared CMake locator/bootstrap path.
3. `just cpp-build` to validate build graph viability through the same preset lane.
4. `just cpp-test` to validate current C++ test registration/execution.

This keeps Milestone 2 honest: build/bootstrap quality and docs improvements only, with no runtime behavior broadening.
