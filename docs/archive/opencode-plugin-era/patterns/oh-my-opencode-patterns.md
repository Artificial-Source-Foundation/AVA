# oh-my-opencode Patterns

Reference documentation from dissecting the oh-my-opencode plugin.

---

## Overview

oh-my-opencode is a multi-agent coordination plugin for OpenCode that provides:

- **Sisyphus**: Primary orchestrator with phased behavior
- **Sisyphus-Junior**: Task executor (blocked from delegation)
- **Oracle**: Read-only high-IQ consultant for debugging/architecture
- **Metis**: Pre-planning consultant for intent classification
- **Prometheus**: Strategic planning consultant

**Key Philosophy**: "Default Bias: DELEGATE. WORK YOURSELF ONLY WHEN IT IS SUPER SIMPLE."

---

## Core Patterns

### 1. Phased Agent Behavior (Sisyphus)

**File**: `src/agents/sisyphus.ts`

Sisyphus operates through distinct phases with different behaviors:

```
Intent Gate → Codebase Assessment → Exploration → Implementation → Completion
```

**Phase Structure**:

| Phase | Purpose | Key Actions |
|-------|---------|-------------|
| **Intent Gate** | Classify the request | Identify task type, check key triggers |
| **Codebase Assessment** | Understand the environment | Fire explore/librarian agents |
| **Exploration** | Deep research | Background agent exploration |
| **Implementation** | Execute work | Delegate to categories or direct tools |
| **Completion** | Wrap up | Verify work, report results |

**Key Principles**:
- Default to delegation over direct work
- Fire agents proactively in background
- Use tools only for simple, local tasks

---

### 2. Dynamic Prompt Building

**File**: `src/agents/dynamic-agent-prompt-builder.ts`

Prompts are assembled dynamically based on available resources:

```typescript
// Build sections based on what's available
buildToolSelectionTable(agents, tools, skills)
buildExploreSection(agents)
buildLibrarianSection(agents)
buildDelegationTable(agents)
buildCategorySkillsDelegationGuide(categories, skills)
buildOracleSection(agents)
buildHardBlocksSection()
buildAntiPatternsSection()
```

**Purpose**: Creates prompts that adapt to the actual available agents, tools, and skills.

**Key Functions**:

| Function | Output |
|----------|--------|
| `buildToolSelectionTable` | Cost-based tool/agent selection guide |
| `buildExploreSection` | When to use explore agent |
| `buildLibrarianSection` | When to use librarian agent |
| `buildDelegationTable` | Domain → Agent → Trigger mapping |
| `buildCategorySkillsDelegationGuide` | Category and skill selection protocol |

---

### 3. Intent Classification (Metis)

**File**: `src/agents/metis.ts`

Pre-planning consultant that classifies work intent BEFORE planning.

**Intent Types**:

| Intent | Signals | Focus |
|--------|---------|-------|
| **Refactoring** | "refactor", "restructure", "clean up" | SAFETY: regression prevention |
| **Build from Scratch** | "create new", "add feature", greenfield | DISCOVERY: explore patterns first |
| **Mid-sized Task** | Scoped feature, bounded work | GUARDRAILS: exact deliverables |
| **Collaborative** | "help me plan", wants dialogue | INTERACTIVE: incremental clarity |
| **Architecture** | "how should we structure" | STRATEGIC: Oracle consultation |
| **Research** | Investigation needed, goal unclear | INVESTIGATION: exit criteria |

**AI-Slop Pattern Detection**:
```typescript
| Pattern | Example | Question to Ask |
| Scope inflation | "Also tests for adjacent modules" | "Should I add tests beyond [TARGET]?" |
| Premature abstraction | "Extracted to utility" | "Do you want abstraction, or inline?" |
| Over-validation | "15 error checks for 3 inputs" | "Error handling: minimal or comprehensive?" |
| Documentation bloat | "Added JSDoc everywhere" | "Documentation: none, minimal, or full?" |
```

**Tool Recommendations by Intent**:
- Refactoring: `lsp_find_references`, `lsp_rename`, `ast_grep_search`
- Build: Explore agents for pattern discovery
- Architecture: Oracle consultation required

---

### 4. Category-Based Task Routing

**File**: `src/tools/delegate-task/constants.ts`

Tasks are routed to optimized models based on category:

```typescript
const DEFAULT_CATEGORIES: Record<string, CategoryConfig> = {
  "visual-engineering": { model: "google/gemini-3-pro-preview" },
  ultrabrain: { model: "openai/gpt-5.2-codex", variant: "xhigh" },
  artistry: { model: "google/gemini-3-pro-preview", variant: "max" },
  quick: { model: "anthropic/claude-haiku-4-5" },
  "unspecified-low": { model: "anthropic/claude-sonnet-4-5" },
  "unspecified-high": { model: "anthropic/claude-opus-4-5", variant: "max" },
  writing: { model: "google/gemini-3-flash-preview" },
}
```

**Category Descriptions**:

| Category | Domain |
|----------|--------|
| `visual-engineering` | Frontend, UI/UX, design, styling, animation |
| `ultrabrain` | Deep logical reasoning, complex architecture |
| `artistry` | Highly creative/artistic tasks |
| `quick` | Trivial tasks - typo fixes, simple modifications |
| `unspecified-low` | Moderate effort, unclassifiable tasks |
| `unspecified-high` | High effort, unclassifiable tasks |
| `writing` | Documentation, prose, technical writing |

**Category Prompt Appends**:
Each category injects domain-specific context:
```typescript
// Example: visual-engineering
const VISUAL_CATEGORY_PROMPT_APPEND = `<Category_Context>
You are working on VISUAL/UI tasks.

Design-first mindset:
- Bold aesthetic choices over safe defaults
- Unexpected layouts, asymmetry, grid-breaking elements
- Distinctive typography
- High-impact animations with staggered reveals
...
</Category_Context>`
```

---

### 5. Delegate Task Tool

**File**: `src/tools/delegate-task/tools.ts`

Central delegation mechanism with:

```typescript
delegate_task({
  category: "visual-engineering",  // OR subagent_type: "oracle"
  load_skills: ["playwright", "frontend-ui-ux"],
  description: "Short task description",
  prompt: "Full detailed prompt",
  run_in_background: false,  // REQUIRED parameter
  resume: "session-id"  // Optional - resume previous session
})
```

**Key Features**:

1. **Mutually Exclusive Routing**:
   - `category` → Spawns Sisyphus-Junior with category config
   - `subagent_type` → Spawns specific agent directly

2. **Skill Loading**:
   ```typescript
   const { resolved, notFound } = await resolveMultipleSkillsAsync(args.load_skills, { gitMasterConfig })
   skillContent = Array.from(resolved.values()).join("\n\n")
   ```

3. **Session Resume**:
   - Preserves full context across calls
   - Use `resume="session-id"` for follow-up work

4. **Background vs Sync**:
   - `run_in_background=true`: Returns task_id immediately
   - `run_in_background=false`: Waits for completion

5. **Agent Validation**:
   ```typescript
   const matchedAgent = findByNameCaseInsensitive(callableAgents, agentToUse)
   if (!matchedAgent) {
     // Check if it's a primary agent (cannot be called)
     const isPrimaryAgent = findByNameCaseInsensitive(
       agents.filter((a) => a.mode === "primary"),
       agentToUse
     )
     if (isPrimaryAgent) {
       return `Cannot call primary agent "${isPrimaryAgent.name}" via delegate_task.`
     }
   }
   ```

---

### 6. Oracle Read-Only Consultant

**File**: `src/agents/oracle.ts`

High-IQ reasoning specialist with tool restrictions:

```typescript
const restrictions = createAgentToolRestrictions([
  "write",
  "edit",
  "task",
  "delegate_task",
])
```

**Oracle Triggers**:
- Architecture decisions with multi-system tradeoffs
- Self-review after completing significant implementation
- Hard debugging after 2+ failed fix attempts

**Decision Framework**:
- **Bias toward simplicity**: Least complex solution that fulfills requirements
- **Leverage what exists**: Favor modifications over new components
- **One clear path**: Present a single primary recommendation
- **Signal the investment**: Quick(<1h), Short(1-4h), Medium(1-2d), Large(3d+)

**Response Structure**:
1. **Bottom line**: 2-3 sentences
2. **Action plan**: Numbered steps
3. **Effort estimate**: Quick/Short/Medium/Large

---

### 7. Prometheus Planning Consultant

**File**: `src/agents/prometheus-prompt.ts`

Strategic planning consultant that NEVER implements:

```
"YOU ARE A PLANNER. YOU ARE NOT AN IMPLEMENTER. YOU DO NOT WRITE CODE."
```

