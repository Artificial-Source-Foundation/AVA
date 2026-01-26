# Delta9 - OpenCode Plugin Specification

> **Tagline**: "Strategic AI Coordination for Mission-Critical Development"

## Overview

Delta9 is an OpenCode plugin that implements a hierarchical, multi-agent system with strategic planning capabilities. Unlike traditional single-agent approaches, Delta9 uses a **Commander + Council + Operators** architecture that separates planning from execution, maintains mission state across compactions, and verifies all work against acceptance criteria.

---

## Core Philosophy

### Problems We Solve

| Problem | Current Tools | Delta9 Solution |
|---------|---------------|-----------------|
| Context pollution kills planning | Plan and execute in same context | Commander context protected, Operators disposable |
| Plans lost after compaction | In-context plans disappear | mission.json persists externally |
| No verification step | Trust self-reports | Dedicated Validator agent |
| Single model blind spots | One model's perspective | Council of heterogeneous models |
| Goal drift over time | Agents forget original mission | Mission state anchors all work |
| Token waste on research | Main agent scans codebase | Cheap Scout agents do recon |

### Design Principles

1. **Separation of Concerns**: Planning, execution, and verification are distinct phases with dedicated agents
2. **Protected Context**: Commander never accumulates implementation details
3. **Heterogeneous Intelligence**: Different models for different strengths
4. **Verified Completion**: Nothing marked done without Validator approval
5. **Graceful Degradation**: Works with any model combination user has access to
6. **Seamless Integration**: Replaces default agents, no commands required for normal use

---

## Architecture

### High-Level Flow

