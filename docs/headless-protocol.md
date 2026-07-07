# AVA Headless Protocol

This document defines the current backend contract for AVA headless modes. AVA supports one-shot print mode and a JSONL RPC MVP over stdin/stdout; provider streaming is exposed through runtime event envelopes. Server mode remains deferred.

## Modes

- `interactive`: current TUI or non-TTY line shell behavior. Human prompts are allowed. Piped stdin without `--print` remains the line shell for slash-command scripts.
- `print`: one prompt in, text or JSONL events out, process exits after the turn. Enable with `ava --print "prompt"`, `ava -p "prompt"`, `ava --mode text "prompt"`, `ava --mode json "prompt"`, or `printf 'prompt' | ava --print`.
- `rpc`: JSONL request/response envelopes over stdio for long-lived automation. Enable with `ava --rpc`, `ava --output rpc`, or `ava --mode rpc`.
- `server` (deferred): no contract yet. Server mode is explicitly out of scope until the stdio RPC contract is proven.

## Session Selection

AVA creates a resumable append-only session by default for interactive, print, and RPC modes. `--continue` resumes the newest persisted session for the current workspace, and Pi-compatible `--resume`/`-r` are aliases. `--session <id-or-prefix>` resumes an exact session id or unique prefix, and Pi-compatible `--session-id <id-or-prefix>` is an alias. `--fork <id-or-prefix>` creates a new persisted branch from an exact session id or unique prefix, copies the selected source history, records fork metadata, and can be combined with `--name <name>` to set the new branch display name. `--name <name>` on a non-fork startup records session metadata for the opened session. `--session-dir <dir>` selects the session storage directory for this process before resume, fork, continue, or new-session creation.

`--session`/`--session-id`, `--continue`/`--resume`/`-r`, and `--fork` are mutually exclusive. `--no-session` starts an ephemeral session for the current process. Runtime entries remain available to in-process commands such as `get_messages`, `/stats`, or `/export`, but AVA does not write a resumable JSONL history file under the configured sessions directory and the session is not returned by session listing. Temporary attachment and spill files may exist only while the process/session store is alive. `--no-session` cannot be combined with `--session`/`--session-id`, `--continue`/`--resume`/`-r`, or `--fork`. RPC `get_state` includes `sessionless:true` when this mode is active.

## Startup Reasoning

`--thinking off|<level>` is a Pi-compatible startup alias for AVA's existing reasoning control. `off` clears the active explicit reasoning selection. Any other value is validated against the active model's declared `reasoning_levels` and provider/API-family rules during startup; for the default GPT-5.5 profile this means `low`, `medium`, `high`, or `xhigh`. Unsupported levels fail with an error that includes `option: --thinking` and the supported levels. The flag changes only AVA's runtime reasoning selection and does not add provider-specific request semantics beyond those already used by RPC `set_reasoning`.

## Offline Mode

`--offline` can be combined with interactive, print, or RPC startup. It sets the session offline flag and disables provider model calls for prompt turns and provider-backed compaction before credential resolution. Print prompt turns fail closed with `permission_denied`, action `prompt` or `compact`, message `offline mode is enabled; provider model calls are disabled`, and hint `rerun without --offline to send prompts to the provider`. RPC provider-backed requests return in-band `success:false` while local non-provider commands remain available. `--offline` does not grant tool permissions and is not an OS/network sandbox; network-capable tools remain controlled by tool visibility plus permission policy.

## Print Input And Output

Print mode accepts an optional prompt argument after `--print`/`-p`. If no prompt argument is supplied and stdin is not a terminal, stdin is used as the prompt. If both a prompt argument and piped stdin are supplied, AVA merges them deterministically as:

```text
<explicit prompt>

<stdin content>
```

Text output is the default. In text output, stdout contains the final assistant text only; diagnostics, tool progress, and errors go to stderr. When stdout or stderr is a TTY, AVA sanitizes terminal control bytes before writing model/tool text to that stream; non-TTY pipes keep raw text for automation. JSONL output is selected with `--json`, `--output json`, or Pi-compatible `--mode json`; stdout contains serialized event envelopes and ends with `done`, `canceled`, or `error` for turns that reach the runtime. Credentials and startup failures are reported on stderr.

## Tool Visibility Flags

Tool visibility flags filter which native model-visible tool names are exported to the provider and accepted by the dispatcher for the current process. They apply to interactive, print, and RPC sessions:

- `--tools name[,name...]` or `-t name[,name...]`: allowlist tool names.
- `--exclude-tools name[,name...]` or `-xt name[,name...]`: remove tool names. Exclusion overrides allowlisting.
- `--no-builtin-tools` or `-nbt`: hide AVA built-ins while leaving enabled plugin and MCP tools available unless other filters remove them.
- `--no-tools` or `-nt`: hide all tools unless `--tools` explicitly re-enables exact names.

These flags are not permission grants. Headless permission flags such as `--allow-tool read_file` only answer compatible backend permission prompts for tools that remain visible and callable.

AVA accepts Pi built-in names as input aliases for visibility filters: `read` maps to `read_file`, `write` to `write_file`, `edit` to `edit_file`, `find` to `glob`, and `ls` to `list_directory`. Provider schemas remain AVA-native, so `--tools read,grep,find,ls` advertises `read_file`, `grep`, `glob`, and `list_directory`.

## Headless Permission Flags

Headless modes are fail-closed by default for backend permission decisions whose action is `ask`. Print mode installs an explicit deny resolver when no policy flag is provided. RPC mode emits a `permission_requested` event for ask prompts unless a supplied headless policy auto-allows the request. Permission decisions whose action is already `allow` proceed without a resolver, and decisions whose action is already `deny` are never upgraded by these flags.

Supported policy flags:

