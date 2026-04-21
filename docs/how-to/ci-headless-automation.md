---
title: "How-to: Run AVA In CI/Headless Automation"
description: "Task-focused guide for non-interactive AVA runs in CI using headless mode, JSON output, and existing repo workflows."
order: 6
updated: "2026-04-18"
---

# How-to: Run AVA In CI/Headless Automation

Use this page when you want to run AVA in unattended environments (CI jobs, scripts, or other headless automation).

See also: [How-to: Run AVA Locally](run-locally.md), [Commands reference](../reference/commands.md), [Environment variables reference](../reference/environment-variables.md)

## 1) Use the non-interactive baseline command

```bash
ava "summarize changes in this branch" --headless --json --no-update-check
```

Why this baseline is safer for automation:

1. `--headless` forces the non-TUI path.
2. `--json` emits structured events line-by-line for scripts.
3. `--no-update-check` avoids startup update-check noise in unattended jobs.

## 2) Set environment and routing explicitly

For CI reliability, prefer explicit model routing and credentials:

```bash
export AVA_OPENAI_API_KEY="${OPENAI_API_KEY}"
export AVA_PROVIDER="openai"
export AVA_MODEL="gpt-5.4"

ava "generate a short release note from git diff" --headless --json --no-update-check --max-turns 4
```

Notes:

1. `AVA_<PROVIDER>_API_KEY` variables are first-priority provider-specific credential overrides.
2. `AVA_PROVIDER` and `AVA_MODEL` are fallback selectors when flags are omitted.
3. You can still override routing per run with `--provider` and `--model`.
4. If this machine uses encrypted local credential storage, set `AVA_MASTER_PASSWORD` as well. Otherwise a headless run can still block on the master-password prompt while loading local credentials.

## 3) Parse JSON output conservatively

In JSON mode, stdout is the primary newline-delimited JSON event stream. Common event `type` values include:

1. `text`
2. `thinking`
3. `tool_call`
4. `tool_result`
5. `progress`
6. `complete`
7. `error` (emitted to stderr)

Current stability note: these event names reflect current behavior, not a strict long-term external schema. Also, stderr can still contain plain-text warnings or status lines in some failure paths.

A practical extraction pattern is to watch for the final `complete` event and then check process exit status:

```bash
set -euo pipefail

tmp_json="$(mktemp)"
tmp_err="$(mktemp)"
trap 'rm -f "${tmp_json}" "${tmp_err}"' EXIT

ava "audit changed Rust files for TODO comments" --headless --json --no-update-check \
  >"${tmp_json}" 2>"${tmp_err}"

# Example: inspect complete events with jq (if available)
jq -c 'select(.type == "complete")' "${tmp_json}" || true
```

Do not assume every event type is stable forever. For automation contracts, treat `complete` + exit code as the primary success signal.

## 4) Queue follow-up messages without TTY prompts

For unattended multi-step runs, prefer CLI queue flags over interactive input:

```bash
ava "fix clippy findings" \
  --headless --json --no-update-check \
  --follow-up "run focused tests for touched crates" \
  --later "summarize unresolved risks"
```

You can also stream queued messages over stdin in JSON mode with this per-line shape:

```json
{"tier":"steer_agent","text":"continue with a smaller patch"}
{"tier":"follow_up_agent","text":"run tests"}
{"tier":"post_complete_agent","group":1,"text":"summarize result"}
```

## 5) Keep non-interactive behavior assumptions conservative

Headless mode is scoped for unattended runs, not full interactive parity.

Current contract notes:

1. Headless is explicitly non-interactive.
2. Interactive approval/question/plan UX is not required in headless mode.
3. Tool approvals in headless are risk-aware: safe/non-dangerous approval-requiring work may auto-resolve, but dangerous approval-requiring actions fail closed instead of hanging for interactive confirmation (`EX-001`).
4. Critical actions remain blocked by the existing permission/risk machinery rather than being approved by the headless adapter.

## 6) Align with existing repository CI workflows

Current GitHub Actions CI in this repo runs:

1. Frontend lint/type/test jobs
2. Rust fmt/clippy/nextest/doc jobs
3. Security and typo jobs
4. Platform build jobs on push to `master`/`develop`

For local pre-flight, this repo uses:

1. `just check` as the pragmatic local confidence gate and pre-push hook gate
2. `just ci` as the broader local verification pass while CI remains authoritative

If you are working on AVA itself, use the contributor docs for the full local verification flow.

## 7) Minimal GitHub Actions headless step (opt-in)

Use this pattern when you intentionally want an automated headless AVA task:

```yaml
- name: Headless AVA automation (JSON)
  env:
    AVA_OPENAI_API_KEY: ${{ secrets.AVA_OPENAI_API_KEY }}
    AVA_PROVIDER: openai
    AVA_MODEL: gpt-5.4
  run: |
    set -euo pipefail
    ava "summarize CI failure and suggest a fix" --headless --json --no-update-check --max-turns 3 > ava-events.json
```

Keep this step opt-in and secret-gated. Avoid assuming interactive prompts or manual approval paths exist in CI.
