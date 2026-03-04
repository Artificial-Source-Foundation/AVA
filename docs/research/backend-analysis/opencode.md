# OpenCode Backend Architecture Analysis

> OpenCode by SST (formerly Anomaly) -- an AI coding agent with TUI interface, ~115k GitHub stars.
> Analyzed from source at `docs/reference-code/opencode/` (commit from early March 2026).

**Key discovery**: OpenCode was originally written in Go but has been fully rewritten in **TypeScript** running on **Bun**. The codebase uses SolidJS for the TUI (via OpenTUI), Drizzle ORM with SQLite, and the Vercel AI SDK for LLM streaming. It ships as a Bun binary.

---

## 1. Project Structure

OpenCode is a Bun-based monorepo with multiple packages:

```
opencode/
├── packages/
│   ├── opencode/           # Core agent + TUI (PRIMARY)
│   │   ├── src/
│   │   │   ├── agent/      # Agent definitions (build, plan, explore, etc.)
│   │   │   ├── auth/       # OAuth + API key authentication
│   │   │   ├── bus/        # Pub/sub event bus
│   │   │   ├── cli/        # CLI commands + TUI renderer
│   │   │   ├── command/    # Slash commands
│   │   │   ├── config/     # JSONC config system (opencode.json)
│   │   │   ├── control/    # Control plane (multi-instance)
│   │   │   ├── env/        # Environment variable management
│   │   │   ├── file/       # File watching, ripgrep integration, timestamps
│   │   │   ├── flag/       # Feature flags via env vars
│   │   │   ├── id/         # ULID-based ID generation
│   │   │   ├── lsp/        # Language Server Protocol client + server defs
│   │   │   ├── mcp/        # Model Context Protocol client
│   │   │   ├── patch/      # Unified diff patch parser + applier
│   │   │   ├── permission/ # Permission system (allow/deny/ask rules)
│   │   │   ├── plugin/     # Plugin loader (npm + built-in)
│   │   │   ├── project/    # Project detection + instance management
│   │   │   ├── provider/   # LLM provider abstraction (20+ providers)
│   │   │   ├── pty/        # Pseudo-terminal support
│   │   │   ├── question/   # Interactive question prompts
│   │   │   ├── scheduler/  # Background task scheduler
│   │   │   ├── server/     # HTTP API (Hono framework)
│   │   │   ├── session/    # Session management, message storage, agent loop
│   │   │   ├── share/      # Session sharing
│   │   │   ├── shell/      # Shell detection + process tree kill
│   │   │   ├── skill/      # Skill discovery + loading (SKILL.md files)
│   │   │   ├── snapshot/   # Git-based file snapshots for undo/revert
│   │   │   ├── storage/    # SQLite via Drizzle ORM
│   │   │   ├── tool/       # All tool implementations
│   │   │   ├── util/       # Shared utilities (glob, filesystem, token, etc.)
│   │   │   └── worktree/   # Git worktree management
│   │   ├── migration/      # Drizzle SQL migrations
│   │   └── test/           # Integration tests
│   ├── app/                # Web app frontend
│   ├── desktop/            # Tauri desktop app
│   ├── plugin/             # Plugin SDK (@opencode-ai/plugin)
│   ├── sdk/                # TypeScript SDK (@opencode-ai/sdk)
│   ├── ui/                 # Shared UI components
│   └── ...                 # Other packages (console, enterprise, storybook, etc.)
├── specs/                  # OpenAPI specs
└── package.json            # Root monorepo config (Bun workspaces)
```

**Runtime**: Bun 1.3+ (uses `bun:sqlite`, `Bun.$`, `Bun.which`, `Bun.resolve`, `HTMLRewriter`)

**Key dependencies**:
- `ai` (Vercel AI SDK v5) -- LLM streaming, tool definitions, model abstraction
- `drizzle-orm` -- Type-safe SQLite ORM
- `solid-js` + `@opentui/solid` -- TUI rendering
- `hono` -- HTTP server framework
- `zod` v4 -- Schema validation everywhere
- `web-tree-sitter` -- Bash command parsing (for permission checks)
- `vscode-jsonrpc` -- LSP communication
- `@modelcontextprotocol/sdk` -- MCP client
- `@pierre/diffs` -- Diff library
- `remeda` -- Functional utilities

---

## 2. Tools

OpenCode has ~20 built-in tools, with conditional availability based on provider, feature flags, and config.

### Core Tools

