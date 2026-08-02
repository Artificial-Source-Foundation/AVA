# AVA Package Manager And Resource Package Plan

Status: planning only. Do not add package install, update, or remote marketplace behavior until this plan is approved and split into implementation goals.

## Current Architecture Anchors

- Package entry points are intentionally inert today: `/packages` is a disabled command and `ava packages ...` prints a deferral message instead of invoking package managers or model prompts.
- Manual resources load from inspectable global paths under `$XDG_CONFIG_HOME/ava` and from project `.ava/` paths only after `/trust project` enables project resources.
- Project trust is stored outside the workspace in `$XDG_STATE_HOME/ava/project-trust.json`; trusted project resources include prompt commands, skills, subagents, plugins, MCP/LSP config, and project system prompt files.
- Plugins are local directories with `plugin.json`, bounded manifest validation, out-of-process execution, local enablement state, and `plugin.execute` / `plugin.tool.call` / `plugin.command.run` permission gates.
- MCP uses explicit global/project `mcp.json`; project MCP config is skipped until project trust, global config rejects workspace-relative executable/script paths, and MCP tools/resources remain permissioned.
- Prompt commands, skills, subagents, plugin prompt/skill resources, and runtime prompt freshness metadata already provide seams for package-delivered resources, but they do not yet understand package identity, provenance, or rollback.

## Gap To Close

AVA can discover resources once users place files in the right directories, but it has no first-class package layer for:

1. package identity, manifest schema, compatibility, and resource filtering;
2. source policy for local archives, git URLs, registries, or marketplace catalogs;
3. provenance records tying installed files to source, digest, version, publisher, and install actor;
4. signature verification and trusted publisher/key policy;
5. atomic install/remove/update with validation-before-commit and rollback;
6. offline/cache semantics and lock files;
7. user-facing audit/diagnostic output for package side effects.

## Product Constraints

- Installing a package must never imply enabling executable authority. Plugin enablement, project trust, MCP/LSP launch approval, skill load approval, and tool permissions remain separate gates.
- Package operations are user actions, not model-dispatched tools. A model may suggest a package, but AVA should not install, update, or execute package manager commands without an explicit user command and approval flow.
- Do not shell out to `npm`, `pnpm`, `yarn`, `bun`, `git`, or language package managers as the trusted install primitive. If those ecosystems are supported later, AVA should fetch/verifiably unpack bounded artifacts instead of running arbitrary lifecycle scripts.
- Project-declared package files must not grant authority by being committed to a repository. Project package activation still needs `/trust project` plus local user approval, with decisions stored outside the workspace.
- Manual install remains supported and inspectable. The package manager should add a managed path, not make hand-authored `$XDG_CONFIG_HOME/ava` or trusted `.ava/` resources obsolete.

## Proposed Resource Package Shape

Use a narrow package manifest, separate from `plugin.json`, for bundle-level identity:

```json
{
  "schema_version": 1,
  "api_version": "ava.package.v1",
  "id": "com.example.resource-pack",
  "version": "0.1.0",
  "name": "Example Resource Pack",
  "resources": {
    "plugins": ["plugins/com.example.todo/plugin.json"],
    "commands": ["commands/review.md"],
    "skills": ["skills/triage/SKILL.md"],
    "agents": ["agents/explorer.md"],
    "themes": ["themes/ocean.json"],
    "mcp": ["mcp/demo.json"],
    "lsp": []
  },
  "compatibility": {
    "ava_min": "1.1.0",
    "plugin_api": ["ava.plugin.v1"]
  }
}
```

Implementation should prefer a content-addressed package store under `$XDG_DATA_HOME/ava/packages/<id>/<version>/<digest>/` plus a small activation/lock index under config or state. Extending loaders to read activated package roots is safer than copying over user-authored files, because uninstall and rollback can remove package-owned resources without touching manual resources. Avoid symlink-based materialization; resolve real paths and keep package files bounded and regular.

## Trust, Provenance, And Signing

- **Trust layers:** source trust answers “may AVA fetch/unpack this package source?”; package activation answers “should these resources participate in this global/workspace runtime?”; existing runtime gates answer “may this plugin/MCP/tool/skill actually execute or enter context?” Keep all three separate.
- **Provenance record:** every managed package revision needs source type, original locator, resolved immutable locator, archive digest, per-file digest set, package manifest digest, publisher identity, signature verification result, install time, installed-by actor, AVA version, and compatibility verdict.
- **Signing policy:** remote packages must be rejected until AVA has canonical manifest/archive signing, trusted publisher keys or source-pinned keys, key rotation/revocation behavior, and clear diagnostics. Unsigned local path packages can exist only as an explicit development mode with local-only provenance and no remote update channel.
- **Authority diff:** before activation or update, AVA should show a diff of newly introduced executable surfaces and context surfaces: plugins, plugin entrypoints/capabilities, MCP/LSP server commands, prompt/system-prompt resources, skills/subagents, and themes.
- **Audit:** package commands should emit durable package audit records outside model context and user-visible command output naming package id, version, source, digest, action, and whether activation changed runtime authority.

## Install, Update, Remove, And Rollback Semantics

Do not implement these in the current task. When approved, sequence them conservatively:

1. `list` reads only local manual/managed resource inventory and provenance lock files.
2. local path/archive `inspect` validates manifests and prints authority diffs without materializing resources.
3. local path/archive `install --activate` stages into a new content-addressed revision, validates every manifest/resource through existing loaders, writes provenance, then atomically updates the activation index.
4. `remove` deactivates package-owned resources without deleting manual resources; garbage collection of unused revisions is separate and explicit.
5. `rollback` switches the activation index to a previous validated revision and must work without network access.
6. remote catalog/install/update comes only after signing, source policy, cache, and rollback are in place.

Updates must be explicit; AVA should not perform automatic version checks on startup. An update that changes plugin entrypoints, MCP/LSP commands, declared capabilities, system prompts, or subagent tool presets should clear or require re-confirmation of the affected activation/enablement decision rather than inheriting trust silently.

## Offline Implications

- Package inventory, activated resources, rollback, and provenance inspection must work offline from local state only.
- The existing `--offline` mode forbids provider model calls today. Future package management must also treat it as a hard stop for package catalog refreshes, remote installs, update checks, remote signature/key refreshes, and future remote MCP discovery. It should still allow local package listing, validation of cached signatures, activation of already-cached packages, and rollback.
- Package lock files must be sufficient to reproduce an installed package from a local cache or to explain why a remote artifact is unavailable offline.
- Offline package behavior should align with the existing MVP posture: no telemetry, no self-update, no remote package/update checks, and network-capable tools remain controlled by tool visibility plus permission policy.

## Open Questions Before Implementation

- Which package sources are in scope first: local directory/archive only, signed static catalog, or a registry protocol?
- Should package activation be global, workspace-scoped, or both, and how should it compose with existing plugin enablement state?
- What user-facing UI presents authority diffs and signing/provenance details in TUI, print, and RPC modes?
- What compatibility fields belong in `ava.package.v1` versus existing `ava.plugin.v1` and MCP/LSP config schemas?
- Which tests become required gates for package operations: manifest fuzzing, path containment, signature fixtures, transaction rollback, project-trust gating, offline cache behavior, and package audit serialization?