- `--allow read-only`: allows read/search-style permission prompts (`read_file`, `list_directory`, `glob`, and `grep` shapes) when the backend asks. Network, write, edit, patch, bash, and question prompts remain denied.
- `--allow-tool glob,grep,list_directory,read_file,skill,task,webfetch,websearch,mcp,plugin`: allows only the listed exact tool families when they produce compatible ask prompts. Supported values are `glob`, `grep`, `list_directory`, `read_file`, `skill`, `task`, `webfetch`, `websearch`, `mcp`, and `plugin`; unsupported values such as `bash`, `write_file`, `edit_file`, `apply_patch`, `question`, or arbitrary strings are rejected as usage errors. `skill` only auto-allows exact `skill` prompts from the `skill` tool, `task` only auto-allows exact `task`/`TaskRun` prompts from the `task` tool, `webfetch` only auto-allows exact `network.fetch` prompts produced by the `webfetch` tool, `websearch` only auto-allows exact `network.search` prompts produced by the `websearch` tool, `mcp` auto-allows MCP server launch/connect plus `mcp.tool.call` and `mcp.resource.read` prompts for MCP-prefixed tools, and `plugin` auto-allows exact plugin model tool launch/call prompts plus exact plugin command launch/run prompts. `plugin` does not auto-allow plugin proxy file/search/shell/network prompts or passive plugin event hook launch/observe prompts. `--allow read-only` never allows skill, task, network, MCP, or plugin prompts.

Examples:

```sh
ava --print "summarize the repo" --allow read-only
ava --print "inspect this file" --allow-tool read_file
ava --print "find symbols" --allow-tool glob,grep,list_directory
ava --print "load the relevant skill" --allow-tool skill
ava --print "delegate focused exploration" --allow-tool task
ava --print "fetch release notes" --allow-tool webfetch
ava --print "search current release notes" --allow-tool websearch
ava --print "use configured MCP context" --allow-tool mcp
ava --print "use configured plugin command" --allow-tool plugin
ava --rpc --allow read-only
```

Invalid permission flag values exit with code `2` and write a usage error to stderr before provider/auth startup. In RPC mode, matching read/search, exact `skill`, exact `task`, exact `webfetch`/`websearch` network prompts, MCP prompts covered by `--allow-tool mcp`, or supported plugin prompts covered by `--allow-tool plugin` are auto-allowed before `permission_requested`; non-matching ask prompts still require an explicit `permission_reply` unless a persistent permission rule matches. RPC clients may reply with `allow_session` to create an in-memory exact-match grant for the current RPC process. Those grants are inspectable, revocable, and clearable through RPC commands; they are not persisted across AVA restarts. Persistent rules are managed only by `permission_rule_add`/`permission_rule_remove` and are stored outside the model-writable workspace path.

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

Runtime provider calls resolve credentials for the active session provider. OpenAI keeps its existing stored OAuth/API-key behavior and falls back to `OPENAI_API_KEY` when no stored OpenAI credential exists. Non-OpenAI connect setup stores provider-scoped API keys in `auth.json`, for example `{"anthropic":{"type":"api_key","api_key":"..."}}`, and can also read provider API-key environment variables such as `ANTHROPIC_API_KEY`, `KIMI_API_KEY`, `MOONSHOT_API_KEY`, and `OPENROUTER_API_KEY`. Anthropic can additionally read Claude OAuth bearer tokens from `ANTHROPIC_OAUTH_TOKEN` or the Anthropic SDK-compatible `ANTHROPIC_AUTH_TOKEN`; when no stored Anthropic credential exists, precedence is `ANTHROPIC_OAUTH_TOKEN`, then `ANTHROPIC_AUTH_TOKEN`, then `ANTHROPIC_API_KEY`. Stored Anthropic Claude OAuth entries use `{"anthropic":{"type":"oauth","access_token":"...","refresh_token":"...","expires_at":1893456000}}` with optional `account_id`/`source` metadata. AVA writes canonical `refresh_token`/`expires_at` fields and accepts `refresh`/`expires` aliases when reading manually-created files. Interactive setup is available with `ava login [provider]`, `ava auth login [provider]`, `ava connect [provider]`, and TUI `/connect`; omitting the provider opens a searchable provider picker, and TUI `/connect` uses a modal. OpenAI interactive setup offers browser OAuth, headless OAuth, and API key methods. Secrets are read with terminal echo disabled where possible and masked in TUI prompts. Headless setup can store credentials without a browser or TTY, for example `ava connect openai --headless-oauth`, `printf '%s\n' "$ANTHROPIC_API_KEY" | ava connect anthropic --api-key-stdin`, or `ava connect moonshot --api-key-env MOONSHOT_API_KEY`. AVA only reads credential files when they are owned by the current user and not readable by group/other users; manually-created files should use owner-only permissions such as `chmod 600 ~/.config/ava/auth.json`. OpenAI and Anthropic OAuth credentials refresh automatically before use when a refresh token is present; expired/near-expiry stored Anthropic OAuth without a refresh token fails closed and requires updating or removing the stored Anthropic entry before environment credentials are considered.

The direct runtime set also recognizes `DEEPSEEK_API_KEY` and `GEMINI_API_KEY`, and headless API-key setup can store Gemini credentials with `ava connect gemini --api-key-env GEMINI_API_KEY`.

## Provider Retry And Idempotency

Provider transport retries are bounded and provider-neutral. AVA retries transport I/O failures, HTTP rate limits, and transient HTTP responses before any streaming body chunks are delivered. It parses `Retry-After` when present, emits `retry` and `retry_tick` events for visible backoff, and checks cancellation during retry waits.

Retry policy by request class:

- OpenAI, Anthropic, DeepSeek, Gemini, Kimi, Moonshot, and OpenRouter prompt requests are best-effort retries. AVA does not currently attach provider-specific idempotency keys, so a provider could process a request even if the client later sees a transport failure.
- Streaming prompt retries stop after the first response chunk is delivered. Once text, reasoning, tool-call, or provider events have begun, AVA treats the stream as non-replayable and surfaces later failures instead of silently retrying a partial turn.
- Non-streaming prompt retries may retry transient/rate-limited failures because no response body has been accepted yet. They are still best-effort because provider-side deduplication is not guaranteed.
- Context-overflow repair is separate from transport retry. AVA may perform one bounded compaction/retry path when the provider error is classified as context overflow.

Headless clients should treat repeated prompt attempts as possible duplicate provider requests unless a future protocol version exposes provider-specific idempotency metadata.

## Event Envelope

Headless JSON event records are newline-delimited envelopes emitted by the shared runtime event bus:

```json
{"schema_version":1,"event_id":"event_...","timestamp":"2026-04-30T00:00:00Z","session_id":"session_...","name":"assistant_message","type":"assistant_message","payload":{"text":"hello"},"text":"hello"}
```