| Tool | File | Purpose | Notable Details |
|------|------|---------|-----------------|
| `bash` | `src/tool/bash.ts` | Execute shell commands | Uses tree-sitter to parse commands, extracts paths for permission checks, supports timeout + workdir |
| `read` | `src/tool/read.ts` | Read files (text, images, PDF) | Line-numbered output, 2000-line default limit, 50KB byte cap, auto-detects binary, returns base64 for images/PDFs |
| `edit` | `src/tool/edit.ts` | Find-and-replace text edits | **9 replacement strategies** with fuzzy matching fallbacks (see below) |
| `write` | `src/tool/write.ts` | Write entire file content | Generates diff for permission review, reports LSP diagnostics after write |
| `glob` | `src/tool/glob.ts` | Find files by pattern | Uses ripgrep `--files` under the hood, 100-file limit, sorted by mtime |
| `grep` | `src/tool/grep.ts` | Search file contents | Direct ripgrep subprocess, 100-match limit, sorted by mtime |
| `task` | `src/tool/task.ts` | Spawn subagent sessions | Creates child session, runs full agent loop, supports task resumption via `task_id` |
| `question` | `src/tool/question.ts` | Ask user clarifying questions | Structured multi-question format, only enabled for TUI/CLI/desktop clients |
| `todowrite` | `src/tool/todo.ts` | Update session todo list | Persisted to SQLite, full replace semantics |
| `websearch` | `src/tool/websearch.ts` | Web search via Exa MCP | Calls Exa's MCP endpoint directly, only enabled for `opencode` provider or via flag |
| `webfetch` | `src/tool/webfetch.ts` | Fetch + convert web pages | Supports text/markdown/html output, HTML-to-markdown via Turndown, Cloudflare bot detection retry |
| `codesearch` | `src/tool/codesearch.ts` | Search API docs via Exa | Uses Exa's `code_search_exa` MCP tool, configurable token count |
| `skill` | `src/tool/skill.ts` | Load domain-specific skills | Discovers SKILL.md files, injects content + bundled file list into context |
| `lsp` | `src/tool/lsp.ts` | LSP operations tool | 9 operations: goToDefinition, findReferences, hover, documentSymbol, workspaceSymbol, goToImplementation, prepareCallHierarchy, incomingCalls, outgoingCalls |
| `apply_patch` | `src/tool/apply_patch.ts` | Apply unified diff patches | Codex-style patch format, supports add/update/delete/move operations, only used for certain GPT models |
| `batch` | `src/tool/batch.ts` | Execute multiple tools in parallel | Max 25 tools per batch, experimental (config flag), cannot batch itself or MCP tools |
| `plan` (exit) | `src/tool/plan.ts` | Exit plan mode | CLI-only, saves plan to `.opencode/plans/` |
| `multiedit` | `src/tool/multiedit.ts` | Multiple edits on one file | Sequential edit operations, delegates to EditTool |
| `list` | `src/tool/ls.ts` | Directory listing with ignore patterns | Uses ripgrep, ignores node_modules/dist/build/.git etc. by default |
| `invalid` | `src/tool/invalid.ts` | Error handler for unknown tool calls | Catches malformed or nonexistent tool names |

### Edit Tool: 9 Replacement Strategies

The edit tool (`src/tool/edit.ts`) implements a cascading chain of replacement strategies sourced from Cline and Gemini CLI:

```
SimpleReplacer → LineTrimmedReplacer → BlockAnchorReplacer →
WhitespaceNormalizedReplacer → IndentationFlexibleReplacer →
EscapeNormalizedReplacer → TrimmedBoundaryReplacer →
ContextAwareReplacer → MultiOccurrenceReplacer
```

1. **SimpleReplacer** -- Exact string match
2. **LineTrimmedReplacer** -- Matches after trimming each line
3. **BlockAnchorReplacer** -- Matches first/last lines as anchors, uses Levenshtein distance for middle content
4. **WhitespaceNormalizedReplacer** -- Collapses whitespace to single spaces
5. **IndentationFlexibleReplacer** -- Strips minimum indentation before comparing
6. **EscapeNormalizedReplacer** -- Unescapes `\n`, `\t`, `\"` etc. before matching
7. **TrimmedBoundaryReplacer** -- Trims leading/trailing whitespace from search string
8. **ContextAwareReplacer** -- Uses first/last lines as context anchors with 50% similarity threshold
9. **MultiOccurrenceReplacer** -- Yields all exact matches (for `replaceAll` mode)

Each replacer is a generator that yields candidate matches. The first unique match wins.

### Tool Conditional Logic

- `apply_patch` is used instead of `edit`/`write` for GPT models (except GPT-4 and OSS variants)
- `websearch`/`codesearch` require the `opencode` provider or `OPENCODE_ENABLE_EXA` flag
- `lsp` requires `OPENCODE_EXPERIMENTAL_LSP_TOOL` flag
- `batch` requires `experimental.batch_tool: true` in config
- `question` only enabled for `app`, `cli`, or `desktop` clients
- `plan_exit` only in CLI with `OPENCODE_EXPERIMENTAL_PLAN_MODE`
- Custom tools loaded from `.opencode/tool/*.{js,ts}` files and plugins

