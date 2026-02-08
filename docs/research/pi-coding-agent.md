# PI Coding Agent — Research Notes

> Analysis of PI Coding Agent architecture and philosophy, with comparison to Estela's approach.

## Overview

PI Coding Agent is a minimalist AI coding assistant built on the principle of radical simplicity. Where Estela uses a multi-agent delegation hierarchy (Team Lead, Senior Leads, Junior Devs), PI takes the opposite approach: a single agent with minimal tools that can self-extend at runtime.

**Repository**: Open source, installed via npm
**Architecture**: Single-agent, tool-minimal, cross-provider

---

## Core Philosophy: Radical Minimalism

PI's defining characteristic is its extreme tool reduction. While most coding agents (Claude Code, Cursor, Estela) provide 15-25+ tools, PI ships with only **4 core tools**:

| Tool | Purpose |
|------|---------|
| `read` | Read file contents |
| `write` | Write/create files |
| `shell` | Execute shell commands |
| `search` | Search codebase |

Everything else — editing, diffing, testing, linting, browsing — is done through these 4 primitives. File editing is `read` + `write`. Testing is `shell` with the test runner. This forces the LLM to compose operations from primitives rather than relying on specialized tools.

### Implications for Estela

Estela's 22 tools include specialized ones like `edit` (8 fuzzy strategies), `codesearch`, `batch`, `multiedit`, `browser`. These provide better UX and fewer LLM errors at the cost of complexity. The question is: does tool specialization justify the prompt engineering overhead?

**Takeaway**: Consider a "minimal mode" for Estela where only core tools are exposed, reducing token usage and latency for simple tasks.

---

## Cross-Provider Context Handoff

PI's most innovative feature is **mid-session provider switching**. Users can change the LLM provider (e.g., switch from Claude to GPT to Gemini) while keeping the full conversation context. The session state is provider-agnostic — stored as simple message arrays that get formatted per-provider on each turn.

### How It Works

1. Session state stores messages in a universal format (role + content)
2. Each provider adapter formats messages to its API spec on send
3. User can switch providers at any point in the conversation
4. Context follows seamlessly — new provider sees all previous messages

### Implications for Estela

Estela already has multi-provider support (14 providers) but provider switching is session-level. Adding mid-conversation provider switching would be a differentiator. Implementation path:
- Store messages in provider-agnostic format (already done in database)
- Add "Switch model" command in chat
- Format history for new provider's API on next turn

---

## Session Branching Tree

PI implements sessions as a **tree structure** rather than a linear history. Users can fork a session at any point, creating branches that share history up to the fork point but diverge after.

### Structure

```
Session Root
├── Message 1
├── Message 2
│   ├── Branch A: Message 3a → 4a → 5a
│   └── Branch B: Message 3b → 4b
└── Message 3 (continued from 2 without branching)
```

Users can navigate between branches, compare outcomes, and merge results. This is particularly useful for:
- Trying different approaches to the same problem
- A/B testing prompts
- Rolling back without losing the alternative attempt

### Implications for Estela

Estela has session fork (just added in Session 37) but it's a full copy, not a tree structure. A tree-based session model would:
- Save storage (shared prefix messages aren't duplicated)
- Enable visual branch comparison in the UI
- Support "try both approaches" workflows

**Deferred**: Full tree-based sessions could be a Phase 2/3 feature. Current fork is sufficient for MVP.

---

## Self-Extension: Agent Creates Its Own Tools

PI allows the agent to create new tools at runtime by writing tool definition files. When the agent encounters a repeated pattern, it can codify it as a reusable tool:

```
Agent: "I keep needing to check TypeScript types. Let me create a tool for that."
→ Creates a `check-types` tool that runs `npx tsc --noEmit`
→ Tool is available for the rest of the session
```

### Implications for Estela

This maps to Estela's plugin/skill system. The key insight is **runtime skill creation** — letting the agent define new skills during a session rather than requiring pre-installation. This could work through:
- Agent writes a TOML command definition
- Estela's command system hot-reloads it
- Tool is available immediately

---

## No Commander Pattern

PI explicitly avoids multi-agent delegation. There's no planner/executor split, no task decomposition into sub-agents. The single agent handles everything sequentially.

### Tradeoffs vs Estela

| Aspect | PI (Single Agent) | Estela (Multi-Agent) |
|--------|-------------------|---------------------|
| Simplicity | Much simpler | Complex hierarchy |
| Parallelism | None | Teams work in parallel |
| Context usage | One context window | Distributed across agents |
| Cost | Lower (one LLM call chain) | Higher (multiple agents) |
| Complex tasks | Struggles with large tasks | Decomposes naturally |
| Debugging | Easy to follow | Need team panel to track |
| Specialization | General-purpose | Domain-specific prompts |

PI's approach works well for individual developers on focused tasks. Estela's multi-agent approach targets larger, multi-domain tasks where parallelism and specialization provide value.

---

## Lessons for Estela

1. **Mid-conversation provider switching** — High-value, medium-effort feature. Users could switch from Opus (expensive, smart) to Haiku (cheap, fast) mid-task.

2. **Session branching UI** — The tree visualization for session forks would be a compelling differentiator. Show branches visually in the sidebar.

3. **Minimal mode** — Offer a "4-tool mode" for simple tasks that reduces token overhead and cost. Power users could toggle between full and minimal tool sets.

4. **Runtime skill creation** — Let the agent create temporary tools during a session. Bridges PI's self-extension with Estela's plugin system.

5. **Provider-agnostic session state** — Ensure messages are stored in a format that doesn't assume a specific provider. This is mostly already done.

---

## Summary

PI represents the "Unix philosophy" end of the AI coding agent spectrum: do one thing well, compose from primitives, stay simple. Estela sits at the other end: rich tooling, team delegation, visual hierarchy. Both approaches have merit. The key takeaways are features that could enhance Estela without changing its core architecture: provider switching, session branching, and minimal mode.