```
┌─────────────────────────────────────────────────────────────────────────┐
│                         USER INPUT                                       │
│                  "Build authentication system"                           │
└─────────────────────────────────────────────────────────────────────────┘
                                  │
                                  ▼
┌─────────────────────────────────────────────────────────────────────────┐
│                      PLANNING PHASE                                      │
│  ┌─────────────────────────────────────────────────────────────────┐   │
│  │                       COMMANDER                                  │   │
│  │                   (Lead Planner)                                 │   │
│  │                                                                  │   │
│  │  1. Analyze request complexity                                   │   │
│  │  2. Dispatch Scout for codebase recon                           │   │
│  │  3. Dispatch Intel for research                                  │   │
│  │  4. Convene Council (if complex)                                │   │
│  │  5. Synthesize into mission.json                                │   │
│  └─────────────────────────────────────────────────────────────────┘   │
│                           │                                             │
│           ┌───────────────┴───────────────┐                            │
│           ▼                               ▼                             │
│  ┌─────────────────┐           ┌─────────────────┐                     │
│  │     SCOUT       │           │     INTEL       │                     │
│  │ (Codebase Scan) │           │   (Research)    │                     │
│  │     Haiku       │           │    GLM 4.7      │                     │
│  └────────┬────────┘           └────────┬────────┘                     │
│           │                             │                               │
│           └─────────────┬───────────────┘                              │
│                         ▼                                               │
│  ┌─────────────────────────────────────────────────────────────────┐   │
│  │                 THE COUNCIL - THE DELTA TEAM                    │   │
│  │              (XHIGH mode: each has recon access)                 │   │
│  │                                                                  │   │
│  │  ┌──────────┐  ┌──────────┐  ┌──────────┐  ┌──────────┐        │   │
│  │  │  CIPHER  │  │  VECTOR  │  │  PRISM   │  │   APEX   │        │   │
│  │  │Strategist│  │ Analyst  │  │ Creative │  │ Optimizer│        │   │
│  │  │ Temp 0.2 │  │ Temp 0.4 │  │ Temp 0.6 │  │ Temp 0.3 │        │   │
│  │  └──────────┘  └──────────┘  └──────────┘  └──────────┘        │   │
│  │                                                                  │   │
│  │  Each provides: recommendation, confidence, caveats              │   │
│  └─────────────────────────────────────────────────────────────────┘   │
│                         │                                               │
│                         ▼                                               │
│  ┌─────────────────────────────────────────────────────────────────┐   │
│  │              COMMANDER SYNTHESIZES                               │   │
│  │                                                                  │   │
│  │  • Weighs confidence scores                                      │   │
│  │  • Identifies consensus vs conflicts                             │   │
│  │  • Resolves disagreements                                        │   │
│  │  • Produces mission.json                                         │   │
│  └─────────────────────────────────────────────────────────────────┘   │
│                         │                                               │
│                         ▼                                               │
│                 ┌──────────────┐                                        │
│                 │    USER      │                                        │
│                 │   APPROVAL   │                                        │
│                 └──────────────┘                                        │
└─────────────────────────────────────────────────────────────────────────┘
                                  │
                                  ▼
┌─────────────────────────────────────────────────────────────────────────┐
│                      EXECUTION PHASE                                     │
│                                                                         │
│  ┌─────────────────────────────────────────────────────────────────┐   │
│  │                       COMMANDER                                  │   │
│  │                  (Mission Controller)                            │   │
│  │                                                                  │   │
│  │  • Reads mission.json                                           │   │
│  │  • Dispatches tasks to Operators                                │   │
│  │  • Routes to specialists (UI-Ops, QA, etc.)                     │   │
│  │  • Receives completion reports                                   │   │
│  │  • Updates mission state                                        │   │
│  │  • NEVER writes code                                            │   │
│  └─────────────────────────────────────────────────────────────────┘   │
│                           │                                             │
│       ┌───────────────────┼───────────────────┐                        │
│       ▼                   ▼                   ▼                         │
│  ┌──────────┐       ┌──────────┐       ┌──────────┐                    │
│  │ OPERATOR │       │ OPERATOR │       │ OPERATOR │                    │
│  │    #1    │       │    #2    │       │    #N    │                    │
│  │ Sonnet 4 │       │ Sonnet 4 │       │ Sonnet 4 │                    │
│  │          │       │          │       │          │                    │
│  │ Task A   │       │ Task B   │       │ Task N   │                    │
│  └────┬─────┘       └────┬─────┘       └────┬─────┘                    │
│       │                  │                  │                           │
│       └──────────────────┼──────────────────┘                          │
│                          ▼                                              │
│  ┌─────────────────────────────────────────────────────────────────┐   │
│  │                      VALIDATOR                                   │   │
│  │                   (Haiku 4.5)                                    │   │
│  │                                                                  │   │
│  │  Input: task description, acceptance criteria, git diff          │   │
│  │  Output: PASS / FIXABLE (with feedback) / FAIL (with reason)    │   │
│  └─────────────────────────────────────────────────────────────────┘   │
│                          │                                              │
│          ┌───────────────┼───────────────┐                             │
│          ▼               ▼               ▼                              │
│       PASS           FIXABLE          FAIL                              │
│          │               │               │                              │
│          ▼               ▼               ▼                              │
│    Mark done      Same Operator     Commander                           │
│    Next task      + feedback        re-evaluates                        │
│                   (max 2 retries)   (replan/skip)                       │
│                                                                         │
└─────────────────────────────────────────────────────────────────────────┘
```

---

## Agent Roster

### Command Layer

| Agent | Role | Default Model | Description |
|-------|------|---------------|-------------|
| **Commander** | Lead planner & orchestrator | User's choice (Opus 4.5 recommended) | Convenes Council, synthesizes plans, manages mission state. Never writes code. |

### Council Layer (Planning Phase) - The Delta Team

Each Oracle has a distinct personality, temperature, and specialty. Users configure which AI model powers each personality in `delta9.json`.

| Codename | Role | Specialty | Temp | Default Model | What They Catch |
|----------|------|-----------|------|---------------|-----------------|
| **CIPHER** | The Strategist | Architecture | 0.2 | Opus 4.5 | Deep reasoning, system design, long-term implications |
| **VECTOR** | The Analyst | Logic | 0.4 | GPT-4o | Edge cases, correctness, error handling, patterns |
| **PRISM** | The Creative | UI/UX | 0.6 | Gemini 2.0 Flash | User experience, alternatives, accessibility, elegance |
| **APEX** | The Optimizer | Performance | 0.3 | DeepSeek Chat | Time/space complexity, memory, bottlenecks, scalability |

**Key Design**: Codenames are stable, models are user-configurable. Each Oracle's personality and specialty remain constant regardless of which model powers them.

### Execution Layer

| Agent | Role | Default Model | Description |
|-------|------|---------------|-------------|
| **Operator** | Primary worker/executor | Sonnet 4 | Does actual implementation. Can invoke support agents. |
| **Validator** | Verification/QA | Haiku 4.5 | Reviews work against acceptance criteria. No code writing. |
| **Patcher** | Quick fixes | Haiku 4.5 | Small targeted fixes when Validator returns FIXABLE. |

