# AVA Contributing

For the complete CMake option reference, see [build-configuration.md](../operations/build-configuration.md). Documentation changes follow [documentation.md](documentation-policy.md), including the offline source-link gate.

For the community entry point and project policies, see the root [`CONTRIBUTING.md`](../../CONTRIBUTING.md). Please read the [`Code of Conduct`](../../CODE_OF_CONDUCT.md), [`Governance`](../../GOVERNANCE.md), [`Security`](../../SECURITY.md), and [`Support`](../../SUPPORT.md) guidance before participating. Use private security reporting for suspected vulnerabilities; do not open public issues with exploit details or secrets.

## Prerequisites

- CMake 3.25 or newer.
- A C++23 compiler. GCC 13+, Clang 16+, or a recent MSVC 2022 toolchain are good starting points.
- `ctest` from CMake.
- Boost development headers and CMake package (`boost-devel` on Fedora).
- Wide-character ncurses development headers/library (`ncurses-devel` on Fedora).
- `clang-format` version 22 or newer.
- `clang-tidy` when touching core logic or safety-sensitive paths.
- Optional `sccache` or `ccache` for faster repeated compilation.
- internet access to `github.com` is required during configuration.

## Cloning the repository

```sh
git clone --branch develop --single-branch --recurse-submodules https://github.com/Artificial-Source/AVA.git
cd AVA
```

A recursive clone is ready for the CMake quick start below. For an older nonrecursive clone, run `git submodule update --init --checkout --recursive`. `./autogen.sh` is optional maintainer convenience: it initializes missing submodules at AVA's pinned commits, sets `push.recurseSubmodules` when missing, and prints build guidance. It does not configure or build AVA and is not required after the recursive clone command.

If you want to compile with debug output then you need to have:

* `GITACHE_ROOT`: full path to an existing (initially empty) directory where gitache packages are compiled. For example `$HOME/gitache` or `/opt/gitache`. Make sure you can write to it; the current requirement is approximately 50MB.

## Quick Start (build and test)

```sh
cmake -S . -B build -DAVA_BUILD_TESTS=ON
scripts/build.sh --build-dir build
scripts/run-tests.sh --build-dir build
```

Preset equivalent:

```sh
cmake --preset dev
scripts/build.sh
scripts/run-tests.sh
```

Sanitizer pass:

```sh
cmake -S . -B build-sanitize -DAVA_ENABLE_SANITIZERS=ON -DAVA_BUILD_TESTS=ON
scripts/build.sh --build-dir build-sanitize --jobs 2
scripts/run-tests.sh --build-dir build-sanitize --jobs 2
```

Preset equivalent:

```sh
cmake --preset sanitize
scripts/build.sh --build-dir build-sanitize --jobs 2
scripts/run-tests.sh --build-dir build-sanitize --jobs 2
```

The repository build and test runners detect the available logical cores and pass an explicit positive parallel level to CMake/CTest. Use `--jobs N`, `CMAKE_BUILD_PARALLEL_LEVEL=N`, or `CTEST_PARALLEL_LEVEL=N` to cap it; build options such as `--target` and CTest options such as `-R` are forwarded. The runners share one build-tree lock because concurrent builds/tests and several fixed integration-test roots are unsafe. Sanitizer examples use two jobs to limit memory pressure.

Parallel jobs speed clean builds; a compiler cache speeds repeated builds. To enable an installed cache, configure once with `cmake --preset dev -DCMAKE_CXX_COMPILER_LAUNCHER=sccache` (or replace `sccache` with `ccache`). Use the same option with the sanitizer preset if desired; compiler flags keep those cache entries separate.

Tests currently build into one `ava_tests` CTest target from focused test sources under `tests/`. LSP coverage uses the `ava_fake_lsp_server` support executable.

## Configuration

