# Delta9 Architecture

> Hierarchical Commander + Council + Operators multi-agent system.

---

## System Overview

```
┌─────────────────────────────────────────────────────────────────────────┐
│                         USER INPUT                                       │
│                  "Build authentication system"                           │
└─────────────────────────────────────────────────────────────────────────┘
                                  │
                                  ▼
┌─────────────────────────────────────────────────────────────────────────┐
│                      PLANNING PHASE                                      │
│                                                                         │
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
│           └─────────────┬───────────────┘                              │
│                         ▼                                               │
│  ┌─────────────────────────────────────────────────────────────────┐   │
│  │                      THE COUNCIL                                 │   │
│  │                                                                  │   │
│  │  ┌──────────┐  ┌──────────┐  ┌──────────┐  ┌──────────┐        │   │
│  │  │  Oracle  │  │  Oracle  │  │  Oracle  │  │  Oracle  │        │   │
│  │  │  Claude  │  │   GPT    │  │  Gemini  │  │DeepSeek  │        │   │
│  │  │ Opus 4.5 │  │GPT 5.2   │  │  3 Pro   │  │   v3     │        │   │
│  │  └──────────┘  └──────────┘  └──────────┘  └──────────┘        │   │
│  │                                                                  │   │
│  │  Each provides: recommendation, confidence, caveats              │   │
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
│  │  • NEVER writes code                                            │   │
│  └─────────────────────────────────────────────────────────────────┘   │
│                           │                                             │
│       ┌───────────────────┼───────────────────┐                        │
│       ▼                   ▼                   ▼                         │
│  ┌──────────┐       ┌──────────┐       ┌──────────┐                    │
│  │ OPERATOR │       │ OPERATOR │       │ OPERATOR │                    │
│  │    #1    │       │    #2    │       │    #N    │                    │
│  │ Sonnet 4 │       │ Sonnet 4 │       │ Sonnet 4 │                    │
│  └────┬─────┘       └────┬─────┘       └────┬─────┘                    │
│       └──────────────────┼──────────────────┘                          │
│                          ▼                                              │
│  ┌─────────────────────────────────────────────────────────────────┐   │
│  │                      VALIDATOR                                   │   │
│  │                   (Haiku 4.5)                                    │   │
│  │                                                                  │   │
│  │  Input: task, acceptance criteria, git diff                     │   │
│  │  Output: PASS / FIXABLE / FAIL                                  │   │
│  └─────────────────────────────────────────────────────────────────┘   │
│                          │                                              │
│          ┌───────────────┼───────────────┐                             │
│          ▼               ▼               ▼                              │
│       PASS           FIXABLE          FAIL                              │
│          │               │               │                              │
│          ▼               ▼               ▼                              │
│    Mark done      Same Operator     Commander                           │
│    Next task      + feedback        re-evaluates                        │
│                   (max 2 retries)                                       │
└─────────────────────────────────────────────────────────────────────────┘
```

---

## Agent Layers

### Command Layer

| Agent | Model | Responsibility |
|-------|-------|----------------|
| **Commander** | Opus 4.5 | Strategic planning, mission orchestration, NEVER writes code |

### Council Layer

| Agent | Model | Specialty |
|-------|-------|-----------|
| **Oracle-Claude** | Opus 4.5 | Architecture, edge cases, deep reasoning |
| **Oracle-GPT** | GPT 5.2 Codex | Logic, code patterns, best practices |
| **Oracle-Gemini** | Gemini 3 Pro | UI/UX, creativity, design implications |
| **Oracle-DeepSeek** | DeepSeek v3 | Performance, algorithms, optimization |

### Execution Layer

| Agent | Model | Responsibility |
|-------|-------|----------------|
| **Operator** | Sonnet 4 | Task execution, code implementation |
| **Validator** | Haiku 4.5 | QA verification against acceptance criteria |
| **Patcher** | Haiku 4.5 | Small targeted fixes |

### Support Layer

