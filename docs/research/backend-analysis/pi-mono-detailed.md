# Pi Mono — Deep Competitive Intelligence Analysis

> A value-driven analysis of pi-mono's architecture. For each tool, pattern, and decision, we explain **why** it exists, **what problem** it solves, and **what would break or be harder** without it.
>
> Source: `github.com/badlogic/pi-mono` by Mario Zechner (badlogic)
> See also: `pi-mono.md` for a reference-style overview.

---

## Table of Contents

1. [Architecture Philosophy](#1-architecture-philosophy)
2. [The Three-Layer Stack: Why Separation Matters](#2-the-three-layer-stack)
3. [Deep Tool Analysis: Why Each Tool Exists](#3-deep-tool-analysis)
4. [The Agent Loop: Why It Is Separate](#4-the-agent-loop)
5. [Provider Abstraction: The Compatibility Problem](#5-provider-abstraction)
6. [Context Management: The Core Survival Problem](#6-context-management)
7. [Session Architecture: Why Trees Beat Lists](#7-session-architecture)
8. [Extension System: Why It Is The Moat](#8-extension-system)
9. [Ancillary Packages: Why They Prove The Architecture](#9-ancillary-packages)
10. [Innovations Worth Stealing](#10-innovations-worth-stealing)
11. [Weaknesses and Trade-offs](#11-weaknesses-and-trade-offs)
12. [Strategic Implications for AVA](#12-strategic-implications-for-ava)

---

## 1. Architecture Philosophy

Pi-mono's architecture is built on a single guiding principle: **every layer should be usable independently**. This is not the typical "clean architecture" lip service — it is structurally enforced through npm package boundaries.

```
@mariozechner/pi-ai          → Zero-dep LLM abstraction (usable standalone)
@mariozechner/pi-agent-core   → Generic agent loop (depends only on pi-ai)
@mariozechner/pi-coding-agent → Full product (depends on both above)
```

**Why this matters**: Most competing agents (Aider, Continue, Cline) have monolithic codebases where the LLM client, agent loop, and tools are entangled. Pi's separation means:

- **pi-ai** can be used as a standalone multi-provider LLM SDK (like Vercel AI SDK but with 9 API protocols and 22 providers).
- **pi-agent-core** can power non-coding agents (proven by the `mom` Slack bot).
- **pi-coding-agent** can be embedded headlessly via RPC mode or the SDK factory.

**What would break without this**: The `mom` package would not exist — it literally depends on `pi-agent-core` but NOT on `pi-coding-agent`. The clean layering is what makes the Slack bot possible with its own tools, own Docker sandbox, and own persistence model, while still getting the full agent loop, retry logic, and streaming for free.

---

## 2. The Three-Layer Stack

### Layer 1: `packages/ai` — The Provider Nightmare Solved

**The problem**: Every LLM provider speaks a slightly different dialect. Even providers that claim "OpenAI compatibility" have subtle differences: Groq requires tool result names, Mistral needs special tool IDs, xAI has a custom thinking format, OpenRouter needs routing hints. Building a coding agent that supports all providers means handling dozens of edge cases.

**The solution**: Pi abstracts by **API protocol**, not by provider name. There are only 9 API implementations, but they serve 22+ providers through compatibility configuration.

**Key insight — the `OpenAICompletionsCompat` interface**: This is the most valuable piece of competitive intelligence in the entire codebase. It has 15+ boolean/enum knobs that capture every quirk discovered when integrating with providers:

| Flag | Problem It Solves |
|------|-------------------|
| `supportsStore` | OpenAI supports `store: true` for data retention; others crash on it |
| `supportsDeveloperRole` | Some providers accept `developer` role; others need `system` |
| `supportsReasoningEffort` | Only some providers support reasoning effort parameters |
| `thinkingFormat` | Three different formats: "openai" (native), "zai" (z.ai custom), "qwen" (Qwen custom) |
| `requiresToolResultName` | Groq crashes without `name` field on tool results |
| `requiresAssistantAfterToolResult` | Some providers require assistant message between tool result and next user message |
| `requiresThinkingAsText` | Some providers cannot handle thinking blocks natively |
| `requiresMistralToolIds` | Mistral requires specific tool ID format |
| `openRouterRouting` | OpenRouter needs routing hints for model selection |
| `vercelGatewayRouting` | Vercel AI Gateway needs different routing |
| `supportsJsonSchema` | Not all "OpenAI-compatible" providers support JSON schema |
| `supportsStreamOptions` | Stream options support varies |
| `supportsPromptCaching` | Only some providers support prompt caching |
| `supportsCacheControl` | Anthropic-style cache control headers |

**What would break without this**: Every time a user switches to a new provider, they'd hit cryptic errors. "Why does my agent crash on Groq but work on OpenAI?" — because Groq requires `name` on tool results and OpenAI doesn't. The compat layer absorbs these differences silently.

**What AVA should learn**: We should build a similar compat configuration system. Currently, any new provider integration requires custom code. Pi's approach means adding a new "OpenAI-compatible" provider is just adding a compat config object.

### Layer 2: `packages/agent` — The Minimal Agent

**The problem**: Agent loops have common patterns (turn-based execution, tool calling, message management, abort handling) that are not specific to coding. Building this into the coding agent means you cannot reuse it.

**The solution**: Five files, ~600 lines. That is it. The entire generic agent loop.

**Why it is exactly the right size**: The agent loop does ONLY these things:
1. Stream an LLM response
2. Parse tool calls from the response
3. Execute tools sequentially (with steering interrupts between each)
4. Check for follow-up messages when the agent would naturally stop
5. Emit events for each phase

It does NOT do: compaction, session persistence, system prompt construction, tool definitions, retry logic, or error recovery. Those are all in the coding-agent layer.

**The steering vs. follow-up distinction is brilliant**:
- **Steering messages**: Injected mid-turn, between tool executions. When a steering message arrives, remaining tool calls are SKIPPED. Use case: user presses Ctrl+C to redirect the agent mid-task.
- **Follow-up messages**: Injected after the agent finishes a turn. The agent starts a new turn to process them. Use case: extension injects a "now run tests" message after the agent finishes editing.

Both support `"all"` (deliver everything at once) or `"one-at-a-time"` (deliver one, let agent process, deliver next) delivery modes.

**What would break without this distinction**: Without steering, there is no clean way to interrupt a running agent without aborting entirely. Without follow-up, extensions cannot chain actions after completion. Without delivery modes, a burst of follow-up messages might overwhelm the agent (all-at-once) or leave it unaware of pending work (one-at-a-time).

### Layer 3: `packages/coding-agent` — The Product

**The problem**: The product (Pi CLI) needs sessions, compaction, extensions, tools, a TUI, settings, and package management. This is necessarily complex. The question is how to organize it.

**The solution**: A single large package (~100KB for `agent-session.ts` alone) that orchestrates all product concerns. The session lifecycle (`AgentSession`) is the God object that ties everything together.

**Honest assessment**: This layer is the least clean. `agent-session.ts` at 3,003 lines is doing too much — it handles session lifecycle, compaction triggers, model switching, retry logic, extension runner binding, tool set management, and branch summarization. But the key insight is that THIS is where complexity should live — not in the agent loop or the LLM layer.

---

## 3. Deep Tool Analysis

### 3.1 `bash.ts` — Shell Execution

**Problem it solves**: Agents need to run arbitrary commands (build, test, git, lint, etc.) and see the output. But shell output can be enormous (npm install, test suites, build logs) and must not crash the context window.

**How it works**:
1. Spawns a child process with configurable shell, cwd, timeout, and environment
2. Streams stdout/stderr to a temp file (not memory) when output is large
3. Keeps a rolling in-memory buffer for tail truncation
4. Returns truncated output with `fullOutputPath` for the agent to read more

**Why it uses tail truncation (not head)**: When a build fails, the error is at the END of the output. When tests fail, the summary is at the END. Truncating the head (keeping the tail) ensures the agent sees the actionable information.

**The `spawnHook` pattern**: Before spawning, the bash tool calls a `spawnHook` if provided. This allows extensions to:
- Rewrite commands (e.g., prefix with `docker exec`)
- Change the working directory
- Inject environment variables
- Log or audit commands

**What would break without this**:
- Without temp file streaming: Large outputs would OOM the process or blow the context window
- Without tail truncation: Build errors would be cut off, leaving the agent with useless preamble
- Without `spawnHook`: Remote execution (SSH, Docker) would require a completely separate tool

**Pluggable `BashOperations`**: The entire spawn logic is behind an interface. Default: local shell. Override: SSH execution, Docker exec, or any custom sandbox. This is how the `mom` Slack bot runs commands inside a Docker container.

### 3.2 `edit.ts` + `edit-diff.ts` — Surgical Text Editing

**Problem it solves**: LLMs need to edit files, but they are unreliable at producing exact text matches. They introduce Unicode artifacts (smart quotes, em dashes, special spaces), hallucinate whitespace, and sometimes get line endings wrong.

**The two-stage approach**:
1. **`edit.ts`**: The tool interface. Takes `file_path`, `old_string`, `new_string`. Validates that old_string exists exactly once. Returns a unified diff.
2. **`edit-diff.ts`**: The fuzzy matching engine. If exact match fails, it tries progressively relaxed matching:
   - Strip trailing whitespace from each line
   - Normalize smart quotes (`""''` → `""''`)
   - Normalize Unicode dashes (em dash, en dash → hyphen)
   - Normalize special spaces (non-breaking space, thin space → regular space)

**Why fuzzy matching is essential**: LLMs frequently produce text that is semantically identical but byte-different from the original. Without fuzzy matching, edit success rates drop significantly. Claude in particular tends to "fix" quotes and dashes in its output.

**Why the edit tool exists at all (vs. write)**: The `write` tool overwrites the entire file. For a 500-line file where the agent needs to change 3 lines, write is dangerous — if the LLM hallucinates any other part of the file, those changes sneak in silently. Edit forces the agent to specify exactly what it is changing and verifies the old text exists.

**What would break without this**:
- Without fuzzy matching: ~15-20% of edits would fail due to Unicode artifacts
- Without the uniqueness check: The agent might edit the wrong occurrence of a string
- Without unified diff output: The agent has no confirmation of what changed

### 3.3 `read.ts` — File Reading with Image Support

**Problem it solves**: Agents need to read files, but files can be enormous (node_modules, generated code, binaries) and some are images that should be processed as vision input.

**Key design decisions**:
1. **Head truncation** (opposite of bash): When reading source code, the beginning is most useful (imports, class definitions, function signatures). The tail is often implementation details.
2. **Offset/limit pagination**: The tool returns actionable continuation notices: "File has 5000 lines. Use offset=2001 to read more." This teaches the agent how to paginate without special instructions.
3. **Image detection and resizing**: If the file is an image (jpg, png, gif, webp), it is automatically resized to max 2000x2000 and returned as a vision input. No separate "view image" tool needed.
4. **Dual limits**: 500 lines OR 64KB, whichever is hit first. This prevents both long-file and wide-file problems.

**What would break without this**:
- Without pagination: Large files would be either truncated without recourse or blow the context
- Without image support: A separate tool or workflow would be needed for visual content
- Without dual limits: A minified JS file (1 line, 500KB) would pass the line limit but explode the context

### 3.4 `write.ts` — File Creation

**Problem it solves**: Sometimes the agent needs to create entirely new files or completely replace file contents.

**Why it auto-creates parent directories**: LLMs frequently try to write to paths where intermediate directories do not exist (e.g., `src/components/new-feature/index.ts` when `new-feature/` does not exist). Without auto-mkdir, these writes fail with cryptic errors and waste a turn.

**Pluggable `WriteOperations`**: Like bash, the write operation is behind an interface. Default: local filesystem. Override: write via SSH, write to a virtual filesystem, or write with additional validation.

**What would break without this**: Without auto-mkdir, the agent would need to explicitly create directories before writing files, wasting context and turns on boilerplate.

### 3.5 `grep.ts` — Content Search

**Problem it solves**: Agents need to find code patterns across a codebase without reading every file. Grep is the agent's "codebase search" — it answers "where is this function used?" or "which files import this module?"

**Why it uses ripgrep (`rg`)**: Native grep is slow on large codebases. Ripgrep respects `.gitignore` by default, handles binary files correctly, and is 10-100x faster. Pi shells out to `rg` rather than reimplementing search.

**Key parameters**: regex support, case-insensitive option, glob filtering, context lines, and a default limit of 100 matches. The limit prevents the context from being flooded with search results.

**What would break without this**: The agent would have to read files one by one to find patterns, burning context and turns. A simple "find all usages of function X" would require reading dozens of files.

### 3.6 `find.ts` — File Discovery

**Problem it solves**: Agents need to discover what files exist before they can read or edit them. "What test files exist?" or "Where are the TypeScript config files?"

**Why it prefers `fd` over `glob`**: `fd` is faster, respects `.gitignore`, and handles large directory trees better. Falls back to the `glob` library when `fd` is not available.

**What would break without this**: The agent would use bash (`find` command or `ls -R`) which is slower, does not respect `.gitignore`, and produces output that needs parsing. The dedicated tool returns clean, structured results.

### 3.7 `ls.ts` — Directory Listing

**Problem it solves**: Quick orientation. The agent needs to see what is in a directory without a recursive search. "What files are in `src/`?" is fundamentally different from "Find all `.ts` files recursively."

**Why it is separate from find**: `ls` is non-recursive and immediate. `find` is recursive and potentially slow. The agent needs both mental models.

**What would break without this**: Without a dedicated ls, the agent would either use `find` (overkill for "what's in this directory?") or bash `ls` (requires parsing, no truncation control).

### 3.8 `truncate.ts` — The Shared Truncation Engine

**Problem it solves**: Every tool that produces text output needs truncation, but the truncation strategy differs by context.

**Two strategies**:
- `truncateHead`: Keep the beginning, cut the end. Used by `read` (file headers are most useful).
- `truncateTail`: Keep the end, cut the beginning. Used by `bash` (errors and summaries are at the end).

**Dual limits**: Line count (2000) AND byte count (50KB). Never returns partial lines.

**Structured output**: Returns `TruncationResult` with `content`, `truncated` (boolean), and `notice` (human-readable explanation with instructions on how to see more).

**Why this is centralized**: Without a shared truncation engine, each tool would implement its own truncation logic, leading to inconsistencies. The structured result ensures every tool can tell the agent "this output was truncated, here's how to see more."

---

## 4. The Agent Loop: Why It Is Separate

### The Context Transform Pipeline

This is one of Pi's most elegant patterns:

```
AgentMessage[]  →  transformContext()  →  convertToLlm()  →  Message[]
    (rich)           (prune/inject)         (format)          (LLM-ready)
```

**Stage 1 — `transformContext()`**: Operates on `AgentMessage[]`. This is where compaction summaries replace old messages, where extensions inject context, where custom messages are filtered. The key: this stage works with the APPLICATION's type system, not the LLM's.

**Stage 2 — `convertToLlm()`**: Converts `AgentMessage[]` to `Message[]` (the LLM's native format). Custom message types (bash execution details, branch summaries, compaction summaries) are converted to standard user/assistant/tool messages.

**Why two stages matter**: If you do everything in one stage, you mix concerns — you are simultaneously deciding what to include (application logic) and how to format it (LLM logic). Separating them means:
- The compaction system only needs to understand `AgentMessage[]`
- The LLM format conversion only needs to understand `Message[]`
- Extensions can operate at either level

**What would break with one stage**: Extensions could not inject application-level messages (like branch summaries) that get converted to LLM format automatically. Compaction would need to understand LLM message format. Model switching would require re-doing application-level transformations.

### Declaration Merging for Custom Messages

```typescript
// In pi-agent-core
interface CustomAgentMessages {}
type AgentMessage = BaseMessage | CustomAgentMessages[keyof CustomAgentMessages];

// In pi-coding-agent (declaration merging)
declare module "@mariozechner/agent" {
  interface CustomAgentMessages {
    bashExecution: BashExecutionMessage;
    compactionSummary: CompactionSummaryMessage;
    branchSummary: BranchSummaryMessage;
    custom: CustomMessage;
  }
}
```

**Why this is clever**: The agent loop in `pi-agent-core` has ZERO knowledge of bash execution messages, compaction summaries, or branch summaries. It works with `AgentMessage[]` generically. But TypeScript's type system, through declaration merging, ensures that the coding-agent's custom messages are type-safe everywhere.

**What would break without this**: Either (a) the agent loop would need to know about coding-specific message types (breaking the layering), or (b) custom messages would be untyped `any` objects (breaking type safety).

---

## 5. Provider Abstraction: The Compatibility Problem

### The API Registry Pattern

```typescript
const apiRegistry = new Map<string, StreamFunction>();

function registerApiProvider(provider: string, fn: StreamFunction, sourceId?: string) {
  apiRegistry.set(provider, fn);
}
```

**Why a registry instead of a factory**: Registries support runtime modification. Extensions can register custom providers (e.g., a corporate proxy, a local model) without modifying core code. The `sourceId` parameter enables grouped unregistration — when an extension is unloaded, all its providers are removed.

**What would break without sourceId**: Extension cleanup would require tracking which providers each extension registered. With `sourceId`, it is `unregisterBySource(extensionId)` — one call.

### Cross-Provider Message Transformation

`transform-messages.ts` handles the hardest problem in multi-provider agents: **what happens when you switch models mid-conversation?**

**Problems solved**:
1. **Thinking blocks**: Claude's thinking blocks are encrypted (redacted). If you switch from Claude to GPT, sending encrypted thinking blocks causes errors. Solution: strip redacted thinking, convert non-redacted thinking to text.
2. **Tool call IDs**: OpenAI Responses API generates 450+ character tool call IDs. Anthropic requires IDs <= 64 characters. Solution: normalize IDs.
3. **Text signatures**: Some providers add signatures to their output. When switching providers, these signatures confuse the new model. Solution: strip them.

**What would break without this**: Model switching would fail silently or with cryptic errors. Users would be locked into one provider per session.

### The Model Registry Problem

Pi auto-generates `models.generated.ts` (329KB) from provider APIs. Each model carries its API protocol, provider, cost, context window, and compatibility flags.

**Why auto-generation matters**: Model availability changes weekly. New models appear, old ones are deprecated, pricing changes. Auto-generation from provider APIs means the model list is always current without manual maintenance.

**What would break without this**: Manual model lists become stale within weeks. Users cannot use new models without a Pi update.

---

## 6. Context Management: The Core Survival Problem

### Why Compaction Exists

**The fundamental problem**: Coding sessions are long. A typical session might involve reading 20 files, making 15 edits, running 10 commands, and having 30 back-and-forth turns. At ~1000 tokens per turn, that is 30,000+ tokens of conversation alone, plus tool results (a single file read can be 2000+ tokens). Context windows fill up.

**Options considered** (implicit from the design):
1. **Sliding window**: Drop old messages. Problem: the agent loses critical context about what it already did.
2. **Truncation**: Summarize everything down to a fixed size. Problem: recent messages are as important as old ones.
3. **Summarization-based compaction**: Summarize old messages, keep recent ones intact. This is what Pi does.

### How Compaction Works (Deep Dive)

**Token estimation**: `chars / 4` heuristic, anchored by the last assistant message's actual `usage.totalTokens`. This is pragmatic — accurate token counting requires a tokenizer per model, which is expensive and model-specific.

**Finding the cut point**: The algorithm walks backward from the oldest message, finding a point where:
- At least `keepRecentTokens` (default: 20,000) of recent messages are preserved
- The cut is NOT at a tool result (cutting at a tool result would orphan the tool call)
- The cut is at a turn boundary (user message or assistant message)

**Split-turn handling**: If a single turn is so large that it exceeds the compaction budget, Pi can split within a turn — summarizing the first part while keeping the rest. This handles cases where a single tool result is enormous.

**File operation tracking**: When messages are compacted, Pi extracts file paths from tool call arguments to build a "files touched" list. This is injected into the summary so the agent knows which files it has previously read or modified, even after compaction.

**The summarization prompt template**:
```
Summarize the following conversation, preserving:
- What the user asked for (goals, constraints)
- What has been done (completed work, key decisions)
- What is in progress (current task state)
- Important context (file paths, error messages, approach decisions)
[Previous summary to update, if exists]
[File operations across compactions]
[Messages to summarize]
```

**Why iterative summary updates matter**: If the conversation has been compacted before, the new compaction receives the previous summary and UPDATES it rather than starting fresh. This prevents information loss across multiple compactions.

### Two Compaction Triggers

1. **Overflow recovery**: The LLM returned a context overflow error. Pi detects this (14 provider-specific regex patterns), compacts, and retries. This is reactive — it happens AFTER a failure.
2. **Threshold-based**: Context tokens exceed `contextWindow - reserveTokens`. This is proactive — it compacts BEFORE the overflow happens.

**Why both triggers**: Threshold-based compaction prevents most overflows. But token estimation is imprecise (`chars/4` is a rough heuristic), and some providers have unpublished limits. Overflow recovery is the safety net.

**What would break without compaction**: Long coding sessions would simply fail when the context window fills. Users would need to manually start new sessions, losing all context. This is the single most important feature for real-world usability.

### Branch Summarization

**Problem**: When you navigate to a different branch in the session tree, the agent has no context about what happened on the branch you left.

**Solution**: When navigating away from a branch, Pi generates a structured summary:

```
Goal: [What was being attempted]
Constraints: [Relevant constraints]
Progress:
  Done: [Completed items]
  In Progress: [Partially done items]
  Blocked: [Blocked items with reasons]
Key Decisions: [Important choices made]
Next Steps: [What was planned]
File Operations: [Files read/modified]
```

This summary walks from the old leaf to the common ancestor, using newest-first token budgeting (recent messages get priority).

**What would break without this**: Branch navigation would discard all context from the abandoned branch. If you want to go back or reference what you did on that branch, the information is lost.

---

## 7. Session Architecture: Why Trees Beat Lists

### The JSONL DAG Format

Every session entry has `id` and `parentId` fields, forming a directed acyclic graph (DAG):

```
root → msg1 → msg2 → msg3 → msg4 (branch A, active)
                 \→ msg5 → msg6 (branch B, abandoned)
```

**Why JSONL over SQLite**: Append-only JSONL is simpler to implement, debug, and recover. You can `cat` a session file and read it. You can `tail -f` it to watch live. There is no schema migration needed for new entry types — you just add new `type` values. Corruption recovery is trivial: truncate the last incomplete line.

**Why a tree over a list**: Linear session history cannot represent branching. When a user says "go back to message 2 and try a different approach," a linear list either loses message 3-4 or creates an ambiguous history. The tree preserves both branches.

### Entry Types and Why Each Exists

| Entry Type | Why It Exists | What Would Break Without It |
|------------|---------------|---------------------------|
| `session` | Header with version, ID, cwd. Enables migration. | No way to detect format changes or session metadata. |
| `message` | User/assistant/tool messages. The core content. | No conversation at all. |
| `thinking_level_change` | Records when thinking level changes mid-session. | After compaction, the agent would not know what thinking level to use. |
| `model_change` | Records when the user switches models. | After compaction, the agent would not know which model generated which messages. Cross-model replay would break. |
| `compaction` | Stores summary + metadata about what was compacted. | Session reload after compaction would lose all pre-compaction context. |
| `branch_summary` | Stores branch summaries when navigating away. | Branch navigation would lose context. |
| `custom` | Extension state persistence (NOT sent to LLM). | Extensions would lose state across session reloads. |
| `custom_message` | Extension-injected messages (sent to LLM). | Extensions could not inject context into the conversation. |
| `label` | User-defined bookmarks. | No way to mark important points in conversation for later reference. |
| `session_info` | Display name metadata. | Session list would show IDs instead of meaningful names. |

### Context Building from Trees

`buildSessionContext()` walks from a leaf node to the root, collecting messages along the path. When a compaction entry is encountered on the path:

1. Emit the compaction summary as a system message
2. Skip all entries before `firstKeptEntryId`
3. Continue with kept entries and post-compaction entries

This means the agent always sees: `[compaction summary] + [kept recent messages] + [new messages]`.

**What would break without tree-aware context building**: After forking, the agent would see messages from the wrong branch. After compaction, it would either see too much (pre-compaction messages that were summarized) or too little (missing the summary).

---

## 8. Extension System: Why It Is The Moat

### The Extension Architecture

Pi's extension system is the single most comprehensive extension system in any open-source coding agent. It exposes 25+ lifecycle events across 5 categories, tool interception, UI primitives, and full provider registration.

**Why this is the moat**: Every user has custom needs. Some work in monorepos. Some need approval flows. Some use corporate proxies. Some want Git checkpoints. Instead of building all these features, Pi provides the hooks for users to build them. The 70+ example extensions prove the system works.

### Event Categories and Their Value

#### Session Lifecycle Events

| Event | Problem It Solves |
|-------|-------------------|
| `session_start` | Extensions need to initialize state (load config, connect to services). |
| `session_shutdown` | Extensions need to clean up (close connections, flush logs). |
| `session_before_switch` | Extensions can save state or block session switches. |
| `session_before_fork` | Extensions can prepare for branching (e.g., create git branch). |
| `session_before_compact` | Extensions can override compaction with custom strategy. |
| `session_before_tree` | Extensions can modify the session tree display. |

**The `before_` pattern**: Events prefixed with `before_` are cancellable — the handler can return a value that prevents or modifies the operation. This is critical for permission gates.

#### Tool Interception

```
Tool call from LLM
  → emit tool_call (can BLOCK execution)
  → execute actual tool
  → emit tool_result (can MODIFY result)
```

**Why block and modify**: 
- **Block**: Permission gates. "The agent wants to delete `/etc/passwd` — block it." Protected paths. Destructive command confirmation.
- **Modify**: Result injection. "The agent read a file — append a reminder about the project's coding standards." Logging. Audit trails.

**Real extension examples that use this**:
- `confirm-destructive.ts`: Blocks bash commands matching dangerous patterns (rm -rf, git push --force) until user confirms
- `protected-paths.ts`: Blocks reads/writes to configured paths
- `permission-gate.ts`: Full permission system with allow/deny rules

#### Context Manipulation

The `context` event fires before each LLM call and receives the full message array. Extensions can:
- Inject additional context (e.g., current git status, project rules)
- Remove messages (e.g., strip sensitive content)
- Reorder messages (e.g., pin important context)

**What would break without this**: No way to inject dynamic context. Every piece of contextual information would need to be in the system prompt (static) or in the conversation (wasting turns).

### Extension API Surface

| Capability | Why It Matters |
|------------|---------------|
| `registerTool()` | Extensions can add ANY tool — web search, database query, API call, custom linter |
| `registerCommand()` | Slash commands (`/mycommand`) for user-facing features |
| `registerShortcut()` | Keyboard shortcuts for TUI actions |
| `registerFlag()` | CLI flags that modify behavior without code changes |
| `registerMessageRenderer()` | Custom rendering for custom message types in the TUI |
| `registerProvider()` | Add custom LLM providers (corporate proxy, local model, custom API) |
| `sendMessage()` | Inject messages into the conversation programmatically |
| `appendEntry()` | Persist extension state in the session JSONL |
| `setActiveTools()` | Dynamically change which tools the agent can use |
| `setModel()` / `setThinkingLevel()` | Programmatic model/thinking control |

**Why `registerProvider()` is particularly powerful**: It means Pi can support any LLM provider without Pi itself knowing about it. Corporate proxy with custom auth? Extension. Local model with custom API? Extension. This is unlimited provider support without core changes.

### Extension Distribution

Extensions can be installed from npm or git:
```bash
pi install my-extension-package
pi install git+https://github.com/user/extension.git
```

**Why this matters**: It creates an ecosystem. Users can share extensions. Organizations can distribute internal extensions as private npm packages. This is the same model that made VS Code successful.

---

## 9. Ancillary Packages: Why They Prove The Architecture

### `packages/tui` — A Full Terminal UI Framework

**Why it exists**: Existing terminal UI libraries (blessed, ink, etc.) do not support:
- Differential rendering (only redraw what changed)
- Synchronized output (CSI 2026 protocol for flicker-free rendering)
- Kitty/iTerm2 image protocols (inline images in terminal)
- Kitty keyboard protocol (proper key detection with modifiers)
- IME support (input method for CJK languages)
- Component composition with overlay system

Pi needed all of these for a polished TUI experience, so they built their own framework.

**What this means for AVA**: We do not need a TUI (we have a desktop app), but the level of polish here sets user expectations. Terminal users expect inline images, smooth scrolling, and responsive input.

### `packages/mom` — The Architecture Proof

**Why it matters**: Mom (Master Of Mischief) is a Slack bot that uses `pi-agent-core` but NOT `pi-coding-agent`. It has:
- Its own tools (adapted for Docker sandbox)
- Its own persistence (per-channel JSONL + MEMORY.md)
- Its own event system (immediate/one-shot/periodic cron)
- Self-installing tools (creates CLI "skills" at runtime)

**What this proves**: The three-layer architecture is not theoretical — it actually works for building different products on the same foundation. If the agent loop were entangled with coding tools, Mom could not exist.

### `packages/pods` — GPU Infrastructure

**Why it exists**: Self-hosted LLMs on GPU pods (DataCrunch, RunPod, Vast.ai, AWS EC2) with:
- Automated vLLM deployment via SSH
- Smart GPU allocation across multiple models
- OpenAI-compatible API endpoints
- Live health monitoring

**What this means**: Pi is not just a coding agent — it is an ecosystem. Users can run their own models and plug them in via the standard provider interface. This is vertical integration: cloud GPU management → model serving → agent runtime → coding tools → user interface.

---

## 10. Innovations Worth Stealing

### 10.1 The Compat Config Pattern

Instead of writing custom code per provider, define a configuration object that captures provider quirks:
```typescript
const groqCompat: OpenAICompletionsCompat = {
  requiresToolResultName: true,
  supportsStore: false,
  supportsDeveloperRole: false,
  // ...
};
```

**Value**: Adding a new "OpenAI-compatible" provider goes from days of debugging to minutes of configuration.

### 10.2 Steering + Follow-up Message Queues

Two separate queues with configurable delivery modes. This is more nuanced than "user interrupts" or "agent finishes."

**Value**: Enables complex workflows — extension chains, progressive disclosure, mid-task redirection — all without custom agent loop modifications.

### 10.3 File Operation Tracking Across Compactions

When compacting, extract file paths from tool call arguments and inject them into the summary.

**Value**: After compaction, the agent still knows "I read file X and modified file Y" even though those messages are gone. This prevents re-reading files the agent already processed.

### 10.4 The Before-Event Cancellation Pattern

Events prefixed with `before_` can return values that cancel or modify operations.

**Value**: Permission systems, approval flows, and safety gates are just extensions, not core features. The core is small; the safety surface is extensible.

### 10.5 Pluggable Operations on Every Tool

Every tool has an `*Operations` interface (BashOperations, ReadOperations, etc.) with default local implementations.

**Value**: Remote execution (SSH, Docker, cloud sandbox) requires zero changes to tool logic. Only the operations implementation changes.

### 10.6 Dual-Strategy Truncation

Head truncation for file reads (headers matter), tail truncation for command output (errors matter). Both with actionable continuation notices.

**Value**: The agent always sees the most useful part of the output AND knows how to get more.

### 10.7 Fuzzy Edit Matching

Progressive relaxation: exact match → whitespace normalized → Unicode normalized.

**Value**: Edit success rates increase significantly. LLM Unicode artifacts are silently handled.

### 10.8 Session Tree with Branch Summarization

DAG-based sessions where abandoned branches are summarized, not lost.

**Value**: Users can explore alternatives without losing context. The agent can reference what was tried on other branches.

### 10.9 Iterative Compaction Summaries

New compactions receive the previous summary and UPDATE it rather than starting fresh.

**Value**: Information survives multiple compaction cycles. In a very long session with 5 compactions, the final summary carries forward critical information from all phases.

### 10.10 Dynamic API Key Resolution

`getApiKey` is a callback invoked on every LLM call, not a static configuration.

**Value**: OAuth tokens that expire during long tool execution phases (common with GitHub Copilot) are re-fetched automatically.

---

## 11. Weaknesses and Trade-offs

### 11.1 The 3000-Line God Object

`agent-session.ts` at 3,003 lines violates every file size guideline. It handles session lifecycle, compaction triggers, model switching, retry logic, extension binding, tool management, and branch summarization. Decomposition would improve maintainability.

### 11.2 Sequential Tool Execution

Tools execute one at a time, with steering checks between each. There is no parallel tool execution. For independent operations (reading multiple files, running multiple commands), this is slower than necessary.

**Contrast with AVA**: AVA supports parallel tool execution via the `batch` tool and the commander's parallel delegation system. Pi relies on sequential execution with steering interrupts.

### 11.3 No Built-in Permission System

Safety is entirely handled via extensions. There is no default "are you sure?" prompt for destructive operations. A fresh Pi install has NO safety guards — the user must install permission extensions.

**Contrast with AVA**: AVA has a built-in permission system with rules and approval flows.

### 11.4 JSONL Limitations

JSONL sessions are simple but have downsides:
- No indexing — finding entries requires reading the entire file
- No transactions — concurrent writes risk corruption
- No schema enforcement — any JSON is valid
- File-per-session means many small files

**Contrast with AVA**: AVA uses SQLite, which provides indexing, transactions, schema enforcement, and handles concurrent access.

### 11.5 No Platform Abstraction

Pi uses Node.js APIs directly. The `Operations` interfaces on tools serve a similar purpose for tool execution, but the rest of the codebase (file I/O, process spawning, path handling) is Node-specific.

**Contrast with AVA**: AVA has `getPlatform()` abstraction supporting both Node.js and Tauri platforms.

### 11.6 Token Estimation

The `chars/4` heuristic for token counting is fast but inaccurate. It can over-estimate or under-estimate by 20-30%, leading to premature compaction or overflow errors.

### 11.7 Monolithic Interactive Mode

`interactive-mode.ts` at 4,401 lines is the largest file in the codebase. It handles all TUI concerns in one file: rendering, input handling, command processing, autocomplete, image display, and status management.

---

## 12. Strategic Implications for AVA

### What Pi Does That AVA Should Consider

1. **Compat config for providers**: Instead of custom code per provider, define compatibility objects. This dramatically reduces the cost of supporting new providers.

2. **Extension-based safety**: Pi's approach of "the core is permissive, extensions add restrictions" is philosophically different from AVA's "the core is restrictive." Neither is definitively better, but Pi's approach is more extensible.

3. **Steering + follow-up queues**: AVA's current interrupt model is simpler but less nuanced. The two-queue system with delivery modes enables more complex agent control flows.

4. **File operation tracking across compactions**: This specific feature prevents the agent from re-reading files after compaction. AVA's compaction should consider this.

5. **Branch summarization**: AVA's session model should consider tree-based branching with automatic summarization of abandoned branches.

6. **SDK factory pattern**: `createAgentSession()` as a one-call setup for headless embedding is valuable for the CLI and editor integrations.

### What AVA Does Better

1. **Parallel tool execution**: The batch tool and commander delegation are more sophisticated than Pi's sequential execution.

2. **Platform abstraction**: AVA's `getPlatform()` enables true cross-platform (Node.js + Tauri) support.

3. **Built-in permissions**: Safety out of the box, not as an opt-in extension.

4. **SQLite sessions**: Better for querying, indexing, and concurrent access than JSONL.

5. **Smaller files**: AVA's 300-line file limit enforces decomposition. Pi's 3000+ line files suggest structural debt.

6. **Commander hierarchy**: Multi-agent delegation (coder, tester, reviewer, researcher, debugger) is more sophisticated than Pi's single-agent model.

### Net Assessment

Pi is a highly capable, well-architected coding agent with the most comprehensive extension system and provider compatibility layer in the open-source space. Its three-layer architecture is genuinely clean, and its innovations (compat configs, steering queues, file operation tracking, fuzzy edits, tree sessions) represent real competitive advantages.

AVA's advantages lie in parallel execution, platform abstraction, built-in safety, and multi-agent coordination. The two products have different philosophies — Pi is extension-first (power through plugins), while AVA is capability-first (power through core features).

**The biggest takeaway**: Pi's extension system is its moat. 70+ example extensions, npm distribution, tool interception, provider registration — this creates an ecosystem that is hard to compete with on features alone. AVA should consider whether its extension/hook system can match this level of extensibility.

---

*Analysis completed from source code review of pi-mono. All findings based on direct code reading of packages/ai, packages/agent, packages/coding-agent, packages/tui, packages/mom, and packages/pods.*
