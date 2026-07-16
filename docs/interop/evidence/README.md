# ACP Client Evidence Policy

This directory contains only bounded, manually reviewed textual evidence derivatives. Raw GUI logs, ACP logs, provider request logs, environment dumps, and screenshots must never be committed here. In a source checkout, keep raw artifacts inside the private temporary root created by `scripts/live-acp-dogfood.sh`; that maintainer script is intentionally not packaged.

## Labels

Use exactly one label for each result:

- `automated`: a repeatable credential-free test gate.
- `manual verified`: a completed, captured, reviewed real-client report for only the observed version and flows.
- `configuration documented but not executed`: a copyable setup was documented, but the client was not run here.
- `deferred`: the check has not produced acceptable evidence.
- `unsupported`: the capability/client is intentionally unavailable.

A client process starting is not evidence that a phase passed. Inference is not observation. Zed 1.9.0 is **manual verified only for the flows captured in [`zed-1.9.0-2026-07-14.md`](zed-1.9.0-2026-07-14.md)**. The label does not carry forward to another version or to unobserved behavior.

## Required report fields

Every captured report must be versioned rather than silently replaced and must contain:

1. `report_status`, evidence label, UTC date, and a non-sensitive operator identifier.
2. Exact client version/commit, AVA version/commit, stable ACP schema release/commit, and fake-provider version/commit.
3. Exact sanitized commands, with no environment values, credentials, user-home paths, or private temporary paths.
4. A confinement record: either a reviewed OS sandbox/container/VM plus the reviewed preflight evidence digest and enforced boundaries, or an explicit record that an already-provisioned disposable credential-free graphical account/VM was used without other sandboxing. Temporary HOME/XDG roots must be described only as state isolation.
5. One outcome (`pass`, `fail`, or `incomplete`) for every claimed phase, with separate **observed**, **inferred**, and **unsupported/unobserved** statements.
6. Cleanup results for the client, AVA descendants, fake provider, owned process groups, and residual-process checks.
7. Redaction/review results, including who reviewed each derivative and confirmation that raw GUI logs/screenshots were not copied.

A report may be complete while documenting a failed result. `manual verified` is allowed only when every claimed flow passed and has direct captured observations.

## Zed phase template

The dogfood script generates this template as `evidence-report.md` under a unique mode-0700 temporary root. It deliberately starts with `report_status: incomplete` and unchecked fields.

```markdown
---
report_status: incomplete
evidence_label: deferred
client: Zed
client_version: "[REQUIRED]"
client_commit: "[REQUIRED or explicitly unavailable]"
protocol: "ACP stable v1 schema-v1.19.0"
date_utc: "[REQUIRED]"
operator: "[REQUIRED — non-sensitive identifier]"
---

## Scope and label
- Claimed scope: [REQUIRED — observed flows only]
- Explicit exclusions: [REQUIRED]

## Version and commit
- AVA version/commit: [REQUIRED]
- Fake-provider version/commit: [REQUIRED]

## Confinement record
- Mode and description: [REQUIRED]
- Reviewed preflight digest or disposable-account acknowledgement: [REQUIRED]
- Enforced/acknowledged boundaries: [REQUIRED]
- HOME/XDG state-isolation clarification: [REQUIRED]

## Commands
- Exact sanitized launcher/client/agent/provider commands: [REQUIRED]

## Phase outcomes
- Lifecycle/tool/permission/client-filesystem/terminal: [pass|fail|incomplete]
- Cancellation: [pass|fail|incomplete]
- Observed facts: [REQUIRED]
- Inferred facts: [REQUIRED]
- Unsupported/unobserved: [REQUIRED]

## Cleanup
- Client/AVA/provider/process-group/residual checks: [REQUIRED]

## Evidence derivatives and redaction
- Reviewed bounded textual derivatives: [REQUIRED]
- Screenshot derivatives: [none, or individually justified/cropped/reviewed]
- Raw artifacts excluded; paths/credentials/environment values removed: [REQUIRED]
- Redaction reviewer: [REQUIRED]

## Observed versus inferred conclusion
- Observed: [REQUIRED]
- Inferred: [REQUIRED]
- Unsupported/unobserved: [REQUIRED]
```

## Sanitization and maintenance

The launcher applies a 16 MiB per-file limit to provider/script raw outputs, a 64 MiB per-file limit to Zed state, and a finite operator deadline; hitting a file or time bound makes the phase incomplete. After manual review, use the script's `zed sanitize-copy` action. It accepts only a completed UTF-8 Markdown report directly into this directory and rejects symlinks, binary/NUL content, reports over 64 KiB, excessive lines, unchecked fields, fake keys, credential-looking values, authorization material, user-home/private-temp paths, embedded images, and overwrites. Screenshots must remain outside the repository unless a future policy change defines a separately reviewed bounded image process.

Changing a client label requires changing its report and `docs/acp-support.json` together. Schema pins, package versions, client versions, commands, and claimed capability scope must not be inferred forward from older evidence.
