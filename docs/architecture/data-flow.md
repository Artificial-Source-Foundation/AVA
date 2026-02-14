# Data Flow

> How data moves through AVA

---

## Main Flow

```
User Input (Desktop App or CLI)
    │
    ▼
AgentExecutor.run(goal, context)
    │
    ├─→ System prompt (model-specific variant)
    ├─→ LLM call (streaming via provider client)
    ├─→ Parse tool calls from response
    │
    ├─→ [PreToolUse Hook] — can cancel the tool call
    ├─→ Permission check — auto-approve or prompt user
    ├─→ Execute tool
    ├─→ [PostToolUse Hook]
    │
    ├─→ Check doom loop (3x repeated calls → pause)
    ├─→ Check context window → compact if needed
    └─→ Continue loop until done or limit hit
```

## Delegation Flow (Dev Team)

```
Team Lead (main AgentExecutor)
    │
    │ calls delegate_frontend(task_description)
    ▼
WorkerToolWrapper.execute()
    │
    │ creates new AgentExecutor with filtered tools
    │ (no delegate_* tools — prevents recursion)
    ▼
Senior Frontend Lead (Worker AgentExecutor)
    │
    │ can spawn sub-workers via task tool
    ▼
Junior Devs (Subagents with further filtered tools)
    │
    ▼
Results bubble up: Junior → Senior → Team Lead
```

## Tool Hook Flow

```
Tool Call
    │
    ├─→ PreToolUse Hook (JSON stdin/stdout, 30s timeout)
    │   ├─→ approve → continue
    │   ├─→ deny → skip tool
    │   └─→ modify → alter parameters
    │
    ├─→ Execute Tool
    │
    └─→ PostToolUse Hook
        └─→ Can log, audit, or trigger side effects
```

## Context Compression Flow

```
Token count approaches limit
    │
    ▼
CompactionManager selects strategy:
    ├─→ Sliding window (drop oldest messages)
    ├─→ Hierarchical (summarize message groups)
    ├─→ Tool truncation (trim large tool outputs)
    ├─→ Split-point detection (find safe conversation boundaries)
    └─→ Verified summarize (LLM summary + state snapshot)
```

## Plugin Flow

```
Skills: File opened → match glob patterns → inject skill content into context
Commands: User types /command → parse args → execute handler → inject result
Hooks: Tool event → discover matching hooks → execute → apply result
```
