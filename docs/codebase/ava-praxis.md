# ava-praxis

> Multi-agent orchestration with domain-specific leads (Director pattern).

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

- **Director pattern**: User → Director → Leads → Workers hierarchy
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
- Sessions are resumable if interrupted
- Solo/Team mode switches preserved in session history

## Competitive Differentiators

- Domain-specific leads (7 domains) with role-appropriate system prompts
- Worker naming with personality (fun names, not "worker-1")
- Lead-level code review before reporting to Director
- Worktree isolation per lead (no file conflicts between domains)
- Merge Worker for clean integration
- Steer workers through Director relay
- Solo/Team mode switching within the same session
