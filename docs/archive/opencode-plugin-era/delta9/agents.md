# Delta9 Agent Roster

> Complete documentation of all 19 Delta9 agents.

---

## Agent Summary

| Layer | Agent | Model | Role |
|-------|-------|-------|------|
| Command | Commander | Opus 4.5 | Lead planner, orchestrator |
| Council | Cipher | Opus 4.5 | Architecture specialist |
| Council | Vector | GPT 5.2 Codex | Code patterns |
| Council | Prism | Gemini 3 Pro | UI/UX specialist |
| Council | Apex | DeepSeek v3 | Performance |
| Execution | Operator | Sonnet 4 | Task executor |
| Execution | Validator | Haiku 4.5 | QA verification |
| Delta Team | RECON | Haiku | Codebase reconnaissance |
| Delta Team | SIGINT | Sonnet 4.5 | Intelligence research |
| Delta Team | TACCOM | GPT 4o | Tactical command advisor |
| Delta Team | SURGEON | Haiku | Surgical precision fixer |
| Delta Team | SENTINEL | Sonnet 4.5 | Quality assurance guardian |
| Delta Team | SCRIBE | Gemini Flash | Documentation writer |
| Delta Team | FACADE | Gemini Flash | Frontend operations |
| Delta Team | SPECTRE | Gemini Flash | Visual intelligence |

---

## Command Layer

### Commander

**Role**: Lead planner and mission orchestrator
**Model**: User's choice (Opus 4.5 recommended)
**Temperature**: 0.7

**Responsibilities**:
- Analyze user requests to determine complexity
- Dispatch Scout and Intel for reconnaissance
- Convene Council for complex tasks
- Synthesize council opinions into mission plan
- Manage mission state (mission.json)
- Dispatch tasks to Operators
- Monitor progress and adapt plans

**Key Constraint**: NEVER writes code directly

**Example Prompt**:
```
You are Commander, the strategic planning agent for Delta9.

Your role:
1. Analyze requests to determine complexity (LOW/MEDIUM/HIGH/CRITICAL)
2. For complex tasks, convene the Council
3. Create structured missions with objectives and tasks
4. Each task needs specific acceptance criteria
5. Dispatch to appropriate Operators
6. Monitor progress via mission.json

You MUST NOT:
- Write or edit code directly
- Execute bash commands
- Make changes without a plan

Always reference mission.json as the source of truth.
```

---

## Council Layer

The Council consists of 4 Oracle agents with specialized codenames:

### Cipher (Claude)

**Role**: Architecture and edge cases specialist
**Model**: Opus 4.5
**Temperature**: 0.7

**Specialty**:
- Deep reasoning about system design
- Identifying subtle bugs and edge cases
- Complex pattern recognition
- Long-term architectural implications

**Example Output**:
```json
{
  "recommendation": "Use NextAuth.js v5 with database sessions",
  "confidence": 0.92,
  "caveats": [
    "Requires database migrations",
    "Session cleanup job needed"
  ],
  "reasoning": "Database sessions provide better security..."
}
```

### Vector (GPT)

**Role**: Logic and code patterns specialist
**Model**: GPT 5.2 Codex
**Temperature**: 0.5

**Specialty**:
- Known issues and gotchas
- Best practices and conventions
- Library-specific patterns
- Common pitfalls

### Prism (Gemini)

**Role**: UI/UX and creativity specialist
**Model**: Gemini 3 Pro
**Temperature**: 0.8

**Specialty**:
- Design implications
- User flow optimization
- Visual aesthetics
- Accessibility considerations

### Apex (DeepSeek)

**Role**: Performance and algorithms specialist
**Model**: DeepSeek v3
**Temperature**: 0.3

**Specialty**:
- Optimization strategies
- Algorithmic complexity
- Efficiency improvements
- Resource management

---

## Execution Layer

### Operator

**Role**: Primary task executor
**Model**: Sonnet 4 (Opus for complex tasks)
**Temperature**: 0.3

**Responsibilities**:
- Execute assigned tasks precisely
- Make minimal, focused changes
- Report completion with summary
- Can invoke support agents

**Tools**: read, write, edit, bash, glob, grep, task_complete

**Example Prompt**:
```
You are Operator, the execution agent for Delta9.

You receive tasks with:
- Clear description
- Acceptance criteria
- Mission context

Your job:
1. Execute the task precisely
2. Make minimal, focused changes
3. Verify work meets criteria
4. Report completion

Stay focused. Don't expand scope.
```

### Validator

**Role**: Quality assurance gate
**Model**: Haiku 4.5
**Temperature**: 0.1

**Responsibilities**:
- Verify EACH acceptance criterion
- Check for regressions
- Run tests if configured
- Return verdict: PASS / FIXABLE / FAIL