AVA's backend/frontend content boundary is semantic. Backend producers emit event names, lifecycle/status metadata, tool/permission/question payload fields, and Markdown or plain text. They do not emit terminal layout instructions, cards, panels, borders, columns, ncurses attributes, or ANSI escape codes as the internal styling model. Frontend adapters convert Markdown/plain text into `ava::tui::Text`, a layout-free sequence of `TextRun` values with explicit `NewLine` runs and styled `TextSpan` runs. The TUI renderer owns wrapping, scrolling, layout, terminal sanitization, SGR conversion, and ncurses drawing.

Envelope fields:

- `schema_version`: integer schema version. The current value is `1`.
- `event_id`: AVA-owned unique id for this emitted event record.
- `timestamp`: event timestamp when available.
- `session_id`: active runtime session id when available.
- `run_id`, `turn_id`, `message_id`, `request_id`, `correlation_id`: optional correlation metadata. RPC prompt and command events include `request_id` for the client request that caused the event.
- `name`: event name.
- `payload_type`: optional stable payload family for typed consumers. Runtime events use values such as `session`, `message`, `reasoning`, `provider`, `tool`, `compaction`, `retry`, `cancellation`, `error`, and `completion`; resolver/queue events may use `permission`, `question`, or `queue`.
- `payload`: event-specific object. Payloads are intentionally minimal and must not require clients to parse session JSONL internals.
- `type` and documented runtime fields such as `text`, `tool`, `status`, `category`, `message`, `stop_reason`, `trigger`, `reason`, and small counters: compatibility aliases for the pre-envelope flat event shape. New clients should prefer `name` and `payload`; unknown future payload fields are not automatically promoted to the top level.

Current event names:

- `session_start`: runtime session metadata with `mode`, `provider`, and `model`. Persisted JSONL `session_start` entries record prompt override and loaded context-source metadata; detailed base prompt source path, byte count, and fingerprint are exposed by `/context`, not this event.
- `user_message`: accepted user input for a turn.
- `message_update`: live assistant text delta emitted while a streaming provider response is in progress; includes `text` and `status`.
- `message_end`: live provider stream completion marker; includes `status`.
- `provider_event`: live non-text provider stream event such as tool-call argument deltas or provider stream errors; includes `status`, and may include `call_id`, `tool`, `text`, or `message` depending on the provider event.
- `reasoning_start`, `reasoning_delta`, `reasoning_end`: provider-neutral reasoning lifecycle events for models that expose visible thinking/reasoning. Payloads may include bounded `text` deltas, `reasoning_format`, `reasoning_redacted`, and `reasoning_signature_present`; they never expose raw provider verification signatures or opaque redacted-thinking payloads.
- `assistant_message`: final assistant text for a completed turn.
- `tool_start`: tool call began; includes `call_id`, `tool`, a safe argument summary when available, and may include structured `args` or fallback `args_json`.
- `tool_progress`: tool progress or partial result update; includes `call_id`, `tool`, `status`, bounded `text` when available, and may include partial `result` or fallback `result_json`.
- `tool_result`: tool call completed; includes `call_id`, `tool`, `status`, a safe result summary when available, and may include `args`, `result`, `structured_result`, `diff`, `changed_paths`, `permission_request_ids`, `diff_truncated`, output truncation/spill fields, and match/byte/line counters. `structured_result` is the canonical semantic tool result when present. It has `schema_version:1`, `call_id`, `tool`, `status` (`success`, `error`, or `canceled`), `ok`, `content_type`, optional `content`, optional `error`, optional `permission_request_ids`, and optional diff/path/truncation/spill counters. Flat `status` remains a compatibility field and mirrors the tool timeline lifecycle.
- `compaction_start`, `compaction_end`: provider-backed compaction lifecycle events. Payloads include `trigger`, `attempt`, `max_attempts`, and known token/summary byte counters when available.
- `retry`: bounded backend retry lifecycle event. Payloads include `attempt`, `max_attempts`, and `delay_ms` when the backend retry path has those values. Provider transport retries use `trigger:"provider_transport"` with provider-neutral reasons such as `rate_limited`, `transient`, or `transport_io`; context-overflow retries use `reason:"context_overflow"`; stale compaction snapshot retries use `reason:"stale_compaction_snapshot"`.
- `retry_tick`: backend retry countdown tick emitted while a bounded retry delay is sleeping. Payloads include `remaining_ms` plus the same retry correlation metadata where available. Clients should treat it as status/progress, not a final failure.
- `cancel_requested`: RPC cancel request was accepted. Payload includes `active_run`, `cleared_steer`, `cleared_follow_up`, and the active prompt request id when one exists.
- `permission_grant_revoked`, `permission_grants_cleared`: RPC session-grant lifecycle events. Payloads include the revoked grant or the number of grants cleared.
- `permission_rule_added`, `permission_rule_removed`: persistent permission rule lifecycle events emitted after successful rule changes. Payloads include the affected `rule` and, for removal, `removed:true`.
- `canceled`: terminal event for a cooperatively canceled runtime turn.
- `error`: runtime, provider, or tool boundary failure; includes `category`, `message`, and optional `details`.
- `done`: terminal event for a successful turn; includes `stop_reason` and small counters when available.
- `steer_queued`, `steer_applied`, `steer_skipped`: RPC steering queue lifecycle events. Payloads include `message` and skipped events include `reason`.
- `follow_up_queued`, `follow_up_started`, `follow_up_skipped`: RPC follow-up queue lifecycle events. Payloads include `message` and skipped events include `reason`.

Enabled plugin event hooks observe the same runtime event envelopes internally using canonical event names such as `tool_result`. Hook execution is best-effort and does not add new RPC envelope fields or make the originating prompt/command fail when a hook fails.

## RPC JSONL MVP

RPC mode reads strict LF-delimited JSON objects from stdin and writes LF-delimited JSON protocol records to stdout. Diagnostics and fatal startup/read/write errors go to stderr. AVA keeps reading after malformed request lines when it can emit a protocol error response; those recoverable request errors are reported in-band rather than through the process exit code.

RPC requests may include `"protocol_version":1`. Omitting the field keeps current-version behavior. Present values must be JSON integers. Unsupported or malformed protocol versions produce an in-band error response and do not terminate the loop.

