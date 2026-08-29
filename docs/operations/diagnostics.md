# Diagnostics And Support

AVA separates two diagnostic layers:

- Carlo Wood's `libcwd` channels provide rich local developer debugging in builds configured with libcwd. Their output is off by default even in those builds; operators opt in with `AVA_DEBUG_OUTPUT=1` (and `AVA_NO_DEBUG_OUTPUT` suppresses it again). They are otherwise controlled by the existing libcwd build/runtime configuration and are not support artifacts.
- `ava doctor`, `--trace`, last-failure state, and support exports are release-safe, typed diagnostics. They do not consume libcwd output or arbitrary formatted errors.

## Passive Doctor

```sh
ava doctor
ava doctor --json
```

Doctor checks fixed readiness categories for the AVA version/platform, private XDG config and state roots, model registry/default model, auth-file metadata, plugins, MCP, LSP, and persistent permission rules. It reports only fixed labels, statuses, codes, and counts.

Doctor is passive and offline: it does not contact providers, refresh or read credential values, launch a plugin/MCP/LSP/process, access the network, mutate configuration, create a session, or write diagnostic state. Missing optional integrations produce warnings rather than failures. Exit status is `0` for pass/warning results, `1` when a required check fails, and `2` for invalid command syntax.

## Private Runtime Trace

Add `--trace` to a normal TUI, print, RPC, or ACP process:

```sh
ava --trace
ava --trace --print "inspect this project"
ava --trace --rpc
ava --trace --acp
```

Tracing is disabled by default. Each traced process creates a unique bounded JSONL file below:

```text
$XDG_STATE_HOME/ava/diagnostics/traces/trace-v1-<opaque>.jsonl
```

The trace is limited to 10,000 events and 10 MiB. Runtime, turn, call, session, provider, and parent identities are replaced with stable per-trace opaque aliases. Only allowlisted numeric/boolean event metadata is retained; paths, commands, URLs, prompts, reasoning, tool input/output, provider/plugin/MCP identities, headers, environment values, credentials, and payload content are omitted. Files are owner-only mode `0600` beneath mode `0700` directories, and unsafe symlink/FIFO/hardlink or permission states fail trace startup.

On orderly shutdown AVA merges typed event/outcome totals plus written, dropped, failed, and byte counts into `diagnostics/trace-counters-v1.json` under a private interprocess lock. Concurrent traced processes retain separate trace files and all contribute to the cumulative snapshot. Older v1 snapshots remain readable and are marked as having incomplete writer-health history. Support export reads only this counter snapshot, never trace event lines or filenames.

## Last Failure

A terminal provider, session, tool, or runtime failure may update:

```text
$XDG_STATE_HOME/ava/diagnostics/last-failure-v1.json
```

The record contains only a timestamp, fixed component/category/code, retryability, occurrence count, and fixed recovery guidance. It never stores `Error::message()`, `Error::format()`, context strings, or remote payloads. Cancellation, invalid user input, and successful runs do not replace the record. Recording is best-effort and cannot change the original runtime result.

MCP and plugin failures use the same closed safe projection before reaching the model, session, RPC, or portable export. Successful integration content is unchanged.

## Support Export

```sh
ava support export
```

AVA prints the exact local path of a newly created, no-replace JSON artifact below `$XDG_STATE_HOME/ava/support/`. The artifact contains only AVA version/platform facts, the passive doctor projection, typed trace counters/state, and the typed last-failure state. It excludes trace events, sessions, exports, prompts, reasoning, commands, paths, configurations, identities, credentials, raw errors, stderr, headers, URLs, provider responses, and plugin/MCP payloads.

AVA never uploads support artifacts and implements no diagnostic telemetry. Inspect and share the generated file manually. Support publication is bounded, descriptor-relative, no-follow, owner-only, and fails closed when its storage path is unsafe.

## Developer Debugging

When AVA is built with libcwd, existing subsystem channels such as `dc::runtime`, `dc::app`, `dc::config`, `dc::provider`, `dc::session`, `dc::mcp`, and `dc::plugin` provide deeper local diagnostics. Workstream 3 adds bounded operation/state/count messages to those channels. Libcwd output can still contain developer-oriented process metadata and must be treated as private; it is intentionally never copied into `ava support export`.
