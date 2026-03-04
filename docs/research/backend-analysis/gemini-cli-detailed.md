# Gemini CLI: Deep Competitive Intelligence Analysis

> VALUE-FOCUSED analysis of Gemini CLI's backend architecture. Not just what exists, but WHY
> each design decision was made, what problems it solves, and what would break without it.
> Companion document to `gemini-cli.md` (which covers the factual "what").

---

## Table of Contents

1. [Executive Summary: 7 Competitive Advantages](#1-executive-summary-7-competitive-advantages)
2. [Architecture Philosophy: Why a Monorepo with Config-as-DI](#2-architecture-philosophy-why-a-monorepo-with-config-as-di)
3. [Tool Framework: Why the 3-Phase Lifecycle Changes Everything](#3-tool-framework-why-the-3-phase-lifecycle-changes-everything)
4. [All 17 Tools: WHY Each Exists and What Breaks Without It](#4-all-17-tools-why-each-exists-and-what-breaks-without-it)
5. [The Scheduler: Why Event-Driven Tool Orchestration Matters](#5-the-scheduler-why-event-driven-tool-orchestration-matters)
6. [Subagent System: Why Full Isolation Is Non-Negotiable](#6-subagent-system-why-full-isolation-is-non-negotiable)
7. [Context Management: Why JIT Discovery Is the Real Innovation](#7-context-management-why-jit-discovery-is-the-real-innovation)
8. [Permission System: Why 4 Layers Are Better Than 1](#8-permission-system-why-4-layers-are-better-than-1)
9. [Unique Innovations Worth Studying](#9-unique-innovations-worth-studying)
10. [Architecture Comparison: Gemini CLI vs AVA](#10-architecture-comparison-gemini-cli-vs-ava)
11. [Actionable Recommendations for AVA](#11-actionable-recommendations-for-ava)
12. [Appendix: File Reference](#12-appendix-file-reference)

---

## 1. Executive Summary: 7 Competitive Advantages

Gemini CLI is Google's open-source TypeScript CLI agent (~42k GitHub stars). It represents
the most sophisticated **tool execution framework** among all analyzed competitors. While
it is Gemini-only (single-provider), the engineering quality of its tool lifecycle, scheduler,
and permission system is best-in-class.

**The 7 things Gemini CLI does better than anyone else:**

| # | Innovation | Why It Matters |
|---|-----------|----------------|
| 1 | **4-tier Edit Recovery** | exact → flexible → regex → fuzzy → LLM self-correction. No other tool survives this many failure modes. |
| 2 | **Dual Output System** | Every tool produces `llmContent` (for the model) AND `returnDisplay` (for the user). Decouples what the LLM sees from what humans see. |
| 3 | **Tail Tool Calls** | A tool can request another tool execute immediately after it, without consuming an LLM turn. Zero-cost tool chaining. |
| 4 | **Config Prototype Chain** | `Object.create(config)` gives subagents isolated tool registries while sharing everything else. Elegant, zero-allocation DI. |
| 5 | **Modifiable Tools** | Users can open an editor to modify AI-proposed edits before they execute. Bridges the gap between full autonomy and full control. |
| 6 | **JIT Context Discovery** | When a tool accesses a path, the system traverses upward looking for `GEMINI.md` files to inject. Context is loaded on-demand, not upfront. |
| 7 | **MessageBus Confirmation Racing** | Tool confirmation races TUI vs IDE promises — whichever resolves first wins. Supports multi-surface approval seamlessly. |

---

## 2. Architecture Philosophy: Why a Monorepo with Config-as-DI

### The Problem: CLI Needs Both Speed and Extensibility

A CLI agent must start fast (sub-second), run tools efficiently, and still support plugins,
MCP servers, hooks, skills, and IDE integration. Traditional DI containers (like InversifyJS)
add startup overhead and ceremony.

### The Solution: Config as God Object

Gemini CLI's `Config` class (~800+ lines) serves as the dependency injection container:

```typescript
// Instead of this:
const toolRegistry = container.resolve(ToolRegistry);
const policyEngine = container.resolve(PolicyEngine);

// Gemini CLI does this:
const toolRegistry = config.getToolRegistry();
const policyEngine = config.getPolicyEngine();
```

**Why this is actually clever:**

1. **Zero framework overhead** — No DI container library, no resolution phase, no scope
   management. Just a class with getters. Startup is fast.

2. **Prototype-chain isolation for subagents** — The killer trick:
   ```typescript
   // In agent-scheduler.ts:
   const agentConfig = Object.create(config);
   agentConfig.toolRegistry = new ToolRegistry(/* subset of tools */);
   ```
   JavaScript's prototype chain means `agentConfig.getToolRegistry()` returns the isolated
   registry, but `agentConfig.getContentGenerator()` falls through to the parent's instance.
   This gives you DI scope isolation for FREE — no container needed.

3. **Single import path** — Any module that has `config` has access to everything. No
   dependency graph to manage, no circular import issues from service-to-service references.

**What would break without it:**
- Subagent tool isolation would require a full DI container or manual parameter threading
- Every service would need explicit dependencies, adding import complexity
- Startup would be slower if using a real DI framework

**The tradeoff:** The class is massive and hard to test in isolation. But for a CLI tool
where startup speed and simplicity matter more than unit test purity, it's the right call.

### Monorepo with 7 Packages

| Package | Purpose | Why Separate? |
|---------|---------|---------------|
| `core` | All business logic | Reusable across surfaces (CLI, SDK, A2A) |
| `cli` | React/Ink terminal UI | UI concerns separated from logic |
| `sdk` | Programmatic embedding | Clean API surface for library usage |
| `a2a-server` | Agent-to-Agent protocol | Experimental, independent lifecycle |
| `devtools` | Developer tools | Development-only, not shipped |
| `vscode-ide-companion` | VS Code extension | IDE integration surface |
| `test-utils` | Shared test utilities | DRY test infrastructure |

**Why this structure matters for AVA:** Gemini CLI proves that separating `core` from `cli`
from `sdk` enables multiple consumption surfaces without code duplication. AVA's current
`packages/core` + `packages/platform-*` pattern is similar but adds a platform abstraction
layer that Gemini CLI avoids (since it's Node.js only).

---

## 3. Tool Framework: Why the 3-Phase Lifecycle Changes Everything

### The Problem: Tools Need More Than "Call and Return"

Most AI coding tools implement tools as simple functions: take params, return result. This
fails when you need:
- Parameter validation before execution (reject bad tool calls without running them)
- Policy checks between validation and execution (should we even run this?)
- User confirmation with preview (show what will happen before it does)
- Content modification before execution (let users edit proposed changes)
- Different outputs for LLM vs human (what the model needs vs what the user sees)
- Tool chaining without extra LLM turns (one tool triggering another)

### The Solution: Build → Validate → Confirm → Execute

```
Model requests tool call
        │
        ▼
[1. Build Phase] ─── DeclarativeTool.build(params) ──→ ToolInvocation
   │ Schema validation (JSON Schema)                      (or Error)
   │ Param-level validation (validateToolParamValues)
   │ Path correction, placeholder detection
        │
        ▼
[2. Policy Phase] ─── shouldConfirmExecute() ──→ ALLOW / DENY / ASK_USER
   │ MessageBus publishes TOOL_CONFIRMATION_REQUEST
   │ PolicyEngine evaluates rules
   │ 30-second timeout defaults to ASK_USER
        │
        ▼
[3. Confirmation Phase] (only if ASK_USER)
   │ Show diff preview / command preview / MCP args
   │ User chooses: ProceedOnce / ProceedAlways / ModifyWithEditor / Cancel
   │ If ModifyWithEditor: open temp file, user edits, read back
        │
        ▼
[4. Execute Phase] ─── invocation.execute(signal, updateOutput) ──→ ToolResult
   │ Returns: { llmContent, returnDisplay, error?, tailToolCallRequest? }
```

### Why the Kind System Matters

```typescript
enum Kind {
  Read, Edit, Delete, Move, Search, Execute, Think, Agent, Fetch, Communicate, Plan, SwitchMode, Other
}

const MUTATOR_KINDS = [Edit, Delete, Move, Execute];  // Need approval
const READ_ONLY_KINDS = [Read, Search, Fetch];         // Can run freely + in parallel
```

**The real value:** The Kind system enables three things at once:
1. **Automatic permission decisions** — mutator tools need approval, read-only don't
2. **Parallel execution** — contiguous read-only tools execute concurrently
3. **Plan mode restrictions** — only allow certain Kinds in planning mode

**What would break without the Kind system:**
- Every tool would need manual permission configuration
- No automatic parallel batching (everything sequential)
- Plan mode would need explicit tool whitelists instead of Kind-based filtering

### Why Dual Output (llmContent vs returnDisplay) Is Critical

```typescript
interface ToolResult {
  llmContent: PartListUnion;       // What the LLM sees in its context
  returnDisplay: ToolResultDisplay; // What the user sees in the terminal
}
```

**Example — Edit tool:**
- `llmContent`: `"Successfully modified file: src/app.ts (1 replacement). Here is the updated code:\n@@ -10,3 +10,5 @@..."`
- `returnDisplay`: `{ fileDiff: "...", fileName: "app.ts", diffStat: { added: 2, removed: 0 } }`

The LLM gets a compact text summary + diff snippet (saves tokens). The user gets a rich
diff display with syntax highlighting. **No other competitor we analyzed has this separation.**

Most tools (AVA included) send the same content to both the LLM and the user, wasting tokens
on formatting the LLM doesn't need, or depriving the user of rich visualization.

### Why Tail Tool Calls Are a Zero-Cost Innovation

```typescript
interface ToolResult {
  // ... other fields
  tailToolCallRequest?: { name: string; args: Record<string, unknown> };
}
```

When a tool returns a `tailToolCallRequest`, the scheduler:
1. Logs the intermediate result
2. Creates a new ValidatingToolCall for the tail tool
3. Replaces the active call (same callId!)
4. Continues the processing loop

**The genius:** From the LLM's perspective, it made ONE tool call and got ONE result. But
internally, two tools executed. This is free — no extra LLM turn, no extra tokens.

**Real-world usage:** The `write_file` tool uses `ensureCorrectFileContent()` which calls
an LLM to fix content if the model used omission placeholders. The corrected content then
chains via tail call to the actual write operation.

**What would break without tail calls:**
- Every "fix and retry" pattern would require the LLM to make a second tool call
- This costs 1 extra round-trip per correction (~2-5 seconds + tokens)
- Over a session, this adds up to significant latency and cost

### Why ModifiableDeclarativeTool Bridges Autonomy and Control

```typescript
interface ModifiableDeclarativeTool<TParams> {
  getModifyContext(signal: AbortSignal): ModifyContext<TParams>;
}

interface ModifyContext<TParams> {
  getFilePath(params: TParams): string;
  getCurrentContent(params: TParams): Promise<string>;
  getProposedContent(params: TParams): Promise<string>;
  createUpdatedParams(oldContent: string, modifiedContent: string, params: TParams): TParams;
}
```

When a user chooses "Modify with Editor" during confirmation:
1. Current file content is written to a temp file
2. Proposed content is written to another temp file
3. User's preferred diff editor opens (VS Code, vim, etc.)
4. User edits the proposed content
5. `createUpdatedParams()` produces new tool params with the user's modifications
6. The tool executes with the modified params

**Tools implementing this:** Edit, WriteFile, Memory

**Why this matters:** It solves the fundamental tension between "let the AI do it" and
"I need to review/tweak this." The user can approve, reject, OR modify — three options
instead of the usual two. This is uniquely powerful for code edits where the AI gets
95% right but misses a detail.

---

## 4. All 17 Tools: WHY Each Exists and What Breaks Without It

### 4.1 ReadFile (`read_file`) — Kind.Read

**The problem it solves:** The LLM needs to see file contents to make informed edits.
Reading the entire file wastes tokens; reading nothing leaves the model blind.

**What makes it clever:**
- Line-range support (`start_line`, `end_line`) for surgical reads
- Truncation with continuation hints — when output exceeds threshold, tells the model
  how to read the next chunk
- Separate `llmContent` (plain text with line numbers) from `returnDisplay` (can be
  formatted differently)

**What would break without it:** The model would need to guess file contents or rely on
grep results, leading to incorrect edits and higher failure rates.

### 4.2 ReadManyFiles (`read_many_files`) — Kind.Read

**The problem it solves:** Understanding a codebase requires reading multiple files. Doing
this one-at-a-time wastes LLM turns.

**What makes it clever:**
- Glob-based batch reading with include/exclude patterns
- `Promise.allSettled()` for parallel I/O — one failed read doesn't block others
- Single tool call reads 10+ files, saving 9+ LLM turns

**What would break without it:** Every multi-file exploration would require N separate
`read_file` calls, each consuming an LLM turn. A 10-file exploration would take 10 turns
instead of 1.

### 4.3 WriteFile (`write_file`) — Kind.Edit + ModifiableDeclarativeTool

**The problem it solves:** Sometimes the model needs to write an entire file, not just
edit a section.

**What makes it clever:**
- `ensureCorrectFileContent()` — calls an LLM to detect and fix content problems
  (omission placeholders like `// ... rest of code ...`) BEFORE writing
- ModifiableDeclarativeTool — user can modify the proposed content in an editor
- Tail tool call pattern for the correction flow

**What would break without it:** Models frequently use omission placeholders in long files.
Without the LLM content correction, these placeholders would be written to disk, destroying
existing code. This is one of the most common failure modes in AI coding tools.

### 4.4 Edit (`replace`) — Kind.Edit + ModifiableDeclarativeTool

**The problem it solves:** LLMs produce imperfect search/replace strings. Exact matching
fails frequently due to whitespace differences, indentation changes, or minor content drift.

**What makes it THE most important tool in the system:**

The 4-tier replacement strategy is Gemini CLI's crown jewel:

| Tier | Strategy | What It Handles | Cost |
|------|----------|----------------|------|
| 1 | **Exact** | Perfect string match | O(n) |
| 2 | **Flexible** | Whitespace-insensitive line matching | O(n*m) |
| 3 | **Regex** | Tokenized pattern with flexible whitespace | O(n*m) |
| 4 | **Fuzzy** | Levenshtein distance with weighted scoring | O(n*L²) |

Plus a **5th recovery layer**: LLM self-correction via Flash model when all 4 fail.

**Deep dive into the fuzzy tier:**
```
weightedDist = d_norm + (d_raw - d_norm) * 0.1
score = weightedDist / searchBlock.length
```
Where:
- `d_raw` = Levenshtein distance on raw text
- `d_norm` = Levenshtein distance on whitespace-stripped text
- Whitespace differences cost only 10% of character differences
- 10% threshold: scores ≤ 0.1 are considered matches

**Complexity guard:** `sourceLines * old_string.length² > 400M` prevents the fuzzy
matcher from hanging on large files. This is a real-world engineering constraint that
most competitors miss.

**LLM self-correction flow:**
1. All 4 tiers fail → `FixLLMEditWithInstruction` called
2. Sends instruction + old_string + new_string + error + current file content to Flash model
3. Flash model produces corrected search/replace strings
4. Run all 4 tiers again with corrected strings
5. If that fails too, return the original error

**What makes the edit confirmation special:**
- Calculates the diff BEFORE showing the confirmation prompt
- Opens IDE diff view (VS Code) via `IdeClient.openDiff()` alongside terminal confirmation
- Races both — whichever the user responds to first wins
- User can accept, reject, or modify the proposed edit

**What would break without the 4-tier system:**
- Exact matching alone fails ~20-30% of the time on real codebases
- Without flexible matching, any indentation mismatch kills the edit
- Without fuzzy matching, minor word changes in comments break edits
- Without LLM correction, the failure rate would be 3-5x higher

**Competitive insight:** AVA's edit tool uses fuzzy matching but lacks the regex tier and
LLM self-correction. Adding these would significantly reduce edit failure rates.

### 4.5 Shell (`run_shell_command`) — Kind.Execute

**The problem it solves:** Agents need to run commands (tests, builds, linters) but shell
execution is the most dangerous tool in any agent's toolkit.

**What makes it clever:**
- Background process support with `is_background` flag and PID tracking via `pgrep`
- Inactivity timeout (kills commands that hang)
- Binary output detection (prevents sending binary garbage to the LLM)
- LLM-based output summarization (compresses verbose command output)
- Root command parsing for policy — extracts the actual command being run (not just the
  shell string) for permission checks
- Live output streaming at 1-second intervals via `canUpdateOutput: true`

**What would break without it:** No ability to run tests, build projects, or execute any
external tools. The agent becomes a text editor, not a coding assistant.

### 4.6 Grep (`grep_search`) — Kind.Search

**The problem it solves:** Finding where things are defined/used in a codebase.

**What makes it clever — the 3-tier strategy:**
1. **git grep** (fastest, respects .gitignore automatically)
2. **System grep** (fallback if not in a git repo)
3. **JavaScript fallback** (works everywhere, no external dependencies)

Plus:
- Streaming results (shows matches as they're found)
- Per-file and total match limits (prevents output explosion)
- Timeout protection (kills runaway searches)

**What would break without the 3-tier approach:**
- In non-git repos, `git grep` fails silently
- On systems without `grep` installed, the tool is dead
- The JS fallback ensures the tool ALWAYS works, everywhere

### 4.7 Glob (`find_files`) — Kind.Search

**The problem it solves:** Finding files by name/pattern in a codebase.

**What makes it clever:**
- **Recency-aware sorting** — files modified in the last 24 hours are sorted first
- Respects `.gitignore` AND `.geminiignore`

**Why recency sorting matters:** When an agent searches for `*.test.ts`, the files most
likely to be relevant are the ones recently modified. This subtle optimization reduces
the number of follow-up reads needed.

### 4.8 LS (`list_directory`) — Kind.Search

**The problem it solves:** Understanding project structure.

**What makes it clever:**
- Directories-first ordering (shows structure before content)
- File sizes included (helps the model estimate if a file is too large to read)
- Respects ignore patterns

### 4.9 WebSearch (`google_web_search`) — Kind.Search

**The problem it solves:** The model needs information not in the codebase — API docs,
error messages, library usage patterns.

**What makes it uniquely different from every competitor:**
- NOT a traditional search API (no Bing/Google Search API calls)
- Uses Gemini API grounding — sends the query to a special `web-search` model
- The API returns `groundingMetadata` with source URLs AND byte-position citations
- Inline citation markers are inserted using UTF-8 byte positions

**Why this matters:** Grounding-based search returns LLM-synthesized answers with proper
source attribution, not a list of 10 blue links. The model gets a direct answer with
citations, not raw search results it needs to parse.

**What would break without it:** The model would be limited to codebase-only knowledge.
Any question about external APIs, error messages, or best practices would go unanswered.

### 4.10 WebFetch (`web_fetch`) — Kind.Fetch

**The problem it solves:** Reading specific web pages (documentation, GitHub issues, etc.).

**What makes it clever:**
- Dual mode: Gemini URL context (API-based, respects Google's web cache) vs direct fetch
- GitHub blob→raw URL conversion (handles `/blob/main/` → `/raw/main/` automatically)
- Rate limiting: 10 requests/minute per host via LRU cache (prevents abuse)
- Content type handling: HTML→text, images/PDF/video as base64

**What would break without the rate limiting:** An agent in a loop could DDoS a website
with rapid fetch requests. The per-host LRU rate limiter prevents this while still
allowing efficient multi-page reading.

### 4.11 Memory (`save_memory`) — Kind.Think + ModifiableDeclarativeTool

**The problem it solves:** Agents forget everything between sessions. Important discoveries
(project conventions, architecture decisions, user preferences) need to persist.

**What makes it clever:**
- Writes to persistent `GEMINI.md` files (not a database — human-readable!)
- Markdown injection sanitization (prevents the model from corrupting the file format)
- Section-aware insertion (adds facts to the right section, not just appending)
- ModifiableDeclarativeTool — user can review/edit what the AI wants to remember

**What would break without the sanitization:** The model could inject markdown that breaks
the GEMINI.md parser, corrupting all project memory. The sanitization (stripping dangerous
markdown constructs) prevents this.

### 4.12 WriteTodos (`write_todos`) — Kind.Other

**The problem it solves:** Complex tasks need a visible plan. The model needs to track
what it's doing and what's left.

**What makes it clever:**
- Status enforcement: only 1 item can be `in_progress` at a time
- Statuses: pending → in_progress → completed/cancelled
- Visible to both the model (LLM context) and the user (terminal display)

**Why the single in_progress constraint matters:** Without it, the model marks everything
as "in progress" simultaneously, losing track of sequential execution order. The constraint
forces linear focus.

### 4.13 ActivateSkill (`activate_skill`) — Kind.Other

**The problem it solves:** Not all context should be loaded upfront. Skills (specialized
instructions for React, testing, debugging, etc.) should be loaded on-demand.

**What makes it clever:**
- Dynamic skill loading into the system prompt context
- Also adds the skill's directory to the workspace (gives tools access to skill files)
- 4-tier discovery: built-in → extension → user → workspace

**What would break without on-demand loading:** Loading all skills upfront would waste
thousands of tokens on irrelevant instructions. On-demand loading keeps the context lean
until a skill is actually needed.

### 4.14 AskUser (`ask_user`) — Kind.Communicate

**The problem it solves:** Sometimes the agent needs clarification before proceeding.

**What makes it clever:**
- Structured questions (text input, single-choice, multi-choice)
- Multi-question support (ask several things at once)
- Question types defined in schema for proper UI rendering

**What would break without structured types:** Free-text questions produce ambiguous
answers. Choice-based questions produce unambiguous responses that the model can reliably
parse and act on.

### 4.15 EnterPlanMode — Kind.Plan

**The problem it solves:** Users want to see what the agent WILL do before it does it.

**What makes it clever:**
- Switches to `ApprovalMode.PLAN` — all mutating tools are restricted
- Confirmation dialog explains the restrictions
- The model can still use read-only tools (search, read, grep) to explore the codebase
  while planning

**Why it's a mode switch, not a tool restriction:** Making it a mode switch means ALL
tools respect the restriction automatically via the Kind system. No need to maintain a
manual whitelist.

### 4.16 ExitPlanMode — Kind.Plan

**The problem it solves:** Transitioning from planning to execution with user approval.

**What makes it clever:**
- Submits the plan file for user review
- Path traversal protection (plan files must be in the plans directory)
- Three outcomes: approve (choose DEFAULT/AUTO_EDIT mode), reject with feedback (stays
  in plan mode), or cancel entirely
- Rejection feedback is fed back to the model, which can revise the plan

**Why the three-outcome system matters:** Most agents have approve/reject. The "reject
with feedback" flow creates a collaborative planning loop where the user and agent
iterate on the plan until it's right.

### 4.17 GetInternalDocs — Kind.Think

**The problem it solves:** The agent needs to reference its own documentation (how tools
work, configuration options, etc.).

**What makes it clever:**
- Self-documentation tool — reads Gemini CLI's own docs
- Path traversal protection (can only read docs, not arbitrary files)
- Finds docs via `sidebar.json` marker (doesn't hardcode paths)

**What would break without it:** The model would hallucinate information about its own
capabilities instead of looking it up. This is especially important for Gemini CLI-specific
features like skills, hooks, and extensions.

---

## 5. The Scheduler: Why Event-Driven Tool Orchestration Matters

### The Problem: Tools Are Not Just Functions

In a real agent system, executing a tool involves:
1. Validating parameters (can fail)
2. Checking policy (may deny)
3. Getting user confirmation (may modify, cancel, or approve)
4. Executing the tool (may fail, timeout, or produce a tail call)
5. Processing the result (may trigger another tool)

Sequential "call and wait" breaks down when you need:
- Parallel execution of read-only tools
- Cancellation propagation across batches
- IDE and terminal racing for confirmations
- Tail call chaining
- Progress tracking for long-running tools

### The Solution: 3-Phase Event-Driven Scheduler

```
Phase 1: Ingestion & Resolution
  _startBatch(requests)
  │ Resolve tools from ToolRegistry
  │ Build invocations (validate params)
  │ Enqueue to SchedulerStateManager
  │
Phase 2: Processing Loop
  _processQueue()
  │ _processNextItem() — runs until queue drained
  │ │ Dequeue next item
  │ │ Batch contiguous read-only/Agent tools for parallel execution
  │ │ Process validating calls (policy + confirmation)
  │ │ Execute scheduled calls
  │ │ Finalize terminal calls
  │ │ Handle stuck states (yield to event loop)
  │
Phase 3: Single Call Orchestration
  _processToolCall()
  │ checkPolicy() → DENY/ASK_USER/ALLOW
  │ resolveConfirmation() → outcome
  │ updatePolicy() → persist rules
  │ _execute() → ToolResult
  │ Handle tailToolCallRequest → replaceActiveCallWithTailCall()
```

### Why Parallel Read-Only Execution Is the Right Default

```typescript
private _isParallelizable(tool?: AnyDeclarativeTool): boolean {
  return tool.isReadOnly || tool.kind === Kind.Agent;
}
```

When the model requests `[read_file, grep_search, glob]`, these are ALL read-only.
The scheduler batches them and executes in parallel via `Promise.all()`.

**The real-world impact:** A typical "explore the codebase" sequence involves 3-5
read-only tool calls. Parallel execution cuts wall-clock time by 60-80%.

But mutating tools MUST be sequential — `edit file A` then `edit file B` cannot
run in parallel because B might depend on A's result.

### Why the State Machine Matters

```
Validating → Scheduled → Executing → Success/Error/Cancelled
                │
                ▼
         AwaitingApproval → (user confirms) → Scheduled
                         → (user cancels) → Cancelled
```

Every state transition publishes `TOOL_CALLS_UPDATE` to the MessageBus. This means:
- The terminal UI updates in real-time as tools progress
- The IDE companion gets live updates
- Progress tracking (MCP progress events) works seamlessly

**What would break without the state machine:**
- No way to show "3/5 tools complete" progress
- No way to cancel a batch mid-execution
- No way to track which tool is awaiting approval vs executing

### Why Confirmation Racing Is Elegant

```typescript
// In confirmation.ts — simplified
const tui = waitForTuiConfirmation(messageBus, callId);
const ide = waitForIdeConfirmation(ideClient, callId);
const result = await Promise.race([tui, ide]);
// Cleanup whichever didn't win
```

This means a user can approve a tool call from EITHER the terminal OR VS Code.
Whichever responds first wins, and the other is cleaned up. This is transparent
to the tool — it just gets an outcome.

**What would break without racing:** Users would be locked to one confirmation
surface. If using VS Code, they'd have to switch to the terminal to approve.
If using the terminal, they couldn't use VS Code's diff viewer to review.

---

## 6. Subagent System: Why Full Isolation Is Non-Negotiable

### The Problem: Subagents Can Recurse Infinitely

If a subagent has access to the `task` tool (which spawns subagents), it can spawn
another subagent, which spawns another, creating infinite recursion. If a subagent
shares the parent's tool registry, it can do anything the parent can do, including
spawning more agents.

### The Solution: Isolated ToolRegistry + Recovery Pattern

```typescript
class LocalAgentExecutor<TOutput> {
  private readonly toolRegistry: ToolRegistry;  // SEPARATE from parent

  constructor(config: Config, definition: AgentDefinition) {
    // Create isolated config with prototype chain trick
    const agentConfig = Object.create(config);

    // New tool registry with SUBSET of tools
    this.toolRegistry = new ToolRegistry(/* only safe tools */);
    agentConfig.toolRegistry = this.toolRegistry;

    // Mandatory termination tool
    this.toolRegistry.registerTool(completeTaskTool);
  }
}
```

**Key isolation properties:**
1. **No recursion** — subagent's registry doesn't include the `task` tool
2. **Tool subset** — only tools appropriate for the agent's role
3. **MCP tools auto-upgrade** — tool names become fully-qualified (`server__tool`)
4. **Mandatory `complete_task`** — the ONLY way to terminate is structured output

### Why the Recovery Pattern Is Critical

```
maxTurns reached or maxTime expired
        │
        ▼
[Grace Period: 1 minute]
  Agent can ONLY call complete_task
  All other tools rejected
        │
        ▼
  If complete_task called → structured output returned
  If grace period expires → forced termination with partial results
```

**Why 1 minute?** It's enough time for the model to produce a summary of what it
accomplished, but not enough to continue executing tools. This ensures the parent
agent always gets some result back, even from a runaway subagent.

### Why DeadlineTimer with Pause/Resume Matters

```typescript
const timer = new DeadlineTimer(5 * 60 * 1000); // 5 min
// When waiting for user confirmation:
timer.pause();
// When confirmation received:
timer.resume();
```

**The insight:** Time spent waiting for user approval shouldn't count against the
agent's execution budget. If a user takes 2 minutes to review an edit, the agent
still has its full 5 minutes of actual execution time.

### Agent Types and Their Roles

| Agent | Tools Available | Max Turns | Max Time | Purpose |
|-------|----------------|-----------|----------|---------|
| Codebase Investigator | read, grep, glob, ls | 15 | 5 min | Read-only code exploration |
| CLI Help Agent | read (internal docs only) | 15 | 5 min | Self-documentation |
| Browser Agent | MCP browser tools | 15 | 5 min | Visual web interaction |
| Generalist | configurable subset | 15 | 5 min | General-purpose tasks |
| Remote (A2A) | remote API | varies | varies | Cross-agent delegation |

---

## 7. Context Management: Why JIT Discovery Is the Real Innovation

### The Problem: Too Much Context Wastes Tokens, Too Little Makes the Agent Dumb

Loading all project context upfront (every GEMINI.md, every skill, every extension
context file) wastes thousands of tokens on information the agent may never need.
But loading nothing means the agent misses important project conventions.

### The Solution: 3-Tier Hierarchical Memory + JIT Discovery

**Tier 1: Global Memory** — `~/.gemini/GEMINI.md` + user-level context files
- Always loaded (typically small — user preferences, global conventions)

**Tier 2: Extension Memory** — context files contributed by installed extensions
- Loaded on session start (if extensions are active)

**Tier 3: Project Memory** — workspace-level `GEMINI.md` files
- Loaded on session start, but ONLY from trusted folders

**Plus JIT (Just-In-Time) Context:**
```
Tool accesses path: /project/packages/api/src/routes/users.ts
        │
        ▼
discoverContext() traverses upward:
  /project/packages/api/src/routes/ — GEMINI.md? No
  /project/packages/api/src/ — GEMINI.md? No
  /project/packages/api/ — GEMINI.md? YES! → inject into context
  /project/packages/ — GEMINI.md? No
  /project/ — already loaded (Tier 3)
```

**Why this is brilliant:** In a monorepo with 20 packages, each package can have its
own GEMINI.md with package-specific conventions. These are ONLY loaded when the agent
actually works in that package. A session that only touches `packages/api/` never loads
context for `packages/frontend/`.

**What would break without JIT discovery:**
- Either load all 20 package contexts upfront (wasting ~2000 tokens per package = 40K tokens)
- Or miss package-specific context (leading to style violations, wrong patterns)
- JIT gives you both: context when needed, no waste when not needed

### Tool Output Masking: Hybrid Backward Scanned FIFO

```
History: [User, Model, Tool(5K), Model, Tool(20K), Model, Tool(30K), Model, Tool(25K)]
                                                                    ▲
                                                          Protection window (50K)
```

The masking algorithm:
1. Protect the newest 50K tokens of tool output (always visible)
2. Protect the latest turn entirely
3. Scan backward past the protection window
4. Only mask if total prunable tokens > 30K (batch trigger)
5. Replace masked content with `<tool_output_masked>` indicator

**Why the 30K batch trigger?** Masking has a cost — the model loses context. Only
trigger masking when there's enough to save. Small savings aren't worth the information loss.

### Chat Compression: Why 50% Threshold with Inflation Validation

Compression triggers at 50% of the 1M token context window (~524K tokens):
1. Find split point — compress oldest ~70%, preserve newest 30%
2. Send older portion to Flash model for summarization
3. Replace compressed portion with `[COMPRESSED CONTEXT]`
4. **Validate**: if new token count > original, skip compression (inflation guard)

**Why the inflation guard matters:** LLM summaries can sometimes be LONGER than the
original, especially for highly structured content (code, JSON). The validation ensures
compression always saves tokens, never wastes them.

---

## 8. Permission System: Why 4 Layers Are Better Than 1

### The Problem: One-Size-Fits-All Permissions Don't Work

Different users want different levels of trust:
- Experienced developers: "Just do it, don't ask me"
- Security-conscious teams: "Ask for every shell command, auto-approve edits"
- Demo/evaluation mode: "Read-only, planning only"
- Enterprise deployments: "Custom rules per team, per tool, per argument pattern"

### The Solution: 4-Layer Permission Stack

```
Layer 1: PolicyEngine Rules
  ├── Rule: { toolName: "run_shell_command", argsPattern: /rm -rf/, decision: DENY }
  ├── Rule: { toolName: "write_file", decision: ALLOW, modes: [AUTO_EDIT] }
  └── Rule: { toolName: "server__*", decision: ASK_USER }  ← MCP wildcard!

Layer 2: SafetyChecker Framework
  ├── allowed-path checker (validates tool args reference workspace paths)
  ├── conseca checker (Google's safety system)
  └── External checkers (subprocess-based, receives JSON on stdin)

Layer 3: FolderTrust
  ├── Trusted folders → load project GEMINI.md, hooks, skills, policies
  └── Untrusted folders → global config only

Layer 4: ApprovalMode
  ├── DEFAULT → ask for mutating operations
  ├── AUTO_EDIT → auto-approve edits, ask for shell
  ├── YOLO → auto-approve everything
  └── PLAN → read-only, planning only
```

### Why MCP Wildcards (`server__*`) Are Essential

MCP servers add arbitrary tools. Without wildcards, you'd need a rule for every tool
on every server. With `server__*`, one rule covers all tools from a server.

Even better: per-tool rules within a server:
```
server__tool_name → specific tool permission
server__* → fallback for all other tools on that server
```

### Why Approval Mode Transitions Matter

```
DEFAULT ──(user says "auto-approve edits")──→ AUTO_EDIT
AUTO_EDIT ──(user says "approve everything")──→ YOLO
YOLO ──(user says "plan mode")──→ PLAN
PLAN ──(user approves plan)──→ DEFAULT or AUTO_EDIT
```

The system remembers which mode you were in and can transition back. Plan mode is
not a dead end — approving a plan returns you to execution mode with the plan
injected as context.

**What would break without layered permissions:**
- A single allow/deny system can't handle "trust edits but not shell commands"
- Without MCP wildcards, adding a new MCP server requires manual rule creation
- Without approval modes, there's no way to progressively increase trust during a session

---

## 9. Unique Innovations Worth Studying

### 9.1 Dual-Layer Loop Detection

**Heuristic layer:**
- Hash-based matching of tool calls (threshold: 5 identical consecutive calls)
- Content chunk matching (threshold: 10 identical chunks of 50 chars)
- Fast, zero-cost, catches obvious loops

**LLM-based layer:**
- Activates after 40 turns in a single prompt
- Evaluates the recent 20 turns for unproductive patterns
- Distinguishes "debugging progress" from "actual loops" (critical distinction!)
- Adaptive check interval: 7-15 turns based on confidence
- Uses a "double-check" with a separate model alias for accuracy

**Why both layers?** The heuristic layer catches 80% of loops instantly. The LLM layer
catches the subtle 20% — like when the model is trying different approaches to fix a bug
but making no progress. A human would recognize "you've tried 5 different things and none
worked, try a different approach." The LLM detector does exactly this.

### 9.2 Model-Family-Specific Tool Schemas

Tools ship with different JSON schemas for different model generations:
- `default-legacy` (Gemini 2.5)
- `gemini-3` (Gemini 3.x)

**Why this matters:** Different model generations may interpret tool schemas differently.
A parameter description that works perfectly for Gemini 2.5 might confuse Gemini 3. By
shipping model-specific schemas, each model gets descriptions optimized for its training.

### 9.3 Composite Model Router

```
FallbackStrategy → OverrideStrategy → ApprovalModeStrategy
    → GemmaClassifierStrategy → ClassifierStrategy
    → NumericalClassifierStrategy → DefaultStrategy
```

Seven strategies in a chain, each with the opportunity to route the request to a
different model. The Gemma classifier runs a LOCAL model to classify task complexity,
routing simple tasks to Flash (cheap/fast) and complex tasks to Pro.

**Why this matters for cost:** On a long session, 70% of turns might be simple
(reading files, running commands). Routing these to Flash instead of Pro could save
50-70% on API costs.

### 9.4 Omission Placeholder Detection

```typescript
function detectOmissionPlaceholders(content: string): string[] {
  // Detects patterns like:
  // "// ... rest of code ..."
  // "/* remaining methods */"
  // "# TODO: implement"
  // "... (abbreviated) ..."
}
```

This runs DURING validation (before execution). If the model's `new_string` contains
omission placeholders that weren't in the `old_string`, the edit is rejected with a
clear error message telling the model to provide exact content.

**Why this is critical:** This is the #1 failure mode in AI code editing. Models
frequently use "..." or "rest of code" placeholders in long files. Without this
detection, the placeholder gets written to disk, destroying existing code.

### 9.5 Environment Sanitization for Shell

```typescript
// Allowlist/blocklist environment variables passed to shell commands
// Optional redaction of sensitive values
```

When the shell tool executes a command, it doesn't pass the full environment.
Sensitive variables (API keys, tokens) can be redacted or excluded entirely.
This prevents accidental secret leakage via shell commands.

---

## 10. Architecture Comparison: Gemini CLI vs AVA

| Aspect | Gemini CLI | AVA |
|--------|-----------|-----|
| **Language** | TypeScript (Node.js ≥ 20) | TypeScript (Tauri + Node.js) |
| **Providers** | Gemini only | Multi-provider (OpenRouter, direct) |
| **Tool lifecycle** | 3-phase (build → confirm → execute) | `defineTool()` pattern |
| **Tool count** | 17 built-in | 24 registered |
| **Edit recovery** | 4-tier + LLM correction | Fuzzy matching |
| **Dual output** | Yes (llmContent + returnDisplay) | No (single output) |
| **Tail tool calls** | Yes | No |
| **Modifiable tools** | Yes (editor integration) | No |
| **Parallel execution** | Read-only tools in parallel | No (sequential) |
| **Subagent isolation** | Separate ToolRegistry + prototype chain | Commander with workers |
| **Context discovery** | JIT (traverse upward on tool access) | Static loading |
| **Permission layers** | 4 (PolicyEngine + SafetyChecker + FolderTrust + ApprovalMode) | 1 (permissions module) |
| **Loop detection** | Dual (heuristic + LLM) | Basic |
| **Model routing** | 7-strategy composite chain | Provider-based |
| **Session persistence** | JSON files | SQLite |
| **Extension system** | Skills + hooks + MCP + themes + policies | MCP |
| **Plan mode** | Full (with plan files + approval flow) | Plan mode (basic) |
| **Config DI** | God object with prototype chain | Module imports |
| **CLI rendering** | React/Ink | SolidJS (Tauri webview) |

---

## 11. Actionable Recommendations for AVA

### Priority 1: High Impact, Moderate Effort

**1. Implement Dual Output System**
- Add `llmContent` and `displayContent` separation to `ToolResult`
- Impact: Better LLM context utilization (fewer wasted tokens) AND richer user display
- Effort: Medium — requires updating all tools to produce separate outputs

**2. Add LLM Self-Correction to Edit Tool**
- When fuzzy matching fails, call a fast model to fix the search/replace strings
- Impact: Significant reduction in edit failure rates (estimated 20-40% fewer failures)
- Effort: Low — the pattern is straightforward: catch failure → call Flash → retry

**3. Add Omission Placeholder Detection**
- Detect `// ... rest of code ...` patterns in tool params during validation
- Impact: Prevents the #1 code destruction failure mode
- Effort: Low — regex-based detection, pure validation logic

### Priority 2: High Impact, Higher Effort

**4. Implement Tail Tool Calls**
- Allow `ToolResult` to include a `tailToolCallRequest` for zero-cost tool chaining
- Impact: Enables write-with-correction, read-then-process, and other compound operations
- Effort: Medium — requires scheduler modification to handle chaining

**5. Add Parallel Read-Only Tool Execution**
- When multiple read-only tools are requested, execute them concurrently
- Impact: 60-80% reduction in wall-clock time for exploration sequences
- Effort: Medium — requires tool Kind classification + scheduler changes

**6. Implement Modifiable Tool Pattern**
- Allow users to modify AI-proposed edits in an external editor before execution
- Impact: Bridges the gap between full autonomy and full control
- Effort: Medium-High — requires temp file management, editor launching, diff flow

### Priority 3: Strategic Investments

**7. Implement JIT Context Discovery**
- Load subdirectory-level context files on-demand when tools access paths
- Impact: Better context relevance in monorepos, fewer wasted tokens
- Effort: High — requires hooking into tool execution path for context injection

**8. Add 4-Tier Edit Recovery**
- Extend current fuzzy matching with flexible (whitespace-insensitive) and regex tiers
- Impact: More robust editing, fewer failures
- Effort: Medium — the algorithms are well-documented in Gemini CLI's source

**9. Implement Dual-Layer Loop Detection**
- Add LLM-based loop detection after N turns (supplement heuristic detection)
- Impact: Catches subtle unproductive loops that heuristics miss
- Effort: Medium — requires LLM call with loop detection prompt

**10. Add Model Routing Strategy Chain**
- Route simple turns to cheaper/faster models automatically
- Impact: 50-70% cost reduction on long sessions
- Effort: High — requires classifier training or rule-based routing

**11. Implement Multi-Surface Confirmation Racing**
- Race Tauri UI vs CLI confirmations (relevant for AVA's multi-surface architecture)
- Impact: Seamless UX across confirmation surfaces
- Effort: Medium — Promise.race pattern with cleanup

**12. Add MessageBus-Based Permission System**
- Decouple permission checks from tool execution via pub/sub
- Impact: Extensible permission system that supports plugins and IDE integration
- Effort: High — requires architectural refactoring

---

## 12. Appendix: File Reference

### Tool Implementations
| File | Tool | Lines |
|------|------|-------|
| `packages/core/src/tools/read-file.ts` | ReadFile | ~200 |
| `packages/core/src/tools/read-many-files.ts` | ReadManyFiles | ~150 |
| `packages/core/src/tools/write-file.ts` | WriteFile | ~350 |
| `packages/core/src/tools/edit.ts` | Edit | 1248 |
| `packages/core/src/tools/shell.ts` | Shell | ~600 |
| `packages/core/src/tools/grep.ts` | Grep | ~300 |
| `packages/core/src/tools/glob.ts` | Glob | ~200 |
| `packages/core/src/tools/ls.ts` | LS | ~150 |
| `packages/core/src/tools/web-search.ts` | WebSearch | ~200 |
| `packages/core/src/tools/web-fetch.ts` | WebFetch | ~400 |
| `packages/core/src/tools/memoryTool.ts` | Memory | ~250 |
| `packages/core/src/tools/write-todos.ts` | WriteTodos | ~150 |
| `packages/core/src/tools/activate-skill.ts` | ActivateSkill | ~100 |
| `packages/core/src/tools/ask-user.ts` | AskUser | ~200 |
| `packages/core/src/tools/enter-plan-mode.ts` | EnterPlanMode | ~100 |
| `packages/core/src/tools/exit-plan-mode.ts` | ExitPlanMode | ~200 |
| `packages/core/src/tools/get-internal-docs.ts` | GetInternalDocs | ~150 |

### Framework and Scheduler
| File | Purpose | Lines |
|------|---------|-------|
| `packages/core/src/tools/tools.ts` | Tool framework (DeclarativeTool, ToolResult, Kind) | 871 |
| `packages/core/src/tools/tool-registry.ts` | Tool registration and discovery | ~300 |
| `packages/core/src/tools/modifiable-tool.ts` | ModifiableDeclarativeTool interface | ~100 |
| `packages/core/src/scheduler/scheduler.ts` | Event-driven scheduler | 764 |
| `packages/core/src/scheduler/state-manager.ts` | State machine for tool calls | 569 |
| `packages/core/src/scheduler/tool-executor.ts` | Tool execution with hooks | 396 |
| `packages/core/src/scheduler/confirmation.ts` | Confirmation flow with IDE racing | 339 |
| `packages/core/src/scheduler/policy.ts` | Policy bridge | 208 |
| `packages/core/src/scheduler/tool-modifier.ts` | External editor modification flow | 107 |

### Agent System
| File | Purpose | Lines |
|------|---------|-------|
| `packages/core/src/agents/local-executor.ts` | Subagent execution loop | 1299 |
| `packages/core/src/agents/agent-scheduler.ts` | Agent-specific scheduler bridge | 71 |
| `packages/core/src/agents/subagent-tool.ts` | Tool wrapper for spawning agents | ~200 |
| `packages/core/src/agents/registry.ts` | Agent registry | ~150 |

### Core Architecture
| File | Purpose | Lines |
|------|---------|-------|
| `packages/core/src/core/turn.ts` | Turn lifecycle (async generator) | ~400 |
| `packages/core/src/core/contentGenerator.ts` | LLM API abstraction | ~300 |
| `packages/core/src/core/geminiChat.ts` | Chat session management | ~500 |
| `packages/core/src/config/config.ts` | Config god object / DI container | ~800+ |
| `packages/core/src/services/contextManager.ts` | 3-tier memory + JIT discovery | ~400 |
| `packages/core/src/services/loopDetectionService.ts` | Dual-layer loop detection | ~300 |
| `packages/core/src/routing/modelRouterService.ts` | Composite model routing | ~200 |