**Request Interpretation**:
| User Says | Prometheus Interprets As |
|-----------|-------------------------|
| "Fix the login bug" | "Create a work plan to fix the login bug" |
| "Add dark mode" | "Create a work plan to add dark mode" |

**Phases**:
1. **Interview Mode** (default): Consult, research, discuss
2. **Plan Generation**: Auto-transitions when clearance check passes
3. **Momus Loop**: High-accuracy review iteration

**Draft Management**:
- Location: `.sisyphus/drafts/{name}.md`
- Continuously updated during interview
- Deleted after plan completion

**Plan Output**:
- Location: `.sisyphus/plans/{name}.md`
- Contains: Context, Objectives, Task Flow, TODOs with acceptance criteria

---

### 8. Executor Blocking (Sisyphus-Junior)

**File**: `src/agents/sisyphus-junior.ts`

Task executor that is BLOCKED from delegation:

```typescript
const BLOCKED_TOOLS = ["task", "delegate_task"]
```

**Purpose**: Prevents infinite delegation loops - executors execute, they don't delegate.

**Allowed Actions**:
- Use `call_omo_agent` for explore/librarian sub-tasks
- Direct tool usage (edit, write, bash, etc.)
- Background agent spawning via `call_omo_agent`

**Notepad System**:
Records learnings and discoveries during execution for future reference.

---

### 9. Compaction Context Preservation

**File**: `src/hooks/compaction-context-injector/index.ts`

Injects structured context before session compaction:

```typescript
const SUMMARIZE_CONTEXT_PROMPT = `
When summarizing this session, you MUST include:

## 1. User Requests (As-Is)
- List all original user requests exactly as stated

## 2. Final Goal
- What the user ultimately wanted to achieve

## 3. Work Completed
- What has been done so far
- Files created/modified

## 4. Remaining Tasks
- What still needs to be done
- Pending items

## 5. MUST NOT Do (Critical Constraints)
- Things explicitly forbidden
- Approaches that failed
- Anti-patterns identified
`
```

---

### 10. Edit Error Recovery

**File**: `src/hooks/edit-error-recovery/index.ts`

Detects Edit tool failures and injects recovery instructions:

```typescript
const EDIT_ERROR_PATTERNS = [
  "oldString and newString must be different",
  "oldString not found",
  "oldString found multiple times",
]

const EDIT_ERROR_REMINDER = `
[EDIT ERROR - IMMEDIATE ACTION REQUIRED]

You made an Edit mistake. STOP and do this NOW:

1. READ the file immediately to see its ACTUAL current state
2. VERIFY what the content really looks like (your assumption was wrong)
3. APOLOGIZE briefly to the user for the error
4. CONTINUE with corrected action based on the real file content

DO NOT attempt another edit until you've read and verified the file state.
`
```

---

### 11. Skill Loading System

Skills inject specialized knowledge into agents:

```typescript
delegate_task(
  category="[selected-category]",
  load_skills=["skill-1", "skill-2"],  // Include ALL relevant skills
  prompt="..."
)
```

**Skill Selection Protocol**:
1. **Select Category** - Match task to category domain
2. **Evaluate ALL Skills** - For every skill, ask: "Does this overlap with my task?"
3. **Justify Omissions** - Provide reason for skipping potentially relevant skills

**Why Justification is Mandatory**:
- Forces reading skill descriptions
- Prevents lazy omission
- Subagents are STATELESS - they only know what you tell them

---

### 12. Agent Prompt Metadata

**File**: `src/agents/types.ts`

Structured metadata for agent selection:

```typescript
interface AgentPromptMetadata {
  category: "orchestrator" | "advisor" | "executor" | "utility"
  cost: "FREE" | "CHEAP" | "EXPENSIVE"
  promptAlias: string
  triggers: Array<{ domain: string; trigger: string }>
  useWhen: string[]
  avoidWhen: string[]
  keyTrigger?: string  // One-line trigger summary
}
```

**Example (Oracle)**:
```typescript
const ORACLE_PROMPT_METADATA: AgentPromptMetadata = {
  category: "advisor",
  cost: "EXPENSIVE",
  promptAlias: "Oracle",
  triggers: [
    { domain: "Architecture decisions", trigger: "Multi-system tradeoffs" },
    { domain: "Self-review", trigger: "After significant implementation" },
    { domain: "Hard debugging", trigger: "After 2+ failed fix attempts" },
  ],
  useWhen: ["Complex architecture design", "2+ failed fix attempts"],
  avoidWhen: ["Simple file operations", "First attempt at any fix"],
}
```

