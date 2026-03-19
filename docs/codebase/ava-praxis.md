# ava-praxis

> Multi-agent orchestration with LLM-powered Director, scouts, Board of Directors, and domain-specific leads (Praxis v2).

## Architecture Overview (v2)

Praxis v2 replaces the code-driven task router with an **LLM-powered Director** that analyzes task complexity and adapts its orchestration strategy. The hierarchy is: **Director → Scouts → Leads → Workers**, with an optional **Board of Directors** for complex decisions.

### Director Intelligence Levels

The Director uses an LLM to analyze the goal, read scout reports, and produce plans. It detects task complexity and selects the appropriate level:

**Level 1 — Simple task** (one-file bug fix):
- Director detects simplicity, spawns one worker + one QA worker
- No leads needed. Agentless fast path: localize → repair → validate
- Example: "Fix the typo in auth.rs line 42"

**Level 2 — Medium task** (multi-file, clear scope):
- Director sends scouts to read relevant code sections
- Scouts report back with structured summaries
- Director creates plan, shows to user (Plannotator-style)
- User can comment/edit plan before execution
- Director spawns 2-3 leads, each with workers
- Sequential or parallel based on dependencies
- Example: "Add pagination to the user list API and update the frontend"

**Level 3 — Complex task** (major refactor, security, architecture):
- Director detects complexity OR user says "consult the board"
- Scouts read codebase, produce summary report
- Board of Directors convenes (3 different SOTA models)
- Each board member has a distinct analytical personality
- One round of opinions based on scout reports (not raw code), then vote
- Main Director synthesizes board recommendations into a plan
- User approves/edits via Plannotator UI
- Example: "Migrate the auth system from JWT to OAuth2 with backward compatibility"

### Scout System

Scouts are lightweight agents controlled directly by the Director:

- **Model**: Cheapest available (Haiku, Flash, Mercury)
- **No lead needed**: Director spawns and manages scouts directly
- **Task**: Read specific parts of codebase, produce structured summaries
- **Lifecycle**: Ephemeral — created for pre-planning intelligence, dismissed after reporting
- **Used before planning**: Give Director (and Board) context about the codebase
- **No names**: Scouts are utility agents, not personified

### Board of Directors

Opt-in multi-model consensus for Level 3 (complex) tasks:

- **Trigger**: Director auto-detects complexity OR user requests "consult the board"
- **Composition**: 3 different SOTA models (configurable in Settings → Agents)
- **Input**: Scout reports as context (not raw code — keeps cost manageable)
- **Personalities**: Each board member has a distinct analytical style (one rigid, one analytical, one creative)
- **Process**: One round of opinions → vote on approach → Director synthesizes
- **User has final say**: Board recommends, user approves
- **Naming**: "Opus (Board)", "Gemini (Board)", "GPT (Board)"

### Plan System (Plannotator-Style)

Two modes for plan creation and editing:

**Solo Plan Mode**: Regular AI suggests a plan using plan tool, user reviews/edits in chat.

**Director Plan Mode**: Director creates plan after scout reports, shows as structured message in chat:
- Each step is clickable — user can add comments
- Steps can be reordered via drag, deleted, or new steps added
- Comments get incorporated into Lead/Worker instructions
- "Approve" / "Add Comment" / "Edit Plan" buttons
- Budget shown per step + total
- Plans exportable as Markdown, saved to `.ava/plans/` for reuse

### Execution Model

**Sequential by default, parallel when safe.** The Lead manages execution order — workers do NOT self-claim tasks.

Sequential (dependencies exist):
1. Pedro fixes auth.rs → waits until done
2. Sofia adds code depending on Pedro's fix → waits
3. Kai verifies both changes

Parallel (independent tasks):
1. Pedro fixes auth.rs } simultaneously
2. Sofia fixes routes.rs } (different files)
3. → After both done: Kai reviews both

The Lead is smart enough to know when parallel is safe vs when sequential is required.