### Tool Output Truncation

All tool output is automatically truncated (`src/tool/truncation.ts`):
- **Max 2000 lines** or **50KB** (whichever hits first)
- Truncated content saved to `$DATA_DIR/tool-output/` with a ULID filename
- Agent is told to use `Grep`/`Read` with offset or delegate to explore agent
- Cleanup scheduler runs hourly, retains files for 7 days

---

## 3. Agent Loop

The agent loop is the core execution engine, located primarily in two files:

- **`src/session/prompt.ts`** (`SessionPrompt.loop`) -- Outer loop: message management, compaction triggers, step counting
- **`src/session/processor.ts`** (`SessionProcessor.process`) -- Inner loop: LLM streaming, part persistence, doom loop detection

### Outer Loop (`SessionPrompt.loop`)

```
SessionPrompt.loop(sessionID)
  │
  ├─ while (true)
  │   ├─ Load messages from DB (filtered for compaction)
  │   ├─ Find lastUser, lastAssistant, lastFinished
  │   │
  │   ├─ Check exit conditions:
  │   │   - If lastAssistant has a terminal finish reason AND is newer than lastUser → break
  │   │
  │   ├─ Handle pending subtasks (from @ mentions / command subtasks)
  │   │   - Create child session, run task tool inline
  │   │
  │   ├─ Handle pending compaction
  │   │   - Run SessionCompaction.process()
  │   │
  │   ├─ Check context overflow → trigger auto-compaction
  │   │
  │   ├─ Normal processing:
  │   │   ├─ Resolve agent, model, tools
  │   │   ├─ Insert "system-reminder" wrappers on queued user messages
  │   │   ├─ Build system prompt (environment + instructions)
  │   │   ├─ Create SessionProcessor
  │   │   ├─ Call processor.process(streamInput)
  │   │   │
  │   │   ├─ If structured output captured → break
  │   │   ├─ If "stop" → break
  │   │   ├─ If "compact" → create compaction marker, continue
  │   │   └─ continue (for tool-calls finish reason)
  │   │
  │   └─ After loop: prune old tool outputs, resolve callbacks
  │
  └─ Return last assistant message
```

### Inner Loop (`SessionProcessor.process`)

The processor handles the actual LLM stream consumption:

```
processor.process(streamInput)
  │
  ├─ while (true)
  │   ├─ Call LLM.stream() → get fullStream
  │   │
  │   ├─ For each stream event:
  │   │   ├─ "start" → set session status to busy
  │   │   ├─ "reasoning-start/delta/end" → persist reasoning parts
  │   │   ├─ "text-start/delta/end" → persist text parts (with delta streaming)
  │   │   ├─ "tool-input-start" → create pending tool part
  │   │   ├─ "tool-call" → update to running, check doom loop
  │   │   ├─ "tool-result" → update to completed
  │   │   ├─ "tool-error" → update to error, check if blocked
  │   │   ├─ "start-step" → take git snapshot
  │   │   ├─ "finish-step" → compute usage/cost, check overflow, create patch
  │   │   ├─ "error" → throw (handled below)
  │   │   └─ "finish" → no-op
  │   │
  │   ├─ Catch errors:
  │   │   ├─ ContextOverflowError → needs compaction
  │   │   ├─ Retryable error → exponential backoff, retry
  │   │   └─ Fatal error → publish error event, break
  │   │
  │   ├─ Clean up incomplete tool parts
  │   └─ Return "continue" | "stop" | "compact"
```

### Doom Loop Detection

The processor detects doom loops when the last 3 tool calls have the same tool name AND identical input. When detected, it triggers a `doom_loop` permission check, which defaults to "ask" mode -- pausing execution for user confirmation.

```typescript
// processor.ts lines 152-176
const lastThree = parts.slice(-DOOM_LOOP_THRESHOLD)
if (
  lastThree.length === DOOM_LOOP_THRESHOLD &&
  lastThree.every(
    (p) => p.type === "tool" && p.tool === value.toolName &&
      p.state.status !== "pending" &&
      JSON.stringify(p.state.input) === JSON.stringify(value.input),
  )
) {
  await PermissionNext.ask({ permission: "doom_loop", ... })
}
```

### Tool Call Repair

The LLM stream uses `experimental_repairToolCall` to handle malformed tool calls:
- If tool name is wrong case (e.g., `Bash` instead of `bash`), it lowercases and retries
- Otherwise, redirects to the `invalid` tool with the error message

