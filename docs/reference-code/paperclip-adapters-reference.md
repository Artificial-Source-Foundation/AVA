# Paperclip Adapters Reference

Comprehensive reference for the Paperclip adapter system at `packages/adapters/` and `packages/adapter-utils/`.

Paperclip is an agent orchestration platform that invokes coding agents (Claude Code, Codex, Cursor, Gemini CLI, OpenCode, Pi) via a unified adapter interface. Each adapter wraps a specific CLI tool or protocol, normalizing execution, session management, billing, skills injection, and environment variable propagation.

---

## Table of Contents

1. [Adapter Interface](#1-adapter-interface)
2. [Session Codecs](#2-session-codecs)
3. [Claude Local Adapter](#3-claude-local-adapter)
4. [Codex Local Adapter](#4-codex-local-adapter)
5. [Cursor Adapter](#5-cursor-adapter)
6. [Gemini Local Adapter](#6-gemini-local-adapter)
7. [OpenCode Adapter](#7-opencode-adapter)
8. [Pi Local Adapter](#8-pi-local-adapter)
9. [OpenClaw Gateway Adapter](#9-openclaw-gateway-adapter)
10. [Process / HTTP / Hermes Adapters](#10-process--http--hermes-adapters)
11. [Billing Integration](#11-billing-integration)
12. [Environment Variables](#12-environment-variables)
13. [Session Compaction](#13-session-compaction)
14. [Skills System](#14-skills-system)
15. [Shared Utilities](#15-shared-utilities)

---

## 1. Adapter Interface

**Location:** `packages/adapter-utils/src/types.ts`

### ServerAdapterModule

The core contract every adapter must satisfy:

```typescript
interface ServerAdapterModule {
  type: string;
  execute(ctx: AdapterExecutionContext): Promise<AdapterExecutionResult>;
  testEnvironment(ctx: AdapterEnvironmentTestContext): Promise<AdapterEnvironmentTestResult>;

  // Optional capabilities
  listSkills?: (ctx: AdapterSkillContext) => Promise<AdapterSkillSnapshot>;
  syncSkills?: (ctx: AdapterSkillContext, desiredSkills: string[]) => Promise<AdapterSkillSnapshot>;
  sessionCodec?: AdapterSessionCodec;
  sessionManagement?: AdapterSessionManagement;
  supportsLocalAgentJwt?: boolean;
  models?: AdapterModel[];
  listModels?: () => Promise<AdapterModel[]>;
  agentConfigurationDoc?: string;
  onHireApproved?: (payload: HireApprovedPayload, adapterConfig: Record<string, unknown>) => Promise<HireApprovedHookResult>;
  getQuotaWindows?: () => Promise<ProviderQuotaResult>;
}
```

### AdapterExecutionContext

The full context provided to every `execute()` call:

```typescript
interface AdapterExecutionContext {
  runId: string;
  agent: AdapterAgent;        // { id, companyId, name, adapterType, adapterConfig }
  runtime: AdapterRuntime;    // { sessionId, sessionParams, sessionDisplayId, taskKey }
  config: Record<string, unknown>;
  context: Record<string, unknown>;
  onLog: (stream: "stdout" | "stderr", chunk: string) => Promise<void>;
  onMeta?: (meta: AdapterInvocationMeta) => Promise<void>;
  onSpawn?: (meta: { pid: number; startedAt: string }) => Promise<void>;
  authToken?: string;
}
```

### AdapterExecutionResult

Returned by `execute()` after the agent CLI finishes:

```typescript
interface AdapterExecutionResult {
  exitCode: number | null;
  signal: string | null;
  timedOut: boolean;
  errorMessage?: string | null;
  errorCode?: string | null;
  errorMeta?: Record<string, unknown>;
  usage?: UsageSummary;               // { inputTokens, outputTokens, cachedInputTokens? }
  sessionId?: string | null;
  sessionParams?: Record<string, unknown> | null;
  sessionDisplayId?: string | null;
  provider?: string | null;           // e.g. "anthropic", "openai", "google"
  biller?: string | null;             // e.g. "anthropic", "chatgpt", "openrouter"
  model?: string | null;
  billingType?: AdapterBillingType | null;
  costUsd?: number | null;
  resultJson?: Record<string, unknown> | null;
  runtimeServices?: AdapterRuntimeServiceReport[];
  summary?: string | null;
  clearSession?: boolean;
  question?: { prompt: string; choices: Array<{ key: string; label: string; description?: string }> } | null;
}
```

### AdapterBillingType

Classifies how costs are incurred:

```typescript
type AdapterBillingType =
  | "api"                    // Direct API key billing
  | "subscription"           // Subscription-based (e.g. Claude Pro, ChatGPT Plus)
  | "metered_api"
  | "subscription_included"
  | "subscription_overage"
  | "credits"
  | "fixed"
  | "unknown";
```

### AdapterInvocationMeta

Sent via `onMeta` to record what command was actually invoked:

```typescript
interface AdapterInvocationMeta {
  adapterType: string;
  command: string;
  cwd?: string;
  commandArgs?: string[];
  commandNotes?: string[];
  env?: Record<string, string>;
  prompt?: string;
  promptMetrics?: Record<string, number>;
  context?: Record<string, unknown>;
}
```

### TranscriptEntry

UI-side parsed log entries for real-time transcript display:

```typescript
type TranscriptEntry =
  | { kind: "assistant"; ts: string; text: string; delta?: boolean }
  | { kind: "thinking"; ts: string; text: string; delta?: boolean }
  | { kind: "user"; ts: string; text: string }
  | { kind: "tool_call"; ts: string; name: string; input: unknown; toolUseId?: string }
  | { kind: "tool_result"; ts: string; toolUseId: string; toolName?: string; content: string; isError: boolean }
  | { kind: "init"; ts: string; model: string; sessionId: string }
  | { kind: "result"; ts: string; text: string; inputTokens: number; outputTokens: number; cachedTokens: number; costUsd: number; subtype: string; isError: boolean; errors: string[] }
  | { kind: "stderr"; ts: string; text: string }
  | { kind: "system"; ts: string; text: string }
  | { kind: "stdout"; ts: string; text: string };
```

### CLIAdapterModule

Minimal CLI-side interface for formatting output:

```typescript
interface CLIAdapterModule {
  type: string;
  formatStdoutEvent: (line: string, debug: boolean) => void;
}
```

---

## 2. Session Codecs

**Location:** Each adapter's `server/index.ts` exports a `sessionCodec: AdapterSessionCodec`.

### AdapterSessionCodec Interface

```typescript
interface AdapterSessionCodec {
  deserialize(raw: unknown): Record<string, unknown> | null;
  serialize(params: Record<string, unknown> | null): Record<string, unknown> | null;
  getDisplayId?: (params: Record<string, unknown> | null) => string | null;
}
```

### Shared Session Params Schema

All local CLI adapters (Claude, Codex, Cursor, Gemini, OpenCode, Pi) store essentially the same session parameters with minor variations:

```typescript
// Canonical session params stored by all local adapters
{
  sessionId: string;       // The CLI session/thread/checkpoint ID
  cwd?: string;            // Working directory the session was created in
  workspaceId?: string;    // Paperclip workspace ID (most adapters)
  repoUrl?: string;        // Repository URL (most adapters)
  repoRef?: string;        // Repository ref/branch (most adapters)
}
```

**Key pattern:** All codecs accept multiple field name variants for resilience:
- `sessionId` / `session_id` / `sessionID` / `session` (Pi uses `session` additionally)
- `cwd` / `workdir` / `folder`
- `workspaceId` / `workspace_id`
- `repoUrl` / `repo_url`
- `repoRef` / `repo_ref`

### Session Resume Logic

All local adapters share identical session resume logic:

```typescript
// Resume is only attempted if:
// 1. A previous sessionId exists
// 2. The stored cwd matches the current cwd (or stored cwd is empty)
const canResumeSession =
  runtimeSessionId.length > 0 &&
  (runtimeSessionCwd.length === 0 || path.resolve(runtimeSessionCwd) === path.resolve(cwd));
```

If the session is stale (the CLI returns "unknown session" or similar errors), every adapter retries once with a fresh session:

```typescript
if (sessionId && isUnknownSessionError(initial)) {
  await onLog("stdout", `[paperclip] Session "${sessionId}" is unavailable; retrying fresh.\n`);
  const retry = await runAttempt(null);
  return toResult(retry, { clearSessionOnMissingSession: true });
}
```

---

## 3. Claude Local Adapter

**Type:** `claude_local`
**Label:** "Claude Code (local)"
**Location:** `packages/adapters/claude-local/`

### CLI Invocation

```
claude --print - --output-format stream-json --verbose [flags]
```

Key flags:
- `--resume <sessionId>` -- resume existing session
- `--model <model>` -- model selection
- `--effort <low|medium|high>` -- reasoning effort
- `--chrome` -- enable browser tool
- `--max-turns N` -- turn limit per run
- `--dangerously-skip-permissions` -- bypass permission prompts
- `--append-system-prompt-file <path>` -- inject agent instructions
- `--add-dir <skillsDir>` -- add skills directory

**Prompt delivery:** Piped via stdin.

### Session Management

- **nativeContextManagement:** `confirmed` -- Claude Code manages its own context window
- **Compaction policy:** adapter-managed (no Paperclip-side compaction thresholds)
- Session params include: `sessionId`, `cwd`, `workspaceId`, `repoUrl`, `repoRef`
- On stale session: auto-retry with fresh session

### Output Parsing

Parses Claude's `stream-json` output format, processing events line-by-line:

```typescript
function parseClaudeStreamJson(stdout: string) {
  // Event types processed:
  // - type="system" subtype="init" -> sessionId, model
  // - type="assistant" -> message content (text, thinking, tool_use blocks)
  // - type="result" -> final usage, cost, summary, session_id
  return { sessionId, model, costUsd, usage, summary, resultJson };
}
```

### Error Detection

- **Auth required:** Regex match on "not logged in", "please log in", "login required", etc.
- **Login URL extraction:** Scans stdout/stderr for URLs containing "claude", "anthropic", or "auth"
- **Max turns:** Detected via `subtype === "error_max_turns"` or `stop_reason === "max_turns"`
- **Unknown session:** Detected via "no conversation found with session id" patterns

### Skills Injection

Claude uses an **ephemeral** skills model:
1. Creates a temp directory with `.claude/skills/` structure
2. Symlinks desired skills from the Paperclip skills directory
3. Passes `--add-dir <tmpDir>` to Claude CLI
4. Cleans up temp directory after execution

### Quota Polling

Two-tier quota polling:

1. **OAuth API** (`https://api.anthropic.com/api/oauth/usage`): reads token from `~/.claude/.credentials.json`, returns 5h window, 7d windows (all models, Sonnet, Opus), extra usage
2. **CLI probe fallback**: spawns `claude --tools ""`, feeds `/usage` via pseudo-terminal using `script`, parses terminal output

### Billing

```typescript
function resolveClaudeBillingType(env): "api" | "subscription" {
  return hasNonEmptyEnvValue(env, "ANTHROPIC_API_KEY") ? "api" : "subscription";
}
```

- **provider:** always `"anthropic"`
- **biller:** always `"anthropic"`

### Models

```typescript
[
  { id: "claude-opus-4-6", label: "Claude Opus 4.6" },
  { id: "claude-sonnet-4-6", label: "Claude Sonnet 4.6" },
  { id: "claude-haiku-4-6", label: "Claude Haiku 4.6" },
  { id: "claude-sonnet-4-5-20250929", label: "Claude Sonnet 4.5" },
  { id: "claude-haiku-4-5-20251001", label: "Claude Haiku 4.5" },
]
```

### Environment Test

1. Validates working directory exists
2. Resolves command in PATH
3. Checks ANTHROPIC_API_KEY presence (warns if it overrides subscription auth)
4. Runs "hello probe": `claude --print - --output-format stream-json --verbose` with "Respond with hello." piped via stdin

### Nesting Guard Stripping

The adapter strips Claude Code nesting-guard env vars before spawning:

```typescript
const CLAUDE_CODE_NESTING_VARS = [
  "CLAUDECODE", "CLAUDE_CODE_ENTRYPOINT",
  "CLAUDE_CODE_SESSION", "CLAUDE_CODE_PARENT_SESSION",
];
```

This prevents "cannot be launched inside another session" errors when Paperclip itself runs inside a Claude Code session.

---

## 4. Codex Local Adapter

**Type:** `codex_local`
**Label:** "Codex (local)"
**Location:** `packages/adapters/codex-local/`

### CLI Invocation

```
codex exec --json [flags] - | resume <sessionId> -
```

Key flags:
- `--search` -- enable web search (prepended before `exec`)
- `--dangerously-bypass-approvals-and-sandbox` -- bypass safety
- `--model <model>` -- model selection
- `-c model_reasoning_effort=<value>` -- reasoning effort

**Prompt delivery:** Piped via stdin (the `-` argument).

### Session Management

- **nativeContextManagement:** `confirmed`
- **Compaction policy:** adapter-managed (no Paperclip thresholds)
- Resume via: `codex exec --json resume <sessionId> -`

### Output Parsing (JSONL)

```typescript
function parseCodexJsonl(stdout: string) {
  // Event types:
  // - type="thread.started" -> sessionId (thread_id)
  // - type="item.completed" with item.type="agent_message" -> assistant text
  // - type="turn.completed" -> usage (input_tokens, cached_input_tokens, output_tokens)
  // - type="error" / type="turn.failed" -> error messages
  return { sessionId, summary, usage, errorMessage };
}
```

The UI parser (`parseCodexStdoutLine`) also handles:
- `item.type="command_execution"` -> tool_call/tool_result entries
- `item.type="file_change"` -> system entries with change summaries
- `item.type="reasoning"` -> thinking entries

### Managed CODEX_HOME

Codex adapter uses a per-company managed home directory to isolate agent state:

```typescript
// Default managed path:
// ~/.paperclip/instances/<instanceId>/companies/<companyId>/codex-home/

function prepareManagedCodexHome(env, onLog, companyId): Promise<string> {
  // 1. Resolve target (managed) and source (shared ~/.codex) homes
  // 2. Symlink auth.json from shared home (stays in sync)
  // 3. Copy config.json, config.toml, instructions.md (one-time seed)
  // 4. Return managed home path
}
```

### Skills Injection

Codex uses **persistent** symlinks in `CODEX_HOME/skills/`:
- Symlinks desired skills from Paperclip skills directory
- Repairs broken symlinks that point to stale Paperclip repo paths
- Prunes symlinks whose targets look like old Paperclip runtime skill paths

### Quota Polling

Two-tier quota system:

1. **Codex RPC** (`codex app-server`): spawns a JSON-RPC client over stdio, calls `account/rateLimits/read` and `account/read`
2. **ChatGPT WHAM API** (`https://chatgpt.com/backend-api/wham/usage`): reads access token from `~/.codex/auth.json`, sends with `ChatGPT-Account-Id` header

Quota windows: 5h limit, Weekly limit, Credits (balance in cents).

### Billing

```typescript
function resolveCodexBillingType(env): "api" | "subscription" {
  return hasNonEmptyEnvValue(env, "OPENAI_API_KEY") ? "api" : "subscription";
}
function resolveCodexBiller(env, billingType): string {
  // "openrouter" if OpenRouter markers present
  // "chatgpt" for subscription
  // "openai" for API key
}
```

- **provider:** `"openai"`
- **costUsd:** always `null` (Codex does not report per-run cost)

### Auth File Parsing

Supports both legacy and modern auth formats:

```typescript
// Legacy: { accessToken, accountId }
// Modern: { OPENAI_API_KEY?, tokens: { id_token, access_token, refresh_token, account_id }, last_refresh }
```

Extracts email and plan type by decoding JWT payloads from id_token/access_token.

### Rollout Noise Filtering

Strips noisy Codex rollout log lines from stderr:

```
2024-01-01T00:00:00Z ERROR codex_core::rollout::list: state db missing rollout path for thread abc-123
```

---

## 5. Cursor Adapter

**Type:** `cursor`
**Label:** "Cursor CLI (local)"
**Location:** `packages/adapters/cursor-local/`

### CLI Invocation

```
agent -p --output-format stream-json --workspace <cwd> [flags]
```

Key flags:
- `--resume <sessionId>` -- resume session
- `--model <model>` -- model selection (default: `"auto"`)
- `--mode <plan|ask>` -- execution mode
- `--yolo` -- auto-added unless `--trust`/`--yolo`/`-f` already in extraArgs

**Prompt delivery:** Piped via stdin.

### Trust/Yolo Bypass

```typescript
function hasCursorTrustBypassArg(args: readonly string[]): boolean {
  return args.some(arg =>
    arg === "--trust" || arg === "--yolo" || arg === "-f" || arg.startsWith("--trust=")
  );
}
```

If no trust bypass is present in `extraArgs`, Paperclip auto-adds `--yolo` for unattended execution.

### Output Normalization

Cursor's stream output sometimes prefixes lines with `stdout:` or `stderr:`:

```typescript
function normalizeCursorStreamLine(rawLine: string): { stream: "stdout" | "stderr" | null; line: string } {
  // Matches patterns like "stdout: {...}" or "stderr: {...}"
  const prefixed = trimmed.match(/^(stdout|stderr)\s*[:=]?\s*([\[{].*)$/i);
}
```

### Output Parsing

Processes multiple stream-json formats (both Claude-style and OpenCode-style):

- `type="assistant"` -- collects text from `message.content[].text` / `message.content[].output_text`
- `type="result"` -- usage, cost, errors
- `type="text"` (legacy) -- `part.text`
- `type="step_finish"` (legacy) -- `part.tokens.input/output`, `part.tokens.cache.read`, `part.cost`

### Skills Injection

Cursor uses **persistent** symlinks in `~/.cursor/skills/`:
- Same symlink strategy as Codex
- Also removes "maintainer-only" skill symlinks (those pointing into `.agents/skills/` paths)
- Full `syncSkills` support: links desired skills, unlinks undesired managed skills

### Billing

```typescript
function resolveCursorBillingType(env): "api" | "subscription" {
  return (hasNonEmptyEnvValue(env, "CURSOR_API_KEY") || hasNonEmptyEnvValue(env, "OPENAI_API_KEY"))
    ? "api" : "subscription";
}
function resolveCursorBiller(env, billingType, provider): string {
  // "openrouter" if detected, "cursor" for subscription, else provider or "cursor"
}
```

### Provider Inference from Model

```typescript
function resolveProviderFromModel(model: string): string | null {
  if (model.includes("/")) return model.slice(0, model.indexOf("/"));
  if (model.includes("sonnet") || model.includes("claude")) return "anthropic";
  if (model.startsWith("gpt") || model.startsWith("o")) return "openai";
  return null;
}
```

### Paperclip Env Note

Cursor does not natively discover environment variables, so the adapter prepends a note to the prompt:

```
Paperclip runtime note:
The following PAPERCLIP_* environment variables are available in this run: PAPERCLIP_AGENT_ID, PAPERCLIP_COMPANY_ID, ...
Do not assume these variables are missing without checking your shell environment.
```

### Models

44+ model IDs including: `auto`, `composer-1.5`, `gpt-5.3-codex*`, `opus-4.6*`, `sonnet-4.6*`, `gemini-3.1-pro`, `grok`, `kimi-k2.5`.

---

## 6. Gemini Local Adapter

**Type:** `gemini_local`
**Label:** "Gemini CLI (local)"
**Location:** `packages/adapters/gemini-local/`

### CLI Invocation

```
gemini --output-format stream-json [flags] --prompt <prompt>
```

Key flags:
- `--resume <sessionId>` -- resume session
- `--model <model>` -- model selection (default: `"auto"`)
- `--approval-mode yolo` -- auto-added for unattended execution
- `--sandbox` / `--sandbox=none` -- sandbox control
- Extra args via `extraArgs`

**Prompt delivery:** Via `--prompt` argument (not stdin).

### Output Parsing

```typescript
function parseGeminiJsonl(stdout: string) {
  // Event types:
  // - type="assistant" -> message text, question content blocks
  // - type="result" -> usage (supports both OpenAI-style and Gemini-native usageMetadata), cost, errors
  // - type="error" / type="system" subtype="error" -> errors
  // - type="text" (legacy) -> part.text
  // - type="step_finish" / events with usage/usageMetadata -> accumulated usage
  return { sessionId, summary, usage, costUsd, errorMessage, resultEvent, question };
}
```

### Session ID Sources

Gemini accepts multiple session ID field names:

```typescript
function readSessionId(event): string | null {
  return event.session_id ?? event.sessionId ?? event.sessionID
    ?? event.checkpoint_id ?? event.thread_id ?? null;
}
```

### Question Support

Gemini is the only adapter that extracts interactive questions from assistant messages:

```typescript
// In assistant messages, looks for content blocks with type="question"
question = {
  prompt: "...",
  choices: [{ key: "a", label: "Yes", description: "..." }, ...]
};
```

This is returned in `AdapterExecutionResult.question`.

### Turn Limit Detection

```typescript
function isGeminiTurnLimitResult(parsed, exitCode): boolean {
  if (exitCode === 53) return true;           // Gemini's turn-limit exit code
  if (parsed.status === "turn_limit" || parsed.status === "max_turns") return true;
  return /turn\s*limit|max(?:imum)?\s+turns?/i.test(parsed.error);
}
```

### Auth Detection

```typescript
const GEMINI_AUTH_REQUIRED_RE = /not\s+authenticated|api[_ ]?key\s+(?:required|missing)|run\s+`?gemini\s+auth/i;
const GEMINI_QUOTA_EXHAUSTED_RE = /resource_exhausted|quota|rate[-\s]?limit|429|billing details/i;
```

### API Access Note

Gemini adapter injects an additional prompt section explaining how to make Paperclip API calls via curl:

```
Paperclip API access note:
Use run_shell_command with curl to make Paperclip API requests.
GET example: run_shell_command({ command: "curl -s -H \"Authorization: Bearer $PAPERCLIP_API_KEY\" \"$PAPERCLIP_API_URL/api/agents/me\"" })
```

### Skills Injection

Persistent symlinks in `~/.gemini/skills/` (same pattern as Cursor).

### Billing

```typescript
function resolveGeminiBillingType(env): "api" | "subscription" {
  return (hasNonEmptyEnvValue(env, "GEMINI_API_KEY") || hasNonEmptyEnvValue(env, "GOOGLE_API_KEY"))
    ? "api" : "subscription";
}
```

- **provider:** `"google"`
- **biller:** `"google"`

### Models

```typescript
[
  { id: "auto", label: "Auto" },
  { id: "gemini-2.5-pro", label: "Gemini 2.5 Pro" },
  { id: "gemini-2.5-flash", label: "Gemini 2.5 Flash" },
  { id: "gemini-2.5-flash-lite", label: "Gemini 2.5 Flash Lite" },
  { id: "gemini-2.0-flash", label: "Gemini 2.0 Flash" },
  { id: "gemini-2.0-flash-lite", label: "Gemini 2.0 Flash Lite" },
]
```

---

## 7. OpenCode Adapter

**Type:** `opencode_local`
**Label:** "OpenCode (local)"
**Location:** `packages/adapters/opencode-local/`

### CLI Invocation

```
opencode run --format json [flags] <prompt>
```

Key flags:
- `--session <sessionId>` -- resume session
- `--model <provider/model>` -- model in provider/model format (e.g. `anthropic/claude-sonnet-4-5`)
- Extra args via `extraArgs`

**Prompt delivery:** Via positional argument.

### Special Features

- **Runtime config injection:** Calls `prepareOpenCodeRuntimeConfig()` which sets `OPENCODE_DISABLE_PROJECT_CONFIG=true` and injects a temporary runtime config with `permission.external_directory=allow` when `dangerouslySkipPermissions` is enabled
- **Model discovery:** `discoverOpenCodeModels()` runs `opencode models` to list available models at runtime
- **Skills injection:** Into `~/.claude/skills/` (OpenCode shares Claude's skill directory)

### Output Parsing (JSONL)

```typescript
function parseOpenCodeJsonl(stdout: string) {
  // Event types:
  // - type="text" -> part.text (assistant messages)
  // - type="step_finish" -> part.tokens.{input,output}, part.tokens.cache.read, part.cost
  //   (output includes reasoning tokens)
  // - type="tool_use" -> checks state.status for errors
  // - type="error" -> error message from event.error or event.message
  // Session ID from: event.sessionID
  return { sessionId, summary, usage, costUsd, errorMessage };
}
```

### Billing

```typescript
function resolveOpenCodeBiller(env, provider): string {
  return inferOpenAiCompatibleBiller(env, null) ?? provider ?? "unknown";
}
```

Provider is inferred from model string (e.g. `anthropic/claude-sonnet-4-5` -> `"anthropic"`).

### Models

Empty static list -- models are discovered dynamically via `opencode models`.

---

## 8. Pi Local Adapter

**Type:** `pi_local`
**Label:** "Pi (local)"
**Location:** `packages/adapters/pi-local/`

### CLI Invocation

```
pi --provider <provider> --model <model> --format json [flags] -p <prompt>
```

Key flags:
- `--session <sessionId>` -- resume session
- `--provider <name>` and `--model <id>` -- extracted from `provider/model` format
- `--thinking <level>` -- thinking level (off, minimal, low, medium, high, xhigh)
- `--append-system-prompt <path>` -- agent instructions
- `--enable-tool <tool>` -- enables tools (read, bash, edit, write, grep, find, ls)

**Prompt delivery:** Via `-p` flag.

### Session Storage

Pi stores sessions in `~/.pi/paperclips/` instead of the default location, to keep Paperclip sessions separate.

### Output Parsing

Same JSONL format as OpenCode (both use `type="text"`, `type="step_finish"`, `type="tool_use"`, `type="error"`).

Session ID from: `event.sessionID` or `event.session_id` or `event.session`.

### Skills Injection

Into `~/.pi/agent/skills/` via persistent symlinks.

### Model Discovery

`discoverPiModels()` runs `pi --list-models` to discover available models at runtime.

### Billing

```typescript
function resolvePiBiller(env, provider): string {
  return inferOpenAiCompatibleBiller(env, null) ?? provider ?? "unknown";
}
```

---

## 9. OpenClaw Gateway Adapter

**Type:** `openclaw_gateway`
**Label:** "OpenClaw Gateway"
**Location:** `packages/adapters/openclaw-gateway/`

### Protocol

Unlike all other adapters, OpenClaw Gateway uses a **WebSocket-based protocol** instead of spawning a CLI process.

Connection flow:
1. Open WebSocket to configured URL
2. Send `connect` request with device identity, client metadata, and scopes
3. Send `agent.wake` with prompt/context payload
4. Receive events via `agent.event` frames
5. Await `agent.completed` or `agent.failed` events

### Frame Types

```typescript
type GatewayRequestFrame = { type: "req"; id: string; method: string; params?: unknown };
type GatewayResponseFrame = { type: "res"; id: string; ok: boolean; payload?: unknown; error?: { code?; message? } };
type GatewayEventFrame = { type: "event"; event: string; payload?: unknown; seq?: number };
```

Protocol version: `3`

### Device Identity & Signing

The adapter supports Ed25519 device authentication:

```typescript
type GatewayDeviceIdentity = {
  deviceId: string;
  publicKeyRawBase64Url: string;
  privateKeyPem: string;
  source: "configured" | "ephemeral";
};
```

When `disableDeviceAuth` is not set, the adapter generates an Ed25519 keypair and includes a signed device payload in the connect params.

### Session Key Strategy

Controls how sessions are routed:

```typescript
type SessionKeyStrategy = "fixed" | "issue" | "run";

function resolveSessionKey(input): string {
  if (strategy === "run") return `paperclip:run:${runId}`;
  if (strategy === "issue" && issueId) return `paperclip:issue:${issueId}`;
  return configuredSessionKey ?? "paperclip";
}
```

### Wake Payload

The adapter builds a structured "wake text" that instructs the remote agent how to interact with the Paperclip API:

```
Paperclip wake event for a cloud adapter.
Run this procedure now. Do not guess undocumented endpoints...

Set these values in your run context:
PAPERCLIP_RUN_ID=...
PAPERCLIP_AGENT_ID=...
PAPERCLIP_API_URL=...
PAPERCLIP_API_KEY=<token from ~/.openclaw/workspace/paperclip-claimed-api-key.json>

Workflow:
1) GET /api/agents/me
2) Determine issueId...
3) POST /api/issues/{issueId}/checkout ...
```

### Standard Paperclip Payload

Every gateway request includes a standardized `paperclip` block:

```typescript
{
  paperclip: {
    runId, companyId, agentId, agentName,
    taskId, issueId, issueIds,
    wakeReason, wakeCommentId, approvalId, approvalStatus,
    apiUrl,
    workspace: { ... },      // resolved execution workspace
    workspaces: [ ... ],     // additional workspace hints
    workspaceRuntime: { ... } // runtime service intents
  }
}
```

### Auto-Pairing

On first "pairing required" error, the adapter attempts automatic device pairing via `device.pair.list` and `device.pair.approve` using the shared auth token.

### Runtime Service Reports

The gateway adapter can return `runtimeServices` in results, supporting `previewUrl`/`previewUrls` metadata shortcuts.

### No Session Codec

OpenClaw Gateway does **not** export a `sessionCodec` -- sessions are managed by the gateway's session key strategy.

### No Skills Support

The gateway adapter does not support skills injection (skills are managed on the remote side).

---

## 10. Process / HTTP / Hermes Adapters

**Not present in this codebase snapshot.** The user-facing configuration documentation references `process` and `http` adapter types, but their implementations are not in the `packages/adapters/` directory included in this reference snapshot. These are likely implemented in the Paperclip server package directly or in a separate repository.

Based on the `agentConfigurationDoc` references in existing adapters:
- **process**: Generic CLI process invocation for one-shot shell commands
- **http**: Webhook-based agent invocation
- **openclaw_gateway**: Is the gateway pattern (documented in section 9)

---

## 11. Billing Integration

**Location:** `packages/adapter-utils/src/billing.ts`

### OpenAI-Compatible Biller Detection

```typescript
function inferOpenAiCompatibleBiller(env: NodeJS.ProcessEnv, fallback: string | null = "openai"): string | null {
  // 1. Check for OPENROUTER_API_KEY -> "openrouter"
  // 2. Check OPENAI_BASE_URL / OPENAI_API_BASE / OPENAI_API_BASE_URL for openrouter.ai -> "openrouter"
  // 3. Return fallback (typically "openai")
}
```

### Per-Adapter Billing Resolution

| Adapter | provider | biller | billingType logic |
|---------|----------|--------|-------------------|
| claude_local | `"anthropic"` | `"anthropic"` | `ANTHROPIC_API_KEY` present -> `"api"`, else `"subscription"` |
| codex_local | `"openai"` | `"chatgpt"` / `"openai"` / `"openrouter"` | `OPENAI_API_KEY` present -> `"api"`, else `"subscription"` |
| cursor | model-inferred | `"cursor"` / `"openrouter"` / model-inferred | `CURSOR_API_KEY` or `OPENAI_API_KEY` -> `"api"`, else `"subscription"` |
| gemini_local | `"google"` | `"google"` | `GEMINI_API_KEY` or `GOOGLE_API_KEY` -> `"api"`, else `"subscription"` |
| opencode_local | model-inferred | `inferOpenAiCompatibleBiller` | `"api"` (always) |
| pi_local | model-inferred | `inferOpenAiCompatibleBiller` | `"api"` (always) |
| openclaw_gateway | N/A | N/A | Determined by remote agent |

### Cost Reporting

- **Claude:** Reports `costUsd` from `total_cost_usd` in result event
- **Codex:** Always returns `costUsd: null`
- **Cursor:** Accumulates cost from `total_cost_usd` or `cost_usd` or `cost` fields and from `step_finish.part.cost`
- **Gemini:** Reports `costUsd` from various `cost`/`cost_usd`/`total_cost_usd` fields
- **OpenCode:** Accumulates `costUsd` from `step_finish.part.cost`
- **Pi:** Same as OpenCode

### UsageSummary

All adapters report token usage in a unified format:

```typescript
interface UsageSummary {
  inputTokens: number;
  outputTokens: number;
  cachedInputTokens?: number;
}
```

---

## 12. Environment Variables

### Paperclip-Injected Variables (All Adapters)

Built by `buildPaperclipEnv()`:

| Variable | Source | Description |
|----------|--------|-------------|
| `PAPERCLIP_AGENT_ID` | `agent.id` | Agent identifier |
| `PAPERCLIP_COMPANY_ID` | `agent.companyId` | Company identifier |
| `PAPERCLIP_API_URL` | Server config | Paperclip API base URL |
| `PAPERCLIP_RUN_ID` | `runId` | Current run identifier |
| `PAPERCLIP_API_KEY` | `authToken` (fallback) | Auth token for Paperclip API |

### Context-Driven Variables (All Adapters)

| Variable | Source | Description |
|----------|--------|-------------|
| `PAPERCLIP_TASK_ID` | `context.taskId` / `context.issueId` | Task/issue identifier |
| `PAPERCLIP_WAKE_REASON` | `context.wakeReason` | Why the agent was woken |
| `PAPERCLIP_WAKE_COMMENT_ID` | `context.wakeCommentId` | Comment that triggered wake |
| `PAPERCLIP_APPROVAL_ID` | `context.approvalId` | Approval identifier |
| `PAPERCLIP_APPROVAL_STATUS` | `context.approvalStatus` | Approval status |
| `PAPERCLIP_LINKED_ISSUE_IDS` | `context.issueIds` | Comma-separated issue IDs |
| `PAPERCLIP_WORKSPACE_CWD` | workspace context | Effective workspace directory |
| `PAPERCLIP_WORKSPACE_SOURCE` | workspace context | How workspace was selected |
| `PAPERCLIP_WORKSPACE_STRATEGY` | workspace context | Workspace strategy type |
| `PAPERCLIP_WORKSPACE_ID` | workspace context | Workspace identifier |
| `PAPERCLIP_WORKSPACE_REPO_URL` | workspace context | Repository URL |
| `PAPERCLIP_WORKSPACE_REPO_REF` | workspace context | Repository ref |
| `PAPERCLIP_WORKSPACE_BRANCH` | workspace context | Branch name |
| `PAPERCLIP_WORKSPACE_WORKTREE_PATH` | workspace context | Worktree path |
| `AGENT_HOME` | workspace context | Agent home directory |
| `PAPERCLIP_WORKSPACES_JSON` | workspace hints | JSON array of workspace info |
| `PAPERCLIP_RUNTIME_SERVICE_INTENTS_JSON` | runtime context | JSON array of service intents |
| `PAPERCLIP_RUNTIME_SERVICES_JSON` | runtime context | JSON array of running services |
| `PAPERCLIP_RUNTIME_PRIMARY_URL` | runtime context | Primary runtime URL |

### Adapter-Specific Variables

| Variable | Adapter | Description |
|----------|---------|-------------|
| `ANTHROPIC_API_KEY` | claude_local | Determines API vs subscription billing |
| `OPENAI_API_KEY` | codex_local, cursor | Determines API vs subscription billing |
| `CODEX_HOME` | codex_local | Codex configuration home directory |
| `CURSOR_API_KEY` | cursor | Cursor API key |
| `GEMINI_API_KEY` / `GOOGLE_API_KEY` | gemini_local | Gemini authentication |
| `OPENCODE_DISABLE_PROJECT_CONFIG` | opencode_local | Prevents writing opencode.json to project |
| `OPENROUTER_API_KEY` | codex/cursor/opencode/pi | Detects OpenRouter as biller |
| `OPENAI_BASE_URL` | codex/cursor/opencode/pi | Detects OpenRouter via URL pattern |

### Env Merging Order

All adapters follow the same merge order:
1. `process.env` (host environment)
2. `buildPaperclipEnv(agent)` (Paperclip identity)
3. Context-driven variables (task, workspace, runtime)
4. `config.env` (user-configured adapter env)
5. `authToken` fallback (if no explicit `PAPERCLIP_API_KEY` in config)

### Sensitive Env Redaction

For logging, sensitive keys are redacted:

```typescript
const SENSITIVE_ENV_KEY = /(key|token|secret|password|passwd|authorization|cookie)/i;
```

### PATH Ensurance

All adapters call `ensurePathInEnv()` which provides a default PATH if none exists:
- Linux/macOS: `/usr/local/bin:/opt/homebrew/bin:/usr/local/sbin:/usr/bin:/bin:/usr/sbin:/sbin`
- Windows: `C:\Windows\System32;C:\Windows;C:\Windows\System32\Wbem`

---

## 13. Session Compaction

**Location:** `packages/adapter-utils/src/session-compaction.ts`

### Policy Structure

```typescript
interface SessionCompactionPolicy {
  enabled: boolean;
  maxSessionRuns: number;        // 0 = no limit
  maxRawInputTokens: number;     // 0 = no limit
  maxSessionAgeHours: number;    // 0 = no limit
}
```

### Default Policies

| Policy | maxSessionRuns | maxRawInputTokens | maxSessionAgeHours |
|--------|---------------|-------------------|-------------------|
| **Default** (threshold-based) | 200 | 2,000,000 | 72 |
| **Adapter-managed** (no rotation) | 0 | 0 | 0 |

### Per-Adapter Session Management

```typescript
const ADAPTER_SESSION_MANAGEMENT: Record<string, AdapterSessionManagement> = {
  claude_local:   { supportsSessionResume: true, nativeContextManagement: "confirmed", defaultSessionCompaction: ADAPTER_MANAGED },
  codex_local:    { supportsSessionResume: true, nativeContextManagement: "confirmed", defaultSessionCompaction: ADAPTER_MANAGED },
  cursor:         { supportsSessionResume: true, nativeContextManagement: "unknown",   defaultSessionCompaction: DEFAULT_POLICY },
  gemini_local:   { supportsSessionResume: true, nativeContextManagement: "unknown",   defaultSessionCompaction: DEFAULT_POLICY },
  opencode_local: { supportsSessionResume: true, nativeContextManagement: "unknown",   defaultSessionCompaction: DEFAULT_POLICY },
  pi_local:       { supportsSessionResume: true, nativeContextManagement: "unknown",   defaultSessionCompaction: DEFAULT_POLICY },
};
```

**Key insight:** Claude and Codex have confirmed native context management (they handle their own compaction internally), so Paperclip does not apply threshold-based session rotation. Other adapters may need Paperclip to rotate sessions when thresholds are exceeded.

### Override Resolution

```typescript
function resolveSessionCompactionPolicy(adapterType, runtimeConfig): ResolvedSessionCompactionPolicy {
  // 1. Read agent-level overrides from runtimeConfig.heartbeat.sessionCompaction
  // 2. Fall back to adapter's default policy
  // 3. Fall back to DEFAULT_SESSION_COMPACTION_POLICY (enabled only for legacy sessioned types)
  // Source: "agent_override" | "adapter_default" | "legacy_fallback"
}
```

Override paths checked: `heartbeat.sessionCompaction`, `heartbeat.sessionRotation`, `sessionCompaction`.

---

## 14. Skills System

### Skill Entry Structure

```typescript
interface PaperclipSkillEntry {
  key: string;              // e.g. "paperclipai/paperclip/skill-name"
  runtimeName: string;      // e.g. "skill-name" (directory name)
  source: string;           // Absolute path to skill source directory
  required?: boolean;
  requiredReason?: string | null;
}
```

### Injection Modes

| Adapter | Mode | Location | Mechanism |
|---------|------|----------|-----------|
| claude_local | **Ephemeral** | Temp dir / `.claude/skills/` | `--add-dir` flag, cleaned up after run |
| codex_local | **Persistent** | `CODEX_HOME/skills/` | Symlinks in managed home |
| cursor | **Persistent** | `~/.cursor/skills/` | Symlinks, full sync support |
| gemini_local | **Persistent** | `~/.gemini/skills/` | Symlinks |
| opencode_local | **Persistent** | `~/.claude/skills/` | Symlinks (shares Claude's location) |
| pi_local | **Persistent** | `~/.pi/agent/skills/` | Symlinks |

### Skill Sync

The `syncSkills` function (Cursor has the most complete implementation) performs:
1. Links all desired skills that are available
2. Unlinks managed skills that are no longer desired
3. Does not touch user-installed (non-managed) skills

### Skill Discovery

Skills are discovered from the Paperclip monorepo's `skills/` directory relative to the adapter module:

```typescript
const PAPERCLIP_SKILL_ROOT_RELATIVE_CANDIDATES = [
  "../../skills",           // When running from packages/adapters/*/src/server/
  "../../../../../skills",  // Alternative depth
];
```

---

## 15. Shared Utilities

**Location:** `packages/adapter-utils/src/server-utils.ts`

### Process Management

```typescript
// Global map of running child processes
const runningProcesses = new Map<string, RunningProcess>();

async function runChildProcess(runId, command, args, opts): Promise<RunProcessResult> {
  // Features:
  // - Resolves command in PATH (with Windows .cmd/.bat special handling)
  // - Streams stdout/stderr via onLog callbacks (sequential promise chain)
  // - Configurable timeout with SIGTERM -> SIGKILL escalation
  // - Captures up to 4MB of output per stream
  // - Records PID and start time via onSpawn callback
  // - Strips Claude Code nesting guard env vars
}
```

### Template Rendering

```typescript
function renderTemplate(template: string, data: Record<string, unknown>): string {
  // Replaces {{ dotted.path }} patterns with values from data
  return template.replace(/{{\s*([a-zA-Z0-9_.-]+)\s*}}/g, (_, path) => resolvePathValue(data, path));
}
```

All adapters use template rendering for prompt construction with a standard template data object:

```typescript
const templateData = {
  agentId: agent.id,
  companyId: agent.companyId,
  runId,
  company: { id: agent.companyId },
  agent,
  run: { id: runId, source: "on_demand" },
  context,
};
```

### Prompt Assembly

```typescript
function joinPromptSections(sections: Array<string | null | undefined>, separator = "\n\n"): string {
  // Trims each section, filters empty, joins with separator
}
```

Standard prompt assembly order for most adapters:
1. `instructionsPrefix` (from `instructionsFilePath` + path directive)
2. `renderedBootstrapPrompt` (one-time, only on fresh sessions)
3. `sessionHandoffNote` (from `context.paperclipSessionHandoffMarkdown`)
4. `paperclipEnvNote` (Cursor/Gemini only: lists PAPERCLIP_* env vars)
5. `apiAccessNote` (Gemini only: curl examples)
6. `renderedPrompt` (from `promptTemplate`)

### Log Redaction

```typescript
// Redacts home directory usernames in transcript entries and paths
function redactHomePathUserSegments(text: string): string {
  // /Users/alice -> /Users/a****
  // /home/bob -> /home/b**
  // C:\Users\carol -> C:\Users\c****
}
```

### Config Form Values

The `CreateConfigValues` type defines the UI form fields for creating an adapter config:

```typescript
interface CreateConfigValues {
  adapterType: string;
  cwd: string;
  instructionsFilePath?: string;
  promptTemplate: string;
  model: string;
  thinkingEffort: string;
  chrome: boolean;
  dangerouslySkipPermissions: boolean;
  search: boolean;
  dangerouslyBypassSandbox: boolean;
  command: string;
  args: string;
  extraArgs: string;
  envVars: string;
  envBindings: Record<string, unknown>;
  url: string;
  bootstrapPrompt: string;
  payloadTemplateJson?: string;
  workspaceStrategyType?: string;
  workspaceBaseRef?: string;
  workspaceBranchTemplate?: string;
  worktreeParentDir?: string;
  runtimeServicesJson?: string;
  maxTurnsPerRun: number;
  heartbeatEnabled: boolean;
  intervalSec: number;
}
```

### Environment Binding Types

Adapter config supports both plain values and secret references:

```typescript
// Plain value
{ type: "plain", value: "the-value" }

// Secret reference (resolved at runtime)
{ type: "secret_ref", secretId: "secret-123", version: "latest" }
```
