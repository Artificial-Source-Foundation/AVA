# AVA Linux Host Artifact

This archive contains the AVA terminal executable for the Linux host on which it was built. It is a dynamically linked host artifact, not a portable Linux distribution package.

This README is the **offline documentation spine** for the artifact. All 31 packaged source documents (29 Markdown and 2 JSON) are indexed below; the source-tree category indexes and `docs/README.md` are intentionally not installed because their maintainer and history navigation depends on the repository checkout.

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

AVA stores user configuration and state under the XDG paths documented in [`docs/core/configuration.md`](docs/core/configuration.md). Provider use may require separately configured credentials; no credentials, local configuration, sessions, or provider output are included in this archive.

## Start, configure, and operate AVA

- [`docs/core/usage.md`](docs/core/usage.md): complete CLI, TUI, print, RPC, ACP, and command usage.
- [`docs/core/configuration.md`](docs/core/configuration.md) and [`docs/core/environment-variables.md`](docs/core/environment-variables.md): configuration, credentials, XDG paths, permissions, and process inputs.
- [`docs/core/providers.md`](docs/core/providers.md): current provider/model support, authentication, compatibility, and evidence status.
- [`docs/core/context-resources.md`](docs/core/context-resources.md): prompts, instructions, commands, skills, subagents, extensions, project trust, and reload behavior.
- [`docs/core/subagents.md`](docs/core/subagents.md): foreground/background delegation, job controls, permissions, completion delivery, durability, limits, and lifecycle.
- [`docs/core/thinking-modes.md`](docs/core/thinking-modes.md): provider reasoning controls and visible-thinking behavior.
- [`docs/session-format.md`](docs/session-format.md): append-only session format, validation, and recovery boundaries.

## Terminal interface

- [`docs/operations/terminal-setup.md`](docs/operations/terminal-setup.md): terminal capabilities, keyboard protocols, images, links, clipboard helpers, and troubleshooting.
- [`docs/interfaces/themes-keybindings.md`](docs/interfaces/themes-keybindings.md): theme and keybinding discovery, customization, validation, and precedence.
- [`docs/operations/troubleshooting.md`](docs/operations/troubleshooting.md): symptom-first diagnosis and recovery.
- [`docs/operations/diagnostics.md`](docs/operations/diagnostics.md): passive doctor, private diagnostics, and sanitized support exports.

## Tools, extensions, and automation

- [`docs/core/tools.md`](docs/core/tools.md): built-in model-visible tools, bounds, aliases, and permission separation.
- [`docs/extensions/lsp.md`](docs/extensions/lsp.md): optional local language-server configuration, operations, bounds, and cleanup.
- [`docs/extensions/mcp.md`](docs/extensions/mcp.md): local stdio MCP configuration, resources, prompts, tools, and safety boundaries.
- [`docs/extensions/plugin-system.md`](docs/extensions/plugin-system.md) and [`docs/plugin-compatibility-policy.md`](docs/plugin-compatibility-policy.md): local plugin authoring and compatibility policy.
- [`docs/headless-protocol.md`](docs/headless-protocol.md): behavior shared by print and RPC headless modes.
- [`docs/rpc-protocol.md`](docs/rpc-protocol.md): normative proprietary AVA RPC v1 contract.
- [`docs/acp.md`](docs/acp.md), [`docs/acp-support.json`](docs/acp-support.json), and [`docs/interop/evidence/README.md`](docs/interop/evidence/README.md): ACP endpoint, machine-readable support profile, and evidence policy.
- [`docs/interop/evidence/zed-1.9.0-2026-07-14.md`](docs/interop/evidence/zed-1.9.0-2026-07-14.md): packaged Zed interoperability evidence record.

## Security and safety

- [`docs/security/sandboxing.md`](docs/security/sandboxing.md): practical trust, permission, extension, network, and external-sandbox guidance.
- [`docs/security/containment.md`](docs/security/containment.md): verified command process-group containment and explicit limitations.
- [`docs/development/side-effect-safety-checklist.md`](docs/development/side-effect-safety-checklist.md): review checklist for operations that add side effects.
- [`docs/development/session-versioning.md`](docs/development/session-versioning.md): compatibility rules for persisted session changes.

## Verification and artifact evidence

- [`docs/operations/testing.md`](docs/operations/testing.md): deterministic, optional, terminal, and live-provider verification surfaces.
- [`docs/product/mvp-coverage-ledger.md`](docs/product/mvp-coverage-ledger.md): packaged capability-to-evidence ledger.
- [`docs/operations/release-checklist.md`](docs/operations/release-checklist.md): exact artifact layout, local verification, provenance checks, and explicitly deferred publication work.
- [`docs/schema/theme.schema.json`](docs/schema/theme.schema.json): installed theme JSON schema.

The adjacent `LICENSE` applies to this artifact. `THIRD_PARTY_NOTICES.md` contains direct-dependency notices and distinguishes bundled dependencies from host runtime libraries. `PROVENANCE.json` records deterministic binary/source/dependency/architecture evidence; `release_qualified: false` is an explicit non-qualification, not a release claim.