### Support Layer (Invokable by Any Agent)

| Agent | Role | Default Model | When Used |
|-------|------|---------------|-----------|
| **Scout** | Fast codebase search | Haiku / Grok | Grep, file discovery, pattern matching |
| **Intel** | Research & documentation | GLM 4.7 / Sonnet | Docs lookup, GitHub search, examples |
| **Strategist** | Mid-execution advice | GPT 5.2 | When Operator hits wall, needs guidance |
| **UI-Ops** | Frontend specialist | Gemini Pro | UI components, styling, accessibility |
| **Scribe** | Documentation writer | Gemini Flash | READMEs, API docs, comments |
| **Optics** | Vision/multimodal | Gemini Flash | Image analysis, PDF reading, diagrams |
| **QA** | Test writer | Sonnet 4 | Unit tests, integration tests |

---

## Council Modes

| Mode | Triggered By | Council Composition | Use Case |
|------|--------------|---------------------|----------|
| **NONE** | `--council=none`, simple tasks | Commander only | Typo fixes, tiny changes |
| **QUICK** | `--council=quick`, moderate tasks | Commander + 1 Oracle | Add a page, small feature |
| **STANDARD** | Default for complex tasks | Commander + all configured Oracles | New systems, integrations |
| **XHIGH** | `--council=xhigh`, critical tasks | Commander + Oracles with recon access | Core refactors, architecture changes |

### XHIGH Mode Detail

In XHIGH mode, each Oracle can invoke Scout and Intel independently:

```
Commander: "Council, investigate this mission. You have recon access."

CIPHER (The Strategist): "Scout, show me authentication-related files"
  → Scout returns file list and snippets
  → CIPHER forms architectural opinion with actual code context

VECTOR (The Analyst): "Intel, find OAuth best practices for Node.js 2025"
  → Intel searches docs and GitHub
  → VECTOR forms opinion validating correctness against standards

PRISM (The Creative): "Scout, show me existing UI components"
  → Scout returns component inventory
  → PRISM forms opinion on user experience and alternatives

APEX (The Optimizer): "Scout, show me database query patterns"
  → Scout returns query code
  → APEX identifies performance bottlenecks and optimization opportunities
```

---

## Mission State Management

### File Structure

```
.delta9/
├── mission.json          # Current mission state (source of truth)
├── mission.md            # Auto-generated human-readable view
├── history.jsonl         # Append-only log of all actions
├── memory.json           # Cross-session learning (optional)
└── checkpoints/          # Git-based rollback points
    ├── obj-1-complete/
    └── obj-2-complete/
```

### mission.json Schema

```json
{
  "$schema": "https://delta9.dev/mission.schema.json",
  "id": "mission_abc123",
  "created_at": "2025-01-23T10:30:00Z",
  "updated_at": "2025-01-23T11:45:00Z",
  
  "mission": "Build REST API for user authentication with OAuth",
  "status": "in_progress",
  
  "council_mode": "standard",
  "council_summary": {
    "consensus": ["Use NextAuth.js v5", "Database sessions over JWT"],
    "disagreements_resolved": ["Task count: 6 (merged from 5 and 7)"],
    "confidence_avg": 0.87
  },
  
  "objectives": [
    {
      "id": "obj_1",
      "description": "Set up project structure",
      "status": "completed",
      "checkpoint": "obj-1-complete",
      "tasks": [
        {
          "id": "task_1_1",
          "description": "Initialize Next.js 14 project with TypeScript",
          "status": "completed",
          "assigned_to": "operator",
          "worker_session": "session_xyz123",
          "attempts": 1,
          "acceptance_criteria": [
            "package.json exists with Next.js 14",
            "tsconfig.json properly configured",
            "src/app directory structure created"
          ],
          "validation": {
            "status": "passed",
            "validated_at": "2025-01-23T10:45:00Z",
            "summary": "All criteria met"
          },
          "files_changed": ["package.json", "tsconfig.json", "src/app/layout.tsx"],
          "tokens_used": 12450,
          "cost": 0.037
        }
      ]
    },
    {
      "id": "obj_2",
      "description": "Implement authentication endpoints",
      "status": "in_progress",
      "tasks": [
        {
          "id": "task_2_1",
          "description": "Create POST /auth/register endpoint",
          "status": "in_progress",
          "assigned_to": "operator",
          "routed_to": null,
          "acceptance_criteria": [
            "Endpoint accepts email and password",
            "Validates input with Zod",
            "Hashes password with bcrypt",
            "Creates user in database",
            "Returns JWT token"
          ]
        }
      ]
    }
  ],
  
  "budget": {
    "limit": 5.00,
    "spent": 1.24,
    "breakdown": {
      "council": 0.67,
      "operators": 0.42,
      "validators": 0.08,
      "support": 0.07
    }
  },
  
  "dependencies": {
    "task_2_1": ["task_1_1", "task_1_2"],
    "task_3_1": ["task_2_1", "task_2_2", "task_2_3"]
  }
}
```

