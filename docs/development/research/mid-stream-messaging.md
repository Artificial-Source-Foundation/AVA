# Mid-Stream Messaging: Competitor Analysis & AVA Design

> How AI coding tools handle user input while the agent is running.
> Research date: 2026-03-11

## Implementation Status

**B34 is implemented** (Sprint 60, 2026-03-11) -- code complete, pending manual testing with real agent runs.

Three-tier `MessageQueue` with steering/follow-up/post-complete pipelines. 12 unit tests.

Key files:
- `crates/ava-agent/src/agent_loop/mod.rs` -- steering queue polling between tool calls
- `crates/ava-agent/src/stack.rs` -- follow-up + post-complete outer loops, message channel
- `crates/ava-types/src/lib.rs` -- `MessageTier` enum, `QueuedMessage` type
- `crates/ava-tui/src/app/event_handler.rs` -- routes keybinds to agent channel
- `crates/ava-tui/src/state/input.rs` -- queue state management
- `crates/ava-tui/src/widgets/composer.rs` -- queue display with tier badges
- `crates/ava-tui/src/state/keybinds.rs` -- SubmitSteer, SubmitFollowUp, SubmitPostComplete actions

CLI flags: `--follow-up`, `--later`, `--later-group`. Slash commands: `/later`, `/queue`.
TUI keybinds: Enter=steer, Alt+Enter=follow-up, Ctrl+Alt+Enter=post-complete (while agent running).

## Competitor Landscape

### Claude Code (Pi-Mono) — State of the Art

**Two-queue architecture** (from pi-mono source):

| Queue | Trigger | Injection Point | Behavior |
|-------|---------|----------------|----------|
| **Steering** | Enter during streaming | After current tool completes | Skips remaining tool calls, injects as next turn |
| **Follow-up** | Alt+Enter during streaming | After agent finishes all tools + steering | Waits for natural completion |

