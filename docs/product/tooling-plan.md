# AVA Tooling Plan

AVA tools are the model's hands. They must be small, predictable, permissioned, and easy to test.

## Tool Contract

Every tool has:

- Stable name.
- JSON-schema-like input definition.
- Short model-facing description.
- Central metadata for permission category, output bounds, execution mode, and event rendering hints.
- Permission requirements.
- Deterministic validation before side effects.
- Structured result with human text plus metadata.
- Output truncation rules.
- Cancellation support where applicable.

Tool metadata should stay generic by default, matching PI's lean tool model. The metadata shape should still allow later provider/model-specific descriptions or availability choices, matching the useful OpenCode pattern, once AVA has the Phase 5 provider/model capability catalog.

Image reading is deferred from the core text `read` tool for now. Supporting images would require provider modality metadata, session attachment shape, and model-specific payload handling; revisit it when multimodal provider capabilities are part of the provider/model catalog.

Every tool result should include:

- `ok`: boolean.
- `summary`: short human-readable result.
- `content`: bounded text output when useful.
- `metadata`: tool-specific structured details.
- `truncated`: boolean.

## MVP Tool Set

### read

Reads a file with line numbers.

Inputs:

- `path`
- `offset`
- `limit`

Rules:

- Normalize path through the safe file layer.
- Deny directories.
- Enforce max bytes and max lines.
- Mark long lines as truncated.

### write

Creates or overwrites one file.

Inputs:

- `path`
- `content`

Rules:

- Permission check before mutation.
- Parent directory must exist unless explicitly allowed later.
- Writes must be atomic where practical.
- No partial write on failure.

### edit

Replaces exact text in one file.

Inputs:

- `path`
- `old_text`
- `new_text`

Rules:

- Fail if `old_text` is missing.
- Fail if `old_text` appears multiple times.
- Preserve raw bytes; exact matching is still the default behavior.
- Preserve line endings and UTF-8 BOM bytes during replacement.
- When an exact match fails, include actionable diagnostics for common CRLF/LF and BOM-at-start mismatches.
- Return a bounded unified diff preview with `diff_truncated` metadata.
- Serialize same-path mutations through the in-process mutation queue.
- Preserve original file if validation fails.

### apply_patch

Applies a structured multi-file patch.

Rules:

- Validate all affected paths first.
- Validate all hunks before applying any mutation.
- Apply atomically as a transaction where practical.
- Acquire affected path mutation locks in deterministic order before validation/staging/commit.
- Preserve existing all-validation-before-write behavior; cross-file commit can still fail partway because filesystem renames are per-file.
- Return per-file summary plus bounded unified diff preview and `changed_files` metadata.

### glob

Discovers files by pattern.

Inputs:

- `pattern`
- `path` optional.

Rules:

- Use project root or explicit safe directory.
- Respect gitignore by default.
- Provider-visible glob calls cannot disable ignore filtering. Local/internal callers may use `no_ignore` only under explicit local control; permission checks, secret-path denial, workspace boundaries, `.git` exclusion, and output caps still apply.
- Limit result count.

### grep

Searches file contents.

Inputs:

- `pattern`
- `path` optional.
- `include` optional.

Rules:

- Use ripgrep-compatible semantics if `rg` is available, or native fallback later.
- Respect gitignore by default.
- Provider-visible grep calls cannot disable ignore filtering. Local/internal callers may use `no_ignore` only under explicit local control; permission checks, secret-path denial, workspace boundaries, `.git` exclusion, binary-file safeguards, and output caps still apply.
- Return file, line, and matched preview.
- Limit matches and output bytes.

Current native ignore support intentionally implements a small `.gitignore` subset without new dependencies: root and nested `.gitignore` files, blank/comment lines, `!` negation, leading `/` anchoring relative to the ignore-file directory, trailing `/` directory-only rules, unescaped trailing-space trimming, backslash escapes for literal characters, `*`, `?`, and common `**` wildcard behavior. Last matching rule wins. Bracket character classes are not modeled yet. Ignored directories are pruned during search; hardcoded generated-directory pruning remains as a default safety/performance fallback while `.git` stays excluded even for local/internal `no_ignore` callers.

### bash

Executes shell commands.

Inputs:

- `command`
- `workdir`
- `timeout_ms`
- `description`

Rules:

- Always require explicit workdir after normalization.
- Enforce timeout.
- Enforce output byte limit.
- Kill process tree on cancellation or timeout.
- Never run detached by default.
- Permission scan before execution.
- Prefer argv-style execution for internal commands; shell string execution is isolated behind this single executor.

### webfetch

Fetches bounded HTTP(S) text content after explicit network permission.

Inputs:

- `url`
- `max_bytes` optional, capped by backend policy
- `timeout_ms` optional, clamped by backend policy

Rules:

- Validate the URL before permission or network access.
- Allow only `http://` and `https://` URLs.
- Deny userinfo, control bytes, empty hosts, IP literal hosts, numeric IP aliases, localhost names, `.localhost`, and `.local` names.
- For default network execution, resolve the host before fetching and reject private, loopback, link-local, multicast, reserved, documentation, shared, or otherwise non-global addresses.
- Request `network.fetch` permission for the validated URL; read-only file/search permission modes do not imply network access.
- Use a bounded GET with timeout and response byte cap. Redirects are disabled for `webfetch` until each redirected target can be revalidated and re-authorized.
- Return only 2xx responses as successful tool results.
- Return text-like bodies only; reject binary content types when known and reject bodies containing NUL bytes.
- Return bounded `content`, `content_type` when available, `status_code`, `truncated`, `total_bytes`, and `output_bytes` metadata.
- Do not perform web search, browser automation, credential handling, image attachment conversion, or provider-specific multimodal payloads in this tool.

### question

Asks the user for structured input.

Inputs:

- `question`
- `choices` optional.
- `allow_custom` default true.

Rules:

- Only available in interactive mode.
- In non-interactive mode, returns unavailable with guidance.

### lsp_diagnostics

Queries diagnostics for one file from a locally configured language server.

Inputs:

- `path`

Rules:

- Request `lsp.query` permission for the target path before any server query.
- Treat permission as read-like: deny secret paths, ask outside the workspace, and allow workspace files by default.
- Provider-visible input includes only the file path. Server command argv is local configuration/test harness state and is not model-controlled.
- Advertise the provider schema only when a local diagnostics provider is configured; otherwise the tool is not available to model calls.
- Start the configured server with an explicit argv vector and workspace root; do not use a shell.
- Use JSON-RPC `Content-Length` framing, initialize the server, then request `textDocument/diagnostic` for the file URI.
- Percent-encode file URI path bytes while preserving real path separators, so literal encoded separators in filenames cannot cross the permission boundary.
- Bound request timeouts and kill the LSP child process group on timeout or client destruction.
- Return bounded structured diagnostics: `severity`, `message`, `line`, `column`, and `code`, plus `truncated` and `total_diagnostics` when provider-facing output is capped.
- Redact local server command and workspace details from provider-visible LSP failures; keep detailed process context for local diagnostics only.

First-slice non-goals: language-server discovery/config catalogs, symbols, definitions, references, workspace-wide diagnostics, TUI rendering, plugin/MCP integration, provider registry changes, and new external dependencies.

## Permission Integration

Tools must not decide safety internally in an ad hoc way. They should produce permission requests and rely on the permission service.

Examples:

- `read` requests `file.read` for the normalized path.
- `write`, `edit`, and `apply_patch` request `file.write` for every affected path.
- File deletion requests `file.delete` and is not part of MVP write/edit behavior.
- `bash` requests `shell.run` and may request `shell.destructive` based on command scan.
- `webfetch` requests `network.fetch`.
- `lsp_diagnostics` requests `lsp.query` for the target file path.

## Output Truncation

Every tool output must be bounded.

Defaults to decide during implementation:

- Max output bytes per tool call.
- Max lines per read.
- Max grep matches.
- Max bash stdout bytes.
- Max bash stderr bytes.

If output is truncated, the result must say so and include enough metadata to re-run a narrower command.

Backend tools may also create a session-local spill file for truncated high-volume output. Spill files are additive local metadata: provider-visible `bash`, `glob`, and `grep` results include only the safe spill filename (`spill_file`) when one is written, plus `spill_truncated` when the spill itself hit the fixed per-file cap; absolute spill paths are not replayed to the provider. Spill directories live under the session file directory (for example `<session-dir>/spill`), are created lazily, and must never be placed in the workspace. Current stable spill formats are raw combined bash output, one glob path per line, and grep lines as `path:line:content`.

Long-running or high-output tools can emit coarse `tool_progress` runtime events with existing event fields: `text`, `call_id`, `tool`, and `status`. Progress is best-effort and additive; consumers that ignore it continue to observe the same tool start/result flow.

## Testing Priority

Tool tests are more important than UI tests early.

Required regression areas:

- Path traversal and external directory behavior.
- Symlink behavior.
- Exact edit ambiguity.
- Patch validation before mutation.
- Bash timeout and cancellation.
- Bash output truncation.
- Search `.gitignore` pruning, provider-inaccessible `no_ignore`, spill files, and `tool_progress` events.
- Webfetch URL validation, DNS pinning, redirect-disabled behavior, content-type filtering, and headless `network.fetch` permission.
- LSP diagnostics permission, file URI encoding, timeout/size caps, provider error redaction, and provider JSON bounds.
- OpenAI Responses tool-call event parsing, including `response.output_item.added` function-call items.
- Live headless smoke for every model-visible tool class: read/search/webfetch in print mode; write/edit/apply_patch/bash/question through RPC resolver replies; LSP through fake-server tests unless a local diagnostics provider is configured.
- Permission allow/ask/deny behavior.
- Nonexistent files and directories.
- Unicode and long-line handling.

## Tooling Rule

AVA should have fewer tools with excellent semantics rather than many tools with unclear behavior.