### Step Limiting

Agents can have a `steps` property limiting the number of loop iterations. When the last step is reached, a `MAX_STEPS` prompt is injected as an assistant message prefix, instructing the model to wrap up.

---

## 4. LLM Providers

OpenCode uses the Vercel AI SDK as its provider abstraction layer. Providers are loaded dynamically from a model registry (`models.dev`) and instantiated via bundled `@ai-sdk/*` packages.

### Provider Architecture

**File**: `src/provider/provider.ts`

The provider system has three layers:

1. **Model Registry** (`src/provider/models.ts`) -- Fetches model definitions from `models.dev` (a JSON file listing all available models with capabilities, pricing, context limits). Cached locally.

2. **Bundled SDK Providers** -- 20 AI SDK packages directly imported:

| Provider SDK | Provider ID |
|---|---|
| `@ai-sdk/anthropic` | anthropic |
| `@ai-sdk/openai` | openai |
| `@ai-sdk/google` | google |
| `@ai-sdk/google-vertex` | google-vertex |
| `@ai-sdk/google-vertex/anthropic` | google-vertex-anthropic |
| `@ai-sdk/amazon-bedrock` | amazon-bedrock |
| `@ai-sdk/azure` | azure |
| `@ai-sdk/openai-compatible` | (custom providers) |
| `@openrouter/ai-sdk-provider` | openrouter |
| `@ai-sdk/xai` | xai |
| `@ai-sdk/mistral` | mistral |
| `@ai-sdk/groq` | groq |
| `@ai-sdk/deepinfra` | deepinfra |
| `@ai-sdk/cerebras` | cerebras |
| `@ai-sdk/cohere` | cohere |
| `@ai-sdk/gateway` | ai-gateway |
| `@ai-sdk/togetherai` | togetherai |
| `@ai-sdk/perplexity` | perplexity |
| `@ai-sdk/vercel` | vercel |
| `@gitlab/gitlab-ai-provider` | gitlab |
| Custom GitHub Copilot | github-copilot |

3. **Custom Loaders** -- Per-provider initialization logic handling auth, API format selection (chat vs. responses), region prefixing (Bedrock), etc.

### Provider-Specific Model Prompts

The system prompt varies by model family (`src/session/system.ts`):

```typescript
export function provider(model: Provider.Model) {
  if (model.api.id.includes("gpt-5")) return [PROMPT_CODEX]
  if (model.api.id.includes("gpt-")) return [PROMPT_BEAST]
  if (model.api.id.includes("gemini-")) return [PROMPT_GEMINI]
  if (model.api.id.includes("claude")) return [PROMPT_ANTHROPIC]
  if (model.api.id.includes("trinity")) return [PROMPT_TRINITY]
  return [PROMPT_ANTHROPIC_WITHOUT_TODO]  // Default (Qwen-style)
}
```

### LLM Streaming (`src/session/llm.ts`)

The `LLM.stream()` function orchestrates the actual API call:

- Builds system prompt (agent prompt OR provider prompt + custom prompt + user system)
- Resolves provider options (temperature, topP, topK, max output tokens)
- Filters tools by permission rules
- Wraps the language model with middleware for message transformation
- Adds a `_noop` dummy tool for LiteLLM proxy compatibility when history has tool calls but no active tools
- Calls `streamText()` from the AI SDK

### Cost Calculation

Token costs are calculated per-step using `Decimal.js` for precision:

```typescript
// src/session/index.ts (getUsage)
cost = (input * cost.input / 1M) + (output * cost.output / 1M)
     + (cache_read * cost.cache.read / 1M) + (cache_write * cost.cache.write / 1M)
     + (reasoning * cost.output / 1M)  // reasoning charged at output rate
```

Special handling for Anthropic (excludes cached tokens from input count) vs. other providers (includes cached tokens in input).

---

## 5. Context / Token Management

### Compaction System (`src/session/compaction.ts`)