---

## Module Organization

```
oh-my-opencode/src/
├── index.ts                    # Plugin entry point
├── agents/
│   ├── sisyphus.ts            # Main orchestrator
│   ├── sisyphus-junior.ts     # Task executor
│   ├── oracle.ts              # Read-only consultant
│   ├── metis.ts               # Pre-planning consultant
│   ├── prometheus-prompt.ts   # Planning consultant
│   ├── dynamic-agent-prompt-builder.ts  # Dynamic prompt assembly
│   └── types.ts               # Agent metadata types
├── tools/
│   └── delegate-task/
│       ├── tools.ts           # Main delegate_task tool
│       ├── constants.ts       # Category definitions
│       └── types.ts           # Delegate task types
├── hooks/
│   ├── compaction-context-injector/  # Context preservation
│   ├── edit-error-recovery/          # Edit failure handling
│   ├── agent-usage-reminder/         # Usage tracking
│   └── ...                    # Many more specialized hooks
├── features/
│   ├── background-agent.ts    # Background task management
│   ├── opencode-skill-loader/ # Skill loading system
│   └── hook-message-injector.ts  # Message injection
└── shared/
    ├── model-resolver.ts      # Model resolution
    └── permission-compat.ts   # Tool restrictions
```

---

## Key Design Decisions

### 1. Delegation as Default
The system biases toward delegation over direct work:
- Sisyphus delegates to categories/agents
- Sisyphus-Junior is blocked from delegation (execution only)
- Oracle is blocked from modifying anything (consultation only)

### 2. Category-Optimized Models
Different task domains use different models:
- Visual: Gemini Pro (good at UI/design)
- Logic: GPT Codex (deep reasoning)
- Quick: Haiku (fast, cheap)
- Writing: Gemini Flash (prose)

### 3. Intent-Driven Planning
Metis classifies intent BEFORE planning:
- Refactoring → Safety focus
- Build → Discovery focus
- Architecture → Oracle required

### 4. Skill Injection
Skills provide domain expertise without hardcoding:
- Loaded at delegation time
- Injected as system content
- Agent receives specialized knowledge

### 5. Error Recovery Hooks
Tool failures trigger recovery guidance:
- Edit errors → Read file first
- Context limits → Structured summarization
- Empty responses → Retry prompts

---

## Delta9 Application

### Already Have
- Commander + Operators (similar to Sisyphus + Sisyphus-Junior)
- Validator (similar to Oracle for review)
- Council system for strategic decisions

### Should Add

1. **Intent Classification**
   - Add Metis-like pre-planning phase
   - Classify requests before creating mission

2. **Category-Based Task Routing**
   - Route tasks to optimal models based on category
   - Different models for different domains

3. **Dynamic Prompt Building**
   - Build prompts based on available agents/tools
   - Include cost/trigger information

4. **Skill Loading System**
   - Inject domain expertise at task time
   - Skills for git, testing, frontend, etc.

5. **Agent Tool Restrictions**
   - Block Commander from Edit/Write (like Prometheus)
   - Block Operators from delegate_task (like Sisyphus-Junior)

6. **Error Recovery Hooks**
   - Edit error detection and recovery
   - Context limit handling

7. **Compaction Context Template**
   - Structured summarization before compaction
   - Preserve critical constraints

---

## File References

| Pattern | File | Key Functions |
|---------|------|---------------|
| Phased Behavior | `agents/sisyphus.ts` | `buildDynamicSisyphusPrompt` |
| Intent Classification | `agents/metis.ts` | `METIS_SYSTEM_PROMPT` |
| Category Routing | `tools/delegate-task/constants.ts` | `DEFAULT_CATEGORIES` |
| Delegate Task | `tools/delegate-task/tools.ts` | `createDelegateTask` |
| Dynamic Prompts | `agents/dynamic-agent-prompt-builder.ts` | `build*Section` functions |
| Oracle Consultant | `agents/oracle.ts` | `createOracleAgent` |
| Planning | `agents/prometheus-prompt.ts` | `PROMETHEUS_SYSTEM_PROMPT` |
| Executor Blocking | `agents/sisyphus-junior.ts` | `BLOCKED_TOOLS` |
| Context Preservation | `hooks/compaction-context-injector/` | `SUMMARIZE_CONTEXT_PROMPT` |
| Error Recovery | `hooks/edit-error-recovery/` | `EDIT_ERROR_PATTERNS` |
