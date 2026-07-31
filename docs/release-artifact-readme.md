# AVA Linux Host Artifact

This archive contains the AVA terminal executable for the Linux host on which it was built. It is a dynamically linked host artifact, not a portable Linux distribution package.

## Runtime requirements

The destination host needs compatible glibc, libstdc++, and libgcc runtimes; compatible ncursesw/tinfo libraries and a usable terminfo database; and `curl` on `PATH` for provider HTTP transport. Python and CMake are packaging-only requirements and are not needed to run AVA.

## Basic commands

From the extracted top-level directory:

```sh
bin/ava --version
bin/ava --help
bin/ava
bin/ava --print "Explain this checkout"
bin/ava --rpc
bin/ava --acp
```

AVA stores user configuration and state under the XDG paths documented in [`docs/CONFIG.md`](docs/CONFIG.md). Provider use may require separately configured credentials; no credentials, local configuration, sessions, or provider output are included in this archive.

## Included documentation

- [`docs/USAGE.md`](docs/USAGE.md): CLI, TUI, print, RPC, and ACP usage.
- [`docs/CONFIG.md`](docs/CONFIG.md) and [`docs/environment-variables.md`](docs/environment-variables.md): configuration, credentials, XDG paths, and process inputs.
- [`docs/tools.md`](docs/tools.md): built-in model-visible tools, bounds, aliases, and permission separation.
- [`docs/lsp.md`](docs/lsp.md) and [`docs/security/containment.md`](docs/security/containment.md): optional local language-server support and the verified command-containment contract.
- [`docs/troubleshooting.md`](docs/troubleshooting.md) and [`docs/diagnostics.md`](docs/diagnostics.md): symptom-first recovery, passive doctor, and sanitized support exports.
- [`docs/rpc-protocol.md`](docs/rpc-protocol.md), [`docs/acp.md`](docs/acp.md), and [`docs/mcp.md`](docs/mcp.md): protocol and extension contracts.
- [`docs/session-format.md`](docs/session-format.md): append-only session format and recovery boundary.
- [`docs/TESTING.md`](docs/TESTING.md): deterministic and optional verification surfaces.
- [`docs/release-checklist.md`](docs/release-checklist.md): artifact layout, local verification, and explicitly deferred publication work.

The adjacent `LICENSE` applies to this artifact. `THIRD_PARTY_NOTICES.md` contains direct-dependency notices and distinguishes bundled dependencies from host runtime libraries. `PROVENANCE.json` records deterministic binary/source/dependency/architecture evidence; `release_qualified: false` is an explicit non-qualification, not a release claim.