### mission.md (Auto-generated)

```markdown
# Mission: Build REST API for user authentication with OAuth

**Status**: In Progress | **Budget**: $1.24 / $5.00 (25%)

## Council Summary
- **Mode**: Standard (3 Oracles)
- **Consensus**: NextAuth.js v5, database sessions
- **Confidence**: 87%

---

## Progress: 4/9 tasks (44%)

### ✅ Objective 1: Set up project structure
- [x] Initialize Next.js 14 project with TypeScript
- [x] Configure ESLint and Prettier
- [x] Set up folder structure

### 🔄 Objective 2: Implement authentication endpoints
- [x] Create POST /auth/register
- [ ] Create POST /auth/login ← **IN PROGRESS**
- [ ] Create POST /auth/refresh-token

### ⏳ Objective 3: Add middleware and validation
- [ ] JWT validation middleware
- [ ] Input validation with Zod
```

---

## Smart Task Routing

Commander analyzes each task and routes to optimal agent:

| Task Pattern | Detected Signals | Routed To |
|--------------|------------------|-----------|
| UI/Frontend | "component", "form", "button", "style", "CSS" | UI-Ops |
| Testing | "test", "spec", "coverage", "mock" | QA |
| Documentation | "README", "docs", "comment", "JSDoc" | Scribe |
| Performance | "optimize", "cache", "performance", "speed" | Operator + Strategist |
| Complex logic | "algorithm", "complex", "architecture" | Operator (Opus if critical) |
| Simple changes | "typo", "rename", "fix", "small" | Patcher |

---

## Configuration

### File Locations

```
~/.config/opencode/delta9.json     # User global config
.delta9/config.json                 # Project-specific overrides
```

### Full Configuration Schema

