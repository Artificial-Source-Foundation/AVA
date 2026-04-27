# AVA Tooling Plan

AVA tools are the model's hands. They must be small, predictable, permissioned, and easy to test.

## Tool Contract

Every tool has:

- Stable name.
- JSON-schema-like input definition.
- Short model-facing description.
- Permission requirements.
- Deterministic validation before side effects.
- Structured result with human text plus metadata.
- Output truncation rules.
- Cancellation support where applicable.

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
- `old`
- `new`
- `replace_all` default false.

Rules:

- Fail if `old` is missing.
- Fail if `old` appears multiple times and `replace_all` is false.
- Preserve original file if validation fails.
- Return changed line summary.

### apply_patch

Applies a structured multi-file patch.

Rules:

- Validate all affected paths first.
- Validate all hunks before applying any mutation.
- Apply atomically as a transaction where practical.
- Return per-file summary.

### glob

Discovers files by pattern.

Inputs:

- `pattern`
- `path` optional.

Rules:

- Use project root or explicit safe directory.
- Respect gitignore by default.
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
- Return file, line, and matched preview.
- Limit matches and output bytes.

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

### question

Asks the user for structured input.

Inputs:

- `question`
- `choices` optional.
- `allow_custom` default true.

Rules:

- Only available in interactive mode.
- In non-interactive mode, returns unavailable with guidance.

## Permission Integration

Tools must not decide safety internally in an ad hoc way. They should produce permission requests and rely on the permission service.

Examples:

- `read` requests `file.read` for the normalized path.
- `write`, `edit`, and `apply_patch` request `file.write` for every affected path.
- File deletion requests `file.delete` and is not part of MVP write/edit behavior.
- `bash` requests `shell.run` and may request `shell.destructive` based on command scan.
- `webfetch` requests `network.fetch`.

## Output Truncation

Every tool output must be bounded.

Defaults to decide during implementation:

- Max output bytes per tool call.
- Max lines per read.
- Max grep matches.
- Max bash stdout bytes.
- Max bash stderr bytes.

If output is truncated, the result must say so and include enough metadata to re-run a narrower command.

## Testing Priority

Tool tests are more important than UI tests early.

Required regression areas:

- Path traversal and external directory behavior.
- Symlink behavior.
- Exact edit ambiguity.
- Patch validation before mutation.
- Bash timeout and cancellation.
- Bash output truncation.
- Permission allow/ask/deny behavior.
- Nonexistent files and directories.
- Unicode and long-line handling.

## Tooling Rule

AVA should have fewer tools with excellent semantics rather than many tools with unclear behavior.
