<!-- Last verified: 2026-03-07 -->
# Praxis — 3-Tier Agent Hierarchy

Praxis is AVA's agent hierarchy system. Instead of a flat delegation model (one agent delegates to workers), Praxis implements a 3-tier structure:

```
Director → Leads → Workers
```

Each tier has distinct responsibilities and tool access.

---

## The 3 Tiers

### Director (1 agent)
- Plans and coordinates — **never writes code directly**
- Only has `delegate_*` tools, `question`, and `attempt_completion`
- Delegates to Leads (and the Planner/Architect workers for planning)
- Uses the most capable model for reasoning

### Leads (4 agents)
- Domain specialists who manage their area
- Can delegate to their own subset of Workers
- Have read-only tools + delegate tools for their workers
- Each Lead has a domain: frontend, backend, testing, fullstack

### Workers (8 agents)
- Execute tasks directly — writing code, running tests, debugging
- Cannot delegate — they do the actual work
- Each has specific tools for their specialty
- Can use cheaper/faster models for cost optimization

---

## Built-in Agents (13)

| Tier | Agent | Domain | Description |
|------|-------|--------|-------------|
| Director | `director` | fullstack | Plans and coordinates the team |
| Lead | `frontend-lead` | frontend | Manages frontend development |
| Lead | `backend-lead` | backend | Manages backend development |
| Lead | `qa-lead` | testing | Manages testing and review |
| Lead | `fullstack-lead` | fullstack | Manages cross-cutting work |
| Worker | `coder` | fullstack | Writes and modifies code |
| Worker | `tester` | testing | Writes and runs tests |
| Worker | `reviewer` | fullstack | Reviews code quality |
| Worker | `researcher` | fullstack | Explores codebase |
| Worker | `debugger` | fullstack | Debugs and fixes errors |
| Worker | `architect` | fullstack | Reviews architecture |
| Worker | `planner` | fullstack | Breaks tasks into subtasks |
| Worker | `devops` | devops | Shell commands, build/deploy |

---

## Delegation Flow

```
User sends complex task
    │
    ▼
Director (plans)
    ├─→ delegate_planner → Planner returns TaskPlan
    ├─→ delegate_architect → Architect reviews plan (optional)
    │
    ├─→ delegate_frontend-lead
    │       ├─→ delegate_coder → writes components
    │       └─→ delegate_tester → writes tests
    │
    ├─→ delegate_backend-lead
    │       ├─→ delegate_coder → writes API routes
    │       └─→ delegate_debugger → fixes issues
    │
    └─→ Director reviews results → attempt_completion
```

For simple tasks:
```
Director → delegate_fullstack-lead → delegate_coder → done
```

---

## Per-Agent Model Configuration

Each agent can use a different model and provider. This enables cost optimization:

```typescript
// In agent-defaults.ts or via Settings UI
{
  id: 'researcher',
  tier: 'worker',
  model: 'claude-haiku-4-5',     // cheap model for exploration
  provider: 'anthropic',
}
```

Typical cost optimization strategy:
- **Director**: Most capable model (Opus/Sonnet) — needs strong reasoning
- **Leads**: Mid-tier model (Sonnet) — delegation + light analysis
- **Workers**: Mix based on task:
  - Coder: Sonnet (code quality matters)
  - Researcher: Haiku (just reading files)
  - Tester: Sonnet (test quality matters)
  - Reviewer: Haiku (analysis, read-only)

---

## AgentDefinition Schema

```typescript
interface AgentDefinition {
  id: string           // unique identifier
  name: string         // machine name
  displayName: string  // human-readable label
  description: string
  tier: 'director' | 'lead' | 'worker'
  systemPrompt: string
  tools: string[]      // concrete tool names
  delegates?: string[] // agent IDs this can delegate to
  model?: string       // per-agent model override
  provider?: string    // per-agent provider override
  maxTurns?: number
  maxTimeMinutes?: number
  icon?: string
  domain?: string
  capabilities?: string[]
  isBuiltIn?: boolean
}
```

---

## Creating Custom Agents

### Via Settings UI

1. Open Settings → Agents
2. Click "+ New"
3. Fill in:
   - **Name** and **Description**
   - **Tier**: Worker, Lead, or Director
   - **Tools**: Select which tools this agent can use
   - **Delegates**: (Leads/Director only) Which agents it can delegate to
   - **Model**: Per-agent model selection
   - **Domain**: frontend, backend, testing, devops, fullstack
4. Save

### Via JSON Import

```json
{
  "praxis_agents": [
    {
      "id": "security-auditor",
      "name": "Security Auditor",
      "description": "Reviews code for security vulnerabilities",
      "tier": "worker",
      "tools": ["read_file", "grep", "glob"],
      "domain": "fullstack",
      "capabilities": ["security-analysis"],
      "model": "claude-sonnet-4",
      "systemPrompt": "You are a security auditor. Review code for OWASP Top 10 vulnerabilities..."
    }
  ],
  "version": 1
}
```

