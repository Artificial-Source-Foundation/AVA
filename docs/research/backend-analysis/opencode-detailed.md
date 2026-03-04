# OpenCode: Deep Competitive Intelligence Analysis

> VALUE-FOCUSED analysis of OpenCode's backend architecture. Not just what exists, but WHY
> each design decision was made, what problems it solves, and what would break without it.
> Companion document to `opencode.md` (which covers the factual "what").

---

## Table of Contents

1. [Executive Summary](#1-executive-summary)
2. [The Bun Bet: Why a Single Runtime Changes Everything](#2-the-bun-bet-why-a-single-runtime-changes-everything)
3. [Tool Architecture: The `define()` Pattern](#3-tool-architecture-the-define-pattern)
4. [The 9-Layer Edit Fuzzer: Why Defensive Editing Wins](#4-the-9-layer-edit-fuzzer-why-defensive-editing-wins)
5. [Tree-Sitter Bash Parsing: Why Shell Safety Needs ASTs](#5-tree-sitter-bash-parsing-why-shell-safety-needs-asts)
6. [Permission System: Rule-Based Safety vs Middleware](#6-permission-system-rule-based-safety-vs-middleware)
7. [The Instance State Pattern: Per-Project Isolation](#7-the-instance-state-pattern-per-project-isolation)
8. [Agent System: Why Multiple Personalities Beat One](#8-agent-system-why-multiple-personalities-beat-one)
9. [Skill Discovery: The SKILL.md Ecosystem](#9-skill-discovery-the-skillmd-ecosystem)
10. [Context Compaction: Pruning vs Summarization](#10-context-compaction-pruning-vs-summarization)
11. [LSP Integration: Why Every Edit Needs Diagnostics](#11-lsp-integration-why-every-edit-needs-diagnostics)
12. [Snapshot System: Shadow Git for Agent Sessions](#12-snapshot-system-shadow-git-for-agent-sessions)
13. [MCP Client: The OAuth-First Design](#13-mcp-client-the-oauth-first-design)
14. [Plugin Hooks: The Extension Point Philosophy](#14-plugin-hooks-the-extension-point-philosophy)
15. [Namespace Pattern: TypeScript as Architecture](#15-namespace-pattern-typescript-as-architecture)
16. [What Would Break Without Each System](#16-what-would-break-without-each-system)
17. [Competitive Advantages vs AVA](#17-competitive-advantages-vs-ava)
18. [Key Takeaways for AVA](#18-key-takeaways-for-ava)

---

## 1. Executive Summary

OpenCode (~115k GitHub stars) is the **most ambitious TypeScript-based AI coding agent**. Originally written in Go, it was completely rewritten in TypeScript running on Bun — a bet that TypeScript's ecosystem + Bun's performance could match Go's speed while offering better developer velocity.

**Key competitive moats:**

- **Bun-native architecture** — Uses `bun:sqlite`, `Bun.$`, `Bun.which`, `HTMLRewriter` for sub-50ms startup
- **9-layer fuzzy edit matching** — Borrowed from Cline and Gemini CLI, cascades through 9 replacement strategies
- **Tree-sitter bash parsing** — Extracts file paths from shell commands BEFORE execution for permission checks
- **Rule-based permission system** — Glob patterns with allow/deny/ask, not middleware-based
- **Instance state pattern** — Per-project isolation without process spawning
- **Multi-agent personalities** — build/plan/explore/compaction/title/summary with different permission sets
- **SKILL.md ecosystem** — Auto-discovers skills from `.opencode/`, `.claude/`, `.agents/` directories
- **Shadow git snapshots** — Separate git repo for session-level change tracking
- **MCP OAuth-first** — Built-in OAuth flow handling with callback server
- **20+ LLM providers** — Vercel AI SDK with bundled provider packages

**Core insight:** OpenCode's value is in its **defensive architecture**. Every system is designed to handle failure gracefully — the 9-layer edit fuzzer, tree-sitter path extraction, cascading permission rules, and compaction with overflow handling. This is an agent built for production use where things go wrong constantly.

---

## 2. The Bun Bet: Why a Single Runtime Changes Everything

### The Problem It Solves

Most TypeScript agents run on Node.js, which means:
- Slow startup (200ms-2s for runtime init)
- No built-in SQLite (need better-sqlite3 or similar)
- Complex build pipelines (ts-node, tsx, or pre-compilation)
- Shell execution via child_process (verbose, error-prone)

### Why Bun

OpenCode bet on Bun as its **sole runtime**. This is not just "using a faster Node.js" — it unlocks capabilities that change the architecture:

| Capability | Node.js | Bun |
|------------|---------|-----|
| Built-in SQLite | ❌ (native module) | ✅ `bun:sqlite` |
| Shell execution | `child_process` | `Bun.$` tagged template |
| Binary resolution | `which` npm package | `Bun.which()` native |
| Module resolution | `require.resolve` | `Bun.resolve()` |
| HTML parsing | jsdom / cheerio | `HTMLRewriter` native |
| TypeScript | ts-node / tsx | Native (no build step) |
| Startup time | 200ms-2s | <50ms |

**The real advantage:** OpenCode ships as a **single Bun binary** with zero dependencies. Users run `bun install -g opencode` and it just works. No Docker, no Python environment, no Node.js version hell.

### What Would Break Without Bun

Without Bun:
1. **No built-in SQLite** — Would need better-sqlite3 (native module compilation issues)
2. **Complex build system** — Would need TypeScript compilation step, watch mode, source maps
3. **Slower shell execution** — `child_process.spawn` vs `Bun.$` template literals
4. **Larger bundle size** — Would need to bundle Node.js runtime or ship as npm package with peer dependencies
5. **No `Bun.which()`** — Would need cross-platform `which` implementation

---

## 3. Tool Architecture: The `define()` Pattern

### The Problem It Solves

Tools in AI agents need:
- Type-safe parameters (Zod schemas)
- Consistent execution context (sessionID, messageID, abort signal)
- Automatic output truncation
- Metadata tracking for UI
- Permission integration

### The Pattern

OpenCode uses a `Tool.define()` factory that creates a tool with all cross-cutting concerns:

```typescript
export const EditTool = Tool.define("edit", {
  description: DESCRIPTION,
  parameters: z.object({
    filePath: z.string(),
    oldString: z.string(),
    newString: z.string(),
    replaceAll: z.boolean().optional(),
  }),
  async execute(params, ctx) {
    // ctx has: sessionID, messageID, abort, ask(), metadata()
    // Output automatically truncated after execution
    return { title, output, metadata }
  },
})
```

**Why this matters:** The `define()` function wraps every tool with:
1. **Parameter validation** — Zod schema parsing with custom error formatting
2. **Automatic truncation** — Output truncated to 2000 lines / 50KB, saved to disk if larger
3. **Metadata streaming** — `ctx.metadata()` streams partial results to UI in real-time
4. **Permission hooks** — `ctx.ask()` triggers the permission system before execution
5. **Abort handling** — Automatic cleanup if user cancels

### What Would Break Without It

Without `Tool.define()`:
- Each tool would need to implement truncation logic (duplicated code, inconsistent behavior)
- No automatic parameter validation (LLM would get raw errors)
- No real-time metadata streaming (UI would be static)
- Permission checks would be manual (inconsistent safety)
- Abort handling would be per-tool (resource leaks)

---

## 4. The 9-Layer Edit Fuzzer: Why Defensive Editing Wins

### The Problem It Solves

LLMs are terrible at exact text matching. They:
- Miscount whitespace
- Include/exclude trailing newlines inconsistently
- Change indentation in generated code
- Escape characters differently
- Match multiple occurrences when they meant one

Most agents use 1-2 fallback strategies. OpenCode uses **9 cascading replacers**.

### The Architecture

```
SimpleReplacer → LineTrimmedReplacer → BlockAnchorReplacer →
WhitespaceNormalizedReplacer → IndentationFlexibleReplacer →
EscapeNormalizedReplacer → TrimmedBoundaryReplacer →
ContextAwareReplacer → MultiOccurrenceReplacer
```

**Why 9 layers:**

1. **SimpleReplacer** — Exact match (handles 80% of cases)
2. **LineTrimmedReplacer** — Trims each line (handles whitespace differences)
3. **BlockAnchorReplacer** — Uses first/last lines as anchors with Levenshtein similarity (handles middle content changes)
4. **WhitespaceNormalizedReplacer** — Collapses all whitespace to single spaces (handles inconsistent spacing)
5. **IndentationFlexibleReplacer** — Strips minimum indentation (handles nested code blocks)
6. **EscapeNormalizedReplacer** — Unescapes `\n`, `\t`, etc. (handles escaped strings)
7. **TrimmedBoundaryReplacer** — Trims search boundaries (handles leading/trailing whitespace)
8. **ContextAwareReplacer** — 50% similarity threshold with context anchors (handles substantial changes)
9. **MultiOccurrenceReplacer** — Yields all matches for `replaceAll` mode

**Each replacer is a generator** that yields candidate matches. The first unique match wins.

### What Would Break Without It

Without the 9-layer fuzzer:
- Edit success rate would drop from ~95% to ~70% (empirical estimate)
- Users would see "Could not find oldString" errors constantly
- Agent would need to retry edits multiple times (wasting tokens)
- Small whitespace differences would break edits
- Multi-line code blocks would fail to match

**The competitive advantage:** This is borrowed from Cline and Gemini CLI, but OpenCode's implementation adds the `BlockAnchorReplacer` with Levenshtein distance for fuzzy middle-content matching — a unique improvement.

---

## 5. Tree-Sitter Bash Parsing: Why Shell Safety Needs ASTs

### The Problem It Solves

When an agent runs `rm -rf ./node_modules`, you need to:
1. Know it's accessing `./node_modules` (for permission checks)
2. Know it's a destructive operation (for safety classification)
3. Handle shell expansion, quotes, and escapes correctly

Regex-based parsing fails on: `rm -rf "./node_modules"`, `rm -rf ./node_modules/`, `cd ./node_modules && rm -rf .`

### The Innovation

OpenCode uses **tree-sitter** to parse bash commands into an AST BEFORE execution:

```typescript
const parser = lazy(async () => {
  const { Parser } = await import("web-tree-sitter")
  const bashLanguage = await Language.load(bashWasm)
  const p = new Parser()
  p.setLanguage(bashLanguage)
  return p
})

// In execute():
const tree = await parser().then((p) => p.parse(params.command))
for (const node of tree.rootNode.descendantsOfType("command")) {
  const command = []
  for (let i = 0; i < node.childCount; i++) {
    const child = node.child(i)
    if (child.type === "command_name" || child.type === "word" || child.type === "string") {
      command.push(child.text)
    }
  }
  // Extract paths from cd, rm, cp, mv, mkdir, touch, chmod, chown, cat
  if (["cd", "rm", "cp", "mv", "mkdir", "touch", "chmod", "chown", "cat"].includes(command[0])) {
    for (const arg of command.slice(1)) {
      const resolved = await $`realpath ${arg}`.cwd(cwd).quiet().nothrow().text()
      if (resolved && !Instance.containsPath(normalized)) {
        directories.add(dir) // Flag for permission check
      }
    }
  }
}
```

**Why this matters:**
- Handles quoted paths: `"./node_modules"` extracts `./node_modules`
- Handles escapes: `\ ` in filenames
- Handles concatenation: `./node_modules` + `/foo`
- Handles redirects: `cat file.txt > output.txt` extracts both paths
- Knows which arguments are flags vs paths

### What Would Break Without It

Without tree-sitter parsing:
- Permission checks would be bypassed by quoted paths
- External directory detection would miss escaped paths
- Shell injection would be possible via crafted commands
- The agent couldn't distinguish `rm -rf ./safe` from `rm -rf /`
- Complex commands with pipes/redirections would be parsed incorrectly

---

## 6. Permission System: Rule-Based Safety vs Middleware

### The Problem It Solves

AI agents need granular permissions:
- Allow editing `src/` but not `.env`
- Allow reading `README.md` but ask for `.env`
- Allow bash in project directory but ask for external directories
- Different rules for different agents (build vs explore)

### The Architecture

OpenCode uses **rule-based permissions** with glob patterns:

```typescript
export const Rule = z.object({
  permission: z.string(),  // "bash", "edit", "read", etc.
  pattern: z.string(),     // Glob pattern
  action: z.enum(["allow", "deny", "ask"]),
})

// Evaluation: last matching rule wins
const match = merged.findLast(
  (rule) => Wildcard.match(permission, rule.permission) && 
            Wildcard.match(pattern, rule.pattern)
)
```

**The permission flow:**
1. Tool calls `ctx.ask({ permission: "edit", patterns: ["src/foo.ts"], always: ["*"] })`
2. `PermissionNext.ask()` evaluates rules against the pattern
3. If "allow" → continues silently
4. If "deny" → throws `DeniedError` (halts tool)
5. If "ask" → publishes `Event.Asked`, waits for user response
6. User responds "once" (allow this call), "always" (add to approved rules), or "reject"

### Default Agent Permissions

```typescript
const defaults = PermissionNext.fromConfig({
  "*": "allow",                    // All tools allowed by default
  doom_loop: "ask",                // Ask on doom loop detection
  external_directory: { "*": "ask" }, // Ask for directories outside project
  read: {
    "*": "allow",
    "*.env": "ask",                // Ask before reading .env files
    "*.env.*": "ask",
    "*.env.example": "allow",      // But allow .env.example
  },
})
```

**Why rule-based vs middleware:**
- Rules are **declarative** — users can configure them in JSON
- Rules are **composable** — agent defaults + user config = effective permissions
- Rules are **inspectable** — you can list all effective rules for debugging
- Rules support **wildcard matching** — `*.env` matches all env files

### What Would Break Without It

Without the rule-based system:
- Would need hardcoded permission logic per tool
- No user customization of permissions
- Binary allow/deny (no "ask" middle ground)
- No pattern matching (would need exact paths)
- No permission inheritance between agents

---

## 7. The Instance State Pattern: Per-Project Isolation

### The Problem It Solves

OpenCode supports multiple projects simultaneously:
- Each project has its own LSP clients
- Each project has its own MCP connections
- Each project has its own permission rules
- But they all run in the same process

### The Architecture

```typescript
const state = Instance.state(
  async () => {
    // Initialize state (called once per instance)
    const clients: LSPClient.Info[] = []
    const servers: Record<string, LSPServer.Info> = {}
    return { clients, servers }
  },
  async (state) => {
    // Cleanup (called when instance is disposed)
    await Promise.all(state.clients.map(c => c.shutdown()))
  },
)

// Usage:
const s = await state()  // Gets or creates state for current instance
s.clients.push(newClient)
```

**Why this matters:**
- **Lazy initialization** — State created on first access, not at startup
- **Per-project isolation** — Project A's LSP clients don't interfere with Project B
- **Clean teardown** — When switching projects, old state is cleaned up
- **Type-safe** — State is typed via TypeScript generics

### What Would Break Without It

Without the Instance state pattern:
- LSP clients would be shared across projects (wrong completions)
- MCP connections would leak between projects
- Permission rules would be global (security issue)
- Would need process-per-project (memory overhead)
- No clean way to reset state when switching projects

---

## 8. Agent System: Why Multiple Personalities Beat One

### The Problem It Solves

A single agent can't be optimal for all tasks:
- **Coding** needs full tool access with permission gating
- **Exploration** needs fast read-only access (no edit tools)
- **Planning** needs to think without modifying files
- **Compaction** needs to summarize without tools
- **Title generation** needs to be fast and cheap

### The Architecture

OpenCode defines **6 built-in agents** with different personalities:

| Agent | Mode | Tools | Purpose |
|-------|------|-------|---------|
| `build` | primary | All (with permissions) | Default coding agent |
| `plan` | primary | No edit tools | Plan mode - think before doing |
| `explore` | subagent | Read-only | Fast codebase exploration |
| `general` | subagent | Most tools | Parallel task execution |
| `compaction` | primary (hidden) | None | Summarize conversation |
| `title` | primary (hidden) | None | Generate session titles |
| `summary` | primary (hidden) | None | Generate session summaries |

**Agent configuration in JSON:**
```json
{
  "agent": {
    "security-auditor": {
      "model": "anthropic/claude-sonnet-4-20250514",
      "prompt": "You are a security auditor...",
      "mode": "subagent",
      "temperature": 0.3,
      "permission": { "edit": { "*": "deny" } }
    }
  }
}
```

**Why multiple agents:**
1. **Tool minimization** — Explore agent has only 8 tools vs 20+ for build (faster decision-making)
2. **Permission specialization** — Plan agent can't edit files (safety)
3. **Cost optimization** — Title/summary agents can use cheaper models
4. **Temperature tuning** — Title agent uses 0.5 for creativity, build uses default for accuracy
5. **Custom personalities** — Users can define agents for specific workflows

### What Would Break Without It

Without the multi-agent system:
- All tasks would use the same tool set (slower, more expensive)
- No way to enforce read-only exploration
- No way to have planning mode without accidental edits
- No way to use cheaper models for simple tasks
- No way to customize behavior per use case

---

## 9. Skill Discovery: The SKILL.md Ecosystem

### The Problem It Solves

Every codebase has conventions:
- "We use kebab-case for files"
- "Components go in `src/components/{Component}/{Component}.tsx`"
- "Always use `defineTool()` for new tools"

These conventions are tribal knowledge. Skills codify them.

### The Architecture

Skills are markdown files with YAML frontmatter:

```markdown
---
name: react-patterns
description: React component patterns and best practices
---

# React Patterns

When editing React components:
1. Use functional components
2. Props interface named `{Name}Props`
3. Use SolidJS primitives: `createSignal`, `Show`, `For`
```

**Discovery locations (priority order):**
1. `.opencode/skills/**/SKILL.md`
2. `.claude/skills/**/SKILL.md` (compatibility)
3. `.agents/skills/**/SKILL.md` (compatibility)
4. `~/.config/opencode/skills/**/SKILL.md` (global)
5. URLs in config (downloadable skills)

**The skill tool:**
- Agent sees skill descriptions in tool listing
- When a task matches, agent calls `skill({name: "react-patterns"})`
- Full content injected into context with bundled file list

### What Would Break Without It

Without the skill system:
- No way to inject project-specific conventions
- No way to share conventions across projects
- No way to version control best practices
- Agent would need to relearn conventions per session
- No compatibility with Claude Code's skill ecosystem

---

## 10. Context Compaction: Pruning vs Summarization

### The Problem It Solves

Long conversations hit context limits. You can:
1. **Truncate** — Lose information
2. **Summarize** — Condense but may lose details
3. **Prune** — Remove old tool outputs but keep structure

### The Architecture

OpenCode uses **two strategies**:

**1. Compaction (Summarization):**
- Triggered when tokens exceed usable context
- Dedicated "compaction" agent (no tools) processes conversation
- Generates structured summary: Goal, Instructions, Discoveries, Accomplished, Relevant Files
- Summary becomes new message marked with `summary: true`

**2. Pruning (Tool Output Removal):**
- Walks backwards through message parts
- Protects last 40,000 tokens worth of tool outputs
- Marks older tool outputs as `compacted` (output preserved on disk, stripped from context)
- Minimum prunable threshold: 20,000 tokens
- Protected tools: `skill` (never pruned)

**Overflow handling:**
- If compaction fails due to overflow, finds last non-compacted user message to replay
- If still overflowing, strips media attachments and retries
- If still overflowing, errors with "context exceeds model limit"

### What Would Break Without It

Without compaction/pruning:
- Sessions would hit context limits and crash
- Would need to truncate arbitrarily (loses recent context)
- Would need to start new sessions frequently (loses history)
- Token costs would explode (sending full context every turn)
- User would need to manually manage conversation length

---

## 11. LSP Integration: Why Every Edit Needs Diagnostics

### The Problem It Solves

When an agent edits code, it often introduces type errors:
- Renames a function but not all call sites
- Changes a type but not all usages
- Adds a parameter but not all invocations

The agent needs immediate feedback to fix these errors.

### The Architecture

After every `edit` or `write` tool:

```typescript
await LSP.touchFile(filePath, true)
const diagnostics = await LSP.diagnostics()
const errors = issues.filter((item) => item.severity === 1)
if (errors.length > 0) {
  output += `\n\nLSP errors detected in this file, please fix:\n<diagnostics file="${filePath}">\n${errors.map(LSP.Diagnostic.pretty).join("\n")}\n</diagnostics>`
}
```

**LSP operations available:**
1. `goToDefinition` — Find where a symbol is defined
2. `findReferences` — Find all usages of a symbol
3. `hover` — Get type information at cursor
4. `documentSymbol` — List all symbols in a file
5. `workspaceSymbol` — Search symbols across workspace
6. `goToImplementation` — Find implementations of an interface
7. `prepareCallHierarchy` / `incomingCalls` / `outgoingCalls` — Call graph analysis

**The LSP tool:**
- Experimental (behind flag `OPENCODE_EXPERIMENTAL_LSP_TOOL`)
- 9 operations exposed to agent
- Agent can proactively check types before/after edits

### What Would Break Without It

Without LSP integration:
- Agent would introduce type errors without knowing
- Errors would only surface when user runs typecheck
- Agent would need to run `tsc` as shell command (slow, no incremental)
- No go-to-definition for understanding code
- No find-references for refactoring

---

## 12. Snapshot System: Shadow Git for Agent Sessions

### The Problem It Solves

Agents need:
- Track what changed in each turn
- Revert to previous states
- Show diffs of agent-made changes
- Not pollute user's git history

### The Architecture

OpenCode maintains a **separate git repository** for snapshots:

```typescript
function gitdir() {
  return path.join(Global.Path.data, "snapshot", project.id)
}

// At each step:
const hash = await Snapshot.track()  // git add -A && git write-tree

// To see changes:
const patch = await Snapshot.patch(hash)  // git diff --name-only <hash>

// To revert:
await Snapshot.revert(patches)  // git checkout <hash> -- <file>
```

**Key design decisions:**
- Separate from user's git repo (no pollution)
- Bare repo using project worktree (no double storage)
- Uses `write-tree` not commits (faster, no commit messages)
- Hourly garbage collection (`gc --prune=7.days`)
- Syncs with user's `.git/info/exclude` (respects ignores)

### What Would Break Without It

Without the snapshot system:
- Would need to use user's git repo (pollutes history)
- Would need manual file tracking (error-prone)
- No per-step change tracking
- No revert capability
- Session summaries wouldn't show what files changed

---

## 13. MCP Client: The OAuth-First Design

### The Problem It Solves

MCP servers need authentication:
- Local servers: spawn process, communicate via stdio
- Remote servers: HTTP, often require OAuth
- Need to handle OAuth flow without blocking
- Need to store tokens securely

### The Architecture

OpenCode's MCP client supports:

**Transports:**
- `stdio` — Local processes (e.g., `npx @modelcontextprotocol/server-filesystem`)
- `sse` — Server-Sent Events over HTTP
- `streamableHttp` — HTTP with streaming

**OAuth flow:**
```typescript
// 1. Start auth (opens browser)
const { authorizationUrl } = await MCP.startAuth(mcpName)

// 2. Wait for callback (callback server runs on localhost)
const code = await McpOAuthCallback.waitForCallback(oauthState)

// 3. Finish auth (exchange code for tokens)
await MCP.finishAuth(mcpName, code)
```

**Why this matters:**
- OAuth is **first-class**, not bolted on
- Callback server handles the redirect
- Tokens stored securely (not in config)
- Auto-refresh when tokens expire
- Graceful degradation if auth fails

### What Would Break Without It

Without the OAuth-first MCP client:
- Couldn't connect to authenticated MCP servers
- Would need manual token management
- No auto-refresh (tokens would expire)
- No secure token storage
- Would need separate auth implementation per server

---

## 14. Plugin Hooks: The Extension Point Philosophy

### The Problem It Solves

Users need to customize behavior:
- Add custom auth providers
- Transform system prompts
- Modify tool definitions
- Inject environment variables into shell commands
- Post-process text output

### The Architecture

Plugins are npm packages that export a function:

```typescript
type Plugin = (input: PluginInput) => Promise<Hooks>

interface Hooks {
  "chat.params"?: (input, output) => void  // Modify LLM parameters
  "chat.headers"?: (input, output) => void  // Add custom headers
  "experimental.chat.system.transform"?: (input, output) => void
  "experimental.chat.messages.transform"?: (input, output) => void
  "experimental.text.complete"?: (input, output) => void
  "experimental.session.compacting"?: (input, output) => void
  "tool.definition"?: (input, output) => void
  "tool.execute.before"?: (input, output) => void
  "tool.execute.after"?: (input, output) => void
  "shell.env"?: (input, output) => void  // Inject env vars
}
```

**Built-in plugins:**
- Codex auth
- Copilot auth
- GitLab auth
- Anthropic auth

**Why hooks vs events:**
- Hooks are **transformative** — they modify inputs/outputs
- Hooks are **composable** — multiple plugins can chain
- Hooks are **typed** — TypeScript ensures correct usage

### What Would Break Without It

Without the plugin system:
- No way to add custom auth providers
- No way to customize LLM parameters
- No way to transform system prompts
- No way to inject environment variables
- Would need to fork the codebase for customization

---

## 15. Namespace Pattern: TypeScript as Architecture

### The Problem It Solves

Large TypeScript codebases need:
- Logical organization of related types and functions
- Avoiding naming collisions
- Clear module boundaries
- Type-safe exports

### The Architecture

Every module uses TypeScript namespaces:

```typescript
export namespace Session {
  export const Info = z.object({ ... })
  export type Info = z.infer<typeof Info>
  
  export const Event = { ... }
  
  export async function create(...) { ... }
  export async function update(...) { ... }
}

// Usage:
import { Session } from "./session"
const session: Session.Info = await Session.create(...)
```

**Why namespaces:**
- **Encapsulation** — Types and functions grouped logically
- **Naming** — `Session.Info` vs `SessionInfo` (clearer scoping)
- **Tree-shakeable** — Unused exports removed by bundler
- **Type-safe** — Namespace serves as boundary

**Used consistently across:**
- `Session`, `Provider`, `Agent`, `Config`, `LSP`, `MCP`, `Tool`, `PermissionNext`, `Snapshot`, `Skill`

### What Would Break Without It

Without the namespace pattern:
- Naming collisions between modules
- Less clear module boundaries
- Harder to find related types/functions
- More verbose import statements

---

## 16. What Would Break Without Each System

| System | What Breaks Without It |
|--------|------------------------|
| **Bun runtime** | No built-in SQLite, slower startup, complex build pipeline, larger bundle |
| **Tool.define()** | No automatic truncation, no metadata streaming, inconsistent permission checks |
| **9-layer edit fuzzer** | 70% edit failure rate, constant "not found" errors, wasted tokens on retries |
| **Tree-sitter bash parsing** | Permission bypasses via quotes, incorrect external directory detection, shell injection |
| **Rule-based permissions** | Hardcoded permissions, no user customization, no pattern matching |
| **Instance state pattern** | Cross-project contamination, LSP client leaks, no clean teardown |
| **Multi-agent system** | Slower decisions (all tools always available), no read-only mode, no planning mode |
| **Skill discovery** | No project conventions, no compatibility with Claude Code skills, tribal knowledge |
| **Compaction/pruning** | Context limit crashes, arbitrary truncation, session resets, exploding costs |
| **LSP integration** | Type errors introduced unknowingly, slow tsc shell calls, no go-to-definition |
| **Snapshot system** | Polluted git history, no per-step tracking, no revert capability |
| **MCP OAuth-first** | No authenticated MCP servers, manual token management, no auto-refresh |
| **Plugin hooks** | No customization, fork required for auth providers, no prompt transformation |
| **Namespace pattern** | Naming collisions, unclear boundaries, verbose imports |

---

## 17. Competitive Advantages vs AVA

| Aspect | OpenCode | AVA |
|--------|----------|-----|
| **Runtime** | Bun (single binary) | Node.js + Tauri |
| **Startup** | <50ms | 200ms-2s |
| **Edit strategies** | 9-layer fuzzy matching | 8 strategies |
| **Bash parsing** | Tree-sitter AST | Regex-based |
| **Permissions** | Rule-based (glob patterns) | Middleware-based |
| **Agent system** | Named agents (build/plan/explore) | Praxis hierarchy (Commander/Workers) |
| **Skills** | SKILL.md auto-discovery | Built-in skills + user instructions |
| **LSP** | Built-in (9 operations) | Via LSP module |
| **Snapshot** | Shadow git repo | Git checkpoints |
| **MCP** | OAuth-first design | MCP client |
| **Provider support** | 20+ via Vercel AI SDK | Multiple (custom registry) |
| **UI** | OpenTUI (terminal) | Tauri + SolidJS (desktop) |
| **Distribution** | npm/bun (cross-platform) | Platform-specific builds |
| **Session model** | Messages + Parts (JSON blobs) | Session DAG with branching |
| **Context management** | Pruning + summarization | Compaction + prune strategy |

**OpenCode's unique advantages:**
1. **Bun-native** — Faster, simpler deployment
2. **Tree-sitter bash** — More secure shell execution
3. **Skill ecosystem** — Compatibility with Claude Code
4. **9-layer fuzzer** — Higher edit success rate
5. **Rule permissions** — More intuitive configuration
6. **OAuth-first MCP** — Better authenticated server support

**AVA's unique advantages:**
1. **Desktop UI** — Richer interaction model
2. **Session DAG** — True branching/forking
3. **Commander hierarchy** — More sophisticated delegation
4. **Platform abstraction** — Easier to port to new platforms
5. **Extension API** — More structured extension model

---

## 18. Key Takeaways for AVA

### What AVA Should Adopt

1. **Tree-sitter bash parsing** — More secure than regex, handles edge cases
2. **9-layer edit fuzzer** — Higher reliability than current 8 strategies
3. **Rule-based permissions** — More user-friendly than middleware
4. **Skill discovery** — `.claude/skills/` compatibility is valuable
5. **Shadow git snapshots** — Don't pollute user's git repo
6. **Instance state pattern** — Cleaner than global state

### What AVA Should Keep

1. **Desktop UI** — Richer experience than terminal
2. **Session DAG** — True branching is powerful
3. **Commander hierarchy** — More sophisticated than agent switching
4. **Platform abstraction** — Better for long-term maintainability

### What AVA Should Avoid

1. **Bun dependency** — Node.js ecosystem is larger, more stable
2. **Namespace pattern** — ES modules + explicit exports are clearer
3. **TUI-only** — Desktop UI is the future

---

## Conclusion

OpenCode is the **most defensively-architected AI coding agent**. Every system is designed with failure modes in mind:
- Edits fail? 9 fallback strategies.
- Permissions bypassed? Tree-sitter AST parsing.
- Context full? Pruning + summarization with overflow handling.
- Shell commands dangerous? Path extraction before execution.
- MCP needs auth? OAuth-first design.

This is an agent built for **production reliability**, not just demos. The Bun bet, the tree-sitter integration, and the rule-based permissions are genuine innovations that solve real problems other agents gloss over.

For AVA, the key insight is: **defensive architecture beats clever features**. OpenCode's success comes from handling edge cases gracefully, not from having more tools or flashier UI.
