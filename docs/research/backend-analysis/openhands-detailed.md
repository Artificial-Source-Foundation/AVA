# OpenHands: Deep Competitive Intelligence Analysis

> **Purpose**: Competitive intelligence for AVA — explaining the *value* of every architectural decision and tool, not just describing them. This document answers: **WHY** does each thing exist? What problem does it solve? What breaks without it?
>
> **Source**: OpenHands codebase snapshot, March 2026 (V0 legacy, mid-migration to V1 SDK).
> **Stars**: ~65k GitHub. Python-based. Cloud-first with Docker sandboxing.

---

## Table of Contents

1. [Executive Summary — Why OpenHands Matters](#1-executive-summary)
2. [The Core Bet: Event-Sourced Architecture](#2-the-core-bet-event-sourced-architecture)
3. [The Docker Sandbox Moat](#3-the-docker-sandbox-moat)
4. [Tool-by-Tool Competitive Intelligence](#4-tool-by-tool-competitive-intelligence)
5. [The Condenser System — Their Context Window Solution](#5-the-condenser-system)
6. [Stuck Detection — The Loop Escape Hatch](#6-stuck-detection)
7. [Security Model — Per-Action Risk Assessment](#7-security-model)
8. [Multi-Agent Delegation](#8-multi-agent-delegation)
9. [Microagent System — Dynamic Knowledge Injection](#9-microagent-system)
10. [Prompt Engineering — What Their System Prompts Reveal](#10-prompt-engineering)
11. [SWE-bench Pipeline — The Benchmark Weapon](#11-swe-bench-pipeline)
12. [Model Routing & LLM Abstraction](#12-model-routing--llm-abstraction)
13. [Critic System — Output Quality Gating](#13-critic-system)
14. [Key Innovations Worth Stealing](#14-key-innovations-worth-stealing)
15. [Architectural Weaknesses to Exploit](#15-architectural-weaknesses-to-exploit)
16. [Gap Analysis vs AVA](#16-gap-analysis-vs-ava)

---

## 1. Executive Summary

OpenHands' competitive position rests on three pillars:

1. **Docker-first sandboxing** — Every action runs inside a container, making OpenHands the safest fully autonomous agent. This is their moat.
2. **Event-sourced architecture** — Everything is an append-only event stream. This enables replay, persistence, multi-subscriber fanout, and principled state management.
3. **SWE-bench domination** — They have a built-in issue-resolver pipeline tuned for benchmarks. Their architecture *is* their benchmark strategy.

**Strategic implications for AVA**: OpenHands wins on safety and benchmarks. We need to win on developer experience (desktop-native, instant startup, no Docker overhead), extensibility (plugin ecosystem), and multi-agent sophistication (Commander hierarchy vs their flat delegation).

---

## 2. The Core Bet: Event-Sourced Architecture

### What It Is

Every interaction in OpenHands is an **Event** — a typed, timestamped, sourced data object written to an append-only stream. Events split into:
- **Actions** (intent): `CmdRunAction`, `FileEditAction`, `BrowseInteractiveAction`, etc.
- **Observations** (result): `CmdOutputObservation`, `FileEditObservation`, `ErrorObservation`, etc.

The `EventStream` class is the pub-sub backbone. Components subscribe as `EventStreamSubscriber.RUNTIME`, `EventStreamSubscriber.AGENT_CONTROLLER`, `EventStreamSubscriber.MEMORY`, etc.

### WHY This Exists — The Problems It Solves

**Problem 1: State reconstruction after crashes.** With event sourcing, the entire session state can be rebuilt by replaying events from storage. No separate "save state" logic needed — the event log *is* the state.

**Problem 2: Multi-component coordination.** The runtime, controller, memory system, and server all need to react to the same events but in different ways. The subscriber model lets each component handle events independently without tight coupling.

**Problem 3: Debugging and replay.** Every agent trajectory is a complete, ordered log of exactly what happened. This is invaluable for SWE-bench evaluation — you can replay any trajectory to understand failures.

**Problem 4: Streaming to clients.** The web UI needs real-time updates. By subscribing the server to the event stream, events flow to Socket.IO clients naturally.

### What Would Break Without It

Without event sourcing, OpenHands would need:
- Separate persistence logic for every component
- Explicit state-passing between controller, runtime, and memory
- A completely different approach to session resumption
- Manual trajectory logging for benchmarks

### Real Advantage vs Simpler Approaches

Most coding agents (Claude Code, Aider, Continue) use a simple turn-based loop: call LLM → execute tool → add to messages → repeat. OpenHands' approach is heavier but enables features those simpler agents cannot offer:
- **Hot session handoff** (new server instance replays events)
- **Parallel observation** (memory and runtime both react to the same action)
- **Cost post-mortem** (every LLM call is a metric-annotated event)

### AVA Implication

AVA uses session persistence with checkpoints, which is simpler and more appropriate for a desktop app. We don't need full event sourcing — but we should steal the **trajectory logging** concept for debugging and the **multi-subscriber** pattern for streaming to the frontend.

---

## 3. The Docker Sandbox Moat

### What It Is

All code execution happens inside a Docker container. The host runs the `AgentController` and communicates with the container via HTTP (FastAPI server running *inside* the sandbox).

```
Host Machine
├── AgentController (Python process)
├── EventStream (file-backed)
└── DockerRuntime
    └── HTTP ←→ Docker Container
                ├── action_execution_server.py (FastAPI)
                ├── BashSession (persistent shell)
                ├── JupyterPlugin (IPython kernel)
                ├── BrowserEnv (Playwright)
                ├── OHEditor (file editor)
                └── MCPProxyManager
```

### WHY This Exists — The Problems It Solves

**Problem 1: Safety for fully autonomous operation.** OpenHands runs in "agentic" mode — it can execute bash commands, install packages, modify files. Without sandboxing, a hallucinating agent could `rm -rf /` the host.

**Problem 2: Reproducible environments.** Docker images mean every session starts from a known state. Custom images can pre-install project dependencies. This is critical for SWE-bench evaluation.

**Problem 3: Cloud deployment.** The HTTP-over-container architecture means the container can run anywhere — local Docker, remote server, Kubernetes pod. Same protocol, different transport.

**Problem 4: Process isolation.** The agent's bash session, Jupyter kernel, browser, and file editor all run isolated from the host Python process. A runaway process in the sandbox doesn't crash the agent.

### What Would Break Without It

Without Docker sandboxing:
- **No safe autonomous execution** — agents couldn't be trusted to run arbitrary commands
- **No environment reproducibility** — "it works on my machine" for every session
- **No cloud scaling** — can't spin up sandboxes on demand
- **SWE-bench scores would drop** — benchmark evaluation requires clean, reproducible environments

### Real Advantage vs Manual / Lighter Approaches

| Approach | Safety | Latency | Flexibility |
|----------|--------|---------|-------------|
| Direct host execution | None | Fastest | Full |
| Process-level sandbox | Moderate | Fast | Limited |
| Docker container (OpenHands) | High | 2-5s startup | Full Linux env |
| VM-based sandbox (E2B) | Highest | 10-30s startup | Full OS |

OpenHands hits the sweet spot: Docker is safe enough for most use cases, fast enough for interactive use, and gives a full Linux environment.

### AVA Implication

AVA is desktop-first. Our users want instant startup (<1s), not 2-5s Docker boot. We should:
- Keep Docker as an optional extension (already planned)
- Focus on permissions-based safety (ask before dangerous operations)
- Consider process-level sandboxing for bash execution as a middle ground

---

## 4. Tool-by-Tool Competitive Intelligence

### 4.1 `execute_bash` — Persistent Shell Session

**File**: `tools/bash.py` → `CmdRunAction`

**What it does**: Executes bash commands in a persistent shell session inside the Docker container. Supports `is_input` for stdin interaction and `timeout` for hard timeouts.

**WHY it exists**: Bash is the universal interface. Every coding task eventually needs shell commands — running tests, installing packages, checking git status, building projects. A persistent session means `cd` and environment variables carry over between commands.

**Problem it solves**: Without persistent sessions, every command would start in the default directory with no environment context. The agent would need to prepend `cd /workspace && source .env && ` to every command.

**The `is_input` innovation**: Many commands need stdin interaction (e.g., answering `y/n` prompts, providing passwords). The `is_input` parameter lets the agent send follow-up input to a still-running process. Without this, any interactive command would hang forever.

**The `timeout` innovation**: Hard timeout prevents runaway processes. Combined with a 10-second soft timeout in the `BashSession` class, this prevents the agent from getting stuck on commands that produce infinite output or never terminate.

**Security risk annotation**: Every bash command carries a `security_risk` field (low/medium/high). This feeds into the security analyzer for confirmation gating. LOW = read-only (ls, cat). MEDIUM = project-scoped (npm install, python script.py). HIGH = system-level (sudo, curl | bash, sending data to external servers).

**What would break without it**: No way to run tests, install dependencies, check git status, or interact with the system. The agent would be limited to file editing — dramatically less capable.

**AVA comparison**: AVA has a bash tool with PTY support, which gives even better interactive handling than OpenHands' `is_input` approach. Our PTY captures the actual terminal output including colors and formatting. **Advantage: AVA**.

---

### 4.2 `str_replace_editor` — The SWE-bench File Editor

**File**: `tools/str_replace_editor.py` → `FileReadAction` / `FileEditAction`

**What it does**: A multi-command file editor with 5 operations: `view`, `create`, `str_replace`, `insert`, `undo_edit`. Based on the `openhands-aci` (Agent-Computer Interface) library.

**WHY it exists**: This is the most critical tool in OpenHands and the key to their SWE-bench performance. The `str_replace` approach forces the agent to specify *exactly* what text to find and what to replace it with. This is more reliable than line-number-based editing because:

1. **Line numbers shift** — After any edit, all subsequent line numbers change. The agent would need to re-read the file to get accurate line numbers.
2. **Exact string matching is verifiable** — If `old_str` doesn't match, the tool fails immediately and clearly. The agent knows the edit didn't apply.
3. **Minimal diff generation** — `str_replace` naturally produces clean, minimal diffs. This is critical for SWE-bench where the patch is evaluated.

**The `undo_edit` innovation**: Lets the agent revert the last edit to a file. This is a safety net — if an edit breaks something, the agent can undo it and try again without needing to reconstruct the original content.

**Problem it solves**: File editing is where agents fail most often. Wrong line numbers, partial matches, corrupted files. `str_replace` minimizes these failure modes by requiring exact context.

**What would break without it**: Agents would need to use `sed`, `echo >>`, or write entire files. These approaches are error-prone:
- `sed` requires regex escaping and is fragile with special characters
- Writing entire files loses content if the agent's copy is stale
- Patch-based editing requires understanding unified diff format

**AVA comparison**: AVA has `edit` (fuzzy text matching) and `apply_patch` (unified diffs). Our fuzzy matching is more forgiving than OpenHands' exact matching — which is better for user experience but potentially less reliable for benchmarks. We also have `multiedit` for editing multiple files in one tool call, which OpenHands lacks. **Advantage: Mixed — OpenHands for precision, AVA for flexibility**.

---

### 4.3 `execute_ipython_cell` — Jupyter Integration

**File**: `tools/ipython.py` → `IPythonRunCellAction`

**What it does**: Executes Python code in a persistent IPython/Jupyter kernel inside the sandbox. Variables, imports, and state persist between cells. Supports magic commands (`%matplotlib`, `%load_ext`).

**WHY it exists**: Three key reasons:

1. **Data exploration** — IPython is vastly better than bash for exploring data structures, testing API calls, and prototyping solutions. The agent can create variables, inspect them, modify them incrementally.
2. **Rich output** — IPython returns structured output including plots, dataframes, and formatted objects. Bash only returns text.
3. **Separate state from bash** — The IPython kernel runs independently from the bash session. This means the agent can have Python state that persists even if it runs bash commands that modify the Python environment.

**Problem it solves**: Without IPython, the agent would need to write temporary Python scripts, run them with `python script.py`, and parse stdout. This is slower, loses state between executions, and can't produce rich output.

**What would break without it**: Data science tasks, API testing, and incremental Python development would be significantly harder. The agent would lose the ability to maintain Python state across multiple interactions.

**AVA comparison**: AVA does not have a dedicated IPython tool. Our bash tool can run `python -c "..."` but this doesn't persist state. For data-heavy use cases, this is a gap. **Advantage: OpenHands**.

---

### 4.4 `web_browser` — BrowserGym Integration

**File**: `tools/browser.py` → `BrowseInteractiveAction`

**What it does**: Full browser automation via BrowserGym + Playwright. 15 high-level actions: `goto`, `go_back`, `go_forward`, `click[bid]`, `fill[bid, value]`, `select_option[bid, options]`, `hover[bid]`, `press[key_comb]`, `scroll[direction, amount]`, `new_tab`, `tab_focus[index]`, `close_tab`, `drag_and_drop[from, to]`, `send_msg_to_user[text]`, `report_infeasible[reason]`.

**WHY it exists**: Two core use cases:

1. **Web scraping and research** — Agents need to look things up: documentation, Stack Overflow, API references. The browser gives them direct access.
2. **Web app testing** — Agents building web applications need to verify their work. The browser can navigate to `localhost:3000` and interact with the UI.

**The `bid` (Browser ID) innovation**: Instead of using CSS selectors or XPath (which are fragile), BrowserGym assigns unique IDs to interactive elements in the accessibility tree. The agent sees `[bid=42] Submit Button` and can `click[42]`. This is more reliable than coordinate-based or selector-based approaches.

**The Accessibility Tree approach**: The agent sees a text representation of the page structure via `axtree`. This is much more token-efficient than screenshots (which require vision models and are expensive) while still providing structural information about interactive elements.

**Dedicated BrowsingAgent**: OpenHands has a specialized `BrowsingAgent` that handles web tasks via delegation. The main `CodeActAgent` delegates to it with `AgentDelegateAction(agent='BrowsingAgent')`. This separation keeps the main agent's context clean.

**VisualBrowsingAgent**: A vision-based variant that uses screenshots instead of axtree. Useful for visually complex pages where the text representation loses important layout information.

**What would break without it**: No web research, no web app testing, no scraping. The agent would be limited to CLI tools like `curl` and `wget`, which can't handle JavaScript-rendered pages or interactive web UIs.

**AVA comparison**: AVA has a Puppeteer-based browser tool. OpenHands' BrowserGym integration is more sophisticated (accessibility tree, bid-based selection, dedicated agent). **Advantage: OpenHands** — but this is a complex, heavy feature that adds significant container size.

---

### 4.5 `think` — The Reasoning Scratchpad

**File**: `tools/think.py` → `AgentThinkAction`

**What it does**: Absolutely nothing externally. It logs the agent's thought and returns an acknowledgment. No side effects.

**WHY it exists — this is subtle and important**:

1. **Structured reasoning in function-calling mode**: When using function calling, the model *must* call a function on every turn. Without `think`, the model would be forced to take an action even when it should be reasoning. This leads to premature or poorly-planned actions.

2. **Transparent reasoning**: Think calls are visible in the event stream. Users and debuggers can see *why* the agent made a decision, not just what it did.

3. **Scratchpad for complex problems**: The tool description explicitly lists use cases: "Brainstorming potential bug sources", "Working through why a test might be failing", "Planning a multi-step refactoring". These are all cases where acting without thinking leads to wasted actions.

**What would break without it**: In function-calling mode, the agent would be forced to take a real action on every turn. This would lead to:
- Premature file edits before understanding the problem
- Unnecessary bash commands to "explore" when the agent should be reasoning
- Worse overall task completion rates because the agent can't plan

**AVA comparison**: AVA doesn't have an explicit `think` tool, but our agent uses `<thinking>` tags in its responses. The functional difference is that AVA's thinking is inline in the message, while OpenHands' thinking is a distinct event in the stream. **Advantage: Roughly equal**, but OpenHands' approach is better for structured logging and debugging.

---

### 4.6 `finish` — The Completion Signal

**File**: `tools/finish.py` → `AgentFinishAction`

**What it does**: Signals task completion with a summary message. Transitions the agent state to `FINISHED`.

**WHY it exists**: Without an explicit finish signal, the system wouldn't know when the agent considers itself done. The agent might take infinite turns, each one costing money. The `finish` tool creates a clean boundary between "working" and "done."

**Problem it solves**:
1. **Budget control** — Without finish, the iteration limit is the only stop mechanism
2. **User experience** — The user needs a clear "I'm done, here's what I did" message
3. **Evaluation** — SWE-bench needs to know when the agent stopped intentionally vs hit a limit
4. **Critic integration** — The `FinishCritic` checks if the last action is `AgentFinishAction` to score the trajectory

**AVA comparison**: AVA has `attempt_completion`, which is essentially the same. **Advantage: Equal**.

---

### 4.7 `task_tracker` — Structured Project Management

**File**: `tools/task_tracker.py` → `TaskTrackingAction`

**What it does**: Maintains a structured task list (stored as `TASKS.md` in the session directory) with two commands: `plan` (create/update task list) and `view` (see current task state). Tasks have statuses: `todo`, `in_progress`, `done`.

**WHY it exists — this is a significant innovation**:

1. **Long-horizon task management**: For complex tasks that span many turns, the agent needs a way to track progress. Without explicit task tracking, the agent forgets what it's done and what remains after context condensation.

2. **User visibility**: Users can see exactly where the agent is in a multi-step task. This reduces anxiety ("is it stuck?") and enables better intervention points.

3. **Condensation survival**: The task tracker is explicitly designed to survive context condensation. The long-horizon system prompt says: "If you were using the task_tracker tool before a condensation event, continue using it after condensation." The task state persists in the session directory, so even if the conversation history is compressed, the task list remains.

4. **Structured decomposition**: Forcing the agent to create task lists encourages systematic problem decomposition. The extremely detailed tool description (with examples and counter-examples) guides the agent toward good planning behavior.

**What would break without it**: For long-running tasks:
- Agent loses track of progress after condensation
- User has no visibility into multi-step workflows
- Agent repeats completed work or skips remaining tasks
- No structured record of what was planned vs executed

**AVA comparison**: AVA has `todoread`/`todowrite` which serve the same purpose. However, OpenHands' integration with the long-horizon prompt and condensation persistence is more explicitly designed. **Advantage: Roughly equal** — but OpenHands' condensation-aware design is worth studying.

---

### 4.8 `condensation_request` — Agent-Initiated Context Management

**File**: `tools/condensation_request.py` → `CondensationRequestAction`

**What it does**: Lets the agent explicitly request context condensation. Returns a `CondensationRequestAction` that triggers the condenser on the next step.

**WHY it exists — this is genuinely novel**:

Most context management systems are automatic — they condense when the context window is too large. OpenHands gives the agent *agency* over its own context. The agent can decide "I've accumulated a lot of noise from debugging — let me compress before continuing."

**Problem it solves**: Automatic condensation has a timing problem. It triggers when the window is full, which might be in the middle of a critical sequence. Agent-initiated condensation lets the agent choose the optimal moment — typically after completing a subtask but before starting the next one.

**What would break without it**: The agent would rely entirely on automatic condensation, which:
- Might trigger at an inopportune moment
- Can't benefit from the agent's understanding of what's important
- May condense away context the agent knows it still needs

**AVA comparison**: AVA uses automatic compaction. We don't have agent-initiated condensation. **Advantage: OpenHands** — this is a feature worth considering for AVA.

---

### 4.9 MCP Tools — Dynamic Tool Extension

**What it does**: Dynamically loads tools from MCP (Model Context Protocol) servers. Both stdio and SSE transports supported. Tools are converted to litellm `ChatCompletionToolParam` format and added to the agent's tool list.

**WHY it exists**: MCP is the emerging standard for tool interoperability. Supporting it means:
1. Users can bring their own tools without modifying OpenHands code
2. Microagents can declare MCP server dependencies in their frontmatter
3. The tool ecosystem grows without OpenHands team effort

**AVA comparison**: AVA also has MCP support. **Advantage: Equal**.

---

### 4.10 ReadOnly Agent Tools (grep, glob, view)

The `ReadOnlyAgent` has a restricted toolset: `grep`, `glob`, `view`, `think`, `finish`. No write operations.

**WHY it exists**: Safe codebase exploration. Used as a delegate for tasks that only require reading — "find where X is defined", "what does this function do?", etc. By restricting the tool set, the agent can't accidentally modify anything.

**Problem it solves**: The main CodeAct agent has full write access. For purely investigative tasks, giving it write tools is risky and wastes context on irrelevant tool descriptions.

**AVA comparison**: AVA doesn't have a dedicated read-only agent mode, though our permission system can restrict tool access. **Advantage: OpenHands** — the dedicated agent is cleaner than runtime restrictions.

---

## 5. The Condenser System

### Architecture

The condenser is a **pluggable pipeline** for reducing event history before sending it to the LLM. It sits between the event stream and the LLM call.

```
Event Stream → Condenser Pipeline → LLM Messages → LLM
```

The condenser returns either:
- **`View`**: A filtered list of events (ready for LLM)
- **`Condensation`**: An instruction to modify the event stream (agent must return this action, then re-step)

### WHY This Exists — The Core Problem

Context windows are finite. A long coding session can generate thousands of events. Without condensation, the agent would either:
1. Hit the context window limit and crash
2. Pay enormous token costs for redundant context
3. Lose focus as important information is buried in noise

### The 10 Condenser Implementations (Ranked by Sophistication)

#### 1. NoOpCondenser
**What**: Pass through everything unchanged.
**Why**: Baseline. For short sessions or testing.

#### 2. RecentEventsCondenser
**What**: Keep only the N most recent events.
**Why**: Cheapest possible reduction. Drops old context entirely.
**Weakness**: Loses the initial task description and early decisions.

#### 3. AmortizedForgettingCondenser
**What**: When events exceed `max_size`, keep first `keep_first` + last `(max_size/2 - keep_first)` events. Drop the middle.
**Why**: Preserves the task description (head) and recent context (tail) without any LLM cost.
**Innovation**: The "amortized" aspect — it halves the view each time, so condensation frequency decreases as the session ages.

#### 4. ConversationWindowCondenser
**What**: Sliding window over events.
**Why**: Like RecentEvents but with configurable windowing.

#### 5. ObservationMaskingCondenser
**What**: Replaces observation content outside an `attention_window` with `<MASKED>`.
**Why**: Observations (bash output, file contents) are usually the largest events. Masking old observations saves massive amounts of tokens while preserving the action history.
**Key insight**: Actions are small and information-dense. Observations are large and often redundant. Selectively masking observations is far better than dropping entire events.

#### 6. BrowserOutputCondenser
**What**: Specifically compresses browser observation output.
**Why**: Browser observations (accessibility trees, page content) can be enormous — 10,000+ tokens per page. This condenser strips redundant DOM information.

#### 7. LLMSummarizingCondenser
**What**: When events exceed `max_size`, calls an LLM to summarize the middle section.
**Why**: Unlike simple truncation, LLM summarization preserves *meaning*. The LLM can identify what was important in the forgotten events and include it in the summary.
**Cost**: Requires an LLM call per condensation. The condenser explicitly disables prompt caching since it never benefits from cache reads (each condensation prompt is unique).

#### 8. StructuredSummaryCondenser (most sophisticated)
**What**: Uses LLM function-calling to generate a structured `StateSummary` with 16 typed fields.
**Why**: Unstructured summaries lose information in unpredictable ways. Structured summaries guarantee that key categories are preserved.
**The 16 fields**: `user_context`, `completed_tasks`, `pending_tasks`, `current_state`, `files_modified`, `function_changes`, `data_structures`, `tests_written`, `tests_passing`, `failing_tests`, `error_messages`, `branch_created`, `branch_name`, `commits_made`, `pr_created`, `pr_status`, `dependencies`, `other_relevant_context`.
**Key insight**: This is essentially a "state machine checkpoint" for the agent. After condensation, the agent has a structured snapshot of everything it needs to continue.

#### 9. LLMAttentionCondenser
**What**: Uses an LLM to rank events by importance, keeps the most important ones.
**Why**: Neither recency nor position perfectly predicts importance. An LLM can understand that an error message from 50 events ago is more relevant than a successful `ls` from 5 events ago.
**Innovation**: Uses `response_schema` for structured output (list of event IDs). Falls back to keeping recent events if the LLM response is insufficient.

#### 10. CondenserPipeline
**What**: Chains multiple condensers sequentially.
**Why**: Different condensers excel at different things. A pipeline of `ObservationMasking → BrowserOutput → LLMSummarizing` gives the best of all worlds: cheap token reduction first, expensive LLM summarization only for the remaining events.

### What Would Break Without the Condenser System

Without condensation:
- Sessions longer than ~50 turns would hit context limits
- Token costs would scale linearly with session length (no amortization)
- The agent would lose the ability to handle complex, multi-step tasks
- SWE-bench performance would drop dramatically (many issues require 100+ turns)

### AVA Implication

AVA uses a single compaction strategy. OpenHands' pipeline approach (cheap filters first, expensive LLM summarization last) and the structured summary condenser are both worth studying. The `ObservationMaskingCondenser` — keeping actions but masking old observations — is a particularly clever insight that AVA should adopt.

---

## 6. Stuck Detection

### The 5 Loop Scenarios

The `StuckDetector` identifies when the agent is trapped in an unproductive loop:

| Scenario | Pattern | Threshold | WHY It's Detected |
|----------|---------|-----------|-------------------|
| **Repeating action/observation** | Same action + observation 4x | 4 identical pairs | Agent found a working command but keeps running it. Common with `ls` or `cat` loops. |
| **Repeating action/error** | Same action + error 3x | 3 pairs | Agent keeps trying the same broken command. Won't self-correct. |
| **Monologue** | 3 identical messages, no observations | 3 messages | Agent is talking to itself without taking action. Usually a function-calling failure. |
| **Alternating pattern** | A→B→A→B→A→B | 6 steps | Agent oscillates between two strategies, never making progress. |
| **Context window error loop** | 10+ consecutive condensation events | 10 events | Condenser can't reduce enough. The agent is in an infinite condense-retry loop. |

### WHY This Exists

Without stuck detection, a looping agent would:
1. Burn through the entire iteration budget doing nothing useful
2. Cost money on meaningless LLM calls
3. Leave the user waiting for a result that will never come

### The Recovery Mechanism

When stuck is detected, the agent gets three options:
1. **Restart from before the loop** — rewind to the event that started the loop pattern
2. **Restart with last user message** — re-read the user's request with fresh context
3. **Stop** — admit defeat gracefully

This is delivered as a `LoopDetectionObservation` with a `LoopRecoveryAction`, making recovery part of the normal event flow.

### Real Advantage

Most coding agents have no stuck detection. They simply run until the iteration limit. OpenHands' approach saves money, reduces user frustration, and produces better outcomes by giving the agent a chance to try a different strategy.

### AVA Implication

AVA should implement stuck detection. The 5-scenario taxonomy is well-designed and could be adopted directly.

---

## 7. Security Model

### Per-Action Risk Assessment

Every tool that can modify the environment accepts a `security_risk` parameter: `low`, `medium`, `high`.

**CLI mode risks**:
- **LOW**: Read-only (viewing files, listing directories)
- **MEDIUM**: Project-scoped (editing project files, running project scripts)
- **HIGH**: System-level (sudo, global installs, sending data externally)

**Sandbox mode risks**:
- **LOW**: Read-only inside sandbox
- **MEDIUM**: Container-scoped edits and installs
- **HIGH**: Data exfiltration, privilege escalation

### WHY This Exists

The risk annotation serves two purposes:

1. **Agent self-assessment**: By requiring the agent to declare risk level, it forces the model to *think* about the safety of each action. This is a form of chain-of-thought safety reasoning.

2. **Confirmation gating**: In confirmation mode, HIGH risk actions trigger `AWAITING_USER_CONFIRMATION` state. The user must approve before the action executes.

### The SecurityAnalyzer Framework

Pluggable analyzers can evaluate actions:
- **Invariant**: Rule-based safety checking
- **GraySwan**: External security model
- **LLM-based**: Uses a separate LLM to assess action safety

**Fail-safe**: If no analyzer is configured and an action has UNKNOWN risk, it requires confirmation. This means the system defaults to safe.

### AVA Implication

AVA has a permission system with rules (allow/deny/ask). OpenHands' per-action risk annotation is a complementary approach worth considering — making the agent self-declare risk level adds a layer of defense.

---

## 8. Multi-Agent Delegation

### How It Works

The `AgentController` supports delegate agents. When the main CodeAct agent encounters a task it should delegate:

```
CodeActAgent → AgentDelegateAction(agent='BrowsingAgent', inputs={...})
    → Controller creates child AgentController(is_delegate=True)
    → Child shares EventStream, metrics, iteration flags
    → Child runs its own step loop with its own State
    → When child finishes → AgentDelegateObservation returns to parent
```

### WHY This Exists

Different tasks need different capabilities and prompts:
- **BrowsingAgent**: Specialized for web navigation. Processes accessibility trees.
- **VisualBrowsingAgent**: Uses screenshots instead of axtree.
- **ReadOnlyAgent**: Safe exploration without write tools.
- **LocAgent**: Specialized for finding relevant code locations.

### Real Advantage

Delegation keeps the main agent's context clean. Web browsing generates enormous observations (axtree representations). By delegating to a specialized agent, the main agent only sees the final result, not the intermediate navigation steps.

### What Would Break Without It

Without delegation:
- The main agent's context would be polluted with browser observations
- No ability to restrict tool access for sub-tasks
- No specialized prompting for different task types
- Higher token costs from irrelevant context

### AVA Comparison

AVA has the Commander hierarchy (Commander → Leads → Workers) with `delegate_coder`, `delegate_tester`, `delegate_reviewer`, `delegate_researcher`, `delegate_debugger`. This is more structured than OpenHands' flat delegation. **Advantage: AVA** — our hierarchy enables more sophisticated multi-agent workflows.

---

## 9. Microagent System

### What It Is

Microagents are markdown files with YAML frontmatter that inject context-specific instructions into the agent's prompt. Three types:

1. **Repo microagents** (`type: repo`): Always loaded. Project-specific instructions.
2. **Knowledge microagents** (`type: knowledge`): Loaded when user message matches keyword triggers.
3. **Task microagents** (`type: task`): Triggered by `/<name>` commands. Can require user input.

### Loading Sources

1. **Global**: ~27 skills shipped with OpenHands (`skills/` directory)
2. **User**: `~/.openhands/microagents/`
3. **Repository**: `.openhands/microagents/` in the workspace
4. **Third-party**: Auto-detects `.cursorrules` and `AGENTS.md`

### WHY This Exists

1. **Domain knowledge injection**: Different projects need different context. A React project needs different instructions than a Django project. Microagents inject this knowledge without modifying the core agent.

2. **Trigger-based efficiency**: Not all knowledge is always relevant. Loading GitHub API instructions only when the user mentions "github" saves context tokens for other information.

3. **Community extensibility**: Anyone can create microagents for their domain. This creates a knowledge ecosystem without requiring code changes.

4. **Task workflows**: Task microagents like `/code-review` and `/fix_test` provide structured workflows for common operations.

### The Recall System

When a user sends a message:
1. First message → `RecallAction(recall_type=WORKSPACE_CONTEXT)` → returns all repo microagents + runtime info
2. Subsequent messages → `RecallAction(recall_type=KNOWLEDGE)` → checks triggers against message text

This is implemented as a Memory component that subscribes to the EventStream, making it part of the event-driven architecture.

### AVA Comparison

AVA has project instructions (auto-detected from file globs) and skills (auto-invoked by file type/project context). OpenHands' keyword-trigger approach is different — it's reactive (responds to what the user says) vs AVA's proactive approach (loads based on what files exist). Both have merits. **Advantage: Roughly equal** — OpenHands' trigger-based loading is more token-efficient; AVA's proactive loading is more reliable.

---

## 10. Prompt Engineering

### System Prompt Structure

OpenHands uses Jinja2 templates for prompt composition. The base `system_prompt.j2` has 11 XML-tagged sections:

| Section | Purpose | Key Insight |
|---------|---------|-------------|
| `<ROLE>` | Agent identity | "If user asks a question, don't try to fix the problem" — prevents over-eager fixing |
| `<EFFICIENCY>` | Cost control | "Each action is somewhat expensive" — encourages batching |
| `<FILE_SYSTEM_GUIDELINES>` | File safety | "NEVER create multiple versions of the same file" — prevents file proliferation |
| `<CODE_QUALITY>` | Output quality | "Minimal comments", "minimal changes" — prevents over-engineering |
| `<VERSION_CONTROL>` | Git safety | Co-authored-by header, don't push to main, use git commit -a |
| `<PULL_REQUESTS>` | PR discipline | "Only ONE per session" — prevents PR spam |
| `<PROBLEM_SOLVING_WORKFLOW>` | Methodology | 5-step: Explore → Analyze → Test → Implement → Verify |
| `<SECURITY>` | Credential safety | Only use tokens in expected ways |
| `<ENVIRONMENT_SETUP>` | Self-sufficiency | "Don't stop if application not installed. Install it." |
| `<TROUBLESHOOTING>` | Recovery | "Step back and reflect on 5-7 different possible sources" |
| `<PROCESS_MANAGEMENT>` | Safety | "Do NOT use pkill -f server" — prevent killing random processes |

### Variant Prompts

1. **Long-horizon prompt**: Adds `<TASK_MANAGEMENT>` and `<TASK_TRACKING_PERSISTENCE>` sections. Mandates task_tracker usage for complex work.

2. **Tech philosophy prompt**: Adds a Linus Torvalds persona with 5-layer analysis (Data Structure → Special Cases → Complexity → Breaking Changes → Practicality). This is for code review mode.

### In-Context Learning Example

A full worked example (169 lines) showing a Flask app creation workflow. This example demonstrates:
- Error recovery (module not installed → install it → retry)
- File creation and editing
- Server management
- Clean finish

### Key Prompt Insights for AVA

1. **"Each action is expensive"** — This framing encourages the agent to be efficient. AVA should adopt similar cost-aware prompting.
2. **"NEVER create multiple versions"** — File proliferation is a real agent failure mode. Our prompts should include this.
3. **5-step problem-solving workflow** — Explore → Analyze → Test → Implement → Verify. This is a good framework for our agent prompts.
4. **"Step back and reflect on 5-7 possible sources"** — A specific number (5-7) gives the model a concrete target. Vague instructions like "think carefully" are less effective.

---

## 11. SWE-bench Pipeline

### What It Is

A complete pipeline for automatically resolving GitHub issues:

```
resolve_issue.py → issue_resolver.py → agent solves issue → send_pull_request.py
```

### WHY This Exists

SWE-bench is the primary benchmark for coding agents. OpenHands' architecture is *designed* for SWE-bench:
- Docker sandbox provides clean, reproducible environments
- `str_replace_editor` generates clean patches
- Event system enables trajectory analysis
- Critic system evaluates agent output

### The Pipeline

1. Takes a repo URL + issue number
2. Clones the repo into the Docker sandbox
3. Runs the CodeActAgent with the issue as the task
4. Collects the resulting git diff
5. Optionally creates a PR

### Competitive Significance

OpenHands consistently ranks high on SWE-bench. Their benchmark performance is their primary marketing asset. The tight integration between their architecture and the benchmark pipeline is not accidental — the architecture was designed for this.

### AVA Implication

AVA is desktop-first, not benchmark-first. We should not try to compete on SWE-bench directly. Instead, focus on developer experience metrics: time-to-first-action, edit accuracy, user satisfaction.

---

## 12. Model Routing & LLM Abstraction

### LiteLLM as Universal Adapter

OpenHands uses LiteLLM as a single abstraction over all LLM providers. This means:
- No per-provider implementation code
- Automatic support for new providers as LiteLLM adds them
- Consistent interface for retries, metrics, and function calling

### Model Feature Detection

```python
@dataclass(frozen=True)
class ModelFeatures:
    supports_function_calling: bool
    supports_reasoning_effort: bool
    supports_prompt_cache: bool
    supports_stop_words: bool
```

Uses glob patterns to determine capabilities per model family.

### Function Calling Mock

For models without native function calling, OpenHands converts tool-call messages to text prompts and parses the response back. This enables tool use with *any* model.

### RouterLLM

A `RouterLLM` can route requests to different models based on rules:
- Use cheaper model for simple tasks (ls, cat)
- Use expensive model for complex tasks (multi-file edits)
- Load balance across providers

### AVA Comparison

AVA has 16 per-provider implementations. This is more code to maintain but gives finer-grained control. OpenHands' LiteLLM approach is simpler but depends on a third-party library. **Trade-off**: AVA has more control; OpenHands has less maintenance burden.

The RouterLLM concept is interesting — AVA could use different models for different worker types (cheaper model for research, expensive model for coding).

---

## 13. Critic System

### What It Is

A pluggable evaluation framework for agent output quality:

```python
class BaseCritic(ABC):
    def evaluate(self, events: list[Event], git_patch: str | None) -> CriticResult:
        pass

class CriticResult(BaseModel):
    score: float  # 0.0 to 1.0
    message: str
    success: bool  # score >= 0.5
```

### Current Implementation

Only `AgentFinishedCritic` — a simple rule-based check:
- Did the agent call `finish`? (score=1 if yes, 0 if no)
- Is the git patch empty? (score=0 if empty)

### WHY This Exists

Primarily for SWE-bench evaluation. The critic evaluates whether the agent's trajectory was successful before submitting the result.

### Future Potential

The abstract base class suggests plans for more sophisticated critics:
- LLM-based evaluation of solution quality
- Test-passing verification
- Code quality analysis

### AVA Implication

AVA has a validator pipeline (QA system). The concept is similar but our implementation is more extensive. **Advantage: AVA**.

---

## 14. Key Innovations Worth Stealing

### 1. Observation Masking (from ObservationMaskingCondenser)
**Insight**: Actions are small and valuable. Observations are large and often redundant. Mask old observations but keep the action history.
**Implementation effort**: Low — add a condenser stage that replaces old tool outputs with `<MASKED>`.

### 2. Agent-Initiated Condensation (from condensation_request tool)
**Insight**: Let the agent choose when to compress, not just trigger on window size.
**Implementation effort**: Medium — add a tool and integrate with compaction system.

### 3. Structured Summary Condensation (from StructuredSummaryCondenser)
**Insight**: When compressing, use structured generation (function calling) to ensure critical categories are preserved.
**Implementation effort**: High — requires designing the state schema and integrating with compaction.

### 4. Per-Action Security Risk (from security_risk parameter)
**Insight**: Make the agent self-declare the risk level of each action. This both improves safety and serves as chain-of-thought reasoning about consequences.
**Implementation effort**: Low — add an optional parameter to dangerous tools.

### 5. Stuck Detection Taxonomy (from StuckDetector)
**Insight**: There are exactly 5 distinct loop patterns, each requiring different detection logic.
**Implementation effort**: Medium — implement pattern matching over recent action history.

### 6. Long-Horizon Task Persistence (from task_tracker + condensation integration)
**Insight**: Task state must survive context condensation. Store it externally and prompt the agent to re-check after condensation.
**Implementation effort**: Low — our todoread/todowrite already stores externally; add post-compaction prompting.

### 7. In-Context Learning Examples
**Insight**: A full worked example in the system prompt dramatically improves tool-use reliability.
**Implementation effort**: Low — add representative examples to our system prompts.

---

## 15. Architectural Weaknesses to Exploit

### 1. Python Performance
OpenHands is pure Python. Startup time, event processing, and serialization are all slower than a TypeScript or Rust-based system. AVA's Tauri/TypeScript stack is inherently faster for interactive use.

### 2. Docker Dependency
The Docker requirement is a significant barrier to adoption:
- Users must have Docker installed and running
- Container startup adds 2-5 seconds per session
- Container images can be 1-10 GB
- Cannot run on restrictive corporate networks that block Docker

AVA's local-first approach with optional Docker is more accessible.

### 3. Cloud-First Architecture
OpenHands is designed for cloud deployment (web server, remote runtime, Kubernetes). This means:
- Higher latency (HTTP round-trips to container)
- No offline capability
- Requires server infrastructure for self-hosting

AVA's desktop-first approach works offline, has lower latency, and requires no infrastructure.

### 4. No Extension System
OpenHands is monolithic. You can add microagents (prompts) and MCP servers (tools), but you cannot:
- Add new condenser strategies without modifying core code
- Create custom agent types without contributing to agenthub
- Extend the UI without forking the frontend

AVA's ExtensionAPI and plugin ecosystem is a significant differentiator.

### 5. Single Agent Architecture (CodeAct)
Despite having multiple agent types, OpenHands is essentially a single-agent system. The CodeActAgent does everything, with occasional delegation to specialists. There's no true multi-agent coordination or planning hierarchy.

AVA's Commander → Leads → Workers hierarchy enables more sophisticated task decomposition and parallel execution.

### 6. V0/V1 Migration Uncertainty
The codebase is mid-migration. Nearly every file has a deprecation header. This means:
- Technical debt is high
- Architecture may change significantly
- Community contributions are harder (which version to target?)

### 7. Limited Undo/Rollback
OpenHands has `undo_edit` (reverts last file edit) but no comprehensive session rollback. If the agent makes 10 edits across 5 files and you want to undo, you need to manually revert each file.

AVA's git-based snapshot system provides better rollback capabilities.

---

## 16. Gap Analysis vs AVA

### Features OpenHands Has That AVA Lacks

| Feature | OpenHands | AVA Status | Priority |
|---------|-----------|------------|----------|
| Docker sandboxing (built-in) | Full container isolation | Docker extension (optional) | Medium |
| IPython integration | Persistent kernel | None | Low (niche) |
| BrowserGym (axtree + bid) | Advanced browser automation | Puppeteer (basic) | Low |
| Agent-initiated condensation | Tool for agent to request | Automatic only | Medium |
| Observation masking | Selective observation compression | Full compaction | High |
| Structured summary condensation | 16-field structured state | Unstructured compaction | Medium |
| Stuck detection (5 scenarios) | Comprehensive loop detection | None | High |
| Condenser pipeline | Chain multiple strategies | Single strategy | Medium |
| Per-action security risk | Agent self-declares risk | Permission rules | Low |
| SWE-bench resolver pipeline | Built-in issue→PR pipeline | None | Low (not our focus) |
| Model routing (RouterLLM) | Dynamic model selection per task | Fixed model per session | Medium |

### Features AVA Has That OpenHands Lacks

| Feature | AVA | OpenHands Status |
|---------|-----|-----------------|
| Desktop-native (Tauri) | Instant startup, offline | Web-only, Docker required |
| Extension/Plugin ecosystem | ExtensionAPI | Monolithic |
| Commander hierarchy | Multi-level delegation | Flat delegation |
| Parallel worker execution | Concurrent agents | Sequential only |
| PTY support | Full terminal emulation | Basic stdin |
| Fuzzy text matching | Forgiving edits | Exact match only |
| Multi-file edit | multiedit tool | One file at a time |
| Git snapshots | Full session rollback | Per-file undo only |
| LSP integration | Language-aware editing | None |
| Plan mode | Structured planning phase | Inline planning only |

### Strategic Recommendations

1. **Steal**: Observation masking, stuck detection, agent-initiated condensation
2. **Study**: Structured summary condensation, condenser pipeline pattern
3. **Ignore**: Docker-first architecture (not our market), SWE-bench pipeline (not our benchmark)
4. **Protect**: Desktop-native experience, extension ecosystem, multi-agent hierarchy — these are our differentiators

---

*Analysis completed March 2026. Based on OpenHands codebase snapshot with V0 legacy code (scheduled for V1 migration April 2026).*