### QA at Every Level

- Each Lead has access to QA workers for verification
- QA Lead exists for cross-lead merge verification
- Workers only mark tasks "done" after their specific changes compile + pass tests
- QA tests only what changed, not the entire codebase

### Smart Model Routing

| Role | Model Tier | Examples | Override |
|------|-----------|----------|----------|
| Scouts | Cheapest | Haiku, Flash, Mercury | Settings → Agents → Scout model |
| Workers | Mid-tier | Sonnet, GPT-5.3 | Per-lead in team config |
| Leads | Strong | Sonnet, Opus (complex) | Per-lead in team config |
| Director | Strongest available | Opus, GPT-5.4 | Settings → Agents |
| Board | Top per provider | Best from each provider | Settings → Agents → Board models |

## Team Configuration (Settings → Agents)

User-configurable options:
- Team presets (which leads, which models, which tools)
- Per-lead model selection (e.g., Backend Lead uses Sonnet, QA uses Haiku)
- Per-lead tool restrictions (Planner = read-only, Coder = full, Reviewer = read-only + diff)
- Custom prompts per lead/worker role
- Scout model (default: cheapest available)
- Board of Directors models (default: top 3 available providers)
- Default execution mode (sequential/parallel/auto)

## Public API

| Type/Function | Description |
|--------------|-------------|
| `Director` | Orchestrates leads, delegates tasks, coordinates workers |
| `DirectorConfig` | Configuration with budget, providers, platform |
| `Lead` | Domain-specific lead (Frontend, Backend, QA, etc.) |
| `Worker` | Spawned agent with task, budget, provider |
| `Domain` | Enum: Frontend, Backend, QA, Research, Debug, Fullstack, DevOps |
| `Budget` | Token/turn/cost limits with interactive factory |
| `Task` | Description, type, file list |
| `TaskType` | Enum: Planning, CodeGeneration, Testing, Review, Research, Debug, Simple |
| `Director::new()` | Create director with configured leads |
| `Director::delegate()` | Assign task to appropriate lead, spawn worker |
| `Director::coordinate()` | Execute workers with timeout/cancellation, stream events |
| `Workflow` | Multi-phase pipeline definition |
| `Phase` | Single phase with role, prompt override, output chaining |
| `PhaseRole` | Planner, Coder, Reviewer, Tester, Custom |
| `WorkflowExecutor` | Executes workflows with iteration/feedback loops |
| `Workflow::plan_code_review()` | Planner→Coder→Reviewer preset |
| `Workflow::code_review()` | Coder→Reviewer preset |
| `Workflow::plan_code()` | Planner→Coder preset |
| `AcpHandler` | Handles ACP requests (specs, artifacts, mailbox) |
| `AcpRequest`/`AcpResponse` | Agent Communication Protocol types |
| `InProcessAcpTransport` | In-process ACP transport with event recording |
| `Artifact` | Workflow output artifact with kind, producer, content |
| `ArtifactStore` | In-memory artifact storage |
| `FileArtifactStore` | Persistent JSON artifact storage |
| `SpecDocument` | Specification with requirements, design, tasks |
| `SpecStore` | In-memory spec storage with CRUD |
| `Mailbox` | Worker-to-worker message queue |
| `PeerMessage` | Message with from/to worker, kind, body |
| `ConflictDetector` | Detects file overlap between worker intents |
| `ReviewResult` | Parsed code review with issues, positives, verdict |
| `ReviewVerdict` | Approve, RequestChanges, Comment |
| `Severity` | Critical, Warning, Suggestion, Nitpick |
| `DiffMode` | Staged, Working, Commit(sha), Range(range) |
| `collect_diff()` | Async git diff collection |
| `parse_review_output()` | Parse structured review from text |
| `PraxisEvent` | Event enum for worker lifecycle, workflow, specs, artifacts |

## Module Map