```json
{
  "$schema": "https://delta9.dev/config.schema.json",
  
  "commander": {
    "model": "anthropic/claude-opus-4-5",
    "temperature": 0.3,
    "planning_model": "anthropic/claude-opus-4-5",
    "dispatch_model": "anthropic/claude-sonnet-4"
  },
  
  "council": {
    "enabled": true,
    "default_mode": "standard",
    "auto_detect_complexity": true,
    "members": [
      {
        "name": "Oracle-Claude",
        "model": "anthropic/claude-opus-4-5",
        "enabled": true,
        "specialty": "architecture"
      },
      {
        "name": "Oracle-GPT",
        "model": "openai/gpt-5.2-codex-xhigh",
        "enabled": true,
        "specialty": "logic"
      },
      {
        "name": "Oracle-Gemini",
        "model": "google/gemini-3-pro",
        "enabled": true,
        "specialty": "ui"
      },
      {
        "name": "Oracle-DeepSeek",
        "model": "deepseek/deepseek-v3",
        "enabled": false,
        "specialty": "performance"
      }
    ],
    "parallel": true,
    "require_consensus": false,
    "min_responses": 2,
    "timeout_seconds": 120
  },
  
  "operators": {
    "default_model": "anthropic/claude-sonnet-4",
    "complex_model": "anthropic/claude-opus-4-5",
    "max_parallel": 3,
    "retry_limit": 2,
    "can_invoke_support": true
  },
  
  "validator": {
    "model": "anthropic/claude-haiku-4-5",
    "strict_mode": false,
    "run_tests": true,
    "check_linting": true
  },
  
  "patcher": {
    "model": "anthropic/claude-haiku-4-5",
    "max_lines": 50
  },
  
  "support": {
    "scout": {
      "model": "anthropic/claude-haiku-4-5",
      "timeout_seconds": 30
    },
    "intel": {
      "model": "zai/glm-4.7",
      "sources": ["docs", "github", "web"]
    },
    "strategist": {
      "model": "openai/gpt-5.2",
      "invoke_threshold": "complex"
    },
    "ui_ops": {
      "model": "google/gemini-3-pro",
      "style_system": "tailwind"
    },
    "scribe": {
      "model": "google/gemini-3-flash",
      "format": "markdown"
    },
    "optics": {
      "model": "google/gemini-3-flash"
    },
    "qa": {
      "model": "anthropic/claude-sonnet-4",
      "framework_detect": true
    }
  },
  
  "mission": {
    "auto_checkpoint": true,
    "checkpoint_on": "objective_complete",
    "state_dir": ".delta9",
    "history_enabled": true
  },
  
  "memory": {
    "enabled": true,
    "learn_from_failures": true,
    "learn_from_successes": true,
    "max_entries": 1000
  },
  
  "budget": {
    "enabled": true,
    "default_limit": 10.00,
    "warn_at": 0.7,
    "pause_at": 0.9,
    "track_by_agent": true
  },
  
  "notifications": {
    "enabled": false,
    "discord_webhook": null,
    "slack_webhook": null,
    "on_events": ["mission_complete", "validation_failed", "budget_warning", "needs_input"]
  },
  
  "ui": {
    "show_progress": true,
    "show_cost": true,
    "verbose_logs": false
  },
  
  "seamless": {
    "replace_build": true,
    "replace_plan": true,
    "keyword_detection": true,
    "keywords": {
      "council_xhigh": ["thorough", "careful", "critical", "important"],
      "council_none": ["quick", "just", "simple", "fast"],
      "force_plan": ["plan", "design", "architect", "strategy"]
    }
  }
}
```

---

## Seamless Integration

Delta9 replaces OpenCode's default agents for frictionless operation:

### Agent Replacement

```javascript
// Plugin registers these replacements
{
  agents: {
    // Replace default "build" with Commander in execution mode
    "build": commanderExecutionAgent,
    
    // Replace default "plan" with Commander in planning mode
    "plan": commanderPlanningAgent,
    
    // Keep originals available if needed
    "opencode-build": originalBuildAgent,
    "opencode-plan": originalPlanAgent
  }
}
```

### Automatic Complexity Detection

```
User: "fix the typo in the footer"
→ Complexity: LOW
→ Council: NONE
→ Direct execution by Patcher

User: "add a user profile page"
→ Complexity: MEDIUM
→ Council: QUICK (1 Oracle)
→ Standard execution

User: "refactor the entire auth system to use Clerk"
→ Complexity: HIGH
→ Council: STANDARD (all Oracles)
→ Full mission planning

User: "redesign the database schema for multi-tenancy"
→ Complexity: CRITICAL
→ Council: XHIGH (Oracles with recon)
→ Full mission + checkpoints
```

### Keyword Detection

```
"just fix the button" → council=none
"carefully plan the migration" → council=xhigh
"design the architecture for..." → force planning phase
```

---

## Commands (Power Users)

While normal use is seamless, power users can use commands:

| Command | Description |
|---------|-------------|
| `/delta9 mission "description"` | Start new mission with explicit planning |
| `/delta9 mission --council=xhigh` | Force XHIGH council mode |
| `/delta9 mission --dry-run` | Preview plan without execution |
| `/delta9 deploy` | Begin/resume execution |
| `/delta9 sitrep` | Show current mission status |
| `/delta9 pause` | Pause current mission |
| `/delta9 abort` | Cancel mission |
| `/delta9 rollback [checkpoint]` | Rollback to checkpoint |
| `/delta9 council` | Show council configuration |
| `/delta9 council add Oracle-X` | Add oracle to council |
| `/delta9 budget` | Show budget status |
| `/delta9 history` | Show mission history |
| `/delta9 memory` | Show learned patterns |
| `/delta9 template list` | List available templates |
| `/delta9 template use [name]` | Start mission from template |

---

## Plugin Structure

