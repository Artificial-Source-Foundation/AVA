# Delta9 User Guide

> Complete guide to using Delta9 for mission-critical development

---

## Table of Contents

1. [Introduction](#introduction)
2. [Installation](#installation)
3. [Getting Started](#getting-started)
4. [Core Concepts](#core-concepts)
5. [The Delta Team](#the-delta-team)
6. [Council Modes](#council-modes)
7. [Mission Workflow](#mission-workflow)
8. [CLI Commands](#cli-commands)
9. [Mission Templates](#mission-templates)
10. [Memory & Learning](#memory--learning)
11. [Budget Management](#budget-management)
12. [Troubleshooting](#troubleshooting)

---

## Introduction

Delta9 is an OpenCode plugin that implements a hierarchical **Commander + Council + Operators** architecture. Instead of a single AI agent attempting complex tasks, Delta9 coordinates multiple specialized agents:

- **Commander**: Strategic planner that never writes code
- **Council (The Delta Team)**: 4 specialized Oracles providing diverse perspectives
- **Operators**: Task executors that implement code changes
- **Validator**: Quality gate ensuring work meets acceptance criteria

This separation ensures better planning, more thorough analysis, and higher quality results.

---

## Installation

### Prerequisites

- [OpenCode](https://opencode.ai) installed and configured
- Node.js 18+ or Bun 1.0+ (for CLI commands)
- API keys for desired model providers (Anthropic, OpenAI, Google, DeepSeek)

### Quick Install (Recommended)

```bash
# Install plugin
npm install delta9
# or
bun add delta9

# Run interactive setup wizard
npx delta9 setup
# or with Bun
bunx delta9 setup
```

The setup wizard will guide you through:
1. **Model providers** - Select which AI providers you have (Claude, OpenAI, Gemini, Copilot)
2. **Agent selection** - Choose which agents to install (all, core only, or custom)
3. **Configuration** - Automatically configures everything

What gets installed:
- Agent prompt files to `~/.config/opencode/agents/`
- Agent definitions in `~/.config/opencode/opencode.json`
- Delta9 configuration in `~/.config/delta9/`

### Setup Options

```bash
# Interactive setup (default)
bunx delta9 setup

# Preview changes without installing
bunx delta9 setup --dry-run

# Force overwrite existing files
bunx delta9 setup --force

# Non-interactive mode (for scripts/CI)
bunx delta9 setup --no-tui
```

### Manual Installation

If you prefer manual setup:

#### 1. Install the plugin

```bash
npm install delta9
# or
bun add delta9
```

#### 2. Add to OpenCode configuration

Edit `~/.config/opencode/opencode.json`:

```json
{
  "plugin": ["delta9"],
  "agent": {
    "commander": {
      "description": "Commander - Strategic AI Coordination (Delta9)",
      "prompt": "{file:~/.config/opencode/agents/commander.md}",
      "temperature": 0.7
    },
    "operator": {
      "description": "Operator - Task Execution (Delta9)",
      "prompt": "{file:~/.config/opencode/agents/operator.md}",
      "mode": "subagent",
      "temperature": 0.3
    },
    "validator": {
      "description": "Validator - Quality Gate (Delta9)",
      "prompt": "{file:~/.config/opencode/agents/validator.md}",
      "mode": "subagent",
      "temperature": 0.1,
      "tools": { "write": false, "edit": false }
    },
    "scout": {
      "description": "RECON - Codebase reconnaissance (Delta9)",
      "prompt": "{file:~/.config/opencode/agents/scout.md}",
      "mode": "subagent",
      "temperature": 0.1
    },
    "intel": {
      "description": "SIGINT - Research and documentation (Delta9)",
      "prompt": "{file:~/.config/opencode/agents/intel.md}",
      "mode": "subagent",
      "temperature": 0.2,
      "tools": { "write": false, "edit": false }
    },
    "strategist": {
      "description": "TACCOM - Tactical advisor (Delta9)",
      "prompt": "{file:~/.config/opencode/agents/strategist.md}",
      "mode": "subagent",
      "temperature": 0.4,
      "tools": { "write": false, "edit": false, "bash": false }
    },
    "patcher": {
      "description": "SURGEON - Quick fixes (Delta9)",
      "prompt": "{file:~/.config/opencode/agents/patcher.md}",
      "mode": "subagent",
      "temperature": 0.1
    },
    "qa": {
      "description": "SENTINEL - Testing (Delta9)",
      "prompt": "{file:~/.config/opencode/agents/qa.md}",
      "mode": "subagent",
      "temperature": 0.2
    },
    "scribe": {
      "description": "SCRIBE - Documentation (Delta9)",
      "prompt": "{file:~/.config/opencode/agents/scribe.md}",
      "mode": "subagent",
      "temperature": 0.3
    },
    "uiOps": {
      "description": "FACADE - Frontend (Delta9)",
      "prompt": "{file:~/.config/opencode/agents/uiOps.md}",
      "mode": "subagent",
      "temperature": 0.4
    },
    "optics": {
      "description": "SPECTRE - Visual analysis (Delta9)",
      "prompt": "{file:~/.config/opencode/agents/optics.md}",
      "mode": "subagent",
      "temperature": 0.2
    }
  }
}
```

#### 3. Copy agent prompt files

Copy the agent prompt files from the Delta9 package to your OpenCode agents directory:

```bash
mkdir -p ~/.config/opencode/agents
cp node_modules/delta9/agents/*.md ~/.config/opencode/agents/
```

### Verify Installation

```bash
# Check Delta9 is loaded
npx delta9 health

# Should show: Environment: OK, Config: OK, Agents: OK
```

### Restart OpenCode

After installation, restart OpenCode for the changes to take effect.

---

## Getting Started

### Your First Mission

Simply describe what you want to accomplish:

```
> Build a user authentication system with JWT tokens
```

Delta9 automatically:

1. **Commander** analyzes the request and determines complexity
2. **Council** convenes if the task warrants strategic input
3. **Operators** execute the implementation tasks
4. **Validator** verifies each task meets acceptance criteria

### Understanding the Output

During execution, you'll see:

```
[Commander] Analyzing request...
[Commander] Complexity: HIGH - Convening Council (STANDARD mode)
[Council] CIPHER recommends: Use refresh token rotation...
[Council] VECTOR identifies: Edge case - expired token handling...
[Council] PRISM suggests: Add OAuth social login option...
[Council] APEX warns: Token storage in localStorage is insecure...
[Commander] Synthesizing council input...
[Commander] Created mission: "User Authentication System" (4 objectives, 12 tasks)
[Operator] Starting task: Set up JWT library...
[Validator] Task PASSED: JWT library installed correctly
...
```

---

## Core Concepts

### Missions

A **mission** is a high-level goal broken into objectives and tasks:

```
Mission: User Authentication System
├── Objective 1: Backend Setup
│   ├── Task 1.1: Install JWT library
│   ├── Task 1.2: Create auth middleware
│   └── Task 1.3: Implement token refresh
├── Objective 2: Frontend Integration
│   ├── Task 2.1: Create login form
│   └── Task 2.2: Add auth context
└── Objective 3: Testing
    ├── Task 3.1: Unit tests
    └── Task 3.2: Integration tests
```

Mission state persists in `.delta9/mission.json`, surviving context compaction.

### Objectives

Objectives are major milestones within a mission. Each objective has:

- **Title**: Clear description
- **Tasks**: Actionable work items
- **Status**: pending | in_progress | completed | failed

### Tasks

Tasks are atomic work units assigned to Operators:

- **Description**: What needs to be done
- **Acceptance Criteria**: How to verify completion
- **Agent**: Which agent type handles it
- **Dependencies**: Tasks that must complete first

---

## The Delta Team

The Council consists of 4 specialized Oracles, each with distinct personalities:

### CIPHER - The Strategist

- **Specialty**: Architecture, system design, technical decisions
- **Temperature**: 0.2 (decisive, low variance)
- **Default Model**: Claude Opus 4.5
- **Asks**: "What's the best architectural approach?"

### VECTOR - The Analyst

- **Specialty**: Logic, edge cases, correctness
- **Temperature**: 0.4 (methodical, balanced)
- **Default Model**: GPT-4o
- **Asks**: "What could go wrong? What edge cases exist?"

### PRISM - The Creative

- **Specialty**: UI/UX, user experience, alternatives
- **Temperature**: 0.6 (creative, higher variance)
- **Default Model**: Gemini 2.0 Flash
- **Asks**: "What would be best for users? What alternatives exist?"

### APEX - The Optimizer

- **Specialty**: Performance, efficiency, scalability
- **Temperature**: 0.3 (precise, analytical)
- **Default Model**: DeepSeek Chat
- **Asks**: "What are the performance implications? How does it scale?"

### Configuring the Delta Team

You can customize which models power each Oracle:

```json
{
  "council": {
    "members": [
      {
        "name": "Cipher",
        "model": "anthropic/claude-opus-4-5",
        "enabled": true,
        "specialty": "architecture"
      },
      {
        "name": "Vector",
        "model": "openai/gpt-4o",
        "enabled": true,
        "specialty": "logic"
      }
    ]
  }
}
```

---

## Council Modes

Delta9 automatically selects the appropriate council mode based on task complexity:

| Mode | When Used | Oracles Involved |
|------|-----------|------------------|
| `none` | Simple tasks ("fix typo", "rename variable") | Commander only |
| `quick` | Moderate tasks ("add validation", "create component") | 1 Oracle (Cipher) |
| `standard` | Complex tasks ("implement feature", "add API endpoint") | All enabled Oracles |
| `xhigh` | Critical tasks ("refactor auth", "database migration") | All Oracles + reconnaissance |

### XHIGH Mode

For critical tasks, XHIGH mode runs additional reconnaissance:

1. **Scout** searches the codebase for relevant patterns
2. **Intel** gathers documentation and context
3. **Council** receives this intelligence before deliberation

This ensures the most informed recommendations for high-stakes changes.

### Forcing a Mode

Use keywords to override auto-detection:

```
# Force XHIGH mode
> thorough implementation of payment processing

# Force no council
> quick fix for the typo in header.tsx
```

Or configure explicitly:

```json
{
  "seamless": {
    "keywords": {
      "councilXhigh": ["thorough", "careful", "critical"],
      "councilNone": ["quick", "just", "simple", "fast"]
    }
  }
}
```

---

## Mission Workflow

### 1. Request Analysis

Commander analyzes your request:
- Identifies scope and complexity
- Determines required council mode
- Extracts key requirements

### 2. Council Deliberation (if applicable)

The Delta Team provides perspectives:
- Each Oracle offers recommendations
- Confidence scores indicate certainty
- Conflicts are surfaced for resolution

### 3. Mission Planning

Commander creates the mission plan:
- Breaks work into objectives
- Defines tasks with acceptance criteria
- Establishes dependencies

### 4. Task Execution

Operators execute tasks:
- One task at a time (or parallel if configured)
- Each task is validated before completion
- Failed tasks can be retried or patched

### 5. Validation

Validator checks each task:
- Verifies acceptance criteria
- Runs tests if configured
- Checks linting if enabled

### 6. Mission Completion

Mission completes when all objectives are done:
- Summary of changes
- Lessons learned stored in memory
- Checkpoint created

---

## CLI Commands

Delta9 provides CLI commands for mission management:

### Setup

Install or update Delta9 agents:

```bash
# Interactive setup wizard
delta9 setup

# Preview changes
delta9 setup --dry-run

# Force reinstall
delta9 setup --force

# Non-interactive (for scripts)
delta9 setup --no-tui
```

### Status

View current mission status:

```bash
# Summary view
delta9 status

# Detailed with all tasks
delta9 status --verbose

# JSON output for scripting
delta9 status --format json
```

### History

View event history:

```bash
# Last 20 events
delta9 history

# Filter by type
delta9 history --type mission
delta9 history --category council

# Different formats
delta9 history --format json
delta9 history --format table
```

### Health

Check environment health:

```bash
# Basic check
delta9 health

# Verbose diagnostics
delta9 health --verbose
```

### Abort

Abort the current mission:

```bash
# Abort with checkpoint
delta9 abort --reason "Requirements changed"

# Force abort (if already aborted)
delta9 abort --force

# Skip checkpoint
delta9 abort --no-checkpoint
```

### Resume

Resume an aborted mission:

```bash
# Resume from latest checkpoint
delta9 resume

# Resume specific checkpoint
delta9 resume abort_1706123456789

# Don't reset failed tasks
delta9 resume --no-reset-failed
```

---

## Mission Templates

Pre-built templates for common scenarios:

### Feature Templates

```
> Use the feature template for user profile page
```

Available: `feature`, `simple-feature`, `complex-feature`

### Bugfix Templates

```
> Use the critical-bugfix template for the payment failure
```

Available: `bugfix`, `quick-bugfix`, `critical-bugfix`, `security-bugfix`

### Refactor Templates

```
> Use the large-refactor template to reorganize the API layer
```

Available: `refactor`, `quick-refactor`, `large-refactor`, `performance-refactor`, `type-safety-refactor`

### Template Variables

Templates use variables that get replaced:

```
{{feature_name}} - Name of the feature
{{affected_area}} - Area of codebase affected
{{priority}} - Priority level
{{root_cause}} - Root cause of bug
```

---

## Memory & Learning

Delta9 learns from past missions:

### Pattern Learning

- Successful patterns are remembered
- Failed approaches are avoided
- Confidence decays over 90 days

### Knowledge Blocks

Store project-specific knowledge:

```
> Remember that we use Prisma for database access
> Remember that all API routes need authentication
```

View stored knowledge:

```bash
# Via CLI (future)
delta9 memory list

# Via OpenCode
> Show me what Delta9 has learned about this project
```

### Scoped Memory

Memory has three scopes:
- **Project**: Specific to current project
- **User**: Across all your projects
- **Global**: Shared patterns (read-only)

---

## Budget Management

Track and control API costs:

### Configuration

```json
{
  "budget": {
    "enabled": true,
    "defaultLimit": 10.00,
    "warnAt": 0.7,
    "pauseAt": 0.9,
    "trackByAgent": true
  }
}
```

### Behavior

- **70% budget**: Warning displayed
- **90% budget**: Mission pauses, confirmation required
- **100% budget**: Mission stops

### Per-Mission Budget

Set budget for specific missions:

```
> Build auth system (budget: $5)
```

### Viewing Costs

```bash
delta9 status --format json | jq '.budget'
```

---

## Troubleshooting

### Mission Won't Start

1. Check OpenCode is running: `opencode --version`
2. Verify plugin loaded: `delta9 health`
3. Check API keys configured for desired models

### Council Not Convening

1. Task may be classified as simple (NONE mode)
2. Check council is enabled: `council.enabled: true`
3. Verify at least one Oracle has valid API key

### Tasks Failing Repeatedly

1. View task details: `delta9 status --verbose`
2. Check validation criteria aren't too strict
3. Review `validator.strictMode` setting

### State Corruption

1. View raw state: `cat .delta9/mission.json`
2. Resume from checkpoint: `delta9 resume`
3. If needed, delete state: `rm -rf .delta9`

### API Rate Limits

Delta9 handles rate limits automatically:
- Exponential backoff on 429 errors
- Fallback to alternate models if configured
- Request queuing during high load

### Context Too Large

If context compaction happens:
- Mission state persists (`.delta9/mission.json`)
- Resume with: `> continue the mission`
- Critical context is preserved via hooks

---

## Best Practices

### 1. Be Specific

```
# Good
> Implement JWT authentication with refresh tokens, storing tokens in httpOnly cookies

# Less good
> Add auth
```

### 2. Use Templates for Common Tasks

Templates ensure consistent, thorough implementation.

### 3. Let the Council Work

Don't skip council for complex tasks. The diverse perspectives catch issues early.

### 4. Review Validation Results

Even when tasks pass, review the validator's notes for improvements.

### 5. Set Appropriate Budgets

Start with lower budgets to understand cost patterns, then adjust.

### 6. Use Checkpoints

Enable auto-checkpoints for long missions:

```json
{
  "mission": {
    "autoCheckpoint": true,
    "checkpointOn": "objective_complete"
  }
}
```

---

## Next Steps

- [Configuration Reference](CONFIGURATION.md) - All configuration options
- [Examples](EXAMPLES.md) - Example missions and workflows
- [Specification](spec.md) - Full technical specification

---

*Delta9 - Strategic AI Coordination for Mission-Critical Development*
