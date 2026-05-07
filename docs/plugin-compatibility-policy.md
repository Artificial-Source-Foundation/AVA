# AVA Plugin And MCP Compatibility Policy

This policy defines the compatibility contract for AVA's local plugin and MCP foundation as it stabilizes for 1.0. It applies to the `ava.plugin.v1` manifest/protocol surface, plugin-contributed tools/commands/static resources/event hooks, and AVA's stdio MCP adapter.

The current compiled runtime version is still managed separately from this release-position work. Do not infer a runtime version bump from this document.

## Stable v1 Contract

AVA treats these surfaces as the supported v1 extension contract:

- Plugin manifests with `schema_version: 1` and `api_version: "ava.plugin.v1"`.
- Out-of-process plugin JSONL records for `initialize`, `tool.call`, `command.call`, `event.observe`, and `cancel`.
- Static plugin contributions: `tools`, `commands`, `prompts`, `skills`, and `event_hooks`.
- Plugin discovery and local enablement state for global and project plugin directories.
- Provider-facing tool schema shape for enabled plugin tools and enabled MCP tools.
- Permission categories and audit data for plugin launch/calls/events and MCP launch/connect/tool calls.
- Stdio MCP `initialize`, `tools/list`, `tools/call`, `prompts/list`, and `prompts/get` through AVA-owned registry and command paths.

The contract is intentionally narrow. Plugin code stays out-of-process, project plugins remain disabled until locally enabled, MCP servers are not trusted as safe by default, and side effects still require AVA permission checks when they go through AVA-owned operations.

## Compatible Changes

The following changes are compatible with `ava.plugin.v1` when they preserve existing behavior:

- Adding optional manifest fields that older AVA builds can ignore safely.
- Adding optional fields to plugin protocol requests or responses when existing required fields keep the same meaning.
- Adding new contribution types only when old contribution arrays continue to parse the same way and unsupported contributions fail closed.
- Adding new permission/audit metadata fields without renaming or removing existing fields.
- Accepting a broader JSON-schema subset for tool inputs while preserving already accepted schemas.
- Adding diagnostics, command output details, or runtime events that do not change success/failure semantics.
- Adding new MCP commands or transports behind explicit names and permissions while preserving current stdio tool/prompt behavior.

Compatible additions should be covered by focused tests and, when they affect stable serialized shapes, by small deterministic golden fixtures.

## Breaking Changes

These changes are breaking and require an explicit versioned transition rather than silent modification of `ava.plugin.v1`:

- Changing required manifest fields, plugin id/name rules, contribution names, or entrypoint semantics.
- Renaming, removing, or changing the meaning of existing JSONL record types or required fields.
- Changing provider-facing plugin/MCP tool names for existing contributions.
- Tightening accepted schemas or resource paths in a way that rejects previously valid safe plugins without a security rationale and migration note.
- Removing permission prompts, weakening default-deny behavior, or changing audit field meanings.
- Treating MCP server-declared read-only or safety metadata as sufficient to bypass AVA permissions.
- Injecting MCP resources into model context without an explicit read-style command/tool design.

Security containment fixes may intentionally reject unsafe behavior that previously slipped through validation. Those changes still need tests, release notes, and clear diagnostics.

## Deprecation Expectations

When a non-security change needs to replace a supported v1 behavior:

1. Add a compatibility-preserving path first.
2. Document the old and new behavior in extension docs and version-position docs.
3. Emit actionable diagnostics where practical.
4. Keep the old path through at least one documented stabilization/release window.
5. Remove or hard-error only after a follow-up plan explicitly owns the breaking transition.

## JSON And Schema Rules

- JSON object key order is not a public plugin/MCP contract, but AVA's checked-in golden fixtures intentionally lock representative emitted key order so accidental serializer churn is visible.
- Unknown top-level manifest fields are ignored unless a schema-controlled contribution object says otherwise.
- Required fields must remain required with the same type and meaning for `ava.plugin.v1`.
- New optional fields must have conservative defaults and must not grant authority by being present.
- Tool input schemas are preserved as JSON objects and adapted into provider-compatible `parameters` objects. Unsupported schemas should disable the affected contribution with diagnostics rather than crash AVA.
- Serialized audit records should remain machine-readable JSON and preserve existing field names for permission request id, operation, mode, tool name, action, reason, risk, command/path, resolution, source, and reason.

## Diagnostics And Golden Coverage

Contract changes should prefer small, deterministic tests over broad snapshots:

- Keep fixtures under `tests/golden/ava-080/` for this stabilization slice.
- Compare stable JSON shapes after removing insignificant whitespace. These fixtures intentionally do not reorder object keys because they guard AVA's current hand-built serializers, not generic JSON semantic equivalence.
- Avoid generated IDs, temp paths, timestamps, process IDs, and environment-dependent paths in golden files. When volatile values are the behavior under test, assert structural linkage directly.
- Use local fake plugins/MCP servers only; provider live-smokes and network credentials are release-validation work outside this contract policy.

At minimum, representative golden coverage should include plugin tool schemas, MCP tool schemas, and permission audit JSON shape. Focused behavioral tests should cover permission-denial audits and contained plugin/MCP failure paths.

## MCP Boundary And Resource Decision

AVA's current 1.0 MCP boundary is stdio server configuration plus `initialize`, `tools/list`, `tools/call`, `prompts/list`, and `prompts/get`. Launching, connecting, and calling tools are permissioned and audited AVA operations.

`resources/list` and `resources/read` are explicitly deferred. They should land only in a separate bounded design that treats resources as read-style operations with clear command/tool names, permission prompts, size limits, cancellation, diagnostics, and tests. MCP resources must not be silently injected into prompt context as part of this contract-hardening slice.