Request ids are client-owned non-empty strings capped at 256 bytes. Resolver `request_id` and `correlation_id` fields are also capped at 256 bytes; over-limit identifiers produce an in-band `success:false` response when a response id can be parsed, or `"id":""` for malformed/no-id records. Identifiers reject ASCII control bytes, spaces, and shell/metasyntax characters such as quotes, backslashes, dollar signs, ampersands, pipes, semicolons, angle brackets, parentheses, and brackets. Permission reply `reason` text is optional, capped at 1024 bytes, and rejects control bytes before the resolver event is emitted. Successful command responses use:

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

Returns `protocol_version`, `supported_protocol_versions`, `session_entry_version`, and `supported_session_entry_versions`. RPC protocol v1 currently writes session entry version `3` and reads legacy session entry versions `1`, `2`, and missing-version entries. Missing-version legacy records are exposed as version `0` in RPC responses.

Prompt turn:

```json
{"id":"1","type":"prompt","message":"hello"}
```

Prompt requests may also include image inputs. `attachments` is an array of non-empty local image paths:

```json
{"id":"1-img","type":"prompt","message":"describe this","attachments":["screens/screen.png"]}
```

`images` accepts Pi-style inline upload objects:

```json
{"id":"1-upload","type":"prompt","message":"describe this","images":[{"type":"image","data":"iVBORw0KGgo=","mimeType":"image/png"}]}
```

Attachment paths are resolved relative to the runtime current directory unless absolute. Inline image `data` must be standard base64, and `mimeType` must match the detected image bytes. AVA imports each supported PNG, JPEG, WebP, or GIF file or upload into the active session's attachment storage before the turn starts, rejects symlink sources, unsupported byte signatures, non-regular files, MIME mismatches, and images outside the bounded byte size, persists metadata only, and sends verified bytes to image-capable providers. Local `attachments` and inline `images` share the 16-image prompt cap.

AVA starts the prompt turn asynchronously inside the RPC session, keeps reading stdin while it runs, streams normal runtime event envelopes (`session_start`, `user_message`, streaming `message_update`/`message_end`/`provider_event`, final `assistant_message`, tool events, compaction/retry events, retry countdown ticks, and `done`/`canceled`/`error`) to stdout, then writes exactly one RPC response with `final_text`, `stop_reason`, `provider_iterations`, `tool_calls`, optional `tool_timeline`, and `session_id` on success or `success:false` on failure. `tool_timeline[]` entries include `status`, `call_id`, `tool`, optional presentation text/summaries, optional `args`/`result`, optional `structured_result`, content/error fields, changed paths, permission request ids, and truncation/spill/diff counters using the same names as tool event payloads. Failures before runtime startup, such as missing auth, invalid attachment import, or unsupported image input for the selected model, may return only the `success:false` RPC response. Prompt event envelopes include the prompt command id as `request_id`. Only one prompt may be active per RPC session; a second `prompt` while one is active returns an in-band error. Prompt requests require credentials for the active session provider unless the embedding test harness supplies runtime credentials.

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

Sets the RPC session cancel flag, emits `cancel_requested`, and returns `{"cancel_requested":true,"active_run":true|false,"cleared_steer":N,"cleared_follow_up":N}`. When a prompt is active, pending permission/question resolver waits are unblocked fail-closed, queued steering/follow-up messages are cleared, skipped queue events are emitted, the cancel command response is written, and then queued follow-up commands receive `success:false` canceled responses. The active prompt emits terminal `canceled` when the runtime observes the cancellation. Idle or previous cancellation is not a future-turn latch; AVA clears a stale cancel flag when it accepts a new prompt/active run. RPC clients must correlate responses by `id`: active prompt terminal events or the active prompt response may interleave with the cancel response because cancellation is observed by the prompt worker at cooperative runtime boundaries. Cancellation is cooperative: AVA cancels retry waits promptly and may interrupt local transport/process work where the backend supports polling a cancel callback, but it does not guarantee provider-side request abort or exactly-once provider semantics after a request has been sent. If RPC stdin reaches EOF while a prompt worker exists, AVA marks input closed, requests cancellation, clears queued steering/follow-up messages, cancels pending resolvers, and prevents queued follow-ups from starting after client disconnect.

State:

```json
{"id":"3","type":"get_state"}
```

Returns the active protocol version, session id/path, mode, provider/model, workspace/current directory, cancel flag, current reasoning selection, and loaded context source summary. Reasoning fields are `reasoning_enabled` plus optional `reasoning_level`, `reasoning_budget_tokens`, and `reasoning_display` when enabled.

Example state response shape:

```json
{"id":"3","type":"response","success":true,"result":{"protocol_version":1,"session_id":"session_...","provider":"anthropic","model":"claude-sonnet-4-5","reasoning_enabled":true,"reasoning_level":"enabled","reasoning_budget_tokens":4096,"reasoning_display":"summarized"}}
```

`get_state`, `list_models`, `list_sessions`, `session_tree`, `session_metadata`, `permission_grants`, `permission_grant_revoke`, and `permission_grants_clear` remain available while a prompt is active. `get_messages`, `get_session_stats`, `validate_session`, `permission_rules`, `permission_rule_add`, `permission_rule_remove`, `set_session_name`, `set_session_labels`, `fork_session`, `clone_session`, `summarize_branch`, `set_model`, `cycle_model`, `set_reasoning`, `clear_reasoning`, `compact`, `export`, `export_html`, and `context` materialize or mutate session history or durable policy and are rejected while a prompt is active. RPC `/compact` also marks the session busy while it generates and records the provider summary, so a prompt cannot start midway through compaction.

Messages:

```json
{"id":"3a","type":"get_messages"}
```

Returns durable message-like session entries for the active session in append order. The current response is `{session_id,messages,truncated,message_count}` where each message includes `version`, `id`, `parent_id`, `type`, `timestamp`, and object-shaped `data` unless the individual entry is too large, in which case the entry is marked `truncated`. Responses are capped to protect headless clients and the AVA process. Reasoning entries are sanitized: visible non-redacted text may be returned, but raw provider signatures and opaque redacted-thinking payloads are replaced by safe status fields such as `signature_present` and `redacted`. User image attachment metadata is also sanitized: `data.attachments[]` is user-message-only and contains only `id`, `type:"image"`, `mime_type`, `byte_size`, `sha256`, `storage_path`, and optional `redacted`; raw image bytes and unknown attachment fields are never returned. This intentionally excludes non-message bookkeeping entries such as `session_start`, `model_change`, `compaction`, and permission audit rows. Internal replay user messages inserted after context compaction are also hidden because they are provider-context repair entries, not user-visible transcript turns.