```
delta9/
├── src/
│   ├── index.ts                    # Main plugin export
│   │
│   ├── agents/
│   │   ├── commander.ts            # Commander agent definition
│   │   ├── council/
│   │   │   ├── index.ts            # Council orchestration
│   │   │   ├── oracle-claude.ts
│   │   │   ├── oracle-gpt.ts
│   │   │   ├── oracle-gemini.ts
│   │   │   └── oracle-deepseek.ts
│   │   ├── execution/
│   │   │   ├── operator.ts
│   │   │   ├── validator.ts
│   │   │   └── patcher.ts
│   │   └── support/
│   │       ├── scout.ts
│   │       ├── intel.ts
│   │       ├── strategist.ts
│   │       ├── ui-ops.ts
│   │       ├── scribe.ts
│   │       ├── optics.ts
│   │       └── qa.ts
│   │
│   ├── mission/
│   │   ├── state.ts                # Mission state manager
│   │   ├── schema.ts               # JSON schemas
│   │   ├── checkpoints.ts          # Git checkpoint logic
│   │   └── markdown.ts             # MD generation
│   │
│   ├── council/
│   │   ├── modes.ts                # Council mode logic
│   │   ├── synthesis.ts            # Opinion aggregation
│   │   └── confidence.ts           # Confidence scoring
│   │
│   ├── routing/
│   │   ├── task-router.ts          # Smart task routing
│   │   ├── complexity.ts           # Complexity detection
│   │   └── keywords.ts             # Keyword detection
│   │
│   ├── hooks/
│   │   ├── session-idle.ts         # When agent goes idle
│   │   ├── tool-execute.ts         # Before/after tool execution
│   │   ├── message-updated.ts      # Message handling
│   │   └── compaction.ts           # Context compaction handling
│   │
│   ├── tools/
│   │   ├── mission-tools.ts        # Mission management tools
│   │   ├── council-tools.ts        # Council invocation tools
│   │   └── support-tools.ts        # Support agent tools
│   │
│   ├── memory/
│   │   ├── store.ts                # Memory persistence
│   │   └── learning.ts             # Pattern learning
│   │
│   ├── budget/
│   │   ├── tracker.ts              # Cost tracking
│   │   └── limits.ts               # Budget enforcement
│   │
│   ├── notifications/
│   │   ├── discord.ts
│   │   ├── slack.ts
│   │   └── system.ts               # OS notifications
│   │
│   ├── commands/
│   │   ├── mission.ts              # /delta9 mission
│   │   ├── sitrep.ts               # /delta9 sitrep
│   │   ├── council.ts              # /delta9 council
│   │   └── ... (other commands)
│   │
│   ├── templates/
│   │   ├── auth-nextjs.json
│   │   ├── crud-api.json
│   │   └── ... (other templates)
│   │
│   ├── lib/
│   │   ├── config.ts               # Configuration loading
│   │   ├── logger.ts               # Structured logging
│   │   ├── git.ts                  # Git operations
│   │   └── utils.ts                # Utilities
│   │
│   └── types/
│       ├── mission.ts              # Mission types
│       ├── agents.ts               # Agent types
│       ├── config.ts               # Config types
│       └── events.ts               # Event types
│
├── assets/
│   ├── delta9.schema.json          # Config schema
│   ├── mission.schema.json         # Mission schema
│   └── logo.png
│
├── docs/
│   ├── README.md
│   ├── CONFIGURATION.md
│   ├── AGENTS.md
│   └── COUNCIL.md
│
├── package.json
├── tsconfig.json
├── .gitignore
└── LICENSE
```

---

## Implementation Status

> **Last Updated**: 2026-01-24

### Completed Features

#### Core Infrastructure
- [x] Plugin scaffold (`src/index.ts`)
- [x] Configuration system (`src/lib/config.ts`)
- [x] Mission state manager (`src/mission/state.ts`)
- [x] Zod schemas for all types (`src/schemas/`)
- [x] TypeScript strict mode throughout

#### SDK Integration
- [x] OpenCode SDK client integration
- [x] Background agent spawning via `client.session.run()`
- [x] Real agent execution (not simulation)
- [x] Abort controller for task cancellation

#### Background Task System
- [x] Background manager with task pool (`src/lib/background-manager.ts`)
- [x] Concurrency limiting (3 parallel tasks)
- [x] Task queueing with FIFO ordering
- [x] Task state tracking (pending/running/completed/failed/cancelled)
- [x] Process cleanup and shutdown handling
- [x] Stale task detection (30min TTL)
- [x] Abort signal propagation