**Tools**: read, bash, validation_result

**Example Prompt**:
```
You are Validator, the QA agent for Delta9.

You receive:
- Task description
- Acceptance criteria
- Changes made (git diff)

Verify EACH criterion:
- PASS: All criteria met
- FIXABLE: Minor issues (max 2 retries)
- FAIL: Fundamental problems

Be strict but fair. Don't nitpick style.
```

### Patcher

**Role**: Quick targeted fixes
**Model**: Haiku 4.5
**Temperature**: 0.2
**Max Lines**: 50

**When Used**:
- Validator returns FIXABLE
- Simple typo fixes
- Minor adjustments

---

## Delta Team (Support Layer)

The Delta Team consists of 8 specialized support agents with military-inspired codenames:

### RECON (Scout)

**Role**: Reconnaissance Agent - Fast codebase search
**Model**: Haiku
**Timeout**: 30 seconds
**Config Key**: `scout`

**Capabilities**:
- File discovery and pattern matching
- Grep operations
- Directory structure analysis
- Quick codebase orientation

### SIGINT (Intel)

**Role**: Intelligence Research Agent
**Model**: Sonnet 4.5
**Config Key**: `intel`

**Capabilities**:
- Documentation lookup
- GitHub search
- Best practices research
- Example finding

**Constraint**: Read-only (denied: Write, Edit)

### TACCOM (Strategist)

**Role**: Tactical Command Advisor
**Model**: GPT 4o
**Config Key**: `strategist`

**When Used**:
- Operator hits a wall
- Unexpected complexity
- Needs architectural guidance

**Constraint**: Advisory only (denied: Bash)

### SURGEON (Patcher)

**Role**: Surgical Precision Fixer
**Model**: Haiku
**Temperature**: 0.1
**Max Tokens**: 1024
**Config Key**: `patcher`

**When Used**:
- Validator returns FIXABLE
- Simple typo fixes
- Minor adjustments (max 50 lines)

### SENTINEL (QA)

**Role**: Quality Assurance Guardian
**Model**: Sonnet 4.5
**Config Key**: `qa`

**Capabilities**:
- Unit tests (Jest, Vitest)
- Integration tests
- Test patterns
- Mock setup

### SCRIBE (Documentation)

**Role**: Documentation Writer
**Model**: Gemini Flash
**Format**: markdown
**Config Key**: `scribe`

**Capabilities**:
- README files
- API documentation
- Code comments
- JSDoc annotations

### FACADE (UI-Ops)

**Role**: Frontend Operations Specialist
**Model**: Gemini Flash
**Style System**: tailwind (configurable)
**Config Key**: `uiOps`

**Capabilities**:
- React, Vue, Svelte components
- Tailwind CSS styling
- Accessibility
- Responsive design

### SPECTRE (Optics)

**Role**: Visual Intelligence Analyst
**Model**: Gemini Flash
**Config Key**: `optics`

**Capabilities**:
- Screenshot analysis
- Diagram interpretation
- Image analysis
- PDF reading

---

## Agent Configuration

Agents are configured in `.delta9/config.json` or `~/.config/opencode/delta9.json`:

```json
{
  "commander": {
    "model": "anthropic/claude-opus-4-5",
    "temperature": 0.7
  },
  "council": {
    "members": [
      {
        "name": "Oracle-Claude",
        "model": "anthropic/claude-opus-4-5",
        "enabled": true
      },
      {
        "name": "Oracle-GPT",
        "model": "openai/gpt-5.2-codex",
        "enabled": true
      }
    ]
  },
  "operators": {
    "default_model": "anthropic/claude-sonnet-4",
    "complex_model": "anthropic/claude-opus-4-5"
  }
}
```

---

## Agent Invocation Patterns

### Commander Invokes Council

```typescript
// Gather council opinions in parallel
const opinions = await Promise.all(
  enabledOracles.map(oracle =>
    invokeOracle(oracle, missionContext)
  )
)

// Synthesize into mission plan
const mission = synthesizeOpinions(opinions)
```

### Commander Dispatches Operator

```typescript
// Route task based on type
const agent = routeTask(task) // Returns "operator", "ui-ops", "qa", etc.

// Dispatch with context
await dispatchToAgent(agent, {
  task,
  missionContext,
  acceptanceCriteria: task.criteria,
})
```

### Operator Completes Task

```typescript
// After execution, trigger validation
await dispatchToAgent("validator", {
  task,
  gitDiff: await getGitDiff(),
  acceptanceCriteria: task.criteria,
})
```

---

## Reference

- Architecture: `ARCHITECTURE.md`
- Full specification: `spec.md`
