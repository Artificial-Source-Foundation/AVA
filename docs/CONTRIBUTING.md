# AVA Contributing

## Prerequisites

- CMake 3.25 or newer.
- A C++23 compiler. GCC 13+, Clang 16+, or a recent MSVC 2022 toolchain are good starting points.
- `ctest` from CMake.
- `clang-format` for changed C++ files.
- `clang-tidy` when touching core logic or safety-sensitive paths.

## Build And Test

```sh
cmake -S . -B build -DAVA_BUILD_TESTS=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

Sanitizer pass:

```sh
cmake -S . -B build-sanitize -DAVA_ENABLE_SANITIZERS=ON -DAVA_BUILD_TESTS=ON
cmake --build build-sanitize
ctest --test-dir build-sanitize --output-on-failure
```

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
- Do not include `build*/` trees or `docs/reference-code/` repositories in reviews.
