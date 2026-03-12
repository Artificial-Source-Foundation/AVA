# Sprint 65: Agent Coordination Backend

## Goal

Improve backend coordination primitives for agents, workflows, and artifacts before returning to heavier UI-facing orchestration work.

## Backlog Items

| ID | Priority | Name | Outcome |
|----|----------|------|---------|
| B50 | P3 | Agent team peer communication | Let parallel agents exchange structured information laterally |
| B59 | P3 | Agent artifacts system | Persist agent-generated artifacts for review/reuse |
| B49 | P3 | Spec-driven development | Add structured specs/tasks as backend workflow objects |
| B76 | P3 | Agent Client Protocol (ACP) | Define a backend protocol boundary for external agent interoperability |

## Why This Sprint

- Strengthens backend workflow orchestration before more TUI-facing team UX
- Gives future Praxis work more durable coordination primitives
- Creates reusable backend concepts instead of one-off UI flows

## Scope

### 1. Peer communication (`B50`)

- Define safe mailbox/message primitives between workers
- Keep conflict resolution explicit and auditable

### 2. Artifacts (`B59`)

- Store structured agent outputs as first-class backend artifacts
- Support later review, reuse, and handoff

### 3. Spec workflow objects (`B49`)

- Model requirements/design/tasks as backend workflow entities
- Keep first iteration compatible with existing Plan mode

### 4. Protocol boundary (`B76`)

- Define a small, testable protocol surface for agent interoperability
- Avoid premature transport sprawl in the first slice

## Non-Goals

- No new default tools
- No final TUI Praxis composer work
- No plugin marketplace work

## Suggested Execution Order

1. `B49` Spec-driven development
2. `B59` Agent artifacts system
3. `B50` Agent team peer communication
4. `B76` ACP

## Verification

- Backend workflow tests for handoff/artifact/state transitions
- Protocol conformance tests for any ACP slice
- Multi-agent coordination tests that avoid UI dependencies

## Exit Criteria

- Specs and artifacts exist as backend primitives
- Agents can exchange limited structured data safely
- ACP has a concrete, documented first slice
