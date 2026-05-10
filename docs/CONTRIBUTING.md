# AVA Contributing

## Prerequisites

- CMake 3.25 or newer.
- A C++23 compiler. GCC 13+, Clang 16+, or a recent MSVC 2022 toolchain are good starting points.
- `ctest` from CMake.
- `clang-format` version 22 or newer.
- `clang-tidy` when touching core logic or safety-sensitive paths.
- internet access to `github.com` is required during configuration.

## Cloning the repository

```sh
git clone --branch develop --single-branch --recurse-submodules https://github.com/Artificial-Source/AVA.git
cd AVA
./autogen.sh
```

If you want to compile with debug output then you need to have:

* GITACHE_ROOT : full path to an existing (initially empty) directory where [gitache](https://github.com/CarloWood/gitache) packages are compiled. For example `$HOME/gitache` or `/opt/gitache`. Just make sure you can write to it. *Current* requirement is to have ~50MB of room. To be completely future proof you'll have enough with 2GB of disk space for this directory.

## Quick Start (build and test)

```sh
cmake -S . -B build -DAVA_BUILD_TESTS=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

Preset equivalent:

```sh
cmake --preset dev
cmake --build --preset dev
ctest --preset dev
```

Sanitizer pass:

```sh
cmake -S . -B build-sanitize -DAVA_ENABLE_SANITIZERS=ON -DAVA_BUILD_TESTS=ON
cmake --build build-sanitize
ctest --test-dir build-sanitize --output-on-failure
```

Preset equivalent:

```sh
cmake --preset sanitize
cmake --build --preset sanitize
ctest --preset sanitize
```

Tests currently build into one `ava_tests` CTest target from focused test sources under `tests/`. LSP coverage uses the `ava_fake_lsp_server` support executable.

## Configuration

Lets assume that, next to GITACHE_ROOT (see above) the following environment variables are set:
* `REPOROOT` : full path of the repository root of ava (i.e. the directory that you cloned ava into.
* `BUILDDIR` : full path to the build directory; this can be inside or outside the repository root (e.g. `$REPOROOT/build` is fine) but should be an empty or non-existent directory.
* `CMAKE_CONFIG` : the CMake build type. Possible options are `Release`, `RelWithDebInfo`, `Debug`, `BetaTest`, `RelWithDebug` explained [here](https://stackoverflow.com/a/59314670/1487069).

Then one can configure AVA with:

```sh
cmake -S "$REPOROOT" -B "$BUILDDIR" -DCMAKE_BUILD_TYPE="$CMAKE_CONFIG" [OPTIONS]
```

where **`OPTIONS`** is one or more of the following:

* `-GNinja` : use ninja instead of make (highly recommended)
* `--log-level=NOTICE` : reduce CMake output to notices, warnings and errors. Possible values are `ERROR`, `WARNING`, `NOTICE`, `STATUS` (default), `VERBOSE`, `DEBUG`, and `TRACE`.
  As a developer you should use the default (STATUS) or more verbose, otherwise the CMake options are not shown.
* `-DCMAKE_CXX_COMPILER_LAUNCHER=ccache` : also set the environment variable `CCACHE_DIR` to a writable directory. As an active developer this is a must to speed up recompilations.
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
cmake --build "$BUILDDIR" --config "$CMAKE_CONFIG" --parallel $CPUS [--verbose]
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

Always finish with:

```sh
git --no-pager diff --check
```

## Review Expectations

- Keep changes small and subsystem-owned.
- Preserve backend permission boundaries for file writes and process execution.
- Add focused regression tests for safety-sensitive changes.
- For plugin or MCP contract changes, follow [`docs/plugin-compatibility-policy.md`](plugin-compatibility-policy.md) and update deterministic golden fixtures when stable serialized shapes intentionally change.
- Do not include `build*/` trees or `docs/reference-code/` repositories in reviews.