#### Robustness
- [x] Graceful shutdown with cleanup
- [x] Process signal handling (SIGINT, SIGTERM, SIGQUIT, exit)
- [x] Stale task pruning on access
- [x] Error recovery and retry support

#### Developer Experience (10/10 DX)
- [x] Structured logging with named component loggers (`src/lib/logger.ts`)
- [x] Rich error handling with recovery suggestions (`src/lib/errors.ts`)
- [x] Context-aware hints system (`src/lib/hints.ts`)
- [x] Health diagnostic tool (`delta9_health`)
- [x] Emoji status indicators in tool outputs
- [x] Duration formatting (human-readable)
- [x] Detailed tool descriptions with examples

#### Tools Implemented (70+ total)
| Category | Tools |
|----------|-------|
| Mission | `mission_create`, `mission_status`, `mission_add_objective`, `mission_add_task`, `mission_complete_task`, `mission_fail_task`, `mission_abort`, `mission_clear` |
| Delegation | `delegate_task`, `retry_task` |
| Background | `background_output`, `background_cancel`, `background_list`, `background_cleanup` |
| Council | `consult_council`, `quick_consult`, `should_consult_council`, `council_status` |
| Memory | `memory_get`, `memory_set`, `memory_delete`, `memory_list`, `memory_replace`, `memory_append` |
| Knowledge | `knowledge_list`, `knowledge_get`, `knowledge_set`, `knowledge_append`, `knowledge_replace` |
| Validation | `validation_result`, `run_tests`, `check_lint`, `check_types` |
| Checkpoint | `checkpoint_create`, `checkpoint_list`, `checkpoint_restore`, `checkpoint_delete`, `checkpoint_get` |
| Budget | `budget_status`, `budget_set_limit`, `budget_check`, `budget_breakdown` |
| Skills | `list_skills`, `use_skill`, `read_skill_file`, `run_skill_script`, `get_skill` |
| Locks | `lock_file`, `unlock_file`, `check_lock`, `list_locks`, `lock_files`, `unlock_all` |
| Messaging | `send_message`, `check_inbox`, `read_message`, `reply_message`, `get_thread`, `message_stats` |
| Decomposition | `decompose_task`, `validate_decomposition`, `search_similar_tasks`, `redecompose`, `list_strategies`, `record_decomposition_outcome` |
| Epics | `create_epic`, `link_tasks_to_epic`, `epic_status`, `epic_breakdown`, `sync_to_git`, `list_epics`, `update_epic` |
| Traces | `trace_decision`, `query_traces`, `get_trace`, `find_similar_decisions`, `trace_stats` |
| Subagents | `spawn_subagent`, `subagent_status`, `get_subagent_output`, `wait_for_subagent`, `list_pending_outputs` |
| Session State | `register_session`, `set_session_state`, `get_session_state`, `list_sessions`, `trigger_resume`, `check_pending_resumes` |
| Routing | `analyze_complexity`, `recommend_agent` |
| Diagnostics | `delta9_health` |

### Completed Features

#### Council System ✅
- [x] Oracle agent definitions (CIPHER, VECTOR, PRISM, APEX)
- [x] Council modes (NONE/QUICK/STANDARD/XHIGH)
- [x] Opinion synthesis with confidence weighting
- [x] Conflict resolution

#### Support Agents ✅
- [x] SCOUT (fast codebase search with Haiku)
- [x] INTEL (research with Librarian-style 4-phase pattern)
- [x] STRATEGIST (mid-execution advice with Metis-style phases)
- [x] FACADE (UI-Ops), SCRIBE (docs), SPECTRE (vision), SENTINEL (QA), SURGEON (patcher)

#### Intelligence Layer ✅
- [x] XHIGH council mode (Scout+Intel recon before oracles)
- [x] Smart task routing to specialists
- [x] Complexity detection (keywords, scope, risk)
- [x] Category-based routing

#### Robustness ✅
- [x] Checkpoints and rollback
- [x] Budget tracking with warn/pause thresholds
- [x] Rate limit handling with exponential backoff
- [x] Model fallback chains with circuit breaker
- [x] Decision traces with precedent chains
- [x] Async subagent system with aliases
- [x] Session resumption on message arrival

