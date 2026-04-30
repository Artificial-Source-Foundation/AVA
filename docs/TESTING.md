# AVA Testing

## Normal Test Run

```sh
cmake -S . -B build -DAVA_BUILD_TESTS=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

## Sanitizers

```sh
cmake -S . -B build-sanitize -DAVA_ENABLE_SANITIZERS=ON -DAVA_BUILD_TESTS=ON
cmake --build build-sanitize
ctest --test-dir build-sanitize --output-on-failure
```

The sanitizer preset enables AddressSanitizer and UndefinedBehaviorSanitizer for supported non-MSVC builds.

## Formatting And Static Checks

Format changed C++ files with the repository `.clang-format`:

```sh
clang-format -i <changed-cpp-or-header-files>
```

Run clang-tidy against changed implementation files after configuring the build:

```sh
clang-tidy <changed-cpp-files> -p build
```

Before handing work off, check for whitespace and patch-format issues:

```sh
git --no-pager diff --check
```

## Coverage Areas

The current single test binary covers:

- mode parsing
- session JSONL storage, resume, listing, corruption handling, and permissions
- XDG path handling
- OpenAI auth loading/storage
- model and prompt configuration
- provider request/SSE parsing
- file/search/bash tools
- tool dispatcher and agent loop
- minimal TUI rendering/input helpers

Add regression tests for every safety-sensitive bug fix.
