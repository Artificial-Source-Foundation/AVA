# AVA Release Artifact Checklist

This is the operator checklist for AVA's implemented local Linux host artifact. It is not a publish pipeline. The workflow does **not** commit, tag, push, publish, upload CI artifacts, sign, notarize, generate an SBOM, or call a live provider.

For the broader deterministic, sanitizer, terminal, and opt-in live-provider evidence map, see [`TESTING.md`](TESTING.md).

## Artifact contract

The version source is the top-level `project(ava VERSION X.Y.Z)` declaration. The packaged executable must report exactly `ava X.Y.Z` from `ava --version`.

The archive pair is:

```text
ava-X.Y.Z-linux-ARCH.tar.gz
ava-X.Y.Z-linux-ARCH.tar.gz.sha256
```

`ARCH` is normalized by the packaging script (for example, `x64` or `arm64`). The archive has one top-level `ava-X.Y.Z-linux-ARCH/` directory with this exact allowlist:

```text
bin/ava
share/doc/ava/README.md
share/doc/ava/LICENSE
share/doc/ava/docs/USAGE.md
share/doc/ava/docs/CONFIG.md
share/doc/ava/docs/TESTING.md
share/doc/ava/docs/headless-protocol.md
share/doc/ava/docs/rpc-protocol.md
share/doc/ava/docs/acp.md
share/doc/ava/docs/mcp.md
share/doc/ava/docs/session-format.md
share/doc/ava/docs/plugin-system.md
share/doc/ava/docs/plugin-compatibility-policy.md
share/doc/ava/docs/release-checklist.md
share/doc/ava/docs/engineering/session-versioning.md
share/doc/ava/docs/engineering/side-effect-safety-checklist.md
share/doc/ava/docs/interop/evidence/README.md
share/doc/ava/docs/interop/evidence/zed-1.9.0-2026-07-14.md
share/doc/ava/docs/product/mvp-coverage-ledger.md
share/doc/ava/docs/acp-support.json
share/doc/ava/docs/schema/theme.schema.json
```

The installed `README.md` comes from the artifact-specific `docs/release-artifact-readme.md`, not the repository README. Documentation remains in source-relative layout so included local links resolve. `scripts/verify-markdown-links.py` verifies every staged Markdown relative path before archive creation.

The allowlist excludes reference repositories, source and test trees, build trees, examples, credentials, auth/config/session state, provider output, raw interoperability evidence, and the optional desktop prototype. CMake component `ava` must remain exact; always pass `--component ava` for a manual stage:

```sh
cmake --install build-release \
  --prefix /absolute/private/stage/outside/the/checkout \
  --component ava
```

## Portability boundary

This is a dynamically linked **host artifact**, not a universal or manylinux-style bundle. The destination host needs compatible Linux glibc, libstdc++, and libgcc runtimes; compatible ncursesw/tinfo shared libraries and a usable terminfo database; and `curl` on `PATH` for provider HTTP transport.

Build-only dependencies are not packaged. Building from this checkout needs CMake 3.25+, a C++23 compiler, Boost and ncurses development files, Git, and configured source dependencies. Packaging additionally needs Bash, Python 3, `tar`, `sha256sum`, and Linux `renameat2`. Python is used only for staged-link verification, negative tests, and descriptor-anchored no-replace publication; it is not a runtime dependency.

## Release-candidate checks

### 1. Preflight

- Confirm the intended top-level CMake version and exact `ava --version` output.
- Confirm scope remains terminal `ava` plus the documented allowlist.
- Confirm no credentials, local configuration, session files, quarantines, or provider output can enter the stage.
- Confirm commit/tag/push/publish/CI-artifact/signing/CPack/cross-build/desktop work remains out of scope unless separately approved.

### 2. Developer build and deterministic tests

```sh
cmake --preset dev
cmake --build --preset dev
ctest --preset dev --output-on-failure
```

Focused release-closure coverage uses the registered CTest names. Python 3 and built `ava`, `ava_fake_provider_server`, and `ava_fake_mcp_server` executables are prerequisites for `ava_cli.acp_subprocess`.

```sh
ctest --test-dir build \
  -R '^(ava_tests\.(session|app_runtime|app_rpc|agent_loop|agent_loop_resilience|acp)|ava_cli\.acp_subprocess|ava_release\.package_linux)$' \
  --output-on-failure
```

The deterministic full-binary model/tool smoke is:

```sh
ctest --test-dir build -R '^ava_cli\.headless_e2e_model_smoke$' --output-on-failure
```