#### Advanced Features ✅
- [x] Event sourcing (48 event types, projections)
- [x] Learning system (outcome tracking, confidence decay, anti-patterns)
- [x] Skills system (YAML frontmatter, model-aware rendering)
- [x] File reservation (CAS locks with TTL)
- [x] Guardrails (Commander discipline, three-strike escalation)
- [x] Agent messaging (inbox/outbox, threading, groups)
- [x] Task decomposition (6 strategies, validation, embedding-based search)
- [x] Epic management (Git sync, task linking)

### In Progress

#### Launch
- [ ] npm publish
- [ ] Marketing (GitHub README, social media)

---

## Development Roadmap

### Phase 1: Foundation (Week 1-2) ✅ COMPLETE
- [x] Plugin scaffold and config system
- [x] Mission state manager (mission.json CRUD)
- [x] Commander agent (basic planning, no council)
- [x] Single Operator execution
- [x] Validator agent

### Phase 2: Council (Week 3-4)
- [ ] Council orchestration system
- [ ] Oracle agent definitions
- [ ] Council modes (none/quick/standard)
- [ ] Opinion synthesis
- [ ] Confidence scoring

### Phase 3: Intelligence (Week 5-6)
- [ ] XHIGH council mode (oracles with recon)
- [ ] Support agents (Scout, Intel, Strategist)
- [ ] Smart task routing
- [ ] Complexity detection
- [ ] Keyword detection

### Phase 4: Robustness (Week 7-8)
- [ ] Checkpoints and rollback
- [ ] Budget tracking
- [ ] Memory and learning
- [ ] Seamless agent replacement
- [ ] Error recovery

### Phase 5: Polish (Week 9-10)
- [ ] All support agents (UI-Ops, Scribe, Optics, QA)
- [ ] Mission templates
- [ ] Notifications (Discord, Slack)
- [ ] Documentation
- [ ] Testing

### Phase 6: Launch
- [ ] npm publish
- [ ] Marketing (ProductHunt, X, Discord)
- [ ] Community feedback integration

---

## Comparison with Alternatives

| Feature | Claude Code | Oh-My-OpenCode | Delta9 |
|---------|-------------|----------------|--------|
| Multi-model | ❌ | ✅ | ✅ |
| Specialized agents | ❌ | ✅ | ✅ |
| Protected planning context | ❌ | ❌ | ✅ |
| Council deliberation | ❌ | ❌ | ✅ |
| Heterogeneous planning | ❌ | ❌ | ✅ |
| Verification gate | ❌ | ❌ | ✅ |
| Mission persistence | ❌ | Partial | ✅ |
| Checkpoints/rollback | ❌ | ❌ | ✅ |
| Budget tracking | ❌ | ❌ | ✅ |
| Cross-session memory | ❌ | ❌ | ✅ |
| Token efficiency | ⚠️ | ⚠️ | ✅ |
| Seamless integration | N/A | ✅ | ✅ |

---

## Technical Requirements

### OpenCode Version
- Minimum: 1.0.150+
- Recommended: Latest

### Runtime
- Bun (for npm plugin loading)
- Node.js 18+ (fallback)

### Dependencies
```json
{
  "dependencies": {
    "@opencode-ai/plugin": "^1.0.0",
    "zod": "^3.22.0",
    "date-fns": "^3.0.0"
  },
  "devDependencies": {
    "typescript": "^5.3.0",
    "@types/node": "^20.0.0",
    "vitest": "^1.0.0"
  }
}
```

---

## Getting Started (For Users)

### Installation

```bash
# Add to opencode.json
{
  "plugin": ["delta9"]
}

# Or install globally
npm install -g delta9
```

### Quick Start

```bash
# Just start typing - Delta9 takes over automatically
opencode

> "Build a user authentication system with Google OAuth"

# Delta9 automatically:
# 1. Detects complexity (HIGH)
# 2. Convenes Council
# 3. Creates mission plan
# 4. Asks for approval
# 5. Executes with verification
```

### Manual Control

```bash
# Force specific council mode
> /delta9 mission "Refactor auth" --council=xhigh

# Check status
> /delta9 sitrep

# Rollback if needed
> /delta9 rollback obj-2-complete
```

---

## Contributing

See CONTRIBUTING.md for development setup and guidelines.

---

## License

MIT License - see LICENSE file.

---

## Links

- GitHub: https://github.com/[your-username]/delta9
- npm: https://npmjs.com/package/delta9
- Documentation: https://delta9.dev
- Discord: https://discord.gg/delta9