# Side-Effect Safety Checklist

Use this checklist before adding or materially changing any AVA feature that can mutate local state, execute code, contact a network service, expose credentials, or persist model-visible data. It complements `docs/engineering/cpp-safety-rules.md` and is part of the Pi MVP parity release evidence.

## Required Design Notes

For every new side-effect class, document the answers in the PR, area log, or feature doc:

1. **Authority boundary**: Which module owns the action, and which user/model inputs are untrusted?
2. **Permission policy**: Which permission operation/risk applies? Is there an allow/ask/deny path, a hard deny, and a headless/RPC fallback?
3. **Audit record**: What session or rule-store entry records the request, decision, actor, reason, target, and request id?
4. **Cancellation and timeout**: How does the action stop safely if the user cancels, the process exits, or the provider/tool times out?
5. **Output bounds**: What limits apply to stdout/stderr, network bodies, file reads, resource lists, diagnostics, and rendered text?
6. **Replay/export semantics**: What persistent session entries are written, and what is safe to replay or export later?
7. **Trust boundary**: Can project-local files influence the behavior? If yes, is outside-workspace trust required first?
8. **Validation before commit**: Are config/session/file writes validated before replacing existing data?
9. **Rollback or recovery**: What happens after partial failure, invalid input, or an interrupted write?
10. **Tests and smokes**: Which deterministic tests and CLI/RPC/TUI smokes prove the safe path and the fail-closed path?

## Side-Effect Classes

| Class | Minimum AVA requirements |
| --- | --- |
| Filesystem reads | Normalize paths, respect workspace/safety limits, bound output, and avoid leaking protected config/secrets. |
| Filesystem writes/edits/patches | Use permissioned file layers, validate paths, use atomic or queued mutation where available, record changed paths/diffs, and test denial. |
| Shell/process execution | Route through the permissioned process tool, use explicit working directories, process-group cleanup, timeouts, output caps, and audit request ids. |
| Network tools | Require network permission, bound response bodies, show remote target, avoid automatic credential forwarding, and test headless fail-closed behavior. |
| Provider calls/auth | Never print secrets, separate credential source from value, validate model/provider compatibility, and keep unsupported OAuth flows deferred. |
| Plugins/extensions | Keep out-of-process isolation, explicit enablement/trust, bounded resources, compatibility checks, and fail-closed startup/execution. |
| MCP | Require explicit config/trust, identify server/tool/resource in permission/audit entries, bound payloads, and keep advanced transports/OAuth deferred until reviewed. |
| LSP servers | Require server-launch permission, bounded diagnostics/symbol payloads, explicit config, and no implicit project executable authority without trust. |
| Session mutation | Preserve append-only validation, reject future/invalid versions, define replay/export behavior, and keep archive/delete/fork flows explicit. |
| Config/settings writes | Validate candidate files before commit, use atomic replacement or documented lock strategy, preserve last-known-good runtime config on reload failure. |
| Packages/resources | Require a source/provenance/signing/rollback policy before remote install/update; local manual install must remain inspectable and trust-gated where executable. |

## Review Gate

A change that introduces a new side-effect class is not release-ready until:

- the checklist above has an owner-visible answer,
- focused unit/integration tests cover success and denial/failure,
- user-facing TUI/headless/RPC output names the operation, target, risk, and follow-up where applicable,
- documentation states any deferred unsupported path instead of silently broadening authority.
