# AVA Session Format Reference

This document describes AVA's public, append-only session JSONL format. It is intended for tools that need to inspect, archive, validate, or copy AVA sessions without depending on UI output. The packaged protocol contract is [`headless-protocol.md`](headless-protocol.md). A source checkout additionally contains the implementation under `src/ava/session/` and regression coverage in `tests/session_tests.cpp`; source and test trees are intentionally not installed in the host artifact.

AVA session JSONL is **not Pi-compatible**. AVA accepts some Pi-compatible CLI/RPC aliases, but persisted session files use AVA-native envelope fields, entry type names, payload schemas, attachment storage, and replay rules. Do not import Pi logs as AVA sessions, and do not assume AVA JSONL can be replayed by Pi.

## Storage

- Persistent sessions are stored under AVA's session root, which defaults to `$XDG_STATE_HOME/ava/sessions` or `~/.local/state/ava/sessions` when `XDG_STATE_HOME` is unset. `--session-dir <dir>` overrides the root for the current process.
- Session files are grouped by a stable hash of the absolute, normalized workspace path: `<session-root>/<workspace-key>/<session_id>.jsonl`.
- Session directories are owner-only and session files are owner read/write. AVA rejects session files that are symlinks or not regular files.
- `--no-session` uses an ephemeral in-memory session store. Runtime entries can still be used by in-process commands, but no resumable JSONL file is written and temporary attachment/spill storage may disappear when the process exits.
- Image bytes live outside the JSONL file under a sibling attachment directory, `<session-root>/<workspace-key>/<session_id>.attachments/...`. See [Attachment caveats](#attachment-caveats).

## JSONL Envelope

Each line is one JSON object. New AVA writers emit these top-level fields in this order:

```json
{"version":3,"id":"entry_...","parent_id":"","type":"user_message","timestamp":"2026-04-30T00:00:00Z","data":{"text":"hello"}}
```

Envelope fields:

- `version`: integer session entry format version. New records must write the current version, `3`.
- `id`: entry id. AVA-generated ids use `entry_...`. Replay validation requires ids to be unique.
- `parent_id`: optional parent entry id. Empty means no parent. When present, replay validation expects it to reference an earlier entry; unsafe path-like values are rejected.
- `type`: stable entry type string. Current readers reject unknown type strings when opening/importing a session.
- `timestamp`: UTC timestamp string written as `YYYY-MM-DDTHH:MM:SSZ` by current AVA writers.
- `data`: entry-specific JSON object. It must be an object, not an array/string/null.

Records are newline-delimited. AVA rejects raw `\n` or `\r` bytes inside `data` and rejects serialized lines at or above the 1 MiB line cap. Use normal JSON escaping for text newlines.

## Versions

- Current writer version: `3` (`kCurrentSessionEntryVersion`).
- Accepted on-disk versions: explicit `1`, `2`, and `3`, plus missing `version` as legacy in-memory version `0`.
- Explicit `version:0` should not be written. `0` in RPC responses means the source JSONL record omitted the field.
- Future versions are rejected with a session error rather than best-effort replay.
- Payloads with independent compatibility use `data.schema_version`. Today `session_metadata` and `branch_summary` use `schema_version:1`; `tool_result.data.structured_result` also uses `schema_version:1`. `compaction` predates that convention and currently has no `data.schema_version`.

More versioning policy is in [`docs/engineering/session-versioning.md`](engineering/session-versioning.md).

## Entry Types

Current AVA entry type strings are:

| Type | Purpose |
| --- | --- |
| `session_start` | Runtime/model metadata captured when a session starts. |
| `session_metadata` | Append-only display/tree metadata such as name, labels, archive state, and branch provenance. |
| `user_message` | User text and optional image attachment metadata. |
| `assistant_message` | Final assistant text plus usage/cost metadata when known. |
| `tool_call` | Provider-requested tool call and arguments. |
| `tool_result` | Tool completion result, including canonical `structured_result` when present. |
| `permission_decision` | Durable permission audit record for allow/ask/deny and resolver outcomes. |
| `mode_change` | Build/plan mode transition. |
| `model_change` | Provider/model transition and model capability snapshot. |
| `reasoning_block` | Provider-native reasoning/thinking content or redaction metadata. |
| `reasoning_change` | Durable reasoning setting change for the active model. |
| `compaction` | Context compaction boundary and summary. |
| `branch_summary` | Caller-supplied summary of a source session range. |
| `error` | Runtime/provider/tool error record. |
| `cancel` | Cooperative cancellation marker. |

External readers that only need transcript data should prefer RPC `get_messages`, which returns sanitized `user_message`, `assistant_message`, `reasoning_block`, `tool_call`, and `tool_result` entries and hides internal replay messages.

## Important Payloads

### `session_start`

Current writers store small session/model metadata such as:

- `mode`: `build` or `plan`.
- `provider` and `model`: active provider/model ids.
- `prompt_override`: boolean.
- `context_sources`: count of loaded context sources.
- Model capability metadata when known: `input_modalities`, `output_modalities`, `reasoning_levels`, `compatibility_quirks`, `supports_tools`, `supports_streaming`, `supports_reasoning`, `reports_usage`, `display_name`, `family`, `api_family`, `context_window_tokens`, `max_output_tokens`, and `reasoning_format`.

`session_start` is intentionally not a complete context archive. It does not persist the base prompt text, detailed context source paths, prompt fingerprints, or loaded file contents; use `/context` or RPC `context` for freshness diagnostics. Token fields are numeric metadata, booleans must be booleans, and the overall entry remains subject to the JSONL line cap.

### Messages

- `user_message.data.text` contains user text.
- `user_message.data.attachments[]` may contain image metadata only. Attachments are user-message-only.
- Internal replay user messages may include `internal_replay:true`, `replay_of`, and `reason`; they are hidden from normal exports/RPC transcript reads.
- `assistant_message.data.text` contains final assistant text. Current writers also include `tool_calls` and a `usage` object when usage/cost data is known or estimated.

### `tool_call` and `tool_result`

- `tool_call.data` includes `call_id`, `name`, and `arguments`. `arguments` is stored as a JSON string containing the provider arguments JSON.
- `call_id` must be non-empty and unique until resolved.
- `tool_result.data` includes `call_id`, `name`, `success`, `status`, `result`, and usually `structured_result`.
- `status` values are `success`, `error`, or `canceled`.
- `structured_result` is the canonical semantic result when present. It uses `schema_version:1`, repeats `call_id` and `tool`, includes `status`, `ok`, `content_type`, optional `content`, optional `error`, optional `permission_request_ids`, and optional diff/path/truncation/spill counters.
- Replay validation pairs each `tool_result` with an earlier `tool_call`, rejects duplicate results, rejects mismatched tool names, and can require/validate `structured_result` when callers enable that stricter mode.

### `permission_decision`

Permission audit entries record backend policy decisions and resolver outcomes. Important fields include:

- Required semantic fields: `permission_request_id`, `operation`, `mode`, `tool_name`, `action`, `reason`, and `risk`.
- Common optional fields: `target_path`, `command`, `resolution`, `resolution_source`, `resolution_reason`, `actor`, and `rule_id`.
- `action` is `allow`, `ask`, or `deny`; `resolution` is `allow`, `deny`, or `cancel` when a prompt has been resolved.
- Current `resolution_source` values include `policy`, `resolver`, `session_grant`, `no_resolver`, `resolver_failed`, `persistent_rule`, `persistent_rule_error`, `client_cancel`, `hard_scope`, `session_config`, and `client`.
- Legacy `acp_hard_policy`, `acp_session_mcp`, `acp_client`, `acp_session_grant`, `acp_client_cancel`, and `acp_client_error` sources are accepted only for read compatibility with older sessions; current writers do not emit them.
- `risk` values are `low`, `medium`, `high`, and `critical`.

Validation checks that policy `allow`/`deny` entries resolve consistently, that `ask` prompts have matching resolver outcomes when resolved, and that unresolved permission prompts are not crossed by compaction boundaries.

### Reasoning

- `reasoning_block.data` includes `provider`, `model`, `format`, `redacted`, and at least one of `text`, `signature`, or `redacted_data`.
- OpenAI Responses native replay metadata, when explicitly present on a current-version `openai_responses` block, is a bounded strict JSON object with `type:"reasoning"`, a non-empty opaque `id`, and a present array-valued `summary` (which may be empty). Unknown additive provider fields are retained only in the private session record.
- RPC `get_messages` sanitizes reasoning blocks: non-redacted text may be returned, but raw provider signatures and opaque redacted-thinking payloads are replaced with safe fields such as `signature_present` and `redacted`.
- `reasoning_change.data` includes `provider`, `model`, `enabled`, `format`, and when enabled a `level`; it may include `budget_tokens` and `display` where the selected model supports those controls.
- Validation checks reasoning entries against the active provider/model boundary established by `session_start` and `model_change`. Legacy malformed optional native OpenAI metadata may fall back to readable synthetic history; current-version explicitly-present malformed metadata is rejected.
- OpenAI native continuation supports the common contiguous reasoning → function-call → matching function-result path. Session version 3 does not carry an ordered-output manifest, so arbitrary commentary/reasoning/function interleaving and assistant phase preservation remain a known backlog limitation.

### `compaction`

Compaction records a context boundary and summary. Current fields include `trigger`, `status:"recorded"`, `summary_unavailable`, `summary`, `instructions`, `model`, `threshold_tokens`, `estimated_tokens`, `keep_recent_tokens`, `keep_recent_messages`, `max_summary_bytes`, and `recent_context`.

Validation requires a non-empty `summary`, boolean `summary_unavailable` when present, `status:"recorded"` when present, numeric token/retention metadata when present, and no unresolved tool calls or permission prompts at the compaction boundary.

### `session_metadata`

`session_metadata.data.schema_version` is `1`. Metadata entries are append-only; the latest value for each field wins. Current fields and limits:

- `name`: string, at most 256 bytes, no control bytes.
- `labels`: unique non-empty strings, at most 32 labels, at most 64 bytes per label, no control bytes.
- `archived`: boolean.
- `parent_session_id`, `source_session_id`: AVA session ids.
- `branch_from_entry_id`: entry id in the source session.
- `branch_origin`: empty or one of `root`, `fork`, `clone`, `manual`, `import`.
- `actor`: optional string, at most 64 bytes.

Forks and clones copy source entries into a new session and append `session_metadata` provenance to the new session. The source session file is left untouched.

### `branch_summary`

`branch_summary.data.schema_version` is `1`. Required fields:

- `summary`: non-empty text, at most 8192 bytes; newlines and tabs are allowed, other control bytes are not.
- `source_session_id`: source session id.
- `branch_root_entry_id` and `branch_tip_entry_id`: earlier entries in the source session, with root not after tip.
- `provider` and `model`: provenance strings, each at most 256 bytes.
- `reason`: provenance string, at most 1024 bytes.
- `actor`: optional string, at most 64 bytes.

Branch summaries are caller-supplied in the current MVP. Provider-generated branch summaries are deferred; provider-generated compaction summaries are a separate `compaction` behavior.

## Attachment Caveats

Persisted image attachments are metadata references, not inline uploads:

- Only `user_message.data.attachments[]` supports attachments.
- Each attachment object may contain `id`, `type:"image"`, `mime_type`, `byte_size`, `sha256`, `storage_path`, and optional `redacted`.
- Supported MIME types are `image/png`, `image/jpeg`, `image/webp`, and `image/gif`.
- `byte_size` must be an integer from 1 through 20 MiB, and `sha256` must be a 64-character hex digest.
- `storage_path` must be a relative `attachments/...` path without absolute roots, `..`, empty segments, backslashes, drive prefixes, or control bytes.
- AVA imports local RPC `attachments` paths and inline RPC `images` uploads into the session attachment store before writing message metadata. Raw base64 image data is invalid in persisted session JSONL.
- Replay loads bytes only from the active session's attachment directory and verifies path containment, symlinks, byte size, and SHA-256 before sending image content to providers.
- Portable JSONL export does not bundle attachment bytes. It converts every exported image attachment to the accepted `redacted:true` metadata form, preserves safe audit fields such as id/MIME/size/digest, and replaces the source storage reference with a deterministic portable placeholder. The export therefore re-imports without attachment bytes, but it cannot replay the original image content.
- Branch/fork/clone code copies non-redacted image attachment bytes for copied entries. Slash `/import <path.jsonl>` accepts portable redacted attachment metadata without reconstructing attachment bytes; directly imported non-redacted attachment references still require an attachment-aware archive because image replay would otherwise be dangling.

Provider replay also has request-level caps, including at most 16 images and aggregate byte limits before base64 expansion; some providers have lower per-image limits.

## Import and Export Guidance

- `/export` writes a Markdown transcript by default.
- `/export html [path]` writes or returns an HTML rendering.
- `/export jsonl [path]` or `/export raw [path]` emits portable sanitized AVA JSONL records. Reasoning `native_item_json`, `signature`, and `redacted_data` values are omitted and replaced by safe visible text and omission metadata where needed, so JSONL export is importable but not lossless.
- RPC `export` and `export_html` return Markdown/HTML command output. Dedicated RPC portable JSONL export/import/share commands are deferred; use slash/line-shell `/export jsonl` for a portable archive.
- `/import <path.jsonl> --confirm` is slash/line-shell only for the current MVP. It reads a local regular file, rejects symlinks, parses each bounded JSONL line, validates replay integrity, creates a new session, appends supported entries, and switches to the new session.
- Import treats missing-version legacy entries as version `0` while reading and rewrites those entries at the current writer version when appending to the new session.
- Normal AVA startup, resume, list, tree, fork, clone, compact, and export do not rewrite existing session files. Prefer copy-forward import/migration into a new session over editing JSONL in place.
- Automation should prefer RPC `get_messages`, `get_session_stats`, `validate_session`, `session_metadata`, and `session_tree` for stable views. Direct JSONL readers should tolerate additive fields and ignore non-message bookkeeping entries they do not need, but AVA itself rejects unknown entry type strings when opening/importing a session.

## Validation Checklist

AVA's parser and replay validator currently enforce these important constraints:

- Each JSONL line must be below the line-size cap and parse as an entry envelope with non-empty `id`, `type`, `timestamp`, and object-shaped `data`.
- Missing top-level `version` is legacy version `0`; explicit supported versions are `1..3`; future versions are rejected.
- `session_id` values must be non-empty safe path segments. Non-empty `parent_id` values must also be safe path segments, with no reserved `.`/`..`, path separators, or control bytes.
- Entry ids must be unique for replay; `parent_id` should reference an earlier entry.
- Message payloads must be valid objects; attachment metadata is user-message-only and must match the shape in [Attachment caveats](#attachment-caveats).
- Tool calls and results must pair by `call_id`; duplicate call ids, result-without-call, mismatched tool names, duplicate results, and unresolved calls are validation errors.
- Permission `ask` prompts must be resolved consistently or remain pending only until the end of validation; compaction cannot cross unresolved permission prompts.
- `session_metadata`, `branch_summary`, and `structured_result` schema versions must be supported.
- Durable model and reasoning entries must be consistent with the active provider/model boundary.
- `validate_session` over RPC returns `{session_id, session_path, ok, error_count, warning_count, issues}` with stable issue `kind` strings, severity, entry index, entry id, optional tool call id, and diagnostic message.

For format changes, update the versioning policy, replay validation, export behavior if affected, and focused session/RPC tests before release.
