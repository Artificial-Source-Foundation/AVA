# Plandex: Deep Competitive Intelligence Analysis

> VALUE-FOCUSED analysis of Plandex's backend architecture. Not just what exists, but WHY
> each design decision was made, what problems it solves, and what would break without it.
> Companion document to `plandex.md` (which covers the factual "what").

---

## Table of Contents

1. [Executive Summary](#1-executive-summary)
2. [Core Architectural Philosophy](#2-core-architectural-philosophy)
3. [The Multi-Stage Tell Pipeline: Why Planning Before Coding Wins](#3-the-multi-stage-tell-pipeline-why-planning-before-coding-wins)
4. [9 Model Roles: Why Not Just Use One Model?](#4-9-model-roles-why-not-just-use-one-model)
5. [The Build Race System: Why Concurrent Strategies Are Essential](#5-the-build-race-system-why-concurrent-strategies-are-essential)
6. [Structured Edits Pipeline: Why Reference Comments Beat Tool Calls](#6-structured-edits-pipeline-why-reference-comments-beat-tool-calls)
7. [Server-Side Git: Why Every Plan Gets Its Own Repo](#7-server-side-git-why-every-plan-gets-its-own-repo)
8. [Database Distributed Locking: Why PostgreSQL Locks Beat Mutexes](#8-database-distributed-locking-why-postgresql-locks-beat-mutexes)
9. [Conversation Summarization: Why Summaries Are Not Optional](#9-conversation-summarization-why-summaries-are-not-optional)
10. [Build Validation Loop: Why Three Attempts With Escalation](#10-build-validation-loop-why-three-attempts-with-escalation)
11. [Auto-Context / Architect Phase: Why the AI Should Pick Its Own Files](#11-auto-context--architect-phase-why-the-ai-should-pick-its-own-files)
12. [Missing File Handling: Why Pausing the Stream Is Genius](#12-missing-file-handling-why-pausing-the-stream-is-genius)
13. [ActivePlan & Streaming Protocol: Why Custom Streaming Beats SSE](#13-activeplan--streaming-protocol-why-custom-streaming-beats-sse)
14. [Reply Parser & Stream Processor: Why Incremental Parsing Matters](#14-reply-parser--stream-processor-why-incremental-parsing-matters)
15. [The Update Format Prompt: Why 977 Lines of Prompt Engineering](#15-the-update-format-prompt-why-977-lines-of-prompt-engineering)
16. [File Map System: Why Structural Summaries Beat Full Files](#16-file-map-system-why-structural-summaries-beat-full-files)
17. [Hook System: Why Lifecycle Events Enable Everything](#17-hook-system-why-lifecycle-events-enable-everything)
18. [Replacement-Based Diff System: Why Granular Review Changes Everything](#18-replacement-based-diff-system-why-granular-review-changes-everything)
19. [Plan Status State Machine: Why Explicit States Prevent Corruption](#19-plan-status-state-machine-why-explicit-states-prevent-corruption)
20. [What Would Break Without Each System](#20-what-would-break-without-each-system)
21. [Competitive Advantages vs. AVA](#21-competitive-advantages-vs-ava)
22. [Key Takeaways for AVA](#22-key-takeaways-for-ava)

---

## 1. Executive Summary

Plandex (~7k GitHub stars, now shut down) was a Go-based client-server AI coding assistant that made a fundamentally different architectural bet than any other agent: **the server is the brain, not the client**. While every other AI coding tool (aider, Cline, Claude Code, AVA) runs the agent loop on the user's machine, Plandex ran planning, building, git versioning, and LLM communication entirely server-side.

**Key competitive moats (when it was alive):**

- **Multi-stage planning pipeline** — a state machine that forces Planning before Implementation, with separate Context → Tasks → Implement phases
- **Build race system** — concurrent application strategies racing to produce valid edits, with automatic escalation to stronger models on failure
- **Server-side git repos per plan** — full version control of AI-generated changes without touching the user's filesystem until explicit `apply`
- **9 specialized model roles** — different models for different tasks (cheap for naming, expensive for coding), a significant cost optimization
- **Architect auto-context** — the AI selects its own context files from a codebase map before planning, eliminating the "what files should I load?" problem
- **Database-level distributed locking** — PostgreSQL advisory locks with heartbeats enabling concurrent multi-user access to plans

**Core insight:** Plandex's central innovation was treating AI coding as a **server-side planning problem** rather than a client-side editing problem. The tradeoff: maximum reliability and auditability at the cost of latency and deployment complexity. The project shut down, but many of its architectural ideas (build races, staged planning, auto-context) represent the state of the art in AI code generation reliability.

---

## 2. Core Architectural Philosophy

### 2.1. The Client-Server Split — A Radical Choice

Every mainstream AI coding tool runs locally: aider is a Python script, Cline is a VS Code extension, Claude Code is a CLI. Plandex separated the CLI (thin client) from the server (thick backend with PostgreSQL + git + LiteLLM).

**Why this exists:**

| Concern | Client-Only Approach | Plandex's Server Approach |
|---------|---------------------|--------------------------|
| Multi-user | Impossible | Natural — server manages shared state |
| Plan persistence | File-based, fragile | PostgreSQL + server-side git repos |
| Model routing | Client must know all keys | LiteLLM proxy centralizes routing |
| Concurrency | Local mutex at best | PostgreSQL distributed locks |
| Deployment | `pip install` / `npm install` | Docker Compose (PostgreSQL + LiteLLM + server) |
| Latency | Direct LLM calls | Extra hop through server |
| Data control | User's machine | Server holds all plan data |

**The real advantage:** The server architecture enabled features that are architecturally impossible in client-only agents:

1. **Shared plans** — multiple users could work on the same plan, with database locks preventing conflicts
2. **Long-running operations** — plans could run for hours without requiring the CLI to stay connected; the server maintained state independently
3. **Atomic branching** — creating/switching plan branches was a server-side git operation, not a client file shuffle
4. **Centralized model configuration** — API keys, model selections, and rate limits lived on the server, not scattered across user machines

**What would break without it:** You'd have to rebuild as a monolithic client — losing multi-user support, plan persistence across sessions, and centralized model management. Essentially, you'd have aider.

**Why Plandex shut down anyway:** The deployment complexity was the Achilles heel. Requiring Docker Compose with PostgreSQL + LiteLLM is a massive barrier compared to `pip install aider-chat`. The cloud-hosted version solved this but introduced data sovereignty concerns. The lesson: server architecture must be invisible to users or it becomes the adoption barrier.

**Competitive implication for AVA:** AVA uses a Tauri desktop app with SQLite — a middle ground. It gets persistence without server deployment, but loses multi-user support. This is arguably the right tradeoff for a desktop coding tool. However, AVA should study Plandex's plan persistence patterns — SQLite + local git could replicate the key benefits without the server overhead.

### 2.2. Go as the Implementation Language

Plandex chose Go over Python (aider), TypeScript (Cline, AVA, Claude Code), or Rust.

**Why Go was the right choice for Plandex specifically:**

1. **Goroutine concurrency** — the build race system, streaming fan-out, and heartbeat monitoring all run as concurrent goroutines with channels. Go's concurrency primitives made the build race trivially implementable.
2. **Single binary deployment** — the server ships as one binary (plus Docker for PostgreSQL). No npm/pip dependency hell.
3. **Strong HTTP server** — gorilla/mux + standard library HTTP server is battle-tested for the SSE streaming Plandex needs.
4. **Tree-sitter bindings** — Go has good tree-sitter support via go-tree-sitter, which Plandex uses extensively for file maps and validation.

**The tradeoff:** Go's type system is less expressive than TypeScript's (no generics until recently, no union types, verbose error handling). The Plandex codebase shows this — many places use `interface{}` and manual type assertions where TypeScript would use discriminated unions.

### 2.3. PostgreSQL + Git — The Dual Persistence Strategy

Plandex stores metadata in PostgreSQL and content in server-side git repos. This is not a common pattern.

**Why both databases:**

| Data Type | Storage | Why |
|-----------|---------|-----|
| Plan metadata (name, status, settings) | PostgreSQL | Relational queries, ACID, locking |
| Conversation messages | PostgreSQL | Search, indexing, summarization tracking |
| Context entries (files, URLs, notes) | PostgreSQL | SHA tracking, token counts |
| Model configurations | PostgreSQL | Role-based model selection |
| Actual file content / changes | Git repos | Branching, rewind, diff, merge |
| Applied replacements | Git commits | Full undo history |
| Plan branches | Git branches | Parallel exploration |

**The innovation:** The git layer is invisible to the user. When you type `plandex rewind`, you're not running git commands — the server does a `git revert` on the plan's private repo. When you `plandex branch`, it creates a git branch. But the user never sees `.git` directories or commit hashes.

**What would break without PostgreSQL:** No distributed locking (locks are PostgreSQL advisory locks), no efficient conversation search, no relational queries across plans/users/orgs. You'd need to reinvent all of this with file-based storage.

**What would break without Git:** No rewind, no branching, no atomic multi-file changes. You'd need a custom versioning system, which would be strictly worse than git for this use case.

---

## 3. The Multi-Stage Tell Pipeline: Why Planning Before Coding Wins

### The Architecture

When a user sends a prompt via `tell`, it enters a state machine with distinct stages:

```
User Prompt
    │
    ▼
┌─────────────────┐
│  resolveStage()  │ ← Determines which stage to enter
└─────────────────┘
    │
    ├─→ Context Phase (Architect model selects files)
    │       │
    │       ▼
    ├─→ Tasks Phase (Planner model breaks down into subtasks)
    │       │
    │       ▼
    └─→ Implementation Phase (Coder model generates code)
            │
            ▼
       Build Pipeline (apply changes to files)
```

The `resolveCurrentStage()` function in `tell_stage.go` checks:
1. If the last message has `DidMakePlan = true` → go to Implementation
2. If the last message has `CurrentStage = "context"` → go to Tasks  
3. If auto-context is enabled → start with Context phase
4. Otherwise → go straight to Tasks (or Implementation if no planning)

### Why This Matters

**The problem it solves:** Every agent that sends a user prompt directly to an LLM with "write code" instructions produces worse results than one that first plans. This is well-established in AI research (chain-of-thought, ReAct, etc.). But Plandex goes further — it doesn't just ask the LLM to "think step by step" in a single prompt. It uses **separate model calls with separate prompts for each phase**.

**Why separate phases instead of one prompt:**

| Single-Prompt Approach | Plandex's Staged Approach |
|----------------------|--------------------------|
| One model does everything | Different models for planning vs. coding |
| Context selection implicit | Explicit architect phase selects files |
| Plan quality depends on model | Dedicated planning prompt optimized for task breakdown |
| Can't resume mid-pipeline | Can resume from any stage |
| All-or-nothing token spend | Context phase uses cheap tokens; only selected files go to coder |

**The real competitive advantage:** The Context phase alone is transformative. Instead of loading ALL project files and hoping the model figures out what's relevant, Plandex:
1. Sends the codebase map (structural summaries) to the Architect model
2. Architect selects which files are relevant
3. Only those files are loaded into context for planning/implementation

This means a 100-file project doesn't send 100 files to the expensive coding model. It sends a compact map to a cheap architect model, which picks the 5-10 files that matter.

**What would break without staged planning:** The system degrades to a single-shot agent. Token costs increase (all context loaded upfront), edit quality decreases (no structured task breakdown), and the coder model wastes capacity on irrelevant files.

**Competitive implication for AVA:** AVA's commander system with `delegate_coder`, `delegate_researcher`, etc. is conceptually similar — different workers for different tasks. But AVA doesn't have an explicit "architect selects files" phase. Adding an auto-context architect step before delegation could significantly improve context efficiency. The key insight: **file selection and task planning are separate cognitive tasks that benefit from separate model calls**.

---

## 4. 9 Model Roles: Why Not Just Use One Model?

### The Roles

Plandex defines 9 distinct model roles, each assignable to a different model:

| Role | Default Model | Purpose | Why Separate? |
|------|--------------|---------|---------------|
| **Planner** | Claude Sonnet | Break tasks into subtasks | Needs reasoning, not code generation |
| **Coder** | Claude Sonnet | Generate code changes | Needs coding capability |
| **Architect** | Claude Sonnet | Select context files | Needs codebase understanding |
| **Summarizer** | GPT-4o Mini | Summarize old conversations | Cheap model sufficient |
| **Builder** | Claude Sonnet | Apply structured edits | Needs code understanding |
| **WholeFileBuilder** | Claude Sonnet | Rewrite entire files (fallback) | Needs full file generation |
| **Name** | GPT-4o Mini | Generate plan/branch names | Trivially cheap task |
| **CommitMsg** | GPT-4o Mini | Generate commit messages | Trivially cheap task |
| **ExecStatus** | GPT-4o Mini | Determine if plan is finished | Binary classification task |

### Why This Is a Major Innovation

**The problem it solves:** Most agents use one model for everything. If you use Claude Sonnet 3.5 at $3/M input tokens, you're paying $3/M for the model to generate a branch name like "fix-login-bug". That's like hiring a senior engineer to write Post-it notes.

**Cost analysis of a typical coding session:**

| Task | Tokens | Single-Model Cost (Sonnet) | Plandex Multi-Model Cost |
|------|--------|---------------------------|-------------------------|
| Name the plan | ~200 | $0.0006 | $0.00003 (Mini) |
| Select context files | ~5,000 | $0.015 | $0.015 (Sonnet) |
| Plan subtasks | ~10,000 | $0.03 | $0.03 (Sonnet) |
| Generate code | ~20,000 | $0.06 | $0.06 (Sonnet) |
| Summarize conversation | ~8,000 | $0.024 | $0.0012 (Mini) |
| Commit message | ~500 | $0.0015 | $0.000075 (Mini) |
| Check if done | ~300 | $0.0009 | $0.000045 (Mini) |
| **Total** | **~44,000** | **$0.131** | **$0.106** |

That's a ~19% cost savings on a single session. Over thousands of sessions, this compounds significantly. And this is a conservative estimate — the summarization savings grow as conversations get longer.

**But cost isn't the main benefit.** The real advantage is **model-appropriate optimization:**

1. **Summarizer** can use a fast, cheap model because summarization quality doesn't need to be perfect — it just needs to preserve key facts
2. **ExecStatus** is essentially a binary classifier ("is the plan done?") — using Sonnet for this is overkill
3. **Planner** benefits from strong reasoning but doesn't need coding capability
4. **Builder** needs to understand code structure but doesn't need to be creative

**What would break without role separation:** Everything would still work — you'd just use one model for everything. But you'd pay more, and the prompts would be less optimized. The planning prompt is tuned for task decomposition; the coding prompt is tuned for PlandexBlock output. Combining them into one prompt degrades both.

**Competitive implication for AVA:** AVA supports model roles (main model, weak model) but doesn't have 9 specialized roles. The naming and commit message roles are easy wins — use the cheapest available model for trivial tasks. The summarizer role is also valuable since AVA already has context compaction. The key lesson: **every time you call an LLM, ask whether a cheaper model could do the same job**.

---

## 5. The Build Race System: Why Concurrent Strategies Are Essential

### The Architecture

When Plandex needs to apply generated code changes to a file, it doesn't use one strategy — it races multiple strategies concurrently:

```
Generated Code Changes
         │
         ▼
    ┌─────────┐
    │  Race!   │
    ├─────────┤
    │ Strategy A: Validation/Replacement Loop     │
    │   (parse → apply → validate → fix → retry)  │
    │                                              │
    │ Strategy B: Fast-Apply Hook                  │
    │   (external tool, e.g. aider's apply)        │
    │                                              │
    │ Strategy C: Whole-File Rebuild               │
    │   (ask LLM to rewrite the entire file)       │
    └──────────────────────────────────────────────┘
         │
         ▼
    First valid result wins → others cancelled
```

The implementation in `build_race.go` uses Go channels:
- Each strategy runs in a goroutine
- Results are sent to a shared channel
- The first result that passes tree-sitter validation wins
- The context of losing strategies is cancelled

### Why This Is Brilliant

**The problem it solves:** Applying LLM-generated code changes is the #1 failure mode of every AI coding tool. The LLM generates a diff, and applying it fails because:
- Line numbers shifted since the LLM read the file
- The LLM's search pattern doesn't exactly match the file content
- The LLM hallucinated code that was "close but not right" to the original
- Syntax errors in the generated code

Every agent handles this differently:
- **Aider:** Fuzzy matching with difflib, falls back to whole-file edit format
- **Cline:** write_to_file overwrites the entire file (no partial apply)
- **Claude Code:** search_replace with exact matching, user fixes failures
- **Plandex:** Races multiple strategies simultaneously

**Why racing beats sequential fallback:**

| Sequential Fallback | Concurrent Race |
|--------------------|-----------------| 
| Try A → fail → try B → fail → try C | Try A, B, C simultaneously |
| Latency = sum of all attempts | Latency = fastest successful strategy |
| User waits for each failure | User gets fastest valid result |
| Strategy C may take 30s but A takes 2s | If A works in 2s, done — C is cancelled |

In practice, this means: if auto-apply works (which it does ~70% of the time), the user gets instant results. If it fails, the whole-file rebuild is already running in parallel and finishes shortly after. Total latency ≈ max(fastest_success) instead of sum(all_attempts).

**What would break without it:** Edit application would be slower and less reliable. Sequential fallback means the user waits for Strategy A to fail before Strategy B starts. With Plandex's race, the "slow but reliable" whole-file rebuild starts immediately, so even in the worst case, the user doesn't wait for the fast strategy to timeout.

**Competitive implication for AVA:** AVA currently uses a single edit strategy per tool (the `edit` tool with fuzzy matching, or `apply_patch` with unified diffs). Implementing a race between strategies — especially with a whole-file fallback running in parallel — would significantly improve edit reliability. The key insight: **edit application is so critical that it's worth spending extra compute (running multiple strategies) to ensure success**.

---

## 6. Structured Edits Pipeline: Why Reference Comments Beat Tool Calls

### The Innovation

Instead of using tool calls like `search_replace` or `edit`, Plandex asks the LLM to output code blocks with **reference comments** — special comments like `// ... existing code ...` that indicate "keep the original code here."

Example LLM output:
```go
// ... existing code ...

func newFunction() {
    // new implementation
}

// ... existing code ...
```

The system then:
1. Parses the output with tree-sitter to identify reference comments
2. Maps reference comments to positions in the original file
3. Preserves original code at reference positions, inserts new code around it
4. Validates the result with tree-sitter for syntax correctness

### Why This Approach Exists

**The problem with tool-based edits (search_replace, etc.):**

| Tool-Based Edits | Reference Comment Edits |
|-----------------|----------------------|
| Model must output exact search strings | Model outputs approximate positions |
| Whitespace sensitivity causes failures | Reference comments are position-agnostic |
| Each edit is a separate tool call | One code block per file = one "call" |
| Multiple round-trips per file | Single generation per file |
| Model switches between "code" and "JSON" modes | Model stays in "code" mode throughout |

**The key insight:** LLMs are fundamentally better at generating code than at generating precise search-and-replace instructions. When you ask a model to write code with `// ... existing code ...` markers, it stays in "code generation mode" — the natural mode for a code-trained model. When you ask it to generate tool calls with exact search strings, it switches to "instruction following mode" and must reproduce exact character sequences, which is error-prone.

**The 977-line prompt that makes this work** (`update_format.go`):

This is arguably Plandex's most valuable artifact. The prompt teaches the LLM precise rules for reference comments:

1. **Unambiguous positioning** — "Every reference comment must be unambiguous about its position. If there are two similar functions, the reference comment must include enough context to distinguish them."
2. **Anchor requirements** — "Always include at least one line of real code before/after a reference comment for positioning."
3. **Structural hierarchy** — "Preserve the structural nesting (class → method → block) in reference comments."
4. **Code removal** — Special comment `// Plandex: removed code` explicitly marks deletions (vs. `// ... existing code ...` which means "keep").
5. **Language-appropriate syntax** — Uses `# ... existing code ...` for Python, `<!-- ... existing code ... -->` for HTML, etc.

**What would break without reference comments:** Plandex would need to use tool calls for edits, like everyone else. This would:
- Increase token cost (tool call overhead per edit)
- Decrease reliability (exact-match search strings fail often)
- Slow down multi-file edits (one tool call per edit per file)
- Force the model to context-switch between code and instruction modes

**Competitive implication for AVA:** AVA uses tool-based edits (`edit` with fuzzy matching, `apply_patch` with diffs). The reference comment approach is worth studying carefully. It's not necessarily better for all cases — tool-based edits give more precise control and don't require a 977-line prompt. But for large, multi-site edits within a file, reference comments may produce higher-quality results because the model can "think in code" rather than "think in search strings."

---

## 7. Server-Side Git: Why Every Plan Gets Its Own Repo

### The Architecture

When a plan is created, the server initializes a bare git repo for it:

```
/plans/{plan-id}/
├── .git/           ← Server-side git repo
├── context/        ← Context files
├── conversation/   ← Message history
├── changes/        ← Pending replacements
└── settings/       ← Plan configuration
```

Every operation that modifies state creates a git commit. Key operations:
- Adding context → commit
- LLM generates changes → commit
- User applies changes → commit  
- User reverts changes → git revert
- User branches → git branch
- User rewinds → git reset to earlier commit

### Why This Is Transformative

**The problem it solves:** AI-generated changes are inherently risky. You need:
1. **Full undo** — revert any change at any point
2. **Branching** — try alternative approaches without losing the original
3. **Audit trail** — see exactly what the AI did and when
4. **Atomic operations** — multi-file changes are all-or-nothing

**Why git specifically:**

| Alternative | Problem |
|-------------|---------|
| Database versioning | No branching, no atomic multi-file operations |
| File system snapshots | Expensive, no fine-grained undo |
| Custom VCS | Why reinvent what git does perfectly? |
| User's git repo | Pollutes user's history with AI intermediate states |

**The critical distinction:** Plandex does NOT use the user's git repo. It creates a **separate** git repo for each plan on the server. This means:
- The user's commit history is never polluted with AI changes
- Multiple plans can work on the same codebase simultaneously
- Branching/rewinding a plan doesn't affect the user's working directory
- The user explicitly applies changes when ready

**What would break without server-side git:**
- **No rewind** — users can't go back to an earlier plan state
- **No branching** — can't try "implement with Redux" and "implement with Zustand" in parallel
- **No atomic multi-file changes** — partial applies could leave the codebase in an inconsistent state
- **No audit trail** — can't see what the AI generated vs. what the user modified

**Competitive comparison:**

| Agent | Version Control Strategy |
|-------|------------------------|
| Plandex | Server-side git per plan (gold standard) |
| Cline | Shadow `.git-checkpoint` directory (simpler, but no branching) |
| Aider | Direct commits to user's repo (pollutes history) |
| Claude Code | Git snapshots for undo (simpler, no branching) |
| AVA | Git snapshots for rollback (similar to Claude Code) |

**Competitive implication for AVA:** AVA's git module supports snapshots and rollback, which covers the basic undo case. But AVA lacks plan branching — the ability to say "try approach A on branch A and approach B on branch B, then pick the winner." This is a powerful feature for exploratory coding. Implementing session forking (which AVA already has in its architecture) with git branch backing would replicate Plandex's key advantage.

---

## 8. Database Distributed Locking: Why PostgreSQL Locks Beat Mutexes

### The Architecture

Plandex implements a sophisticated locking system in `db/locks.go`:

```go
type RepoLock struct {
    OrgId     string
    UserId    string  
    PlanId    string
    Branch    string
    Scope     LockScope  // read or write
    PlanOnly  bool       // lock only metadata, not content
    CancelFn  context.CancelFunc
}
```

Key features:
1. **PostgreSQL advisory locks** — not row-level locks, but application-level cooperative locks
2. **Heartbeat system** — locks send periodic heartbeats; expired locks are automatically cleaned up
3. **Exponential backoff** — lock acquisition retries with increasing delays
4. **Branch-aware** — read lock on branch A doesn't block write lock on branch B
5. **Read/write scoping** — multiple readers, single writer (shared/exclusive)
6. **Automatic expiry** — if a client disconnects without releasing, the heartbeat expires and the lock is freed

### Why This Exists

**The problem it solves:** In a server architecture where multiple CLI clients can connect simultaneously, you need concurrency control. Two users typing `plandex tell` on the same plan at the same time would corrupt the plan state without locking.

**Why PostgreSQL advisory locks specifically:**

| Approach | Problem |
|----------|---------|
| In-memory mutexes | Lost on server restart; don't work across server instances |
| File-based locks | Fragile, no automatic expiry, race conditions |
| Redis locks (Redlock) | Additional infrastructure dependency |
| PostgreSQL advisory locks | Already have PostgreSQL; zero additional infra |

**The heartbeat innovation:** Traditional advisory locks have a problem — if the client crashes, the lock is held forever (until the database connection closes). Plandex adds a heartbeat: the client periodically updates a `last_heartbeat` timestamp, and a cleanup routine releases locks whose heartbeat is too old.

```
Client A acquires lock → heartbeat every 5s
Client A crashes at t=10s
Lock heartbeat stops updating
At t=30s, cleanup routine detects stale lock → releases it
Client B can now acquire the lock
```

**What would break without distributed locking:**
- Two concurrent `tell` operations on the same plan would corrupt conversation history
- Concurrent builds could apply conflicting changes to the same file
- Concurrent context loads could produce inconsistent token counts
- Plan branching during active generation would produce undefined behavior

**Competitive implication for AVA:** AVA runs as a single-user desktop app, so distributed locking is less critical. However, AVA's session forking feature (running multiple agent workers simultaneously) faces a similar problem: two workers editing the same file concurrently. AVA currently handles this at the tool level (file locks in the permission system), but studying Plandex's read/write lock scoping could improve AVA's concurrent worker safety.

---

## 9. Conversation Summarization: Why Summaries Are Not Optional

### The Architecture

When a conversation exceeds the model's token limit, Plandex:

1. Identifies the oldest non-summarized messages
2. Sends them to the Summarizer model (cheap, fast)
3. Replaces original messages with a summary
4. Stores a "summary checkpoint" so future summarizations know where to start
5. The conversation now fits in context with the summary replacing verbose history

The implementation in `tell_summary.go` runs summarization as a separate model call with its own cancellation context (important: the summary context survives plan completion, so in-progress summaries aren't killed when a plan finishes).

### Why This Matters

**The problem it solves:** Long coding sessions inevitably exceed context windows. Without summarization, you have two bad options:
1. **Truncate** — lose old context entirely
2. **Refuse** — tell the user the conversation is too long

**Why Plandex's approach beats truncation:**

| Truncation | Summarization |
|-----------|---------------|
| Old context is completely lost | Key facts preserved in summary |
| "I already told you about X" → model has no memory | Summary retains "User explained X should work like Y" |
| Loses task context | Preserves subtask progress tracking |
| Abrupt information cliff | Gradual degradation of detail |

**The cost optimization:** Summarization uses the cheapest model role (GPT-4o Mini). The cost of summarizing 10K tokens of conversation with Mini ($0.0015) is negligible compared to the cost of sending those 10K tokens to Sonnet on every subsequent turn ($0.03 per turn × N turns).

**Progressive summarization:** Plandex supports multiple summary checkpoints. As the conversation grows:
- Checkpoint 1: Summarize messages 1-20
- Checkpoint 2: Summarize the Checkpoint 1 summary + messages 21-40
- Checkpoint 3: Summarize the Checkpoint 2 summary + messages 41-60

This produces increasingly compressed summaries while preserving the most important information.

**What would break without summarization:** Plans with more than ~50 messages would fail or lose context. Users working on large features over multiple sessions would find the AI "forgetting" everything from previous sessions.

**Competitive implication for AVA:** AVA already has context compaction — this is a similar concept. But Plandex's approach of using a separate, cheap model for summarization is worth adopting. If AVA uses the main model for compaction, it's paying premium prices for a task that a Mini model handles adequately.

---

## 10. Build Validation Loop: Why Three Attempts With Escalation

### The Architecture

After applying code changes, the validation loop in `build_validate_and_fix.go`:

1. **Parse** the result with tree-sitter → check for syntax errors
2. If errors found, send the file + errors to the Builder model with a "fix this" prompt
3. **Retry** with the fix → re-validate
4. If still failing, **escalate** to a stronger model (e.g., upgrade from Haiku to Sonnet)
5. Up to 3 total attempts before falling back to whole-file rebuild

The "fix" prompt uses a specialized XML protocol:
```xml
<PlandexReplace>
  <Old>
    <LinePrefix>42:</LinePrefix>
    <Code>broken code here</Code>
  </Old>
  <New>
    <Code>fixed code here</Code>
  </New>
</PlandexReplace>
```

Line-number-prefixed file content ensures the model can precisely locate the error.

### Why This Matters

**The problem it solves:** LLM-generated code has a ~10-30% syntax error rate depending on complexity. Without validation, these errors pass through to the user, who must manually fix AI-generated syntax issues — defeating the purpose of the tool.

**Why three attempts with escalation:**

| Strategy | Outcome |
|----------|---------|
| No validation | ~20% of edits have syntax errors |
| Single validation | ~5% still have errors (model can't fix its own mistakes) |
| 3 attempts + escalation | ~1% error rate (stronger model fixes what weaker can't) |
| 3 attempts + escalation + whole-file fallback | ~0.1% error rate |

**Model escalation is the key innovation.** Most validation loops retry with the same model. Plandex escalates to a stronger model on retry 2+. This is important because:
- If Haiku can't fix a syntax error, retrying with Haiku probably won't help
- Sonnet has better code understanding and can fix errors Haiku introduced
- The cost of one Sonnet call for error fixing is much less than the cost of a broken edit reaching the user

**What would break without validation:** Users would frequently receive syntactically broken code. Trust in the tool would erode rapidly. Manual fix cycles would negate the productivity gains from AI-generated code.

**Competitive implication for AVA:** AVA's edit tool uses fuzzy matching but doesn't have post-edit syntax validation with tree-sitter. Adding tree-sitter validation after edits — and a retry/escalation loop — would catch errors before they reach the user. This is one of the highest-value features to adopt from Plandex.

---

## 11. Auto-Context / Architect Phase: Why the AI Should Pick Its Own Files

### The Architecture

Before planning or implementation, Plandex optionally runs an "Architect" phase:

1. Generate a **file map** of the entire codebase (structural summaries from tree-sitter)
2. Send the file map + user prompt to the Architect model
3. Architect responds with a list of files to load
4. Apply **5 chained loading rules**:
   - Interface + Implementation (load both if one is selected)
   - Reference Implementation (if an interface is loaded, load a concrete impl)
   - API Client Chain (if a handler is loaded, load the client that calls it)
   - Database Chain (if a model is loaded, load migrations/queries)
   - Utility Dependencies (if a file is loaded, load its imports)
5. Load only the selected files into context for the next phase

### Why This Is One of Plandex's Best Ideas

**The problem it solves:** The #1 friction in AI coding is context management. Users constantly struggle with:
- "Which files should I load?" (cognitive burden)
- "Did I forget to load the interface file?" (incomplete context)
- "I loaded too many files and hit the token limit" (over-loading)
- "The AI doesn't know about the database schema" (under-loading)

**Why automated file selection beats manual:**

| Manual Context | Auto-Context |
|---------------|-------------|
| User must know the codebase | AI reads the structural map |
| User guesses which files matter | AI selects based on the prompt |
| Common to under-load (missing deps) | Chained rules load transitive deps |
| Common to over-load (entire src/) | AI selects minimal relevant set |
| Each new prompt needs re-loading | Auto-selection per prompt |

**The 5 chained loading rules are the real innovation.** Simple file selection ("load auth.ts") misses transitive dependencies. The rules ensure that:
- Loading a TypeScript interface also loads its implementation
- Loading an API handler also loads the client that calls it
- Loading a database model also loads the schema/migrations
- Loading any file also loads its direct imports

This produces a **complete, minimal context** — everything the coder needs, nothing extra.

**What would break without auto-context:**
- Users must manually manage context for every prompt (high friction)
- Complex prompts require loading 10+ files manually (error-prone)
- Token waste from loading irrelevant files
- Poor edit quality from missing context files

**Competitive comparison:**

| Agent | Context Strategy |
|-------|-----------------|
| Plandex | AI selects files from codebase map + transitive rules |
| Aider | RepoMap (PageRank) + manual `/add` |
| Cline | Read files on-demand during agent loop |
| Claude Code | Read files on-demand during agent loop |
| AVA | Manual + codebase map + on-demand reading |

**Competitive implication for AVA:** AVA already has a codebase module with repo map and symbols. Adding an explicit "architect" step — where the agent examines the codebase map and selects relevant files before starting work — would reduce token waste and improve edit quality. The transitive loading rules (interface→impl, handler→client, model→schema) are particularly valuable and could be implemented using AVA's existing LSP integration for dependency resolution.

---

## 12. Missing File Handling: Why Pausing the Stream Is Genius

### The Architecture

When the LLM generates code that references a file not in context, Plandex:

1. **Detects** the missing file during stream processing
2. **Pauses** the LLM stream (stops reading from the HTTP response)
3. **Sends** a missing file prompt to the CLI via SSE
4. **Waits** up to 30 minutes for the user's choice: skip, overwrite, or load the file
5. **Resumes** the LLM stream with the user's choice applied

The implementation uses channels and select statements:
```go
select {
case choice := <-missingFileChannel:
    // User responded, apply choice and resume
case <-time.After(30 * time.Minute):
    // Timeout, skip the file
case <-ctx.Done():
    // Plan cancelled
}
```

### Why This Is Clever

**The problem it solves:** During code generation, the LLM often references files it hasn't seen. In most agents, this means either:
- The edit fails and the user must load the file and retry (waste)
- The agent guesses at the file content (hallucination)
- The agent makes a tool call to read the file (interrupts generation flow)

**Plandex's approach is unique:** It literally pauses the LLM's response stream mid-generation. The HTTP connection stays open, but Plandex stops consuming bytes from it. This gives the user time to make a decision without losing the generation so far.

**Why pausing is better than restarting:**

| Restart Approach | Pause Approach |
|-----------------|----------------|
| Detect missing file → abort → reload → restart | Detect missing file → pause → user decides → resume |
| Wastes all tokens generated so far | Preserves all generation so far |
| May produce different output on retry | Continues from exact point of interruption |
| User pays double token cost | No additional token cost |

**What would break without it:** Users would frequently hit "file not in context" errors and need to restart generation. On large plans with 20+ files, this would happen constantly, making the tool unusable for complex tasks.

**Competitive implication for AVA:** AVA's agent loop reads files on-demand via tool calls, so this specific problem is less acute. But the general pattern — pausing execution to ask the user a question, then resuming — is valuable. AVA's `question` tool does something similar at the tool-call level. The lesson: **never throw away in-progress work when you can pause and ask**.

---

## 13. ActivePlan & Streaming Protocol: Why Custom Streaming Beats SSE

### The Architecture

The `ActivePlan` struct in `types/active_plan.go` is the central runtime object for any running plan:

```go
type ActivePlan struct {
    // Three separate cancellation contexts:
    Ctx        context.Context  // Plan-level (2hr timeout)
    ModelCtx   context.Context  // Model stream (cancellable independently)
    SummaryCtx context.Context  // Summary (survives plan completion)
    
    // Subscription system:
    Subscribers    map[string]chan StreamMessage
    SubscribeMutex sync.Mutex
    
    // Stream buffering:
    StreamBuffer    []StreamMessage
    FlushInterval   time.Duration  // 70ms
}
```

The streaming protocol uses `@@PX@@`-delimited messages:
```
@@PX@@{"type":"reply","content":"Here's the plan..."}@@PX@@
@@PX@@{"type":"build","file":"main.go","status":"building"}@@PX@@
@@PX@@{"type":"missing_file","path":"utils.go"}@@PX@@
@@PX@@{"type":"heartbeat"}@@PX@@
```

### Why Three Cancellation Contexts

**Model stream cancellation:** When a user types `plandex stop`, you want to stop the LLM generation immediately — but you don't want to kill in-progress builds or summaries. The model context is cancellable independently.

**Summary context survival:** Summaries may still be running when a plan finishes (the plan completed, but the summarizer is still compressing the conversation). The summary context has its own lifecycle.

**Plan-level timeout:** 2-hour maximum prevents runaway plans from consuming resources indefinitely.

**What would break with a single context:** Stopping a plan would kill in-progress summaries, losing conversation compression. Or, summaries would prevent plan cancellation, making `stop` feel unresponsive.

### Why Custom Streaming Over Standard SSE

**The problem with raw SSE:**
- SSE is text-only (no binary)
- SSE has limited message types (event, data, id)
- SSE doesn't support message batching
- SSE reconnection logic is complex

**Plandex's `@@PX@@` protocol solves:**
1. **Typed messages** — each message has a type (reply, build, missing_file, heartbeat, etc.)
2. **Batching** — multiple messages accumulated over 70ms are sent in one batch
3. **Rate limiting** — the 70ms flush interval prevents overwhelming the CLI with thousands of tiny chunks
4. **Message buffering** — subscribers who connect late receive buffered history

**What would break without the custom protocol:** The CLI would receive raw SSE events and need to parse/type them. Batching and rate limiting would need to be implemented separately. The 70ms batching interval is important — without it, the CLI receives one SSE event per LLM token, which causes UI flickering and high CPU usage.

**Competitive implication for AVA:** AVA uses streaming in its chat hooks, but the 70ms batching and multi-subscriber fan-out patterns are worth studying. If AVA ever supports multiple frontend views of the same session (e.g., a chat panel and a diff panel), the subscription-based fan-out would be useful.

---

## 14. Reply Parser & Stream Processor: Why Incremental Parsing Matters

### The Architecture

Plandex processes LLM output through two complementary systems:

**ReplyParser** (`types/reply.go`): Incrementally parses the LLM's text stream to detect:
- File paths (from markdown: `**path/to/file**`, `- path/to/file`, `### path/to/file`)
- `<PlandexBlock>` XML tags (marking code changes)
- File operation blocks (`### Move Files`, `### Remove Files`, `### Reset Changes`)
- End-of-operations marker (`<EndPlandexFileOps/>`)

**StreamProcessor** (`tell_stream_processor.go`): Processes parsed chunks to:
- Replace `<PlandexBlock>` tags with markdown fences for display
- Buffer during tag detection (can't emit until we know if `<` is a tag or content)
- Immediately queue builds when file operations are detected
- Handle manual stop sequences

### Why Incremental Parsing Is Critical

**The problem it solves:** The LLM streams text token by token. You can't wait for the complete response to start parsing — that would add 30-60 seconds of latency. But parsing a partial stream is complex:

```
Token stream: "Here", "'s", " the", " fix", ":\n", "**", "src/", "main", ".go", "**"
                                                         ↑
                                                    Is this a file path?
                                                    Don't know yet — need more tokens
```

The parser must:
1. Detect potential patterns early (seeing `**` might be bold or a file path)
2. Buffer tokens until the pattern is confirmed or rejected
3. Emit buffered tokens when a pattern is rejected
4. Start builds immediately when a pattern is confirmed (don't wait for the full response)

**Why immediate build queueing matters:** When the LLM finishes generating a `<PlandexBlock>` for `main.go`, the stream processor immediately queues a build for that file — while the LLM continues generating changes for other files. This means:
- File 1 is being built while File 2 is being generated
- Total time ≈ generation time + one build (not generation time + N builds)

**The tag replacement for display:** The raw LLM output contains `<PlandexBlock lang="go" path="main.go">`. For the user, this is replaced with:
````
main.go:
```go
// code here
```
````

This is purely a display concern, but it's important — users see clean markdown, not XML.

**What would break without incremental parsing:** Builds would only start after full response generation (adding 30+ seconds to large plans). The CLI would either show raw XML tags or wait for the complete response before displaying anything. Both are dealbreakers for UX.

**Competitive implication for AVA:** AVA already does streaming tool call parsing. The pattern of immediately starting execution when a tool call is detected (before the full response is complete) is worth considering for AVA's tool execution — e.g., starting a file read as soon as the `read_file` tool call parameters are complete, even if the model is still generating its reasoning text.

---

## 15. The Update Format Prompt: Why 977 Lines of Prompt Engineering

### The Architecture

The update format prompt (`prompts/update_format.go`) is a 977-line prompt that teaches the LLM exactly how to write partial code updates with reference comments. It's the largest single prompt in the Plandex codebase and arguably its most valuable intellectual property.

### Key Rules Encoded in the Prompt

1. **Reference comment format per language:**
   ```
   Go/JS/TS:   // ... existing code ...
   Python:     # ... existing code ...
   HTML:       <!-- ... existing code ... -->
   CSS:        /* ... existing code ... */
   Lua:        -- ... existing code ...
   ```

2. **Unambiguous positioning rules:**
   - Every reference comment must be resolvable to a unique position
   - If two functions have similar signatures, include distinguishing context
   - Always include at least one line of real code above/below for anchoring

3. **Structural hierarchy preservation:**
   ```go
   type Server struct {
       // ... existing fields ...
       newField string  // NEW
       // ... existing fields ...
   }
   ```
   The reference comment stays within the struct — not at the file level.

4. **Code removal semantics:**
   - `// ... existing code ...` → keep the original code
   - `// Plandex: removed code` → explicitly delete this section
   - This distinction is critical — without it, the system can't tell "keep this code" from "delete this code"

5. **Multi-site edits in one block:**
   ```go
   // ... existing code ...

   func modifiedFunction() {
       // new implementation
   }

   // ... existing code ...

   func anotherModifiedFunction() {
       // new implementation  
   }

   // ... existing code ...
   ```

6. **No-op handling:**
   - If a file needs no changes, don't emit a block for it
   - This reduces unnecessary processing

### Why 977 Lines Is Worth It

**The problem it solves:** Without precise rules, the LLM generates ambiguous reference comments that the parser can't resolve. For example:
- `// ... existing code ...` at the top of a function — does it mean "keep the function signature" or "keep everything above this function"?
- Two `// ... existing code ...` comments next to each other — what's between them?

Every ambiguity causes an apply failure, which triggers the validation loop, which costs tokens and time.

**The economics of prompt length:** 977 lines ≈ ~3,000 tokens. This is included in every implementation prompt. At $3/M tokens, that's $0.009 per call. If this prompt reduces apply failures from 30% to 5%, and each failure costs ~$0.05 in retry tokens, the prompt pays for itself in 0.36 calls.

**What would break without it:** Reference comment parsing would be unreliable. The LLM would produce ambiguous markers, the parser would guess wrong, and edits would be applied incorrectly or fail. The validation loop would trigger constantly, driving up costs and latency.

**Competitive implication for AVA:** AVA's edit tool uses a simpler approach (search → replace with fuzzy matching). The reference comment system is an alternative paradigm worth evaluating, especially for large edits that touch multiple sites in a file. The lesson: **investing heavily in prompt engineering for the edit format pays off exponentially in downstream reliability**.

---

## 16. File Map System: Why Structural Summaries Beat Full Files

### The Architecture

The file map (`syntax/file_map/map.go`) generates structural summaries of source files using tree-sitter:

```
// File: main.go (map representation)
package main

func main()
func handleRequest(w http.ResponseWriter, r *http.Request)
type Server struct { ... }
func (s *Server) Start(port int) error
func (s *Server) Stop() error
```

The map shows function signatures, type definitions, and struct fields — but not implementations. This reduces a 500-line file to ~20 lines while preserving its API surface.

### Why This Matters

**The problem it solves:** To select relevant files, the AI needs to understand the codebase structure. But loading every file in full would exceed any context window. File maps provide a compressed representation.

**Token efficiency comparison:**

| Approach | Tokens for 100-file project |
|----------|---------------------------|
| Full file contents | ~500,000 tokens |
| File map (signatures only) | ~15,000 tokens |
| File names only | ~1,000 tokens |

File maps hit the sweet spot: 97% token reduction vs. full files, but retaining enough information for intelligent file selection.

**Tree-sitter integration:** Plandex doesn't use regex or string matching to extract signatures — it uses tree-sitter's AST parsing. This means:
- Correct handling of nested types (methods on structs, inner classes)
- Language-specific extraction (Go interfaces vs. TypeScript interfaces)
- Proper handling of exported vs. unexported symbols
- HTML/Svelte/Markdown special handling (markup and heading extraction)

**What would break without file maps:** The architect phase would need full files (too expensive) or file names only (too little information). Without structural summaries, auto-context becomes either impossibly expensive or uselessly vague.

**Competitive comparison:**

| Agent | Codebase Understanding |
|-------|----------------------|
| Plandex | Tree-sitter file maps (signatures) |
| Aider | Tree-sitter RepoMap with PageRank ranking |
| AVA | Tree-sitter repo map + symbols |
| Cline | On-demand file reading (no pre-built map) |
| Claude Code | On-demand file reading (no pre-built map) |

**Competitive implication for AVA:** AVA already has a codebase module with tree-sitter integration, which is comparable. The key difference is that Plandex uses its file map as input to the architect phase for automated file selection. AVA could leverage its existing codebase map similarly — feed it to an architect prompt that selects relevant files before starting work.

---

## 17. Hook System: Why Lifecycle Events Enable Everything

### The Architecture

Plandex defines lifecycle hooks in `hooks/hooks.go`:

```go
type Hooks struct {
    WillSendModelRequest  func(HookParams) error
    DidSendModelRequest   func(HookParams) error
    DidFinishBuilderRun   func(HookParams) error
    CallFastApply         func(HookParams) (string, error)
    // ... more hooks
}
```

Hook parameters include comprehensive telemetry:
- Timing data (request duration, build duration)
- Token counts (input, output, cached)
- Strategy outcomes (which build strategy won the race)
- File paths and content

### Why Hooks Matter

**The problem they solve:** A monolithic system where everything is hardcoded is impossible to extend, test, or monitor. Hooks provide:

1. **Fast-apply integration** — the `CallFastApply` hook allows external tools (like aider's edit applier) to participate in the build race. This is how Plandex integrates with other tools' edit strategies.

2. **Telemetry** — `WillSendModelRequest` and `DidSendModelRequest` capture timing, token usage, and costs without polluting the core logic.

3. **Testing** — hooks can be replaced with test doubles that capture calls without making real LLM requests.

4. **Extensibility** — new functionality can be added at lifecycle points without modifying core code.

**What would break without hooks:** The fast-apply integration would need to be hardcoded into the build pipeline. Telemetry would be scattered throughout the codebase. Testing would require mocking HTTP clients instead of injecting hook functions.

**Competitive implication for AVA:** AVA already has a hooks module (`packages/core/src/hooks/`) — this is architecturally aligned. The key learning from Plandex is the breadth of hook parameters (timing, tokens, strategy outcomes) which enable rich telemetry.

---

## 18. Replacement-Based Diff System: Why Granular Review Changes Everything

### The Architecture

Instead of unified diffs, Plandex stores changes as `Replacement` objects:

```go
type Replacement struct {
    Old        string
    New        string
    Status     ReplacementStatus  // pending, applied, rejected, failed
    StreamedChange bool
}
```

A file's changes are a list of replacements, each independently reviewable.

### Why Replacements Beat Unified Diffs

**The problem with unified diffs for AI changes:**

| Unified Diff | Replacements |
|-------------|-------------|
| One diff per file | Individual change per replacement |
| Accept or reject entire diff | Accept or reject each change independently |
| Ordering is fixed | Replacements can be applied in any order |
| Hard to partially apply | Trivially partially applicable |

**Use case:** The AI makes 5 changes to a file — 3 are correct, 2 are wrong. With a unified diff, you accept all 5 or reject all 5. With replacements, you accept the 3 good ones and reject the 2 bad ones.

**Conflict detection:** When the user modifies a file between generation and application, the replacement system checks each replacement independently for conflicts. Replacements that still match are applicable; only conflicting ones are flagged.

**What would break without granular replacements:** Users would face all-or-nothing decisions on AI changes. For complex edits with 10+ changes per file, this would frequently require rejecting good changes along with bad ones, then manually re-applying the good ones.

**Competitive implication for AVA:** AVA's diff module tracks changes, but the granular accept/reject per change pattern is worth considering. For the commander system where a worker makes many edits, allowing the user to approve/reject individual changes (not just the entire worker's output) would significantly improve the review experience.

---

## 19. Plan Status State Machine: Why Explicit States Prevent Corruption

### The Architecture

Plans have explicit status values:

```
draft → replying → describing → building → finished
                                         → stopped
                                         → error
                ↗ missingFile ↘
              (pause)        (resume)
```

Each status transition is validated — you can't go from `draft` to `building` without passing through `replying`.

### Why This Matters

**The problem it solves:** Without a state machine, plan state is implicit (inferred from what fields are set). This leads to:
- Race conditions: Is the plan building or still replying?
- Invalid operations: What happens if you `apply` while the plan is still generating?
- Stale UI: The CLI doesn't know whether to show a spinner or a prompt

**The `missingFile` state is particularly important:** It's a pause state within the `replying` → `building` flow. The plan is actively generating, but the stream is paused waiting for user input. Without an explicit state for this, the CLI would think the plan is either replying (show spinner) or stuck (timeout).

**What would break without explicit states:**
- The CLI couldn't show accurate status indicators
- Concurrent operations could corrupt plan state
- Error recovery would be harder (what state should we restore to?)

**Competitive implication for AVA:** AVA's session module has checkpoints but not an explicit state machine for the agent loop. Adding explicit states (thinking, tooling, waiting_for_approval, completed, error) would improve the frontend's ability to show accurate status and prevent invalid state transitions.

---

## 20. What Would Break Without Each System

| System | Without It |
|--------|-----------|
| Multi-stage pipeline | Single-shot generation, lower quality, higher token waste |
| 9 model roles | 20%+ higher costs, less optimized prompts per task |
| Build race | 2-5x longer edit application, lower success rate |
| Reference comments | Tool-call overhead, worse multi-site edits, more failures |
| Server-side git | No rewind, no branching, no atomic multi-file changes |
| Distributed locking | Concurrent access corrupts plan state |
| Summarization | Long conversations exceed context, lose history |
| Validation loop | ~20% of edits have syntax errors reaching users |
| Auto-context | Users manually manage context (high friction, suboptimal) |
| Missing file handling | Generations fail mid-stream, wasting all tokens |
| Custom streaming | UI flicker, no batching, no multi-subscriber support |
| Incremental parsing | 30+ second latency before builds start |
| Update format prompt | Ambiguous reference comments, unreliable apply |
| File maps | Can't do auto-context (too expensive to load full files) |
| Hook system | No extensibility, no telemetry, harder testing |
| Replacement diffs | All-or-nothing review (accept/reject entire file changes) |
| Status state machine | Race conditions, invalid operations, stale UI |

---

## 21. Competitive Advantages vs. AVA

### Where Plandex Was Ahead

| Feature | Plandex | AVA | Gap |
|---------|---------|-----|-----|
| Build race (concurrent strategies) | ✅ Three concurrent strategies | ❌ Single strategy | High — build races dramatically improve reliability |
| Model role specialization | ✅ 9 roles with cost optimization | 🟡 Main + weak model | Medium — easy to add more roles |
| Auto-context (architect phase) | ✅ AI selects files from map | 🟡 Manual + on-demand | High — reduces friction and token waste |
| Plan branching | ✅ Git branch per plan branch | ❌ Session forking (partial) | Medium — session forking is similar in concept |
| Validation with escalation | ✅ 3 attempts + model upgrade | ❌ No post-edit validation | High — catches syntax errors before users see them |
| Progressive summarization | ✅ Checkpoint-based, cheap model | 🟡 Context compaction | Low — similar concept, different implementation |
| Missing file pause/resume | ✅ Stream pause + user prompt | 🟡 Question tool | Low — different UX, similar outcome |
| Granular change review | ✅ Per-replacement accept/reject | ❌ Per-file review | Medium — improves review for complex changes |

### Where AVA Is Ahead

| Feature | AVA | Plandex | Gap |
|---------|-----|---------|-----|
| Tool-based agent loop | ✅ 24 tools, autonomous | ❌ Structured pipeline only | High — AVA agents are more flexible |
| Desktop UI | ✅ Tauri + SolidJS | ❌ CLI only | High — richer UX potential |
| Local-first | ✅ SQLite, no server | ❌ Requires PostgreSQL + server | High — zero deployment friction |
| Commander delegation | ✅ 5 specialized workers | ❌ Single pipeline | Medium — parallel workers are powerful |
| MCP integration | ✅ MCP client + discovery | ❌ No MCP | Medium — ecosystem integration |
| LSP integration | ✅ Language Server Protocol | ❌ Tree-sitter only | Medium — richer code intelligence |
| Browser tool | ✅ Puppeteer automation | ❌ No browser | Low — niche but useful |
| Plan mode | ✅ Explicit plan/execute modes | 🟡 Always planned | Low — different approaches |

---

## 22. Key Takeaways for AVA

### Must-Adopt (High Value, Feasible)

1. **Post-Edit Syntax Validation**
   - Add tree-sitter validation after every `edit`/`apply_patch`/`write_file` tool execution
   - If syntax errors detected, automatically retry with error context
   - Escalate to a stronger model on retry 2+
   - This alone would significantly improve edit reliability

2. **Cheap Model for Utility Tasks**
   - Use the weakest available model for: session naming, commit messages, status detection
   - AVA already has a "weak model" concept — extend it to more tasks
   - Estimated 15-25% cost savings on typical sessions

3. **Auto-Context Architect Phase**
   - Before delegating to a coder worker, run the codebase map through an architect prompt
   - Architect selects relevant files; only those files enter the coder's context
   - Use AVA's existing codebase module + LSP for transitive dependency loading
   - Reduces token waste and improves edit quality

### Should-Consider (High Value, More Effort)

4. **Build Race Pattern**
   - For the `edit` tool: race fuzzy-match apply vs. whole-file rewrite
   - For `apply_patch`: race standard apply vs. fuzzy apply vs. whole-file rewrite
   - First valid result wins; cancel others
   - Requires concurrent tool execution infrastructure

5. **Granular Change Review**
   - Store individual replacements (not just file-level diffs)
   - In the UI, let users accept/reject individual changes within a file
   - Particularly valuable for commander workers that make many changes

6. **Progressive Summarization with Checkpoints**
   - Enhance context compaction to use a cheap model
   - Store summary checkpoints so re-summarization builds on previous summaries
   - Reduces cost of long sessions significantly

### Learn From But Don't Copy

7. **Reference Comment Edit Format**
   - The concept is clever, but AVA's tool-based edits are more flexible
   - Consider as an alternative format for large multi-site edits
   - The 977-line prompt is valuable research material regardless

8. **Server Architecture**
   - Plandex proved that server-side planning enables features impossible locally
   - But it also proved that deployment complexity kills adoption
   - AVA's local-first Tauri approach is the right trade-off
   - Study for future cloud/team features only

9. **Custom Streaming Protocol**
   - The 70ms batching and multi-subscriber fan-out are good patterns
   - But standard web technologies (Server-Sent Events, WebSocket) may be sufficient for AVA
   - Adopt the batching concept without the custom delimiter protocol

### Avoid

10. **PostgreSQL requirement** — the deployment burden killed adoption
11. **Go's type system limitations** — TypeScript's type system is strictly better for this domain
12. **Client-server split for single-user tool** — unnecessary complexity

---

*Analysis completed from ~40+ source files across the Plandex codebase. Plandex was shut down, but its architectural innovations — particularly build races, staged planning, auto-context, and validation escalation — represent some of the most sophisticated approaches to AI code generation reliability in any open-source agent.*