No command above opts into live-provider calls.

### 3. Sanitizer gate

```sh
cmake --preset sanitize
cmake --build --preset sanitize
ctest --preset sanitize --output-on-failure
```

If the host cannot run ASan/UBSan, record the exact environment blocker rather than claiming a pass.

### 4. Optional terminal evidence

On a host with tmux and terminal support:

```sh
AVA_TUI_TMUX_SMOKE=1 ctest --test-dir build -R '^ava_tui\.tmux_smoke$' --output-on-failure
```

Other terminal and intentionally credential-gated live-provider checks remain classified in [`TESTING.md`](TESTING.md). They are not package-script steps.

## Build and verify the Linux host artifact

Build mode configures/builds the current checkout and publishes to a caller-supplied secure output directory:

```sh
scripts/package-linux.sh --output-dir /absolute/path/outside/AVA
```

Accepted-binary mode does not configure or build:

```sh
scripts/package-linux.sh \
  --binary /absolute/path/to/ava \
  --fake-provider /absolute/path/to/ava_fake_provider_server \
  --output-dir /absolute/path/outside/AVA
```

Accepted-binary mode rejects any executable whose exact `ava X.Y.Z` output differs from the current checkout's top-level CMake version before checkout documentation is staged. `--fake-provider` is optional in this mode; omitting it produces an explicit deterministic model-smoke skip.

If `--output-dir` is omitted, the script creates and reports a new unpredictable mode-`0700` directory outside the checkout. A supplied output directory must be empty, outside the checkout, owned by the effective user, and have exact mode `0700`. The script rejects a symlink output directory and never overwrites existing archive/checksum destinations (including symlinks).

All staging, allowlist/link checks, archive and checksum creation, checksum verification, extraction, CLI smoke, and fake-provider model smoke happen inside one private unpredictable work directory. The archive is never executed, extracted, or verified after publication. Publication is the final operation: unique temporary files are written and synced through one verified output-directory descriptor, moved with atomic no-replace `renameat2`, directory-synced, and the output namespace identity is revalidated. The script removes only its private work directory; it never recursively deletes caller output.

The deterministic package harness covers secure accepted-binary packaging, default private output, staged allowlist, checksum/extraction, insecure and in-repository output rejection, version mismatch, and symlink no-clobber behavior:

```sh
ctest --test-dir build -R '^ava_release\.package_linux$' --output-on-failure
```

Build mode should also be run once for the release candidate because the CTest harness deliberately uses the already-built test binary.

Independent inspection remains useful:

```sh
tar -tzf /absolute/output/ava-X.Y.Z-linux-ARCH.tar.gz
(
  cd /absolute/output
  sha256sum -c ava-X.Y.Z-linux-ARCH.tar.gz.sha256
)
```

## Session quarantine policy

Leased recovery may create one sibling `<session>.jsonl.torn-tail.<unique>.bin` file with exact mode `0600`. It contains only the exact strict-JSON-invalid unterminated suffix. Recovery first writes and syncs a non-final temporary sibling, atomically publishes a unique final quarantine without overwrite, syncs the anchored session directory, and only then truncates the verified source inode. Prepublication failure cleans the temporary name and leaves the source byte-for-byte unchanged. A namespace change after publication aborts truncation and preserves the completed quarantine; the error reports its path.

Quarantines are operator recovery aids and are never replayed or packaged automatically. Stop AVA writers and preserve both files before investigation. Never append a quarantine after newer records. Reconstruct the previous bytes only in a separate offline copy, repair and strictly validate that copy, and retain the originals until verification is complete. See [`engineering/session-versioning.md`](engineering/session-versioning.md) and [`session-format.md`](session-format.md).

## Final local checks

```sh
shellcheck scripts/package-linux.sh  # when installed
python3 -m py_compile scripts/verify-markdown-links.py scripts/publish-linux-artifacts.py tests/package_linux_tests.py
git --no-pager diff --check
```

Format changed C++ with the repository `clang-format`. Record exact commands, pass/skip results, and environment blockers; do not report checks that were not run.

## Explicitly deferred work

- CI artifact jobs and retention policy.
- Commit/version-bump automation, tags, pushes, and release publication.
- CPack or distribution package formats.
- Cross-compilation and cross-distribution compatibility matrices.
- Static/runtime dependency bundling.
- Artifact signing, notarization, SBOM, and provenance attestations.
- Registry/package-manager publishing.
- Desktop artifacts.