- `getSteeringMessages()` polled **after each individual tool execution** in the agent loop
- Remaining tools get `"Skipped due to queued user message."` (marked `isError: true`)
- Both queues support `"all"` or `"one-at-a-time"` delivery modes
- `Alt+Up` dequeues messages back to editor for editing
- Pending messages shown in a visible container below editor
- Escape = hard abort (destructive, discards in-progress work)
- Known bugs: Esc/Ctrl+C unreliable during "Boondoggling" state (#17466, #3455)

### Cursor — Two-Mode System

| Mode | Trigger | Behavior |
|------|---------|----------|
| **Queue** | Alt+Enter | Injected after next tool call (mid-turn) |
| **Interrupt** | Ctrl+Enter | Stops agent immediately, sends message now |

- Default mode configurable in Settings > Chat > Queue messages
- Stop button **reverts all changes** from current turn (controversial — loses work)
- Known bug: queue mode was incorrectly interrupting after tool calls instead of waiting

### Windsurf (Cascade) — Atomic Flows

- Queue only, no steering/interrupt
- Messages processed **after full task completion** (not mid-turn)
- Philosophy: "Don't interrupt Cascade mid-flow" — treats agent runs as atomic
- Checkpoint-based revert system (per-step, not per-file)
- Auto-Continue for 20+ tool call sessions

### Codex CLI — Backtrack System

- Esc primes backtrack mode, double-Esc opens transcript overlay
- Navigate to any previous user message, rollback to that point
- Composer prefilled with selected message for re-editing
- Can type during streaming but cannot submit until turn completes
- `pending_primary_events: VecDeque<Event>` for event buffering

### Aider — Basic Interrupt

- Ctrl+C stops streaming, preserves partial response in history
- Double Ctrl+C within 2s = force exit
- Cannot type during streaming (blocks on `prompt_session.prompt()`)
- No message queue

### OpenCode — AbortSignal

- `AbortController` per session, `abort.throwIfAborted()` per stream event
- Double-Escape within 5s for session abort
- Cannot type during streaming, no queue
- Status model: idle/busy/retry

### Goose — Triple Ctrl+C

- Ctrl+C: clear line → interrupt request → exit session (contextual)
- No mid-stream input, no queue

### Gemini CLI — Feature Requested

- Esc to interrupt (buggy — cancels wrong thing in shell mode)
- No mid-stream input
- `/inject` command proposed for async steering (#17197)

## Comparison Matrix

| Tool | Type During Stream | Message Queue | Steering (Priority) | Follow-up (Deferred) | Post-Complete | Interrupt | Rollback |
|------|-------------------|---------------|---------------------|---------------------|---------------|-----------|----------|
| **Pi/CC** | Yes | Dual | Enter (after tool) | Alt+Enter (after done) | No | Esc (destructive) | No |
| **Cursor** | Yes | Yes | Ctrl+Enter (immediate) | Alt+Enter (after tool) | No | Stop (reverts all) | No |
| **Windsurf** | Yes | Yes | No | Enter (after task) | No | Checkpoint revert | Per-step |
| **Codex CLI** | Yes (no submit) | Event queue | No | No | No | Esc backtrack | Transcript rollback |
| **Aider** | No | No | No | No | No | Ctrl+C (partial kept) | No |
| **OpenCode** | No | No | No | No | No | AbortSignal | No |
| **Goose** | No | No | No | No | No | Ctrl+C (3 states) | No |
| **AVA (current)** | Yes (no submit) | No | No | No | No | Ctrl+C (abort) | No |

## AVA Design: Three-Tier Message System

AVA will implement a **three-tier message system** — going beyond all competitors by adding a third tier that no tool currently offers.

### Tier 1: Steering (Priority)

> "I need the agent to read this NOW"

- **Trigger**: Enter while agent is running
- **Injection point**: After current tool execution completes
- **Behavior**: Remaining queued tool calls are skipped. Steering message injected as next user turn before the LLM call.
- **Visual**: Message appears in queue area with priority badge
- **Use case**: "Stop, wrong file — look at auth.rs instead" / "Use the grep tool for this"

### Tier 2: Follow-up (After Task)

> "When you're done with this, also do X"

- **Trigger**: Alt+Enter while agent is running
- **Injection point**: After agent completes current task (no more tools, no steering)
- **Behavior**: Waits for natural agent completion, then injects as next user turn. Agent continues with a new turn.
- **Visual**: Message in queue area with "queued" badge
- **Use case**: "After this, also check the tests" / "When done, commit the changes"

### Tier 3: Post-Complete (After AI Stops)

> "For after everything is done and the AI has said its final word"

- **Trigger**: Ctrl+Alt+Enter (or `/later` prefix) while agent is running
- **Injection point**: After the agent has emitted `Complete` event and stopped responding entirely — not after a tool batch, not after a task, but after the agent has said "I'm done" and the conversation is back to idle.
- **Behavior**: Stored in a **grouped pipeline queue**. Messages are assigned to numbered groups (stages). Group 1 runs first; when all Group 1 messages complete, Group 2 runs, etc. New messages submitted during a group's execution go into the next group automatically. If a group fails, the AI can retry the group (mini-loop).
- **Grouping**: Users can build the full pipeline at any time — before, during, or after agent work. `Ctrl+Alt+Enter` adds to current group, `Ctrl+Alt+Enter` again immediately after increments to next group (double-tap = new group). Explicit group via `/later 2 "message"` (specific group number). Groups can be pre-built entirely upfront: queue G1, G2, G3 all before the agent even starts its main task.
- **Visual**: Message in queue area with "later" badge + group number, dimmed. Pipeline visualization: `[G1: review + compile] → [G2: commit]`
- **Use case**:
  - G1: "Ask a code reviewer to check the code" + "Also check that everything compiles"
  - G2 (submitted while G1 runs): "Commit everything after group 1 is done and works nicely"
  - If G1 fails, AI retries G1 before moving to G2
- **Novel**: No competitor has this. This is a user-defined workflow pipeline injected mid-conversation. Groups act as stages — sequential, retriable, dynamically extendable.

### Queue Management

- All three tiers visible in a queue area below the composer
- `Alt+Up` / `/dequeue` to pull messages back to editor for editing
- Queue count shown in status bar: `[2 queued]`
- `/queue` command to view/reorder/delete pending messages
- Messages removable from queue before delivery

### Interrupt/Cancel (Orthogonal)

- **Esc**: Soft cancel — stops streaming, preserves partial response in history
- **Double Esc**: Opens rewind modal (existing B22 implementation)
- **Ctrl+C**: Hard abort — cancels all pending tools, clears steering queue, keeps follow-up and post-complete queues

### Agent Loop Changes

```
Pipeline loop: for each group in post_complete_groups (G1, G2, G3...)
  Inject all messages from current group as user turns
  Middle loop (follow-up): runs while follow_up_queue has messages
    Inner loop (steering): processes tool calls
      - Execute tool
      - Check steering_queue → if messages: skip remaining tools, inject
      - Continue until no more tools or steering
    Check follow_up_queue → if messages: inject, continue middle loop
  Agent says "complete" for this group
    → Any new messages submitted during this group → next group
    → If agent reports failure → offer retry of current group
    → If success → advance to next group
  No more groups: truly done
```

**Post-complete group state machine:**
```
                    ┌─────────┐
              ┌────→│ Running │────→ Success ──→ Next Group
              │     └─────────┘                    │
              │          │                         ▼
              │        Fail                   More groups?
              │          │                    Yes → loop
              │          ▼                    No  → Done
              │     ┌─────────┐
              └─────│  Retry  │
                    └─────────┘
```

### Implementation Scope

**Backend** (`crates/ava-agent/`):
- `MessageQueue` struct with steering `VecDeque`, follow-up `VecDeque`, and post-complete `BTreeMap<u32, Vec<String>>` (group_id → messages)
- `AgentLoop` polls `steering_queue` after each tool execution
- `AgentStack::run()` middle loop for follow-ups, outer pipeline loop for post-complete groups
- `mpsc::Sender<QueuedMessage>` channel from TUI to agent loop (checked via `try_recv`)
- `QueuedMessage { text: String, tier: MessageTier }` where `MessageTier::PostComplete(group_id)`
- `PostCompleteGroup` state: `Pending | Running | Succeeded | Failed(retry_count)`
- Auto-increment group_id: messages submitted while no group runs → current group; while group runs → next group

**TUI** (`crates/ava-tui/`):
- Composer: Enter (steer), Alt+Enter (follow-up), Ctrl+Alt+Enter (post-complete)
- Queue display widget below composer
- Status bar queue count
- `/queue` command for management
- `/dequeue` or Alt+Up to edit pending messages

**Headless CLI**:
- `--queue-message "text"` flag for scripted follow-ups
- Stdin reader for interactive mid-stream input in headless mode

### Key Files to Modify

| File | Change |
|------|--------|
| `crates/ava-agent/src/agent_loop/mod.rs` | Poll steering queue between tool calls |
| `crates/ava-agent/src/stack.rs` | Follow-up + post-complete outer loops, message channel |
| `crates/ava-types/src/lib.rs` | `MessageTier` enum, `QueuedMessage` type |
| `crates/ava-tui/src/app/mod.rs` | Key bindings for three submission modes |
| `crates/ava-tui/src/app/event_handler.rs` | Route queued messages to agent channel |
| `crates/ava-tui/src/state/input.rs` | Queue state management |
| `crates/ava-tui/src/widgets/composer.rs` | Queue display below input |
| `crates/ava-tui/src/state/keybinds.rs` | New actions: SubmitSteer, SubmitFollowUp, SubmitPostComplete |

### References

- Pi-mono agent loop: `docs/reference-code/pi-mono/packages/agent/src/agent-loop.ts`
- Pi-mono agent queues: `docs/reference-code/pi-mono/packages/agent/src/agent.ts`
- Pi-mono keybindings: `docs/reference-code/pi-mono/packages/coding-agent/src/core/keybindings.ts`
- Cursor 1.4 changelog: queue vs interrupt modes
- Competitive analysis: `docs/development/research/competitive-analysis-2026-03.md` (CG-05)
- Onur Solmaz analysis: "Agentic coding tools should give more control over message queueing"
