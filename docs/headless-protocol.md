# AVA Headless Protocol

This document defines the backend contract for AVA headless modes. Phase 5 adds the first JSONL RPC MVP over stdin/stdout.

## Modes

- `interactive`: current TUI or non-TTY line shell behavior. Human prompts are allowed. Piped stdin without `--print` remains the line shell for slash-command scripts.
- `print`: one prompt in, text or JSONL events out, process exits after the turn. Enable with `ava --print "prompt"`, `ava -p "prompt"`, or `printf 'prompt' | ava --print`.
- `rpc`: JSONL request/response envelopes over stdio for long-lived automation. Enable with `ava --rpc` or `ava --output rpc`.
- `server` (deferred): no contract yet. Server mode is explicitly out of scope until the stdio RPC contract is proven.

## Print Input And Output

Print mode accepts an optional prompt argument after `--print`/`-p`. If no prompt argument is supplied and stdin is not a terminal, stdin is used as the prompt. If both a prompt argument and piped stdin are supplied, AVA merges them deterministically as:

```text
<explicit prompt>

<stdin content>
```

Text output is the default. In text output, stdout contains the final assistant text only; diagnostics, tool progress, and errors go to stderr. JSONL output is selected with `--json` or `--output json`; stdout contains serialized runtime events and ends with either `done` or `error` for turns that reach the runtime. Credentials and startup failures are reported on stderr.

`--no-session` is not implemented in Phase 3 because the shared runtime currently opens a session for every turn. Sessionless print mode is deferred until the runtime supports it directly.

## Print Permission Flags

Print mode is fail-closed by default for backend permission decisions whose action is `ask`: AVA installs an explicit deny resolver when no policy flag is provided. Permission decisions whose action is already `deny` are never upgraded by these flags.

Supported policy flags:

- `--allow read-only`: allows read/search-style permission prompts (`read_file`, `glob`, and `grep` shapes) when the backend asks. Write, edit, patch, bash, and question prompts remain denied.
- `--allow-tool glob,grep,read_file`: allows only the listed exact tool names when those tools produce compatible ask prompts. Supported values are `glob`, `grep`, and `read_file`; unsupported values such as `bash`, `write_file`, `edit_file`, `apply_patch`, `question`, or arbitrary strings are rejected as usage errors.

Examples:

```sh
ava --print "summarize the repo" --allow read-only
ava --print "inspect this file" --allow-tool read_file
ava --print "find symbols" --allow-tool glob,grep
```

Invalid permission flag values exit with code `2` and write a usage error to stderr before provider/auth startup. AVA does not persist headless permission rules; every print invocation must provide the desired policy explicitly.

## Stdout / Stderr Contract

- In headless `print` and `rpc` modes, stdout is protocol output only.
- Human-readable diagnostics, startup warnings, and fatal errors go to stderr.
- Protocol events are newline-delimited JSON objects. Writers must not emit partial JSON records.
- Consumers should treat unknown event fields as forward-compatible metadata and unknown event types as non-fatal unless the enclosing request fails.

## Exit Codes

- `0`: success; the requested turn or RPC command completed.
- `1`: runtime failure, provider failure, tool failure, permission denial, or unavailable headless interaction.
- `2`: command-line usage error or malformed protocol request.

## Event Types

All events include at least `type`. Runtime emitters should include `timestamp` and `session_id` when available.

- `session_start`: session/runtime metadata, including `mode`, `provider`, and `model`.
- `user_message`: accepted user input for a turn.
- `assistant_message`: final assistant text for a completed turn.
- `tool_start`: tool call began; includes `call_id`, `tool`, and a safe argument summary when available.
- `tool_result`: tool call completed; includes `call_id`, `tool`, `status`, and a safe result summary when available.
- `error`: runtime, provider, or tool boundary failure; includes `category`, `message`, and optional `details`.
- `done`: terminal event for a successful turn; includes `stop_reason` and small counters when available.

Event payloads are intentionally minimal and must not require clients to parse session JSONL internals.

## RPC JSONL MVP

RPC mode reads strict LF-delimited JSON objects from stdin and writes LF-delimited JSON protocol records to stdout. Diagnostics and fatal startup/read/write errors go to stderr. AVA keeps reading after malformed request lines when it can emit a protocol error response.

Request ids are client-owned non-empty strings. Successful command responses use:

```json
{"id":"req_1","type":"response","success":true,"result":{}}
```

Errors use:

```json
{"id":"req_1","type":"response","success":false,"error":{"category":"invalid_argument","message":"...","details":"..."}}
```

Malformed lines that do not contain a valid id are answered with `"id":""`.

### Commands

Prompt turn:

```json
{"id":"1","type":"prompt","message":"hello"}
```

AVA streams normal runtime events (`session_start`, `user_message`, `assistant_message`, tool events, `done`/`error`) to stdout while the turn runs, then writes a response with `final_text`, `stop_reason`, `provider_iterations`, `tool_calls`, and `session_id`. Prompt requests require configured OpenAI auth unless the embedding test harness supplies runtime credentials.

Cancel:

```json
{"id":"2","type":"cancel"}
```

Sets the synchronous MVP cancel flag and returns `{"cancel_requested":true,"active_run":false}`. There is no asynchronous question/cancel reply protocol yet, so cancellation is observed at subsequent checked runtime boundaries.

State:

```json
{"id":"3","type":"get_state"}
```

Returns the active session id/path, mode, provider/model, workspace/current directory, cancel flag, and loaded context source summary.

List sessions:

```json
{"id":"4","type":"list_sessions"}
```

Returns `sessions`, an array of `{session_id,path,last_updated,entry_count}` for the current workspace.

Open a session by id or unambiguous prefix:

```json
{"id":"5","type":"open_session","session_id":"session-prefix"}
```

Switches the active runtime session and returns the same state shape as `get_state`.

Backend slash-command equivalents:

```json
{"id":"6","type":"compact","instructions":"optional notes"}
{"id":"7","type":"export"}
{"id":"8","type":"context"}
```

These dispatch `/compact`, `/export`, and `/context` through the shared backend command dispatcher. Responses include `handled`, `quit`, `output` (array of strings), and `text` (joined output).

Unknown command types return an error response and do not terminate the RPC loop.

## Future RPC Envelope (Deferred)

Requests:

```json
{"id":"req_1","method":"prompt","params":{"text":"hello","mode":"build","session_id":"optional"}}
```

Successful responses:

```json
{"id":"req_1","ok":true,"result":{"session_id":"session_..."}}
```

Error responses:

```json
{"id":"req_1","ok":false,"error":{"code":"permission_denied","message":"tool requires permission"}}
```

RPC notifications may reuse the event objects above with no `id`. Request ids are client-owned strings and are echoed verbatim after validation.

## Permission And Question Behavior

Headless operation is fail-closed by default:

- Permission decisions that require user approval fail unless print mode supplies `--allow read-only` or `--allow-tool` for a supported read/search tool.
- The `question` tool fails with an unavailable interaction error unless a future RPC client supplies a resolver.
- Destructive operations remain behind existing backend permission policy checks.

## Server Mode Deferral

No socket, daemon, HTTP server, or background service semantics are defined in this phase. Server mode must not be implemented by inferring behavior from `print` or `rpc`; it requires a separate contract review.
