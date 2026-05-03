# AVA Headless Protocol

This document defines the current backend contract for AVA headless modes. AVA supports one-shot print mode and a JSONL RPC MVP over stdin/stdout; provider streaming is exposed through runtime event envelopes. Server mode remains deferred.

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

Text output is the default. In text output, stdout contains the final assistant text only; diagnostics, tool progress, and errors go to stderr. JSONL output is selected with `--json` or `--output json`; stdout contains serialized event envelopes and ends with either a `done` or `error` event for turns that reach the runtime. Credentials and startup failures are reported on stderr.

`--no-session` is not implemented because the shared runtime currently opens a session for every turn. Sessionless print mode is deferred in the product plan until the runtime supports it directly.

## Headless Permission Flags

Headless modes are fail-closed by default for backend permission decisions whose action is `ask`. Print mode installs an explicit deny resolver when no policy flag is provided. RPC mode emits a `permission_requested` event for ask prompts unless a supplied headless policy auto-allows the request. Permission decisions whose action is already `deny` are never upgraded by these flags.

Supported policy flags:

- `--allow read-only`: allows read/search-style permission prompts (`read_file`, `glob`, and `grep` shapes) when the backend asks. Network, write, edit, patch, bash, and question prompts remain denied.
- `--allow-tool glob,grep,read_file,webfetch`: allows only the listed exact tool names when those tools produce compatible ask prompts. Supported values are `glob`, `grep`, `read_file`, and `webfetch`; unsupported values such as `bash`, `write_file`, `edit_file`, `apply_patch`, `question`, or arbitrary strings are rejected as usage errors. `webfetch` only auto-allows exact `network.fetch` prompts produced by the `webfetch` tool; `--allow read-only` never allows network prompts.

Examples:

```sh
ava --print "summarize the repo" --allow read-only
ava --print "inspect this file" --allow-tool read_file
ava --print "find symbols" --allow-tool glob,grep
ava --print "fetch release notes" --allow-tool webfetch
ava --rpc --allow read-only
```

Invalid permission flag values exit with code `2` and write a usage error to stderr before provider/auth startup. AVA does not persist headless permission rules; every headless invocation must provide the desired policy explicitly. In RPC mode, matching read/search or exact `webfetch` network prompts are auto-allowed before `permission_requested`; non-matching ask prompts still require an explicit `permission_reply`.

## Stdout / Stderr Contract

- In headless `print` and `rpc` modes, stdout is protocol output only.
- Human-readable diagnostics, startup warnings, and fatal errors go to stderr.
- Protocol events are newline-delimited JSON objects. Writers must not emit partial JSON records.
- Consumers should treat unknown envelope fields as forward-compatible metadata and unknown event names as non-fatal unless the enclosing request fails.

## Exit Codes

- `0`: success; in RPC mode, the protocol loop exited cleanly, even if individual requests returned in-band `success:false` responses.
- `1`: print-mode runtime/provider/tool/permission failure, unavailable headless interaction, RPC startup failure, or unrecoverable RPC stdio read/write failure.
- `2`: command-line usage error. Recoverable malformed RPC request records produce `success:false` responses and do not change the process exit code by themselves.

## Provider Credentials

Runtime provider calls resolve credentials for the active session provider. OpenAI keeps its existing stored OAuth/API-key behavior and falls back to `OPENAI_API_KEY` when no stored OpenAI credential exists. Non-OpenAI providers support provider-scoped API keys and bearer tokens in `auth.json`, for example `{"anthropic":{"type":"api_key","api_key":"..."}}`, and then provider environment variables such as `ANTHROPIC_OAUTH_TOKEN` or `ANTHROPIC_API_KEY`. Interactive setup is available with `ava login [provider]`, `ava auth login [provider]`, `ava connect [provider]`, and TUI `/connect`; omitting the provider opens a searchable provider picker, and TUI `/connect` uses a modal. Secrets are read with terminal echo disabled where possible and masked in TUI prompts. Headless setup can store credentials without a browser or TTY, for example `printf '%s\n' "$ANTHROPIC_API_KEY" | ava connect anthropic --api-key-stdin` or `ava connect moonshot --api-key-env MOONSHOT_API_KEY`. AVA only reads credential files when they are owned by the current user and not readable by group/other users; manually-created files should use owner-only permissions such as `chmod 600 ~/.config/ava/auth.json`. Anthropic OAuth tokens are bearer tokens supplied by the environment or auth file; AVA does not refresh Anthropic OAuth tokens yet.