Image attachment metadata is the backend-safe replay contract for image input. Valid metadata requires canonical unescaped object keys, `mime_type` to be one of `image/png`, `image/jpeg`, `image/webp`, or `image/gif`, `sha256` to be a 64-character hex digest, `byte_size` to be an unquoted base-10 JSON integer literal from `1` through `20971520` with no fraction or exponent notation, and `storage_path` to be a relative `attachments/...` path without absolute roots, `..`, backslashes, drive prefixes, or empty path segments. Inline image data fields such as base64 payloads are invalid in persisted session metadata. AVA reconstructs provider-neutral image content parts for replay, loads bytes only from the active session's `<session_id>.attachments` storage after path, symlink, byte-size, and SHA-256 checks, and serializes verified image payloads for OpenAI Responses, OpenAI-compatible chat-completions, and Anthropic Messages. Provider requests are capped at 16 images and 40 MiB total image bytes before base64 expansion; Anthropic image parts are additionally capped at 5 MiB each. RPC prompt `attachments` is a local file import field, RPC prompt `images` is an inline upload import field, and `get_messages` `data.attachments[]` is persisted metadata, not an inline upload field.

Session stats:

```json
{"id":"3b","type":"get_session_stats"}
```

Returns `session_id`, `session_path`, `entry_count`, first/last timestamps, usage/cost totals when known, and counts for session entry types including `session_metadata` and `branch_summary`. User-message counts exclude internal replay entries. Session JSONL entry types are additive; clients that inspect session files directly should ignore unknown non-message bookkeeping entries and prefer RPC `get_messages`/`get_session_stats` for stable automation data. AVA writes session entry version `3` for all new entries and continues to read legacy versions `1`, `2`, and missing-version entries under RPC protocol v1. Missing-version legacy entries are surfaced as version `0`.

Usage fields are additive and appear only when present in saved assistant entries: `input_tokens`, `output_tokens`, `reasoning_tokens`, `cache_read_tokens`, `cache_write_tokens`, and `total_tokens`. Exact provider token totals are kept separate from byte-count fallback estimates: `estimated_input_bytes`, `estimated_output_bytes`, and `estimated_total_bytes` report fallback byte estimates when provider usage was unavailable. `exact_usage_entries` and `estimated_usage_entries` show how many assistant entries contributed to each category.

Cost fields are conservative. `known_cost_usd` is the sum of assistant entries whose model pricing was known. `total_cost_usd` is emitted only when `cost_complete:true`, meaning every exact billable usage entry had known pricing. If any exact billable entry lacks pricing, `cost_complete:false`, `unknown_cost_entries` is greater than zero, and clients should treat `known_cost_usd` as a partial subtotal.

Session validation:

```json
{"id":"3b2","type":"validate_session"}
```

Runs the backend replay validator over the active session and returns `{session_id,session_path,ok,error_count,warning_count,issues}`. Issues include stable `kind` strings, severity, entry index, entry id when available, tool call id when relevant, and a short diagnostic message. The validator currently checks entry versions, entry ids, parent links, tool call/result pairing, permission prompt/resolution pairing, structured tool results when required by callers, compaction integrity, durable model/reasoning entry shape, and image attachment metadata shape. Compaction validation requires a durable summary and reports compaction boundaries that occur while tool calls or permission prompts are unresolved.

Model catalog and switching:

```json
{"id":"3c","type":"list_models"}
{"id":"3d","type":"set_model","provider":"anthropic","model":"claude-sonnet-4-5"}
{"id":"3e","type":"cycle_model"}
{"id":"3f","type":"set_reasoning","reasoning_level":"low"}
{"id":"3g","type":"clear_reasoning"}
```

`list_models` returns the configured effective model catalog with local `models.json` overrides taking precedence over built-ins. The response includes `default_provider`, `default_model`, `current_provider`, `current_model`, and `models`, where each model includes `provider`, `model`, `display_name`, `family`, `api_family`, `registered`, `selectable`, capability metadata, modality arrays, and `selected` for the active model. `current_provider`/`current_model` are authoritative; when a session restores a removed model, AVA includes a synthetic selected model entry with `selectable:false`. `set_model` switches to a configured selectable model, appends a durable `model_change` session entry only when the provider/model actually changes, reloads provider/model-specific prompt context, and returns the same state shape as `get_state`. If `provider` is omitted, AVA first tries the current provider and then accepts the model id only when it is unique across registered providers. `cycle_model` advances to the next configured selectable model, appends `model_change` when the selection changes, reloads prompt context, and returns the state shape. Model switching commands are rejected while a prompt or RPC compaction is active.

