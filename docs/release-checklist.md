# AVA Release Artifact Checklist

This document is a concrete release-artifact plan and operator checklist. It is
not an implemented release pipeline, and it intentionally does not require CI,
tagging, pushing, publishing, or package-manager work unless that work is
explicitly requested later.

For detailed test coverage, provider-live classifications, TUI smoke gates, and
performance thresholds, use [`docs/TESTING.md`](TESTING.md). This checklist only
summarizes the artifact-specific sequence so the release steps do not duplicate
the existing test evidence map.

## Pi Reference Gap

Pi has an artifact pipeline that AVA does not yet have:

| Pi reference | What Pi does | AVA gap / local decision |
| --- | --- | --- |
| `docs/reference-code/pi/package.json` | Defines `build`, `check`, `test`, `release:local`, `release:*`, `publish`, and dry-run publish scripts. | AVA has CMake presets and CTest, but no release/package script surface. Keep this checklist manual until a pipeline is requested. |
| `docs/reference-code/pi/test.sh` | Runs tests after hiding local auth and API keys. | AVA's default CTest skips credential-gated provider tests unless opted in. Preserve that model; do not make live credentials mandatory. |
| `docs/reference-code/pi/scripts/build-binaries.sh` | Builds Bun binaries for darwin/linux/windows x64/arm64, copies runtime assets, archives, and re-extracts for smoke testing. | AVA currently builds a host C++ binary with CMake. Cross-platform archive production, runtime dependency manifests, and asset-copy rules are not defined. |
| `docs/reference-code/pi/scripts/local-release.mjs` | Produces unpublished npm tarballs plus local Bun binary archives outside the repo, then asks for Node/Bun smoke tests from the isolated install. | AVA needs an equivalent isolated outside-repo smoke for the packaged `ava` binary before any artifact is called release-ready. |
| `docs/reference-code/pi/scripts/release.mjs` | Enforces clean tree, bumps versions, updates changelogs, regenerates release artifacts, runs checks, commits, tags, and pushes. | AVA should not automate commit/tag/push here. Version, changelog, and release-note policy remain manual. |
| `docs/reference-code/pi/scripts/publish.mjs` | Validates package contents and performs idempotent npm publish with provenance. | AVA has no package registry target, signing/provenance policy, or artifact publishing helper. |

## Current AVA Build Surfaces

- Version source: `project(ava VERSION ...)` in `CMakeLists.txt`; the binary
  reports it with `ava --version`.
- Primary release target: `ava` from `src/CMakeLists.txt`, emitted at the CMake
  build directory root.
- Test/support targets: `ava_tests`, `ava_fake_provider_server`,
  `ava_fake_lsp_server`, and `ava_fake_mcp_server` from `tests/CMakeLists.txt`.
- Optional prototype target: `ava-desktop` behind `AVA_BUILD_DESKTOP_QML=ON`;
  do not include it in terminal release artifacts unless the desktop prototype
  becomes a release goal.
- CMake presets: `dev`, `sanitize`, `release`, and `desktop-qml` in
  `CMakePresets.json`.

## Release Candidate Checklist

### 1. Preflight

- Confirm the release scope: terminal `ava` binary only unless explicitly
  expanded.
- Confirm no CI edits are part of this pass.
- Confirm the version reported by the release binary will match the intended
  release number.
- Do not record provider keys, auth files, session contents, or local config in
  release notes or artifacts.

### 2. Build Targets

Required developer/test build:

```sh
cmake --preset dev
cmake --build --preset dev --target ava ava_tests ava_fake_provider_server ava_fake_lsp_server ava_fake_mcp_server
```

Required optimized binary build:

```sh
cmake --preset release
cmake --build --preset release --target ava
./build-release/ava --version
./build-release/ava --help
```

Optional release-optimized test build when compiler/runtime risk is high:

```sh
cmake -S . -B build-release-tests -DCMAKE_BUILD_TYPE=Release -DAVA_BUILD_TESTS=ON
cmake --build build-release-tests --target ava ava_tests
ctest --test-dir build-release-tests --output-on-failure
```

### 3. CTest Gate

Run the default deterministic suite from the dev build:

```sh
ctest --preset dev --output-on-failure
```

The full-binary fake-provider smoke remains the default artifact gate for the
agent loop and RPC/tool path:

```sh
ctest --test-dir build -R '^ava_cli\.headless_e2e_model_smoke$' --output-on-failure
```

See [`docs/TESTING.md`](TESTING.md) for the complete suite map, focused plugin
/ MCP commands, live-provider matrix, and performance thresholds.

### 4. Sanitizer Gate

Run before a release candidate unless the environment cannot support ASan/UBSan;
record any environment blocker in the release journal.

```sh
cmake --preset sanitize
cmake --build --preset sanitize
ctest --preset sanitize --output-on-failure
```

### 5. Opt-in Live Provider Smokes

Live provider checks are evidence, not default gates. Run them only when the
operator intentionally provides credentials:

```sh
AVA_LIVE_PROVIDER_SMOKE=1 ctest --test-dir build -R provider_live_smoke --output-on-failure
AVA_LIVE_PROVIDER_SMOKE=1 sh scripts/live-model-dogfood.sh
AVA_LIVE_PROVIDER_SMOKE=1 sh scripts/live-coding-dogfood.sh
```