| File | Purpose |
|------|---------|
| `lib.rs` | Director, Lead, Worker, Budget, Task, Domain, coordination logic |
| `workflow.rs` | Workflow, Phase, PhaseRole, WorkflowExecutor, phase prompts |
| `acp.rs` | ACP protocol types: Method, Request, Response, Error |
| `acp_handler.rs` | AcpHandler with spec/artifact/mailbox request handlers |
| `acp_transport.rs` | InProcessAcpTransport with event accumulation |
| `artifact.rs` | Artifact, ArtifactKind with workflow factory methods |
| `artifact_store.rs` | ArtifactStore (memory), FileArtifactStore (persistent) |
| `spec.rs` | SpecDocument, SpecTask, SpecStatus, SpecStore |
| `spec_workflow.rs` | build_spec_goal() for spec-driven workflows |
| `mailbox.rs` | Mailbox, PeerMessage, PeerMessageKind |
| `conflict.rs` | ConflictDetector, WorkerIntent, ConflictReport |
| `decomposition.rs` | Task decomposition with heuristic and LLM planner |
| `synthesis.rs` | Worker result synthesis with LLM merge |
| `review.rs` | Code review parsing, formatting, agent runner |
| `events.rs` | PraxisEvent enum for all lifecycle events |

## Dependencies

Uses:
- `ava-agent` - AgentLoop, AgentEvent, AgentConfig
- `ava-context` - ContextManager
- `ava-llm` - LLMProvider, SharedProvider
- `ava-platform` - StandardPlatform
- `ava-tools` - ToolRegistry, register_core_tools
- `ava-types` - Message, Session, Role, AvaError, Result
- `ava-cli-providers` - Optional CLI provider discovery

Used by:
- `ava-agent` - For multi-agent coordination
- `ava-tui` - For workflow execution

## Key Patterns

- **LLM-powered Director**: Director uses an LLM to analyze goals and produce plans (not a code-driven switch statement)
- **Intelligence levels**: Task complexity drives orchestration depth (1=simple, 2=medium, 3=complex+board)
- **Scout-first planning**: Lightweight agents gather context before Director plans
- **Board consensus**: Multi-model voting for complex architectural decisions
- **Sequential-first execution**: Lead manages worker order; parallel only when tasks are independent
- **Domain routing**: Tasks routed to leads based on TaskType or decomposition
- **Budget splitting**: Director splits budget across leads proportional to task complexity; leads split across workers
- **Event streaming**: All worker progress streamed via `PraxisEvent` mpsc channel
- **Phase chaining**: Workflow phases can receive prior phase output
- **Feedback loops**: Reviewer phase triggers iteration if `needs_revision()`
- **ACP protocol**: Agents communicate via structured requests/responses
- **Artifact accumulation**: Workflow outputs stored with spec association
- **Conflict detection**: File-level overlap detection before coordination
- **Tool restriction**: Planner/Reviewer get read-only tools, Coder gets full set

## Agent Naming Convention

- **Director**: "Director" (crown icon, amber color)
- **Leads**: Professional role names — "Backend Lead", "Frontend Lead", "QA Lead", "Research Lead", "Debug Lead", "DevOps Lead", "Fullstack Lead"
- **Workers**: Fun first name + Jr. role — "Pedro (Jr. Backend)", "Sofia (Jr. Backend)", "Luna (Jr. QA)", "Kai (Jr. Research)"
- **Scouts**: No names (ephemeral utility agents)
- **Board members**: Named by model — "Opus (Board)", "Gemini (Board)", "GPT (Board)"
- **Name pool**: Pedro, Sofia, Luna, Kai, Mira, Rio, Ash, Nico, Ivy, Juno, Zara, Leo

## Error Handling (Tiered)

| Level | Error Type | Response |
|-------|-----------|----------|
| 1 | Tool error | Worker retries automatically (up to 2x) |
| 2 | LLM error | Lead switches to fallback model |
| 3 | Logic error | Lead reviews, spawns fix worker |
| 4 | Budget exhausted | Lead asks Director → Director asks user |
| 5 | Catastrophic | Director asks user directly |

