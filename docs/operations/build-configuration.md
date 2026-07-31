# Build configuration reference

This document describes the current CMake surface. For prerequisites and normal workflows, see [CONTRIBUTING.md](../development/contributing.md); for test selection, see [TESTING.md](testing.md).

## Presets

CMake 3.25+ reads `CMakePresets.json` (version 6). Configure with `cmake --preset NAME`, then use the repository wrappers so build and test operations share the per-tree lock:

```sh
cmake --preset dev
scripts/build.sh --build-dir build
scripts/run-tests.sh --build-dir build
```

| Preset | Binary directory | Key cache settings |
| --- | --- | --- |
| `dev` | `build` | `EnableAvaBuildTests=ON`, `CMAKE_BUILD_TYPE=BetaTest`, `EnableDebug=OFF`, `CMAKE_EXPORT_COMPILE_COMMANDS=ON` |
| `sanitize` | `build-sanitize` | Inherits `dev`; `EnableAvaSanitizers=ON` |
| `tsan` | `build-tsan` | Inherits `dev`; `AVA_ENABLE_TSAN=ON` |
| `release` | `build-release` | Tests on; `CMAKE_BUILD_TYPE=Release` |
| `desktop-qml` | `build-desktop-qml` | `AVA_BUILD_DESKTOP_QML=ON`, tests off, compile commands on |

There are corresponding build presets. Test presets are `dev`, `sanitize`, `release-rpc`, `sanitize-rpc`, `release-acp`, `sanitize-acp`, and `tsan`; the latter intentionally runs a focused race-sensitive subset.

## AVA cache options

Pass cache values at configure time, for example `cmake --preset dev -DAVA_ENABLE_GITACHE=OFF`.

| Option | Default | Effect |
| --- | --- | --- |
| `AVA_ENABLE_GITACHE` | `ON` | Fetches/enables gitache-managed developer dependencies. With it off, required dependency provisioning must already be available. |
| `EnableAvaBuildTests` | Compatibility-derived | Canonical AICxx/cwds option that builds AVA tests. |
| `AVA_BUILD_TESTS` | `ON` | Compatibility alias/default input for `EnableAvaBuildTests`. An explicitly supplied `EnableAvaBuildTests` wins. |
| `EnableAvaSanitizers` | Compatibility-derived | Canonical AICxx/cwds option: AddressSanitizer plus UndefinedBehaviorSanitizer. |
| `AVA_ENABLE_SANITIZERS` | `OFF` | Compatibility alias/default input for `EnableAvaSanitizers`; the canonical option wins when explicitly set. |
| `AVA_ENABLE_TSAN` | `OFF` | Applies ThreadSanitizer to project-built objects for focused coordinator/ACP race coverage. It is mutually exclusive with ASan/UBSan. |
| `AVA_BUILD_DESKTOP_QML` | `OFF` | Builds the experimental Qt Quick/QML desktop prototype under `src/ava/desktop`. |
| `AVA_DEBUG_MAXLEN` | `100` | Numeric default debug-stream string truncation length; `0` means unlimited. It generates `config::ava_debug_maxlen_c`. |
| `AVA_REQUIRE_ACP_SDK_INTEROP` | `OFF` | Requires Node and installed official SDK packages at configure/test time; fails rather than skipping when unavailable. |
| `AVA_ENABLE_ACPX_INTEROP` | `OFF` | Enables the opt-in acpx interoperability smoke. It requires Python and the pinned Node setup. |

`AVA_NODE_EXECUTABLE` is a discovered CMake cache entry used only by ACP interop tests; normally do not set it. The two ACP options only exist when tests are configured. See [acp.md](../acp.md) and [TESTING.md](testing.md); they are not production features.

## Relevant AICxx/cwds options and build types

AVA imports AICxx through `cmake/aicxx/Project` and uses its `cw_option` mechanism for `EnableAvaBuildTests` and `EnableAvaSanitizers`. The preset also sets `EnableDebug=OFF`; that is an AICxx debugging option, not an AVA alias. Do not replace canonical `EnableAva*` names in new automation with the compatibility names.

The configured AICxx build-type validation accepts:

`Release`, `Debug`, `RelWithDebInfo`, `BetaTest`, `RelWithDebug`, `Perf`, `Tracy`, and `None`.

`BetaTest` is the normal developer preset: optimized developer build with assertions/tests as configured. `Release` is the release preset. Select one through `CMAKE_BUILD_TYPE` for single-config generators; do not assume an unlisted CMake build type will be accepted. AVA itself requires C++23, disables compiler extensions, and treats warnings as errors (with its documented GCC maybe-uninitialized exception).

## Generators, parallelism, and compiler caches

Ninja is the recommended generator. Enable compile commands for clangd/clang-tidy with `-DCMAKE_EXPORT_COMPILE_COMMANDS=ON` (already set by `dev`). A compiler launcher is configured once, not exported as a runtime feature:

```sh
cmake --preset dev -DCMAKE_CXX_COMPILER_LAUNCHER=sccache
# or: -DCMAKE_CXX_COMPILER_LAUNCHER=ccache
```

Use a writable `SCCACHE_DIR` or `CCACHE_DIR` if the default cache location is unsuitable. Cache entries are naturally separated by compiler flags, so configure sanitizer and normal trees independently.

Use `scripts/build.sh` and `scripts/run-tests.sh`, optionally `--jobs N`. The wrappers honor `CMAKE_BUILD_PARALLEL_LEVEL` and `CTEST_PARALLEL_LEVEL`, choose a positive logical-core count otherwise, and serialize work with a per-build-tree lock. Do not run a build and test concurrently in one tree. A native `cmake --build ... --parallel N` does not make CTest parallel; use the test wrapper or `ctest --parallel N`.

## Platform and dependency boundaries

The maintained build is a Unix-like C++23 terminal application with wide ncurses, Boost, CMake, and gitache/AICxx dependencies; the repository CI/build scripts and security-sensitive process implementation target Linux. Linux-specific containment, descriptor handling, and package/test paths are not portable promises. The QML target is explicitly experimental.

CMake has conditional MSVC warning/sanitizer branches, but this is **not** a claim that MSVC or Windows builds work or are supported. Likewise, do not infer macOS support from generic CMake code. Validate a target platform from its current CI/release evidence before treating it as supported.

For a clean source checkout, dependency pins/submodules and the release provenance rules remain authoritative; see [CONTRIBUTING.md](../development/contributing.md), [release-checklist.md](release-checklist.md), and `THIRD_PARTY_NOTICES.md`.