Classify each provider result using the vocabulary in
[`docs/TESTING.md`](TESTING.md): passed, skipped/no credential,
credential/auth-blocked, provider/rate-limited, network-blocked,
provider-behavior/inconclusive, or AVA regression.

### 6. Opt-in TUI / Terminal Smokes

Run gated terminal smokes when the host has the required tools and terminal
support:

```sh
AVA_TUI_TMUX_SMOKE=1 ctest --test-dir build -R ava_tui.tmux_smoke --output-on-failure
AVA_TUI_KITTY_IMAGE_SMOKE=1 ctest --test-dir build -R ava_tui.kitty_image_smoke --output-on-failure
AVA_TUI_OSC8_SMOKE=1 ctest --test-dir build -R ava_tui.osc8_smoke --output-on-failure
```

Skipped prerequisite-gated smokes are acceptable when documented. A failure with
the gate enabled is a release-candidate blocker unless triaged to environment.

### 7. Package Artifacts

Until AVA has `install()`/CPack/package scripts, create only a host-platform
manual archive for review. The archive should be built from `build-release/ava`
and staged outside the repository.

Minimum archive contents:

- `ava` executable.
- `README.md` and `LICENSE`.
- User/operator docs needed offline: `docs/USAGE.md`, `docs/CONFIG.md`,
  `docs/TESTING.md`, `docs/headless-protocol.md`, and this checklist.

Do not include:

- `build*/` directories.
- `docs/reference-code/` repositories.
- auth/config/session state from `$XDG_CONFIG_HOME` or `$XDG_STATE_HOME`.
- temporary live-smoke evidence that may contain local paths or provider output.

Manual host artifact sketch:

```sh
version="$(./build-release/ava --version | awk '{print $2}')"
platform="$(uname -s | tr '[:upper:]' '[:lower:]')-$(uname -m)"
stage="/tmp/ava-release/ava-${version}-${platform}"
rm -rf "$stage" "$stage.tar.gz" "$stage.tar.gz.sha256"
mkdir -p "$stage/docs"
install -m 0755 build-release/ava "$stage/ava"
install -m 0644 README.md LICENSE "$stage/"
install -m 0644 docs/USAGE.md docs/CONFIG.md docs/TESTING.md docs/headless-protocol.md docs/release-checklist.md "$stage/docs/"
tar -C "$(dirname "$stage")" -czf "$stage.tar.gz" "$(basename "$stage")"
sha256sum "$stage.tar.gz" > "$stage.tar.gz.sha256"
```

Package smoke from outside the repo:

```sh
"$stage/ava" --version
"$stage/ava" --help
"$stage/ava" packages list
```

For a stronger outside-repo smoke, reuse the checked-in CMake smoke script with
the packaged binary and the dev fake-provider server:

```sh
cmake \
  -DAVA_EXE="$stage/ava" \
  -DAVA_FAKE_PROVIDER_EXE="$PWD/build/ava_fake_provider_server" \
  -DAVA_CLI_TEST_ROOT=/tmp/ava-release-smoke \
  -P tests/cli_headless_e2e_model_smoke.cmake
```

Future artifact targets should be explicit per platform, for example
`ava-<version>-linux-x64.tar.gz`, `ava-<version>-linux-arm64.tar.gz`,
`ava-<version>-darwin-x64.tar.gz`, `ava-<version>-darwin-arm64.tar.gz`,
`ava-<version>-windows-x64.zip`, and `ava-<version>-windows-arm64.zip`.

### 8. Checksums

- Produce one checksum file per archive during manual packaging.
- Before handoff, verify the checksum file from a different working directory:

```sh
sha256sum -c "$stage.tar.gz.sha256"
```

- When multiple artifacts exist, also produce a top-level `SHA256SUMS` file and
  verify all entries before uploading or attaching artifacts anywhere.

### 9. Final Local Checks

Always finish with:

```sh
git --no-pager diff --check
```

If the release work touched C++ files, also run `clang-format` on changed C++ /
header files and `clang-tidy <changed-cpp-files> -p build` when available, as
described in [`docs/TESTING.md`](TESTING.md).

## Known Blockers Before a Real Pipeline

- No CMake `install()` rules, CPack config, or packaging script exists for AVA.
- No checked-in CI artifact workflow is present in this checkout, and this plan
  intentionally avoids CI changes.
- Cross-platform release builds are not defined; current commands only produce a
  host-platform binary.
- Runtime dependency bundling is not specified for Boost, `ncursesw`, Curses
  terminfo expectations, `curl`, or platform-specific dynamic libraries.
- Artifact signing, notarization, SBOM/provenance, and registry publishing are
  not designed.
- Source archive policy is undecided because `docs/reference-code/` contains
  local behavior references that should not be swept into review/build/package
  flows accidentally.
- The optional Qt desktop prototype is not connected to the backend runtime and
  should stay out of release artifacts unless separately scoped.
- Remote package/resource install remains deferred pending source allowlists,
  provenance/signing, compatibility, rollback, and trust UX; see
  [`docs/CONFIG.md`](CONFIG.md) and [`docs/plugin-system.md`](plugin-system.md).

## Next Implementation Slices If Approved Later

1. Add CMake install rules for the terminal binary and selected docs only.
2. Add a local packaging script that stages outside the repo, archives, extracts,
   runs package smokes, and writes checksums.
3. Define platform artifact names and runtime dependency policy.
4. Add signing/provenance/SBOM decisions.
5. Only after the local path is stable, add CI artifact jobs and publish gates.