Lets assume that, next to `GITACHE_ROOT` (see above), the following environment variables are set:
* `REPOROOT` : full path of the repository root of ava (i.e. the directory that you cloned ava into.
* `BUILDDIR` : full path to the build directory; this can be inside or outside the repository root (e.g. `$REPOROOT/build` is fine) but should be an empty or non-existent directory.
* `CMAKE_CONFIG` : the CMake build type. Accepted values are `Release`, `Debug`, `RelWithDebInfo`, `BetaTest`, `RelWithDebug`, `Perf`, `Tracy`, and `None`, as enforced by `cmake/aicxx/cmake/CW_OPTIONS.cmake`.

Then one can configure AVA with:

```sh
cmake -S "$REPOROOT" -B "$BUILDDIR" -DCMAKE_BUILD_TYPE="$CMAKE_CONFIG" [OPTIONS]
```

where **`OPTIONS`** is one or more of the following:

* `-GNinja` : use ninja instead of make (highly recommended)
* `--log-level=NOTICE` : reduce CMake output to notices, warnings and errors. Possible values are `ERROR`, `WARNING`, `NOTICE`, `STATUS` (default), `VERBOSE`, `DEBUG`, and `TRACE`.
  As a developer you should use the default (STATUS) or more verbose, otherwise the CMake options are not shown.
* `-DCMAKE_CXX_COMPILER_LAUNCHER=sccache` (or `ccache`) : use an installed compiler cache to speed up recompilations; set `SCCACHE_DIR` or `CCACHE_DIR` when a non-default writable cache directory is needed.
* `-DAVA_BUILD_TESTS=ON` : to compile the testsuite of ava.
* `-DCMAKE_EXPORT_COMPILE_COMMANDS=ON` : generate the `compile_commands.json` compilation database, useful for tools that need the exact compiler command line for each source file.
  Typical tools that use it are:
  - `clangd`, used by editors and IDEs for C/C++ language-server features such as code completion, diagnostics, go-to-definition and include resolution.
  - `clang-tidy`, for static analysis and automated lint/fix checks.
  - other Clang-based tooling, including custom LibTooling tools, refactoring tools, include analyzers and source indexers.

Provided the `--log-level` is `STATUS` other options are printed, using colors, during configuration.
For example:
```
-- Option EnableDebug (Build for debugging) =
        ON (default)
```
If everything is green then you are using the defaults, which is usually the best.
If an option is printed in red then it was manually overridden and different from the default.
For example, using `-DCMAKE_BUILD_TYPE=Debug` will print
```
-- Option CMAKE_BUILD_TYPE =
        Debug
```
where `Debug` is in red, because the default is `Release`.

## Building

After (re)configuration AVA can be build by issuing the command:
```sh
scripts/build.sh --build-dir "$BUILDDIR" --jobs "$CPUS" --config "$CMAKE_CONFIG" [--verbose]
```

Adding `--verbose` shows the exact commands that are being executed by ninja;
mostly useful if one want to inspect if the expected compiler arguments are being passed to the compiler.

Note that the `--config "$CMAKE_CONFIG"` is only needed for multi-target generators, so not for `Makefile` or `Ninja`.
`CPUS` should be set to the number of processors that you want to use for compilation. For example, use `CPUS=$(nproc --all)`.

## Formatting And Static Checks

Format changed C++ with the repository `.clang-format`:

```sh
clang-format -i <changed-cpp-or-header-files>
```

Run clang-tidy against files you changed after configuring the build:

```sh
clang-tidy <changed-cpp-files> -p build
```

For Markdown changes, run both direct repository gates and their four focused
CTest cases:

```sh
python3 scripts/verify-markdown-links.py . --source-tree
python3 scripts/verify-documentation-structure.py .
scripts/run-tests.sh --build-dir build \
  -R '^ava_tests\.(markdown_(link_verifier|links_source)|documentation_structure_(checker|source))$' \
  --output-on-failure
```

When documentation paths or release-artifact payloads change, also run the
offline package, install, and provenance checks:

```sh
scripts/run-tests.sh --build-dir build \
  -R '^ava_release\.(provenance|install_component|package_linux)$' \
  --output-on-failure
```

Always finish with:

```sh
git --no-pager diff --check
```

## Review Expectations

- Keep changes small and subsystem-owned.
- Preserve backend permission boundaries for file writes and process execution.
- Add focused regression tests for safety-sensitive changes.
- For plugin or MCP contract changes, follow [`docs/plugin-compatibility-policy.md`](../plugin-compatibility-policy.md) and update deterministic golden fixtures when stable serialized shapes intentionally change.
- Do not include `build*/` trees or `docs/reference-code/` repositories in reviews.