## Worktree Strategy

- Each Lead gets its own git worktree; workers share their lead's worktree
- Leads assign specific files to workers to avoid intra-lead conflicts
- When all leads finish, Director spawns a **Merge Worker**
- Clean merge: automatic. Minor conflicts: Merge Worker resolves. Hard conflicts: Director shows user diffs to choose.

## Solo/Team Mode Switching

- **Solo → Team**: User clicks Team button in status bar. Director creates plan, spawns leads and workers.
- **Team → Solo**: Only possible when all agents are stopped. "Stop All" → Director asks "What's on your mind?" → UI collapses to solo mode.
- **Resume Team**: Director reviews what was done, asks "Continue or replan?"
- Mode switches are preserved in session history. Same session throughout.

## Frontend Integration

The desktop frontend (SolidJS) surfaces Praxis through several components (~2,309 LOC across 18 files):

| Component | Purpose |
|-----------|---------|
| `TeamPanel` | Right sidebar: hierarchy view (Director → Leads → Workers) with status badges, progress bars, stop buttons, metrics footer |
| `TeamChatView` | Read-only lead chat: worker activity, tool calls, review actions. Input relays steering through Director. |
| `TeamStatusStrip` | Status bar integration showing team mode state |
| `SubagentCard` | Individual agent card with name, status, turn count |
| `agent-team-bridge` | Bridge layer: Rust `PraxisEvent` → Tauri emit → `useAgent` hook → team store → UI |

### Event Flow

```
Rust (PraxisEvent) → Tauri emit → frontend listener → useAgent hook → team store → TeamPanel/TeamChatView
```

### Director Chat Content

- Plan breakdown with named workers
- Progress updates on state changes only (not play-by-play)
- Lead question relay (colored border card)
- Completion messages
- No tool calls, code, or file diffs (those live in lead/worker views)

## Session Persistence

- Each Praxis run is a session with full history
- Artifacts saved to `.ava/praxis/{session-id}/{lead-name}/`
- Plans saved to `.ava/plans/` for reuse and sharing
- Sessions are resumable if interrupted
- Solo/Team mode switches preserved in session history

## SOTA Research Context (2026-03-18)

### What Competitors Do

| Product | Approach |
|---------|----------|
| **Claude Code** | Inbox JSON messaging, self-claiming tasks, 3-5 teammates max |
| **Cursor** | VM per agent (cloud), worktree per agent (local), Automations (event-driven) |
| **Devin** | Cloud VM per instance, Interactive Planning Checkpoints, Playbooks, Self-Review |
| **Codex** | Container per task, subagent orchestration, TOML custom agents |

### Research Findings

- Google/MIT: Central coordinator improves parallel tasks by 80%, independent agents amplify errors 17x
- Optimal team size: 3-4 specialized roles (Planner, Coder, Reviewer, Tester)
- Structured handoffs (artifacts) outperform free-form chat between agents
- Single strong agent still beats multi-agent on most benchmarks (80.9% vs 72.2% SWE-bench) — but gap is narrowing with current SOTA models

### AVA's Competitive Advantages

- **LLM-powered Director** with intelligence levels (adapts orchestration to task complexity)
- **Scout system** for pre-planning codebase intelligence (unique)
- **Board of Directors** multi-model consensus for complex decisions (unique)
- **Plannotator-style plan editing** with inline comments, reordering, budget per step (unique integration)
- **Sequential execution with Lead-managed ordering** (more controlled than self-claiming)
- **Domain-specific leads** (7 domains) with role-appropriate system prompts
- **Named workers with personality** (fun names, not "worker-1")
- **Peer messaging / mailbox** (only Claude Code has similar)
- **Worktree isolation per lead** (no file conflicts between domains)
- **Solo/Team mode switching** within the same session