## Event Envelope

Headless JSON event records are newline-delimited envelopes emitted by the shared runtime event bus:

```json
{"schema_version":1,"event_id":"event_...","timestamp":"2026-04-30T00:00:00Z","session_id":"session_...","name":"assistant_message","type":"assistant_message","payload":{"text":"hello"},"text":"hello"}
```

Envelope fields:

- `schema_version`: integer schema version. The current value is `1`.
- `event_id`: AVA-owned unique id for this emitted event record.
- `timestamp`: event timestamp when available.
- `session_id`: active runtime session id when available.
- `run_id`, `turn_id`, `message_id`, `request_id`, `correlation_id`: optional correlation metadata. RPC prompt and command events include `request_id` for the client request that caused the event.
- `name`: event name.
- `payload`: event-specific object. Payloads are intentionally minimal and must not require clients to parse session JSONL internals.
- `type` and documented legacy runtime fields such as `text`, `tool`, `status`, `category`, `message`, and `stop_reason`: compatibility aliases for the pre-envelope flat event shape. New clients should prefer `name` and `payload`; new payload fields are not automatically promoted to the top level.

Current event names:

- `session_start`: session/runtime metadata, including `mode`, `provider`, and `model`.
- `user_message`: accepted user input for a turn.
- `message_update`: live assistant text delta emitted while a streaming provider response is in progress; includes `text` and `status`.
- `message_end`: live provider stream completion marker; includes `status`.
- `provider_event`: live non-text provider stream event such as tool-call argument deltas or provider stream errors; includes `status`, and may include `call_id`, `tool`, `text`, or `message` depending on the provider event.
- `assistant_message`: final assistant text for a completed turn.
- `tool_start`: tool call began; includes `call_id`, `tool`, and a safe argument summary when available.
- `tool_result`: tool call completed; includes `call_id`, `tool`, `status`, and a safe result summary when available.
- `error`: runtime, provider, or tool boundary failure; includes `category`, `message`, and optional `details`.
- `done`: terminal event for a successful turn; includes `stop_reason` and small counters when available.
- `steer_queued`, `steer_applied`, `steer_skipped`: RPC steering queue lifecycle events. Payloads include `message` and skipped events include `reason`.
- `follow_up_queued`, `follow_up_started`, `follow_up_skipped`: RPC follow-up queue lifecycle events. Payloads include `message` and skipped events include `reason`.

## RPC JSONL MVP

RPC mode reads strict LF-delimited JSON objects from stdin and writes LF-delimited JSON protocol records to stdout. Diagnostics and fatal startup/read/write errors go to stderr. AVA keeps reading after malformed request lines when it can emit a protocol error response; those recoverable request errors are reported in-band rather than through the process exit code.

RPC requests may include `"protocol_version":1`. Omitting the field keeps current-version behavior. Present values must be JSON integers. Unsupported or malformed protocol versions produce an in-band error response and do not terminate the loop.

Request ids are client-owned non-empty strings capped at 256 bytes. Resolver `request_id` and `correlation_id` fields are also capped at 256 bytes; over-limit identifiers produce an in-band `success:false` response when a response id can be parsed, or `"id":""` for malformed/no-id records. Successful command responses use:

```json
{"id":"req_1","type":"response","success":true,"result":{}}
```

Errors use:

```json
{"id":"req_1","type":"response","success":false,"error":{"category":"invalid_argument","message":"...","details":"..."}}
```

Malformed lines that do not contain a valid id are answered with `"id":""`.

### Commands

Protocol version:

```json
{"id":"0","type":"get_protocol","protocol_version":1}
```

Returns `protocol_version` and `supported_protocol_versions`.

Prompt turn:

```json
{"id":"1","type":"prompt","message":"hello"}
```