OpenCode uses a compaction system (analogous to Claude Code's context compression):

**Overflow Detection**:
```typescript
const usable = model.limit.input
  ? model.limit.input - reserved
  : context - maxOutputTokens
return count >= usable
```
Where `reserved` defaults to `min(20000, maxOutputTokens)`.

**Compaction Process**:
1. When token usage exceeds the usable context, a compaction is triggered
2. A dedicated "compaction" agent (hidden, no tools) processes the full conversation
3. It generates a structured summary with sections: Goal, Instructions, Discoveries, Accomplished, Relevant Files
4. The summary becomes a new message marked with `summary: true`
5. On overflow during compaction, it finds the last non-compacted user message to replay after compaction
6. Media attachments are stripped during compaction to save space

**Pruning** (`SessionCompaction.prune`):
- Walks backwards through message parts
- Protects the last 40,000 tokens worth of tool outputs
- Marks older tool outputs as `compacted` (output is preserved on disk but stripped from context)
- Minimum prunable threshold: 20,000 tokens
- Protected tools: `skill` (never pruned)

### Output Truncation

Tool output truncation is applied uniformly (2000 lines / 50KB), with full output saved to disk. This is a key mechanism for keeping context small.

### Token Estimation

`src/util/token.ts` uses a character-based estimation (not a tokenizer):
```typescript
export function estimate(text: string): number {
  return Math.ceil(text.length / 4)  // rough 4 chars per token
}
```

---

## 6. Session Management

### Database Schema (`src/session/session.sql.ts`)

SQLite database at `$DATA_DIR/opencode.db` using Drizzle ORM with WAL mode.

**Tables**:

| Table | Purpose |
|-------|---------|
| `session` | Session metadata (id, project_id, workspace_id, parent_id, title, slug, version, summary stats, share URL, revert info, permissions, timestamps) |
| `message` | Messages within sessions (id, session_id, timestamps, JSON data blob) |
| `part` | Message parts (id, message_id, session_id, timestamps, JSON data blob) |
| `todo` | Per-session todo items (session_id, content, status, priority, position) |
| `permission` | Per-project permission rules (project_id, JSON ruleset) |
| `project` | Project metadata (id, name, worktree path) |

**Key design decisions**:
- Messages and parts use JSON blobs for data (flexible schema, avoids migrations for new part types)
- CASCADE deletes from session -> messages -> parts
- Descending ULID IDs for sessions (newest first), ascending for messages/parts
- WAL journal mode with `synchronous = NORMAL` and 5-second busy timeout

### Session Model

```typescript
Session.Info = {
  id: string              // ULID
  slug: string            // URL-friendly slug
  projectID: string
  workspaceID?: string
  directory: string
  parentID?: string       // For child/forked sessions
  title: string
  version: string         // OpenCode version
  summary?: { additions, deletions, files, diffs }
  share?: { url }
  revert?: { messageID, partID?, snapshot?, diff? }
  permission?: PermissionRuleset
  time: { created, updated, compacting?, archived? }
}
```

### Message Types

Messages are discriminated by `role`:

- **User** -- Contains model selection, agent name, variant, tools config, format preference, optional system prompt
- **Assistant** -- Contains model/provider IDs, agent name, mode, cost, token counts, finish reason, optional error, optional summary flag, optional structured output

### Part Types

Parts are the atomic units within messages:

| Part Type | Purpose |
|-----------|---------|
| `text` | Text content (with delta streaming support) |
| `reasoning` | Chain-of-thought / thinking content |
| `tool` | Tool call with state machine: pending -> running -> completed/error |
| `file` | Attached files (images, PDFs, directories) |
| `step-start` | Marks the beginning of an LLM step (with git snapshot hash) |
| `step-finish` | Marks end of step (with token usage, cost, finish reason) |
| `patch` | Git diff patches (files changed between snapshots) |
| `compaction` | Marks a compaction boundary |
| `agent` | Agent mention (@ syntax) |
| `subtask` | Queued subtask for execution |

### Session Operations

- **Create** -- New session with auto-title, optional parent for child sessions
- **Fork** -- Deep copy of session up to a specific message, with ID remapping
- **Archive** -- Soft delete via `time_archived` timestamp
- **Share** -- Upload to sharing service, store URL
- **Revert** -- Track revert state with snapshot hash for undo

### Event Bus (`src/bus/index.ts`)

Simple pub/sub system scoped to project instances:
- Type-safe events defined with `BusEvent.define(type, zodSchema)`
- Subscriptions return unsubscribe functions
- Events forwarded to `GlobalBus` for cross-instance communication
- Used for session events, message events, LSP updates, file changes, permissions

---

## 7. LSP Integration

### Architecture

**Files**: `src/lsp/index.ts`, `src/lsp/client.ts`, `src/lsp/server.ts`

OpenCode manages LSP servers as long-lived processes, spawning them on demand when files of matching extensions are opened.

### LSP Servers (`src/lsp/server.ts`)

Built-in server definitions:

| Server | Extensions | Root Detection |
|--------|-----------|----------------|
| `typescript` | .ts, .tsx, .js, .jsx, .mjs, .cjs, .mts, .cts | Nearest package lock file (excludes Deno projects) |
| `deno` | .ts, .tsx, .js, .jsx, .mjs | `deno.json` or `deno.jsonc` |
| `pyright` | .py | Nearest Python project marker |
| `ty` | .py | Experimental alternative to pyright |
| Custom | configurable | Via `opencode.json` lsp config |

Custom LSP servers can be defined in config:
```json
{
  "lsp": {
    "my-server": {
      "command": ["my-lsp", "--stdio"],
      "extensions": [".rs"],
      "env": { "RUST_LOG": "info" }
    }
  }
}
```

### LSP Client (`src/lsp/client.ts`)

The client uses `vscode-jsonrpc` for communication over stdio:

- Full LSP initialization with workspace folders, configuration, and capabilities
- Tracks open files with version numbers
- Diagnostics collection with debounced publishing (150ms)
- Supports `textDocument/didOpen`, `textDocument/didChange`, `workspace/didChangeWatchedFiles`

### LSP Operations Available

The `LspTool` exposes these operations:
1. `goToDefinition`
2. `findReferences`
3. `hover`
4. `documentSymbol`
5. `workspaceSymbol`
6. `goToImplementation`
7. `prepareCallHierarchy`
8. `incomingCalls`
9. `outgoingCalls`

### Integration with Edit/Write Tools

After every file edit or write, the tool:
1. Calls `LSP.touchFile(filePath, true)` to notify the LSP server
2. Waits for diagnostics (3-second timeout with 150ms debounce)
3. Appends any errors to the tool output:
```
LSP errors detected in this file, please fix:
<diagnostics file="/path/to/file.ts">
ERROR [12:5] Property 'foo' does not exist on type 'Bar'
</diagnostics>
```

This gives the agent immediate feedback on type errors introduced by edits.

---

## 8. Permissions / Safety

### Permission System (`src/permission/next.ts`)

OpenCode uses a rule-based permission system with three actions: `allow`, `deny`, `ask`.

**Rule Structure**:
```typescript
{
  permission: string  // Tool name or category (e.g., "bash", "edit", "external_directory")
  pattern: string     // Glob pattern for the argument (e.g., "*.env", "*/node_modules/*")
  action: "allow" | "deny" | "ask"
}
```

**Evaluation**: Rules are evaluated in order (last matching rule wins), using wildcard matching on both `permission` and `pattern`.

### Default Permissions

The default agent ("build") has these permissions:

```typescript
{
  "*": "allow",                    // All tools allowed by default
  doom_loop: "ask",                // Ask on doom loop detection
  external_directory: {
    "*": "ask",                    // Ask for directories outside project
    "<skill_dirs>": "allow",       // Allow skill directories
  },
  question: "allow",               // Allow asking questions
  plan_enter: "allow",             // Allow entering plan mode
  read: {
    "*": "allow",
    "*.env": "ask",                // Ask before reading .env files
    "*.env.*": "ask",
    "*.env.example": "allow",      // But allow .env.example
  },
}
```

The "plan" agent restricts edit tools:
```typescript
edit: {
  "*": "deny",                     // No edits allowed
  ".opencode/plans/*.md": "allow", // Except plan files
}
```

The "explore" agent is read-only:
```typescript
{
  "*": "deny",
  grep: "allow", glob: "allow", list: "allow",
  bash: "allow", webfetch: "allow", websearch: "allow",
  codesearch: "allow", read: "allow",
}
```

### Bash Command Parsing

The bash tool uses **tree-sitter** to parse commands before execution:

1. Parses the bash command into an AST
2. Extracts file paths from commands like `cd`, `rm`, `cp`, `mv`, `mkdir`, `touch`, `chmod`, `chown`, `cat`
3. Resolves paths via `realpath` to check if they're outside the project directory
4. Extracts command names for permission pattern matching
5. Uses `BashArity.prefix()` to generate canonical patterns for "always allow" rules

### Permission Request Flow

```
Tool calls ctx.ask({ permission, patterns, always, metadata })
  → PermissionNext.ask() evaluates rules
    → If "allow": continue silently
    → If "deny": throw DeniedError (halts tool)
    → If "ask": publish Event.Asked, wait for user response
      → User responds "once": allow this call only
      → User responds "always": add to approved ruleset, resolve pending
      → User responds "reject": throw RejectedError (optionally with correction message)
```

### Error Types

- **RejectedError** -- User rejected without message, halts execution
- **CorrectedError** -- User rejected with feedback message, agent receives guidance
- **DeniedError** -- Auto-rejected by config rule, includes relevant rules in error

### File Time Assertion

The `FileTime` system (`src/file/time.ts`) tracks when files were last read by the agent. Before writing, it asserts that the file hasn't been modified externally since the agent last read it. This prevents overwriting user changes.

---

## 9. Git Integration

### Snapshot System (`src/snapshot/index.ts`)

OpenCode maintains a **separate git repository** for tracking file changes during agent sessions:

- Location: `$DATA_DIR/git/` (a bare git repo using the project worktree)
- **`Snapshot.track()`** -- Runs `git add -A` + `git write-tree` to capture current state, returns tree hash
- **`Snapshot.patch(hash)`** -- Runs `git diff --name-only <hash>` to get changed files since snapshot
- Called at `start-step` and `finish-step` of each LLM turn

This enables:
- Per-step file change tracking (patch parts in messages)
- Session-level diff summaries
- Undo/revert to any step's snapshot

### Git Worktree Support (`src/worktree/index.ts`)

Full git worktree management:
- Create worktrees with custom names
- Switch between worktrees
- Clean up worktrees when done
- Events: `worktree.ready`, `worktree.failed`

### Project Detection (`src/project/instance.ts`)

Projects are identified by their git root (or working directory for non-git projects):
- VCS detection (`git`)
- Directory containment checks (`Instance.containsPath()`)
- Worktree vs. repository root distinction

---

## 10. TUI Architecture

OpenCode uses **OpenTUI** (`@opentui/solid`) -- a SolidJS-based terminal UI framework (Ink-like but for SolidJS). The TUI code lives in `src/cli/cmd/tui/`.

**Key patterns**:
- SolidJS reactive primitives (signals, effects, memos)
- Event-driven via the Bus system
- Session status tracking (`idle`, `busy`, `retry`)
- Real-time streaming updates via `MessageV2.Event.PartDelta`

The TUI is not the focus of this analysis, but it is worth noting that the same HTTP server (`src/server/server.ts` using Hono) serves both the TUI and the web/desktop frontends, providing a unified API.

---

## 11. Unique Features and Design Patterns

### Agent System

OpenCode defines multiple agent personalities, not just one:

| Agent | Mode | Purpose |
|-------|------|---------|
| `build` | primary | Default agent. Full tool access with permission-gated edits. |
| `plan` | primary | Plan mode. Disallows all edit tools except plan files. |
| `explore` | subagent | Fast read-only exploration. No edit tools. |
| `general` | subagent | General-purpose subagent for parallel tasks. |
| `compaction` | primary (hidden) | Generates conversation summaries for compaction. |
| `title` | primary (hidden) | Generates session titles. |
| `summary` | primary (hidden) | Generates session summaries. |

Agents are fully configurable in `opencode.json`:
```json
{
  "agent": {
    "my-agent": {
      "model": "anthropic/claude-sonnet-4-20250514",
      "prompt": "You are a security auditor...",
      "description": "Security-focused code reviewer",
      "mode": "subagent",
      "temperature": 0.3,
      "permission": { "edit": { "*": "deny" } }
    }
  }
}
```

Agents can also be generated via LLM (`Agent.generate()`).

### Instance State Pattern

OpenCode uses a pervasive `Instance.state()` pattern for managing singleton state per project instance:

```typescript
const state = Instance.state(
  async () => {
    // Initialize state (called once per instance)
    return { clients: [], servers: {} }
  },
  async (state) => {
    // Cleanup (called when instance is disposed)
    await Promise.all(state.clients.map(c => c.shutdown()))
  },
)
```

This pattern ensures:
- Lazy initialization
- Per-project isolation (multiple projects can run simultaneously)
- Clean teardown on instance disposal
- Type-safe state access

### Namespace Pattern

Every module uses TypeScript namespaces as the primary organization unit:

```typescript
export namespace Session {
  export const Info = z.object({ ... })
  export type Info = z.infer<typeof Info>
  export const Event = { ... }
  export async function create(...) { ... }
}
```

This is used consistently across the entire codebase (Session, Provider, Agent, Config, LSP, MCP, etc.).

### Config System (`src/config/config.ts`)

Multi-layered JSONC configuration:

1. **System managed** (`/etc/opencode/` or platform equivalent) -- Enterprise admin settings, highest priority
2. **User global** (`~/.config/opencode/config.json`)
3. **Project local** (`.opencode/config.json` in project root)
4. **Config directories** -- `.opencode/`, `.claude/`, `.agents/` (for skills and custom tools)
5. **Environment overrides** -- Via `OPENCODE_*` env vars

Config supports:
- Provider configuration with custom base URLs
- Agent definitions with custom prompts
- LSP server definitions
- MCP server definitions
- Permission rules
- Experimental feature flags

### Plugin System (`src/plugin/index.ts`)

Plugins are npm packages that export a function receiving a `PluginInput`:

```typescript
type Plugin = (input: PluginInput) => Promise<Hooks>

interface PluginInput {
  client: OpencodeClient  // SDK client for the local server
  project: Project
  worktree: string
  directory: string
  serverUrl: string
  $: BunShell
}
```

Hooks available:
- `chat.params` -- Modify LLM parameters before streaming
- `chat.headers` -- Add custom headers to LLM requests
- `experimental.chat.system.transform` -- Modify system prompts
- `experimental.chat.messages.transform` -- Modify message history
- `experimental.text.complete` -- Post-process text output
- `experimental.session.compacting` -- Inject context into compaction
- `tool.definition` -- Modify tool descriptions/parameters
- `tool.execute.before` / `tool.execute.after` -- Pre/post tool execution hooks
- `shell.env` -- Inject environment variables into shell commands
- Custom tool definitions

Built-in plugins: Codex auth, Copilot auth, GitLab auth, Anthropic auth.

### Skill System (`src/skill/skill.ts`)

Skills are markdown files (`SKILL.md`) with YAML frontmatter:

```markdown
---
name: react-patterns
description: React component patterns and best practices
---

# React Patterns

When editing React components, follow these patterns...
```

Discovery locations (in priority order):
1. `.opencode/skills/**/SKILL.md`
2. `.claude/skills/**/SKILL.md`
3. `.agents/skills/**/SKILL.md`
4. `~/.config/opencode/skills/**/SKILL.md` (global)

Skills are loaded on demand via the `skill` tool. The agent sees skill descriptions in the tool listing and loads full content when a task matches.

### HTTP Server (`src/server/`)

A Hono-based HTTP server provides:
- REST API for session management (CRUD, prompt, cancel)
- WebSocket/SSE for real-time events
- Permission request/reply endpoints
- LSP status, MCP status
- Session sharing
- OpenAPI spec generation

This enables the web frontend, desktop app, and external tools to interact with the agent.

### MCP Client (`src/mcp/index.ts`)

Full MCP client implementation supporting:
- **stdio** transport (local processes)
- **SSE** transport (HTTP-based servers)
- **StreamableHTTP** transport
- OAuth authentication flow with callback handling
- Tool discovery and dynamic registration
- Auto-reconnection

MCP tools are exposed alongside native tools in the agent's tool set.

### Identifier System (`src/id/id.ts`)

Uses ULIDs for all entity IDs:
- `Identifier.ascending("session")` -- Time-sorted ascending (for messages, parts)
- `Identifier.descending("session")` -- Time-sorted descending (for sessions, newest first)
- Type-prefixed for debugging (`session_`, `message_`, `part_`, `permission_`, `tool_`)

### Feature Flags (`src/flag/flag.ts`)

Environment variable-based feature flags:
- `OPENCODE_EXPERIMENTAL_LSP_TOOL` -- Enable LSP tool
- `OPENCODE_EXPERIMENTAL_PLAN_MODE` -- Enable plan mode
- `OPENCODE_EXPERIMENTAL_BASH_DEFAULT_TIMEOUT_MS` -- Custom bash timeout
- `OPENCODE_EXPERIMENTAL_LSP_TY` -- Use ty instead of pyright
- `OPENCODE_ENABLE_EXA` -- Enable web search/code search
- `OPENCODE_CLIENT` -- Client type (cli, app, desktop, acp)
- `OPENCODE_DISABLE_DEFAULT_PLUGINS` -- Skip built-in plugins

---

## Summary of Key Architectural Differences from AVA

| Aspect | OpenCode | AVA |
|--------|----------|-----|
| **Runtime** | Bun (single binary) | Node.js + Tauri |
| **Language** | TypeScript (was Go) | TypeScript |
| **LLM SDK** | Vercel AI SDK v5 | Direct provider SDKs |
| **Database** | SQLite via Drizzle ORM | SQLite via Platform abstraction |
| **UI Framework** | SolidJS + OpenTUI (terminal) | SolidJS + Tauri (desktop) |
| **Tool Definition** | `Tool.define(id, init)` with Zod | `defineTool()` with Zod |
| **Edit Strategy** | 9 cascading fuzzy replacers | 8 strategies |
| **Permission Model** | Rule-based allow/deny/ask with glob patterns | Middleware-based with config |
| **Context Management** | Summary-based compaction + pruning | Compaction + prune strategy |
| **Extension Model** | npm plugins + hooks system | ExtensionAPI + built-in extensions |
| **Agent Types** | Named agents (build/plan/explore/custom) | Praxis hierarchy (Commander/Leads/Workers) |
| **LSP** | Direct spawn + vscode-jsonrpc | Direct spawn + vscode-jsonrpc |
| **Model Registry** | models.dev (external JSON) | Built-in model registry |
| **Namespace Pattern** | TypeScript namespaces everywhere | Module-level exports |
| **Session Model** | Messages + Parts (JSON blobs in SQLite) | Session DAG with branching |
| **Snapshot/Undo** | Separate git repo for snapshots | Git checkpoints |