`set_reasoning` validates a reasoning selection against the active model metadata, appends a durable `reasoning_change` entry when it changes, and returns the same state shape as `get_state`. `reasoning_level` is required and must appear in the selected model's `reasoning_levels`. `reasoning_budget_tokens` and `reasoning_display` are accepted only where the selected model's `api_family` permits them: `openai_responses` accepts level/effort only; `openai_chat_completions` accepts level only and uses `clear_reasoning` rather than a `disabled` level; `anthropic_messages` accepts `enabled` with `budget_tokens >= 1024` and below `max_output_tokens` (or AVA's 4096-token provider default when no model limit is declared), accepts `display` values `summarized` or `omitted`, and accepts `adaptive` only when a model profile explicitly lists that level and no budget is supplied. The built-in Claude Sonnet 4.5 profile currently exposes `enabled` only. `clear_reasoning` appends a disabled `reasoning_change` entry when reasoning was enabled and returns the updated state; it omits explicit provider reasoning controls on future requests so the provider/model default applies. Model switches clear the active in-memory reasoning selection; restored sessions recover only the latest valid `reasoning_change` for the restored provider/model after the most recent session-start/model-change boundary.

Unsupported reasoning selections fail before a provider request is built:

```json
{"id":"bad_reasoning","type":"set_reasoning","reasoning_level":"enabled","reasoning_budget_tokens":4096}
{"id":"bad_reasoning","type":"response","success":false,"error":{"category":"invalid_argument","message":"Kimi reasoning supports level only"}}
```

The provider-native MVP adds compatibility rules to model switching: clients should only offer reasoning controls for models that declare support, and AVA rejects provider switches that cannot safely replay the existing conversation history instead of silently dropping tool, reasoning, or image context. A switch is rejected before any `model_change` entry is appended when replayed tool-call history would target a model without explicit tool support, when provider-native reasoning blocks use a format the target provider/model cannot replay, or when the active replay window contains non-redacted image attachments the target cannot replay. Image replay compatibility requires `image` in `input_modalities` and applies the same replay caps used before provider requests: at most 16 images, at most 40 MiB aggregate image bytes, and Anthropic's lower 5 MiB per-image limit for Anthropic Messages targets. Compaction/redaction is required before switching when active image history exceeds the target provider's replay limits. Compatibility is checked only for the active replay window after the latest compaction entry. Anthropic `anthropic_thinking` blocks require an Anthropic Messages model. OpenAI-compatible `reasoning_content` replay requires matching model metadata plus the `preserve_reasoning_content` compatibility quirk.

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

Session metadata, tree, fork, clone, and branch summaries:

```json
{"id":"5d","type":"session_metadata"}
{"id":"5e","type":"set_session_name","session_name":"Auth follow-up"}
{"id":"5f","type":"set_session_labels","labels":["auth","bug"]}
{"id":"5g","type":"session_tree"}
{"id":"5h","type":"fork_session","session_id":"session_source","branch_from_entry_id":"entry_...","session_name":"Experiment","labels":["branch"]}
{"id":"5i","type":"clone_session","session_id":"session_source","session_name":"Copy"}
{"id":"5j","type":"summarize_branch","session_id":"session_source","branch_root_entry_id":"entry_root","branch_tip_entry_id":"entry_tip","summary":"Branch explored X and was abandoned because Y.","provider":"openai","model":"gpt-test","reason":"manual review"}
```

`session_metadata` returns the current session's append-only metadata view: `session_id`, `name`, `labels`, `labels_updated`, `archived`, `parent_session_id`, `source_session_id`, `branch_from_entry_id`, `branch_origin`, and `actor`. `labels_updated` is the timestamp of the most recent metadata entry that changed labels, or an empty string when labels have not been set. `set_session_name` and `set_session_labels` append `session_metadata` entries and return the same metadata shape. Names are capped at 256 bytes; labels are unique non-empty strings, capped at 32 labels and 64 bytes per label. Metadata writes are rejected while a prompt is active.

`session_tree` returns `{current_session_id,roots,leaves,path,sessions}` for the current workspace. Each session node includes summary fields, metadata fields including `labels_updated`, `archived`, `actor`, `children`, `leaf`, and `current`. Parent metadata cycles are cut for tree output: affected nodes remain visible as usable roots/leaves instead of making `roots`, `leaves`, or `path` unusable.

`fork_session` creates a new session by copying the source through `branch_from_entry_id`, appends provenance metadata (`parent_session_id`, `source_session_id`, `branch_from_entry_id`, `branch_origin:"fork"`, `actor:"rpc"`), switches to the new session, and returns the state shape with `created:true`. If `session_id` is omitted, the current session is the source; if `branch_from_entry_id` is omitted, the source tip is used. When provided, `session_id` and `branch_from_entry_id` must be non-empty strings. `clone_session` copies the full source session, appends provenance metadata with `branch_origin:"clone"`, switches to it, and returns the state shape with `created:true`. `clone_session` rejects `branch_from_entry_id` because clones always copy the full source. Both commands leave the source session file untouched and are rejected while a prompt is active.

`summarize_branch` explicitly appends a durable `branch_summary` entry to the selected source session and does not switch the active session. If `session_id` is omitted, the current session is the source. `branch_root_entry_id`, `branch_tip_entry_id`, `summary`, `provider`, `model`, and `reason` are required. The root and tip entry ids must exist in the source session and root must not appear after tip. `summary` is capped at 8192 bytes and may contain newlines/tabs but not other control bytes. `provider` and `model` are capped at 256 bytes; `reason` is capped at 1024 bytes. The response returns `{source_session_id,entry_id,parent_id,timestamp,entry}` where `entry` is the persisted branch-summary session entry. Provider-generated branch summaries remain deferred for the current MVP; this command stores caller-supplied summary text with explicit provider/model/reason provenance. Provider-generated compaction summaries are a separate `/compact` behavior.

Session metadata and branch-summary entries use `data.schema_version:1` inside session entry version `3` JSONL records. Unsupported metadata/summary `schema_version` values are replay-validation errors. `branch_summary.data` includes required `summary`, `source_session_id`, `branch_root_entry_id`, `branch_tip_entry_id`, `provider`, `model`, and `reason`; `actor` is optional. Session entry and payload versioning rules are documented in [`docs/engineering/session-versioning.md`](engineering/session-versioning.md).

Backend slash-command equivalents:

```json
{"id":"6","type":"compact","instructions":"optional notes"}
{"id":"7","type":"export"}
{"id":"8","type":"export_html"}
{"id":"8b","type":"export_html","outputPath":"session.html"}
{"id":"8c","type":"export_html","output_path":"session.html"}
{"id":"9","type":"context"}
{"id":"10","type":"list_plugins"}
{"id":"11","type":"plugin_failures"}
{"id":"12","type":"inspect_plugin","plugin_id":"com.example.tool"}
{"id":"13","type":"enable_plugin","plugin_id":"com.example.tool"}
{"id":"14","type":"disable_plugin","plugin_id":"com.example.tool"}
{"id":"15","type":"validate_plugin","path":".ava/plugins/com.example.tool/plugin.json"}
{"id":"16","type":"list_plugin_prompts","plugin_id":"com.example.tool"}
{"id":"17","type":"get_plugin_prompt","plugin_id":"com.example.tool","name":"review"}
{"id":"18","type":"list_plugin_skills","plugin_id":"com.example.tool"}
{"id":"19","type":"get_plugin_skill","plugin_id":"com.example.tool","name":"triage"}
{"id":"20","type":"run_plugin_command","plugin_id":"com.example.tool","name":"status","arguments":{}}
{"id":"21","type":"list_mcp_servers"}
{"id":"22","type":"inspect_mcp_server","server_id":"demo"}
{"id":"23","type":"list_mcp_tools","server_id":"demo"}
{"id":"24","type":"restart_mcp_server","server_id":"demo"}
```

These dispatch `/compact`, `/export`, `/export html`, `/context`, `/plugins`, `/plugin run`, and `/mcp` through the shared backend command dispatcher. `export` returns Markdown in command output. `export_html` returns HTML in command output when no path is supplied; with `outputPath` or the equivalent `output_path`, it writes HTML through AVA's permissioned file-mutation path and returns the written path/byte summary plus command-side tool events. Raw JSONL export is available through slash/line-shell `/export jsonl [path]`. Local `/import <path.jsonl> --confirm` is also slash/line-shell only for MVP and validates a local AVA JSONL archive into a new session; a dedicated RPC JSONL export/import/share command is deferred. Plugin commands require `plugin_id` for plugin-specific operations, `path` for validate, and `name` for prompt/skill retrieval or command execution. MCP server commands require `server_id` except `list_mcp_servers`. Responses include `handled`, `quit`, `output` (array of strings), and `text` (joined output). Responses may also include `tool_timeline[]` using the same entry shape and status semantics as prompt `tool_timeline[]`, including `structured_result.status:"canceled"` for canceled command-side tools. Command-side permission asks use the supplied headless policy and fail closed when no policy allows the operation.

`compact` requires credentials for the active session provider and asks that provider to generate the compaction summary before appending a compaction boundary. RPC compact emits `compaction_start` and `compaction_end` events around successful provider-backed compaction; stale-session retries emit `retry` with `reason:"stale_compaction_snapshot"` and bounded `attempt`/`max_attempts` metadata. It returns `success:false` for missing auth, provider summary failure, empty or oversized summaries, stale-session append failures, or active-run conflicts. On success, the recorded compaction entry contains summary metadata such as trigger, estimated tokens, threshold, retained recent context, and keep-recent settings.

Most plugin RPC commands only discover, inspect, validate, read static prompt/skill files, or record enablement state. `run_plugin_command` starts the enabled plugin entrypoint, emits command tool events, and requires `plugin.execute` plus `plugin.command.run` permission approval. Its `arguments` field is canonically a JSON object; legacy clients may send a string containing a JSON object, but present non-object values are rejected. `list_mcp_tools` starts a fresh stdio MCP process for discovery, emits `mcp_tools` tool events, and requires `mcp.server.launch` plus `mcp.server.connect` permission approval. `restart_mcp_server` is informational because current stdio MCP servers are launched per discovery or tool call, not kept resident.

Unknown command types return an error response and do not terminate the RPC loop.

### Resolver Requests And Replies

When an active prompt reaches a backend permission prompt, RPC emits a resolver event:

```json
{"schema_version":1,"name":"permission_requested","type":"permission_requested","request_id":"prompt_req","correlation_id":"prompt_req","payload":{"resolver_request_id":"permission_...","permission_request_id":"permreq_...","operation":"read","mode":"build","target_path":"/workspace/file","command":"","tool_name":"read_file","reason":"...","risk":"high"}}
```

File mutation prompts may include a backend-provided unified diff preview. Clients must treat `diff_preview` as display data only and still reply through the resolver decision:

```json
{"schema_version":1,"name":"permission_requested","type":"permission_requested","request_id":"prompt_req","correlation_id":"prompt_req","payload":{"resolver_request_id":"permission_...","permission_request_id":"permreq_...","operation":"edit","mode":"build","target_path":"/workspace/file","command":"","tool_name":"edit_file","reason":"...","risk":"high","diff_preview":"--- /workspace/file\n+++ /workspace/file\n@@ -1,1 +1,1 @@\n-old\n+new\n","diff_truncated":false}}
```

Permission `operation` values are backend policy categories such as `read`, `search`, `edit`, `bash`, `task`, `network.fetch`, `lsp.server.launch`, `lsp.query`, `plugin.execute`, `plugin.tool.call`, `plugin.command.run`, `plugin.event.observe`, `mcp.server.launch`, `mcp.server.connect`, `mcp.tool.call`, and `mcp.resource.read`. `task` prompts carry the requested `subagent_type` in `command` and use `tool_name:"task"`. Network fetch prompts use an empty `target_path` and carry the URL in `command`:

```json
{"schema_version":1,"name":"permission_requested","type":"permission_requested","request_id":"prompt_req","correlation_id":"prompt_req","payload":{"resolver_request_id":"permission_...","permission_request_id":"permreq_...","operation":"network.fetch","mode":"build","target_path":"","command":"https://example.com/page","tool_name":"webfetch","reason":"network fetch requires explicit approval","risk":"high"}}
```

The payload always includes `resolver_request_id`, `operation`, `mode`, `target_path`, `command`, `tool_name`, `reason`, and `risk`; when the backend permission layer has generated the durable audit id it also includes `permission_request_id`, which is the id shown by tool cards, text stderr diagnostics, and `/permissions audit show <request_id>`. A persistent-rule match is surfaced through the normal `permission_replied`/tool-result/audit details with the rule id and resolution source rather than by weakening built-in hard-deny policy. The event top-level `request_id` remains the prompt command id. The client must answer with `payload.resolver_request_id` and the prompt `correlation_id`:

```json
{"id":"perm_reply_1","type":"permission_reply","request_id":"permission_...","correlation_id":"prompt_req","decision":"allow"}
{"id":"perm_reply_1b","type":"permission_reply","request_id":"permission_...","correlation_id":"prompt_req","decision":"allow_session"}
{"id":"perm_reply_2","type":"permission_reply","request_id":"permission_...","correlation_id":"prompt_req","decision":"deny","reason":"not approved for this run"}
```

`decision` must be exactly `allow`, `allow_session`, or `deny`. `allow_session` resolves the current prompt as allow and records an in-memory exact-match grant for later permission prompts with the same operation, mode, tool name, target path, and command. `reason` is optional free text for clients to explain the resolution, especially denials. Denial reasons are also preserved in permission-denied tool errors and durable permission audit entries as `resolution_reason`. A successful reply emits `permission_replied` before the in-band response:

```json
{"schema_version":1,"name":"permission_replied","type":"permission_replied","request_id":"prompt_req","correlation_id":"prompt_req","payload":{"resolver_request_id":"permission_...","decision":"deny","reason":"not approved for this run"}}
```

Missing, unknown, or wrong-correlation resolver ids return in-band errors. Cancellation unblocks pending permission requests fail-closed.

RPC session grants can be inspected, revoked by id, or cleared:

```json
{"id":"perm_grants_1","type":"permission_grants"}
{"id":"perm_grants_2","type":"permission_grant_revoke","grant_id":"permgrant_..."}
{"id":"perm_grants_3","type":"permission_grants_clear"}
```

`permission_grants` returns `{"grants":[...]}`. Each grant includes `grant_id`, the original `permission_request_id`, `operation`, `mode`, `tool_name`, `target_path`, `command`, `reason`, and `risk`. `permission_grant_revoke` returns `success:false` if the grant id is unknown; otherwise it emits `permission_grant_revoked` and returns the revoked grant. `permission_grants_clear` emits `permission_grants_cleared` and returns the number of grants removed. Session grants are process-local RPC state, not durable permission rules.

Persistent permission rules can be listed, added, and removed:

```json
{"id":"rules_1","type":"permission_rules"}
{"id":"rules_2","type":"permission_rule_add","action":"allow","operation":"read","scope":"workspace","mode":"any","target_path":"/path/to/file","reason":"approved by operator"}
{"id":"rules_3","type":"permission_rule_remove","rule_id":"permrule_..."}
```

`permission_rules` returns `global_rules_file`, the enforceable `workspace_rules_file`, and `rules`. Workspace rules are keyed to the normalized workspace directory but are stored under AVA config outside `$workspace/.ava`, because normal model file tools can write workspace files. Legacy `$workspace/.ava/permission-rules.json` files are not enforceable. Rule files are JSON with `schema_version:1`; malformed, too-broad, corrupt, or unsupported-version rule storage fails closed before resolver prompts.

`permission_rule_add` requires `action` (`allow` or `deny`), `operation`, and non-empty `reason`. Optional `scope` is `workspace` (default) or `global`; optional `mode` is `any` (default), `build`, or `plan`. Path operations (`read`, `search`, `edit`, `lsp.query`) require `target_path`; relative workspace rule paths must stay inside the workspace, while global path rules require absolute paths. Command/network operations, `lsp.server.launch`, and `mcp.resource.read` match exact `command`; LSP server launch commands are JSON-array encoded argv vectors such as `["clangd","--background-index"]`, and MCP resource read commands are formatted as `<server_id>:<resource_uri>`. Other operations may match exact `tool_name`. Matching deny rules win over allows. Within the same action, rules with more explicit constraints (`target_path`, `command`, `tool_name`, and non-`any` mode) win before insertion order; workspace rules are preferred when specificity ties. Persistent rules are consulted only after built-in hard policy allows an `ask` path to continue. Built-in hard denies are never upgraded by persistent allow rules. Successful add/remove commands emit `permission_rule_added` or `permission_rule_removed` before the success response. Unknown `rule_id`, invalid fields, unsupported schema versions, and active-run conflicts return `success:false`.

Example MCP resource rule:

```json
{"id":"rules_mcp_resource","type":"permission_rule_add","action":"allow","operation":"mcp.resource.read","command":"demo:file:///workspace/notes.md","reason":"approved configured MCP notes resource"}
```

When an active prompt reaches the `question` tool, RPC emits:

```json
{"schema_version":1,"name":"question_requested","type":"question_requested","request_id":"prompt_req","correlation_id":"prompt_req","payload":{"resolver_request_id":"question_...","header":"Choose","question":"Continue?","options":[{"value":"yes","label":"Yes"}],"multiple":false,"allow_custom":true,"secret":false,"modal":false,"searchable":false}}
```

Question payloads carry prompt text, option metadata, single/multiple-selection capability, custom-text allowance, and local prompt flags such as `secret`, `modal`, and `searchable`. The client may answer with custom text when `allow_custom` is true, one valid selected option value through `selected`, or multiple valid option values through `selected_options` when `multiple` is true. Multi-select replies may also include `answer` when custom text is allowed.

```json
{"id":"question_reply_1","type":"question_reply","request_id":"question_...","correlation_id":"prompt_req","answer":"text"}
{"id":"question_reply_2","type":"question_reply","request_id":"question_...","correlation_id":"prompt_req","selected":"yes"}
{"id":"question_reply_3","type":"question_reply","request_id":"question_...","correlation_id":"prompt_req","selected_options":["alpha","beta"],"answer":"Use both"}
```

Successful replies emit `question_replied` with the submitted value before the in-band response:

```json
{"schema_version":1,"name":"question_replied","type":"question_replied","request_id":"prompt_req","correlation_id":"prompt_req","payload":{"resolver_request_id":"question_...","selected":"yes"}}
```

Missing, unknown, or wrong-correlation resolver ids; replies without exactly one supported answer shape; custom answers when `allow_custom` is false; unknown `selected` values; and invalid `selected_options` values return in-band errors. Single-select replies use exactly one of `answer` or `selected`. Multi-select replies may use `selected_options` and may also include `answer` only when custom text is allowed; `selected_options` values must be strings from the current option set and remain within backend count/size limits. Cancellation unblocks pending question requests with a canceled error.

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

- Permission decisions that require user approval fail unless headless policy supplies `--allow read-only`/`--allow-tool` for a supported read/search tool, `--allow-tool skill` for exact skill loads, `--allow-tool task` for exact subagent task prompts, `--allow-tool webfetch` for exact `network.fetch` webfetch prompts, `--allow-tool websearch` for exact `network.search` websearch prompts, `--allow-tool mcp` for MCP launch/connect/tool/resource prompts, or RPC mode receives an explicit `permission_reply` for the active resolver request.
- Permission-denied tool results include the backend reason, risk label, resolution reason when available, generated `permission_request_id`, and follow-up `/permissions audit show <permission_request_id>` plus `/permissions diagnose <permission_request_id>` commands in `structured_result.error.details`. Text print mode writes those details to stderr for human operators.
- The `question` tool fails with an unavailable interaction error unless RPC mode receives an explicit `question_reply` for the active resolver request.
- Destructive operations remain behind existing backend permission policy checks.

## Server Mode Deferral

No socket, daemon, HTTP server, or background service semantics are defined in this phase. Server mode must not be implemented by inferring behavior from `print` or `rpc`; it requires a separate contract review.