AVA starts the prompt turn asynchronously inside the RPC session, keeps reading stdin while it runs, streams normal runtime event envelopes (`session_start`, `user_message`, streaming `message_update`/`message_end`/`provider_event`, final `assistant_message`, tool events, `done`/`error`) to stdout, then writes exactly one RPC response with `final_text`, `stop_reason`, `provider_iterations`, `tool_calls`, and `session_id` on success or `success:false` on failure. Failures before runtime startup, such as missing auth, may return only the `success:false` RPC response. Prompt event envelopes include the prompt command id as `request_id`. Only one prompt may be active per RPC session; a second `prompt` while one is active returns an in-band error. Prompt requests require credentials for the active session provider unless the embedding test harness supplies runtime credentials.

Steer an active prompt:

```json
{"id":"1a","type":"steer","message":"adjust course"}
```

`steer` requires an active prompt. AVA queues the message, emits `steer_queued` with `request_id` set to the steer command id and `correlation_id` set to the active prompt/follow-up id, and returns `{"queued":true,"correlation_id":"..."}`. At the next safe provider boundary, AVA appends the steering text as an additional user message before building the next provider request and emits `steer_applied`. The current safe boundary is before a provider request, including the request after tool execution. If the active run completes before a queued steer reaches a safe boundary, AVA emits `steer_skipped` with reason `run_completed_before_safe_point`; it is not converted into a follow-up. Steering queues are bounded to 64 entries and 64 KiB of aggregate message bytes; once the queue is capped, canceled, or input is closed, new `steer` requests are rejected with `success:false` and no queue event. Queue lifecycle event payloads may truncate long `message` values and include `message_truncated:true` plus `message_bytes`.

Queue a follow-up turn:

```json
{"id":"1b","type":"follow_up","message":"continue with this next"}
```

`follow_up` requires an active prompt. AVA queues the message and emits `follow_up_queued` with `request_id` set to the follow-up command id and `correlation_id` set to the currently active prompt/follow-up id. The command's RPC response is delayed until the queued follow-up runs or is skipped. After the current prompt response is written, AVA makes the follow-up the active request, emits `follow_up_started` with both `request_id` and `correlation_id` set to the follow-up command id, starts a normal prompt turn in the same session with the same provider/runtime options, streams runtime events with the follow-up id as their prompt correlation, and finally writes exactly one RPC response for the follow-up id. A `steer` accepted after `follow_up_started` targets that follow-up run. If no prompt is active, `follow_up` returns an in-band error; use `prompt` when idle.

Follow-up queues are bounded to 64 entries and 64 KiB of aggregate message bytes. Once the queue is capped, canceled, or input is closed, new `follow_up` requests are rejected immediately with `success:false` and no queue event. Accepted queued follow-ups have these terminal outcomes: `follow_up_started` followed by a success or error response for the follow-up id; `follow_up_skipped` with `reason:"canceled"` plus a canceled error response; `follow_up_skipped` with `reason:"prompt_start_failed"` plus an error response; or `follow_up_skipped` with `reason:"run_completed_before_safe_point"` plus an error response. Queue lifecycle event payloads may truncate long `message` values and include `message_truncated:true` plus `message_bytes`.

Cancel:

```json
{"id":"2","type":"cancel"}
```

Sets the RPC session cancel flag and returns `{"cancel_requested":true,"active_run":true|false,"cleared_steer":N,"cleared_follow_up":N}`. When a prompt is active, pending permission/question resolver waits are unblocked fail-closed, queued steering/follow-up messages are cleared, skipped queue events are emitted, the cancel command response is written, and then queued follow-up commands receive `success:false` canceled responses. RPC clients must correlate responses by `id`: active prompt terminal events or the active prompt response may interleave with the cancel response because cancellation is observed by the prompt worker at cooperative runtime boundaries. Provider transport calls are not interrupted mid-request yet, so cancellation may complete after the in-flight provider call returns. If RPC stdin reaches EOF while a prompt worker exists, AVA marks input closed, requests cancellation, clears queued steering/follow-up messages, cancels pending resolvers, and prevents queued follow-ups from starting after client disconnect.

State:

```json
{"id":"3","type":"get_state"}
```

Returns the active protocol version, session id/path, mode, provider/model, workspace/current directory, cancel flag, and loaded context source summary.