---

## Planning Pipeline

For complex tasks, the Director uses a planning chain:

1. **Planner** agent breaks the task into subtasks with file assignments
2. **Architect** agent validates the plan (optional)
3. Director delegates subtasks to the appropriate leads

The Planner returns structured JSON:

```json
{
  "subtasks": [
    {
      "description": "Add login form component",
      "domain": "frontend",
      "files": ["src/components/Login.tsx"],
      "assignTo": "frontend-lead"
    },
    {
      "description": "Add auth API endpoint",
      "domain": "backend",
      "files": ["src/api/auth.ts"],
      "assignTo": "backend-lead"
    }
  ],
  "dependencies": [[1, 0]]
}
```

Dependencies are `[blocker, blocked]` pairs — subtask 0 waits for subtask 1.

---

## Orchestrator (Parallel Execution)

The orchestrator (`commander/src/orchestrator.ts`) automates the planning → execution → aggregation pipeline:

1. **Parse** the TaskPlan into dependency-ordered batches
2. **Execute** independent subtasks in parallel via `Promise.all()`
3. **Retry** failed subtasks with more specific prompts
4. **Aggregate** results into a structured summary

### Configuration

```typescript
interface OrchestratorConfig {
  maxParallelDelegations: number  // Default: 3
  retryFailedSubtasks: boolean    // Default: true
  maxRetries: number              // Default: 1
}
```

### Batch Execution

Subtasks are grouped into batches based on their dependency graph. Within each batch, independent subtasks run in parallel (up to `maxParallelDelegations`). Batches execute sequentially — batch N+1 starts only after batch N completes.

```
Batch 0: [subtask-0, subtask-1]  ← parallel (no deps)
Batch 1: [subtask-2]             ← depends on subtask-0
Batch 2: [subtask-3, subtask-4]  ← depend on subtask-1
```

Deadlock detection: if no subtasks are ready but not all are complete, the orchestrator breaks the cycle by force-completing remaining subtasks.

### Events

- `orchestration:batch-start` — fired before each batch with subtask indices
- `orchestration:batch-complete` — fired after each batch with success/fail counts

---

## Per-Domain Tool Filtering

Each Lead agent gets a specialized tool set matching their domain:

| Lead | Tools |
|------|-------|
| **Frontend Lead** | read_file, write_file, edit, bash, glob, grep, create_file, ls, websearch |
| **Backend Lead** | All tools + lsp_diagnostics, lsp_hover, lsp_definition |
| **QA Lead** | read_file, bash, grep, glob, lsp_diagnostics (read-only + test runner) |
| **Fullstack Lead** | All tools |

Workers inherit their Lead's tool set minus delegation tools.

---

## Task Routing

The router (`commander/src/router.ts`) analyzes task descriptions and maps them to domains using keyword matching:

```
"build a React form"        → frontend  → frontend-lead
"add REST API endpoint"     → backend   → backend-lead
"write unit tests"          → testing   → qa-lead
"set up CI/CD pipeline"     → devops    → fullstack-lead
```

The `analyzeDomain()` function scores keywords across 4 domains (frontend, backend, testing, devops) and picks the highest-scoring match.

---

## Error Recovery

When a worker or lead fails:

1. **Retry** — re-execute with a more specific prompt that includes the error context
2. **Escalate** — if retry fails, escalate to the parent agent with error details
3. **Re-delegate** — parent can assign to a different agent

Configurable via `maxRetries` (default: 1). Emits `delegation:retry` events for monitoring.

---

## Result Aggregation

After multi-lead execution, the aggregator (`commander/src/aggregator.ts`) combines results:

- **Files changed** — deduplicated list across all workers
- **Tests run** — count of tests executed and pass/fail breakdown
- **Issues found** — any errors, warnings, or review findings
- **Duration** — total and per-subtask timing

---

## Extension Integration

Praxis is implemented as the `ava-praxis` extension using the same `ExtensionAPI` as community plugins:

- `registerTool()` — registers `delegate_*` tools
- `registerAgentMode()` — registers the `praxis` mode
- Agent registry — central store for all `AgentDefinition` objects
- Settings sync — custom agents from Settings UI are registered on activation

The `praxis` agent mode:
- `filterTools()` — Director only gets delegate + meta tools
- `systemPrompt()` — Appends the Director prompt with lead/worker docs

---

## Disabling Praxis

To fall back to single-agent mode:

1. **Settings UI**: Disable the Director agent
2. **Settings JSON**: Set `director.enabled: false`
3. **Programmatically**: The agent loop checks `getAgentModes().has('praxis')` — if absent, runs as a single agent with all tools

When Praxis is disabled, AVA behaves like a standard single-agent coding assistant with direct tool access.
