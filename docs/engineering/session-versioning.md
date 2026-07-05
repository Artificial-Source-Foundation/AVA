# Session Versioning Policy

AVA sessions are append-only JSONL files. The storage format is intentionally inspectable and conservative: normal startup must not rewrite old session files, and migration work must be explicit, test-backed, and reversible by keeping the original file available.

## Current Contract

- Session entries use the top-level `version` field. The current writer version is `3` (`kCurrentSessionEntryVersion`).
- Entries without a `version` field are treated as legacy version `0` for read compatibility. New files should not write explicit `version:0` entries.
- Readers accept missing-version legacy entries and explicit versions `1..kCurrentSessionEntryVersion`, then reject future versions with an actionable session error. Replay validation also treats legacy version `0` as supported.
- Payloads that need independent evolution use `data.schema_version`. Today, session metadata and branch summary payloads use `schema_version:1`. Tool-result `structured_result` objects also use `schema_version:1` inside their parent entry.
- Compaction payloads predate this payload-versioning convention and do not currently carry `data.schema_version`; a future incompatible compaction payload change should add and validate a schema version before relying on this policy.
- Unknown or unsupported payload schema versions are replay-validation errors, not silent best-effort reads.

## Compatibility Rules

- New fields should be additive whenever possible. Older readers may ignore fields they do not understand, but they must reject a newer top-level entry `version` when the shape cannot be safely interpreted.
- Do not bump the top-level session entry version for cosmetic fields, optional metadata, or display-only additions that current validation can safely ignore.
- Bump `data.schema_version` for one payload type when only that payload changes incompatibly.
- Bump `kCurrentSessionEntryVersion` only when the common entry envelope, parent/id semantics, replay ordering, or provider-replay meaning changes for multiple entry types.
- A version bump must update protocol/docs text, replay validation, session export if affected, and focused session tests before release.

## Migration Expectations

- Normal AVA startup opens old supported sessions in place and appends new entries with the current writer version.
- AVA must not destructively rewrite session JSONL files during startup, resume, list, export, tree, fork, clone, or compact.
- If a future change needs migration, prefer a copy-forward flow: read the source session, validate it, write a new session or backup file with upgraded entries, and keep the original file untouched unless the user explicitly requests replacement.
- Repair or migration tools must reject symlinked, oversized, malformed, or future-version input the same way normal session loading does.
- Partial migration output must not replace a usable source session. Write new output atomically or to a fresh session id, then validate the result before reporting success.

## Validation Requirements

Any session format change must include focused coverage in `ava_tests.session` or a narrower suite that is also run from CTest. At minimum, cover:

- current-version round trips,
- legacy missing-version reads,
- future-version rejection,
- payload `schema_version` rejection for unsupported values,
- export/replay behavior for affected entry types,
- branch/tree behavior when metadata or branch summaries are affected,
- failure behavior for corrupt, oversized, symlinked, or traversal-prone paths when migration or repair tooling is involved.

Before marking a release-ready session-format change complete, run the focused session tests, the relevant RPC/headless session smokes, full default CTest when practical, and `git --no-pager diff --check`.

## Branch Summary Ownership

For the current MVP, branch summaries are caller-supplied only. RPC `summarize_branch` stores user/client-provided text with explicit `provider`, `model`, and `reason` provenance. Provider-generated branch summaries are deferred until branch navigation UX needs them and the product can define when model calls are allowed, how they are budgeted, and how stale-session races are handled. Provider-generated compaction summaries remain separate and are already implemented through the compaction flow.