`get_state`, `list_models`, and `list_sessions` remain available while a prompt is active. `get_messages`, `get_session_stats`, `set_model`, `cycle_model`, `compact`, `export`, and `context` materialize or mutate session history and are rejected while a prompt is active. RPC `/compact` also marks the session busy while it generates and records the provider summary, so a prompt cannot start midway through compaction.

Messages:

```json
{"id":"3a","type":"get_messages"}
```

Returns durable message-like session entries for the active session in append order. The current response is `{session_id,messages,truncated,message_count}` where each message includes `id`, `parent_id`, `type`, `timestamp`, and raw object-shaped `data` unless the individual entry is too large, in which case the entry is marked `truncated`. Responses are capped to protect headless clients and the AVA process. This intentionally excludes non-message bookkeeping entries such as `session_start`, `model_change`, `compaction`, and permission audit rows. Internal replay user messages inserted after context compaction are also hidden because they are provider-context repair entries, not user-visible transcript turns.

Session stats:

```json
{"id":"3b","type":"get_session_stats"}
```

Returns `session_id`, `session_path`, `entry_count`, first/last timestamps, usage/cost totals when known, and counts for session entry types. User-message counts exclude internal replay entries. Session JSONL entry types are additive; clients that inspect session files directly should ignore unknown non-message bookkeeping entries and prefer RPC `get_messages`/`get_session_stats` for stable automation data.

Usage fields are additive and appear only when present in saved assistant entries: `input_tokens`, `output_tokens`, `reasoning_tokens`, `cache_read_tokens`, `cache_write_tokens`, and `total_tokens`. Exact provider token totals are kept separate from byte-count fallback estimates: `estimated_input_bytes`, `estimated_output_bytes`, and `estimated_total_bytes` report fallback byte estimates when provider usage was unavailable. `exact_usage_entries` and `estimated_usage_entries` show how many assistant entries contributed to each category.

Cost fields are conservative. `known_cost_usd` is the sum of assistant entries whose model pricing was known. `total_cost_usd` is emitted only when `cost_complete:true`, meaning every exact billable usage entry had known pricing. If any exact billable entry lacks pricing, `cost_complete:false`, `unknown_cost_entries` is greater than zero, and clients should treat `known_cost_usd` as a partial subtotal.

Model catalog and switching:

```json
{"id":"3c","type":"list_models"}
{"id":"3d","type":"set_model","provider":"anthropic","model":"claude-sonnet-4-5"}
{"id":"3e","type":"cycle_model"}
```

`list_models` returns the configured effective model catalog with local `models.json` overrides taking precedence over built-ins. The response includes `default_provider`, `default_model`, `current_provider`, `current_model`, and `models`, where each model includes `provider`, `model`, `display_name`, `family`, `api_family`, `registered`, `selectable`, capability metadata, modality arrays, and `selected` for the active model. `current_provider`/`current_model` are authoritative; when a session restores a removed model, AVA includes a synthetic selected model entry with `selectable:false`. `set_model` switches to a configured selectable model, appends a durable `model_change` session entry only when the provider/model actually changes, reloads provider/model-specific prompt context, and returns the same state shape as `get_state`. If `provider` is omitted, AVA first tries the current provider and then accepts the model id only when it is unique across registered providers. `cycle_model` advances to the next configured selectable model, appends `model_change` when the selection changes, reloads prompt context, and returns the state shape. Model switching commands are rejected while a prompt or RPC compaction is active.

List sessions:

```json
{"id":"4","type":"list_sessions"}
```

Returns `sessions`, an array of `{session_id,path,last_updated,entry_count}` for the current workspace.

Open a session by id or unambiguous prefix:

```json
{"id":"5","type":"open_session","session_id":"session-prefix"}
{"id":"5b","type":"switch_session","session_id":"session-prefix"}
```

Switches the active runtime session and returns the same state shape as `get_state`. Use `switch_session` for new integrations; `open_session` remains supported as a compatibility alias. Session-switching commands are rejected while a prompt is active.

New session:

```json
{"id":"5c","type":"new_session"}
```

Creates a new session for the current workspace/current directory, makes it active, clears the cancel flag, and returns the same state shape as `get_state`. `new_session` is rejected while a prompt is active.

Backend slash-command equivalents:

```json
{"id":"6","type":"compact","instructions":"optional notes"}
{"id":"7","type":"export"}
{"id":"8","type":"context"}
```

These dispatch `/compact`, `/export`, and `/context` through the shared backend command dispatcher. Responses include `handled`, `quit`, `output` (array of strings), and `text` (joined output).

`compact` requires credentials for the active session provider and asks that provider to generate the compaction summary before appending a compaction boundary. It returns `success:false` for missing auth, provider summary failure, empty or oversized summaries, stale-session append failures, or active-run conflicts. On success, the recorded compaction entry contains summary metadata such as trigger, estimated tokens, threshold, retained recent context, and keep-recent settings.

Unknown command types return an error response and do not terminate the RPC loop.

### Resolver Requests And Replies

When an active prompt reaches a backend permission prompt, RPC emits a resolver event:

```json
{"schema_version":1,"name":"permission_requested","type":"permission_requested","request_id":"prompt_req","correlation_id":"prompt_req","payload":{"resolver_request_id":"permission_...","operation":"read","mode":"build","target_path":"/workspace/file","command":"","tool_name":"read_file","reason":"..."}}
```

Permission `operation` values are backend policy categories: `read`, `search`, `edit`, `bash`, `network.fetch`, and `lsp.query`. Network fetch prompts use an empty `target_path` and carry the URL in `command`:

```json
{"schema_version":1,"name":"permission_requested","type":"permission_requested","request_id":"prompt_req","correlation_id":"prompt_req","payload":{"resolver_request_id":"permission_...","operation":"network.fetch","mode":"build","target_path":"","command":"https://example.com/page","tool_name":"webfetch","reason":"network fetch requires explicit approval"}}
```

The event top-level `request_id` remains the prompt command id. The client must answer with `payload.resolver_request_id` and the prompt `correlation_id`:

```json
{"id":"perm_reply_1","type":"permission_reply","request_id":"permission_...","correlation_id":"prompt_req","decision":"allow"}
{"id":"perm_reply_2","type":"permission_reply","request_id":"permission_...","correlation_id":"prompt_req","decision":"deny"}
```

`decision` must be exactly `allow` or `deny`. Missing, unknown, or wrong-correlation resolver ids return in-band errors. Cancellation unblocks pending permission requests fail-closed.

When an active prompt reaches the `question` tool, RPC emits:

```json
{"schema_version":1,"name":"question_requested","type":"question_requested","request_id":"prompt_req","correlation_id":"prompt_req","payload":{"resolver_request_id":"question_...","header":"Choose","question":"Continue?","options":[{"value":"yes","label":"Yes"}],"multiple":false,"allow_custom":true}}
```

The client may answer with custom text when `allow_custom` is true, or one valid selected option value. Multi-select question prompts are not supported by RPC protocol version 1; AVA emits no resolver request for them and returns a failed `question` tool result to the active prompt.

```json
{"id":"question_reply_1","type":"question_reply","request_id":"question_...","correlation_id":"prompt_req","answer":"text"}
{"id":"question_reply_2","type":"question_reply","request_id":"question_...","correlation_id":"prompt_req","selected":"yes"}
```

Missing, unknown, or wrong-correlation resolver ids; replies without exactly one of `answer` or `selected`; custom answers when `allow_custom` is false; and unknown `selected` values return in-band errors. Cancellation unblocks pending question requests with a canceled error.

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

RPC notifications may reuse the event envelopes above. Request ids are client-owned strings and are echoed verbatim after validation.

## Permission And Question Behavior

Headless operation is fail-closed by default:

- Permission decisions that require user approval fail unless headless policy supplies `--allow read-only`/`--allow-tool` for a supported read/search tool, `--allow-tool webfetch` for exact `network.fetch` webfetch prompts, or RPC mode receives an explicit `permission_reply` for the active resolver request.
- The `question` tool fails with an unavailable interaction error unless RPC mode receives an explicit `question_reply` for the active resolver request.
- Destructive operations remain behind existing backend permission policy checks.

## Server Mode Deferral

No socket, daemon, HTTP server, or background service semantics are defined in this phase. Server mode must not be implemented by inferring behavior from `print` or `rpc`; it requires a separate contract review.