| Agent | Model | When Used |
|-------|-------|-----------|
| **Scout** | Haiku | Fast codebase search |
| **Intel** | GLM 4.7 | Research, documentation lookup |
| **Strategist** | GPT 5.2 | Mid-execution guidance |
| **UI-Ops** | Gemini Pro | Frontend components |
| **Scribe** | Gemini Flash | Documentation writing |
| **Optics** | Gemini Flash | Image/PDF analysis |
| **QA** | Sonnet 4 | Test writing |

---

## Council Modes

| Mode | Complexity | Oracles | Trigger |
|------|------------|---------|---------|
| **NONE** | Simple | Commander only | "fix typo", "quick" |
| **QUICK** | Moderate | 1 Oracle | "add page", small feature |
| **STANDARD** | Complex | All configured | New systems, integrations |
| **XHIGH** | Critical | All + recon access | Architecture, core refactors |

### XHIGH Mode Detail

Each Oracle can invoke Scout and Intel independently:

```
Commander: "Council, investigate this mission. You have recon access."

Oracle-Claude: "Scout, show me authentication-related files"
  → Scout returns file list and snippets
  → Oracle-Claude forms opinion with actual code context

Oracle-GPT: "Intel, find OAuth best practices for Node.js 2025"
  → Intel searches docs and GitHub
  → Oracle-GPT forms opinion with current standards
```

---

## Mission State

### File Structure

```
.delta9/
├── mission.json          # Current mission (source of truth)
├── mission.md            # Human-readable view (auto-generated)
├── history.jsonl         # Append-only audit log
├── memory.json           # Cross-session learning
└── checkpoints/          # Git-based rollback points
    ├── obj-1-complete/
    └── obj-2-complete/
```

### Data Flow

```
User Request
     │
     ▼
Commander analyzes
     │
     ▼
mission.json created ─────────────────┐
     │                                │
     ▼                                │
Tasks dispatched                      │
     │                                │
     ▼                                │
Operator executes ◄───────────────────┤
     │                                │
     ▼                                │
Validator verifies                    │
     │                                │
     ├──► PASS: mission.json updated ─┘
     │
     ├──► FIXABLE: retry with feedback
     │
     └──► FAIL: Commander re-plans
```

---

## Smart Task Routing

| Task Pattern | Signals | Routed To |
|--------------|---------|-----------|
| UI/Frontend | "component", "form", "CSS" | UI-Ops |
| Testing | "test", "spec", "coverage" | QA |
| Documentation | "README", "docs", "JSDoc" | Scribe |
| Performance | "optimize", "cache", "speed" | Operator + Strategist |
| Complex logic | "algorithm", "architecture" | Operator (Opus if critical) |
| Simple fixes | "typo", "rename", "fix" | Patcher |

---

## Key Design Decisions

### 1. External State Persistence

**Problem**: Context compaction loses in-memory state.
**Solution**: `mission.json` on disk survives compaction.

### 2. Protected Commander Context

**Problem**: Planning context polluted with implementation details.
**Solution**: Commander NEVER writes code, stays strategic.

### 3. Validation Gate

**Problem**: No verification that work meets requirements.
**Solution**: Dedicated Validator agent before any task completion.

### 4. Heterogeneous Council

**Problem**: Single model has blind spots.
**Solution**: Multiple models with different strengths deliberate.

### 5. Graceful Degradation

**Problem**: Users have different API access.
**Solution**: Works with any model combination; disabled oracles skip.

---

## Comparison with Alternatives

| Feature | Claude Code | Oh-My-OpenCode | Delta9 |
|---------|-------------|----------------|--------|
| Multi-model | No | Yes | Yes |
| Protected planning | No | No | Yes |
| Council deliberation | No | No | Yes |
| Verification gate | No | No | Yes |
| Mission persistence | No | Partial | Yes |
| Checkpoints/rollback | No | No | Yes |
| Budget tracking | No | No | Yes |

---

## Reference

- Full specification: `spec.md`
- Implementation plan: `../PLAN.md`
