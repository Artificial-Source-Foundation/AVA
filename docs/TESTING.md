# AVA Testing

## Normal Test Run

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

The test suite is built as one `ava_tests` CTest target from focused test sources under `tests/`. The LSP tests also build and use `ava_fake_lsp_server` from `tests/support/`.

## Headless Tool Smoke

After provider streaming, tool schema, permission, or dispatcher changes, run a live headless smoke with configured OpenAI auth. Keep the workspace isolated under `/tmp/opencode` so mutating tools do not touch the repository.

Recommended coverage:

- `ava --print ... --json --allow read-only` for `read_file`, `glob`, and `grep`. This verifies OpenAI tool-call streaming, read/search permission auto-allow, `.gitignore` behavior, and tool progress events.
- `ava --print ... --json --allow-tool webfetch` for `webfetch`. This verifies the explicit `network.fetch` headless allow path and real bounded HTTP fetch behavior.
- `ava --rpc` with a small JSONL harness that answers `permission_requested` with `permission_reply` for `write_file`, `edit_file`, `apply_patch`, and `bash`. The checked-in headless bash cleanup smoke verifies that timed-out shell process groups do not leave a child process behind.
- `ava --rpc` with `question_reply` for the `question` tool.

`lsp_diagnostics` is capability-gated in normal headless runtime. Verify it through `ava_tests` and the fake LSP server unless a local diagnostics provider is configured for a live run.

## Sanitizers

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

The `ava_tests` binary covers:

- mode parsing
- session JSONL storage, resume, listing, corruption handling, and permissions
- XDG path handling
- OpenAI auth loading/storage and OAuth refresh preflight
- model and prompt configuration
- provider request/SSE parsing, including OpenAI Responses function-call starts from `response.output_item.added`
- permission audit persistence, file/search/bash/webfetch/LSP tools, bash process-group cleanup, spill files, and atomic file writes
- tool dispatcher and agent loop
- print mode and JSONL RPC success, denial/recovery, malformed input, cancellation, and refresh paths
- TUI rendering, input, keybindings, palette, permission prompt, markdown, UTF-8, and scroll helpers

Add regression tests for every safety-sensitive bug fix.
