# Delta9 Examples

> Example missions, configurations, and workflows

---

## Table of Contents

1. [Example Missions](#example-missions)
2. [Configuration Examples](#configuration-examples)
3. [CLI Usage Examples](#cli-usage-examples)
4. [Template Usage](#template-usage)
5. [Integration Patterns](#integration-patterns)

---

## Example Missions

### Simple Feature: Add Dark Mode Toggle

**Request:**
```
> Add a dark mode toggle to the settings page
```

**What Happens:**

1. Commander classifies as MODERATE complexity (QUICK mode)
2. CIPHER provides architectural recommendation
3. Mission created with 2 objectives:

```
Mission: Dark Mode Toggle
├── Objective 1: Backend
│   ├── Task 1.1: Add user preference storage
│   └── Task 1.2: Create preference API endpoint
└── Objective 2: Frontend
    ├── Task 2.1: Create toggle component
    ├── Task 2.2: Integrate with settings page
    └── Task 2.3: Apply theme CSS variables
```

**Sample Council Response:**

```json
{
  "oracle": "Cipher",
  "recommendation": "Store preference in localStorage for immediate effect, sync to user profile for persistence across devices",
  "confidence": 0.85,
  "caveats": ["Consider prefers-color-scheme media query for initial state"]
}
```

---

### Complex Feature: User Authentication System

**Request:**
```
> Build a complete user authentication system with JWT tokens, refresh token rotation, and OAuth social login
```

**What Happens:**

1. Commander classifies as HIGH complexity (STANDARD mode)
2. Full council convenes (all 4 oracles)
3. Mission created with 5 objectives, 18 tasks

**Council Synthesis:**

```
CIPHER (Architecture): Use httpOnly cookies for token storage. Implement refresh token rotation with family tracking.

VECTOR (Logic): Edge cases - expired refresh token, concurrent requests, race conditions in token refresh.

PRISM (UX): Add "Remember me" option, show login providers prominently, provide clear error messages.

APEX (Performance): Token verification should use caching. Consider Redis for session store at scale.

Consensus: 4/4 agree on httpOnly cookies. 3/4 prefer refresh token rotation.
Conflict: PRISM suggests localStorage for token, others disagree (security).
```

**Mission Structure:**

```
Mission: User Authentication System
├── Objective 1: Core Auth Infrastructure
│   ├── Task 1.1: Set up JWT library and config
│   ├── Task 1.2: Create auth middleware
│   ├── Task 1.3: Implement token generation
│   └── Task 1.4: Implement refresh token rotation
├── Objective 2: API Endpoints
│   ├── Task 2.1: POST /auth/register
│   ├── Task 2.2: POST /auth/login
│   ├── Task 2.3: POST /auth/logout
│   ├── Task 2.4: POST /auth/refresh
│   └── Task 2.5: GET /auth/me
├── Objective 3: OAuth Integration
│   ├── Task 3.1: Google OAuth setup
│   ├── Task 3.2: GitHub OAuth setup
│   └── Task 3.3: OAuth callback handling
├── Objective 4: Frontend Integration
│   ├── Task 4.1: Auth context provider
│   ├── Task 4.2: Login/Register forms
│   ├── Task 4.3: Protected route component
│   └── Task 4.4: OAuth button components
└── Objective 5: Testing & Security
    ├── Task 5.1: Unit tests for auth logic
    ├── Task 5.2: Integration tests for endpoints
    └── Task 5.3: Security audit
```

---

### Critical Task: Database Migration

**Request:**
```
> Migrate from MongoDB to PostgreSQL (critical - this is our production database)
```

**What Happens:**

1. Keyword "critical" triggers XHIGH mode
2. Scout searches codebase for all MongoDB usage
3. Intel gathers PostgreSQL migration best practices
4. Full council with recon intelligence

**XHIGH Reconnaissance:**

```
Scout Report:
- 47 files using MongoDB
- 12 Mongoose models
- 3 aggregation pipelines
- 2 MongoDB-specific features (change streams, geospatial)

Intel Report:
- Recommended ORM: Prisma (type-safe, migration support)
- PostgreSQL equivalents for geospatial: PostGIS
- Change streams alternative: LISTEN/NOTIFY
```

**Council Response (with recon context):**

```
CIPHER: Given 47 files, recommend phased migration. Start with read-only models, then write operations.

VECTOR: The 3 aggregation pipelines need special attention. Map each to SQL CTEs or views.

PRISM: Add monitoring dashboard to track migration progress. Users should see no degradation.

APEX: Change streams → PostgreSQL LISTEN/NOTIFY has different latency characteristics. Add caching layer.
```

---

### Quick Fix: Typo Correction

**Request:**
```
> quick fix the typo in the header component - it says "Wlecome" instead of "Welcome"
```

**What Happens:**

1. Keyword "quick" triggers NONE mode
2. Commander handles directly (no council)
3. Single task dispatched to Patcher agent

```
Mission: Fix Header Typo
└── Objective 1: Typo Fix
    └── Task 1.1: Correct "Wlecome" → "Welcome" in header.tsx
```

**Result:** Fast, efficient fix without council overhead.

---

## Configuration Examples

### Minimal Configuration (Budget-Conscious)

For users wanting minimal API costs:

```json
{
  "council": {
    "enabled": true,
    "defaultMode": "quick",
    "members": [
      {
        "name": "Cipher",
        "model": "anthropic/claude-haiku-4",
        "enabled": true,
        "specialty": "architecture"
      }
    ],
    "minResponses": 1
  },
  "operators": {
    "defaultModel": "anthropic/claude-haiku-4",
    "maxParallel": 1
  },
  "budget": {
    "enabled": true,
    "defaultLimit": 2.0,
    "warnAt": 0.5
  }
}
```

**Estimated cost:** ~$0.10-0.50 per mission

---

### Maximum Quality Configuration

For critical projects requiring thorough analysis:

```json
{
  "council": {
    "enabled": true,
    "defaultMode": "standard",
    "autoDetectComplexity": true,
    "members": [
      {
        "name": "Cipher",
        "model": "anthropic/claude-opus-4-5",
        "enabled": true,
        "specialty": "architecture",
        "temperature": 0.2
      },
      {
        "name": "Vector",
        "model": "openai/gpt-4o",
        "enabled": true,
        "specialty": "logic",
        "temperature": 0.4
      },
      {
        "name": "Prism",
        "model": "google/gemini-2.0-flash",
        "enabled": true,
        "specialty": "ui",
        "temperature": 0.6
      },
      {
        "name": "Apex",
        "model": "deepseek/deepseek-chat",
        "enabled": true,
        "specialty": "performance",
        "temperature": 0.3
      }
    ],
    "requireConsensus": true,
    "minResponses": 3
  },
  "operators": {
    "defaultModel": "anthropic/claude-sonnet-4",
    "complexModel": "anthropic/claude-opus-4-5",
    "maxParallel": 2
  },
  "validator": {
    "model": "anthropic/claude-sonnet-4",
    "strictMode": true,
    "runTests": true,
    "checkLinting": true
  },
  "budget": {
    "enabled": true,
    "defaultLimit": 50.0
  }
}
```

**Estimated cost:** ~$2-10 per mission

---

### Single Provider Configuration (Anthropic Only)

For users with only Anthropic API access:

```json
{
  "council": {
    "enabled": true,
    "members": [
      {
        "name": "Cipher",
        "model": "anthropic/claude-opus-4-5",
        "enabled": true,
        "specialty": "architecture"
      },
      {
        "name": "Vector",
        "model": "anthropic/claude-sonnet-4",
        "enabled": true,
        "specialty": "logic"
      }
    ],
    "minResponses": 1
  },
  "operators": {
    "defaultModel": "anthropic/claude-sonnet-4",
    "complexModel": "anthropic/claude-opus-4-5"
  },
  "support": {
    "scout": { "model": "anthropic/claude-haiku-4" },
    "intel": { "model": "anthropic/claude-sonnet-4" },
    "strategist": { "model": "anthropic/claude-sonnet-4" },
    "uiOps": { "model": "anthropic/claude-sonnet-4" },
    "scribe": { "model": "anthropic/claude-haiku-4" },
    "optics": { "model": "anthropic/claude-sonnet-4" },
    "qa": { "model": "anthropic/claude-sonnet-4" }
  }
}
```

---

### CI/CD Configuration

For automated pipeline usage:

```json
{
  "council": {
    "enabled": true,
    "defaultMode": "quick",
    "timeoutSeconds": 60
  },
  "operators": {
    "maxParallel": 5,
    "retryLimit": 1
  },
  "validator": {
    "strictMode": true,
    "runTests": true
  },
  "budget": {
    "enabled": true,
    "defaultLimit": 5.0,
    "pauseAt": 1.0
  },
  "notifications": {
    "enabled": true,
    "slackWebhook": "https://hooks.slack.com/...",
    "onEvents": ["mission_complete", "validation_failed"]
  },
  "ui": {
    "showProgress": false,
    "verboseLogs": true
  }
}
```

---

### Frontend-Focused Configuration

For frontend-heavy projects:

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
        "name": "Prism",
        "model": "google/gemini-2.0-flash",
        "enabled": true,
        "specialty": "ui",
        "temperature": 0.7
      }
    ]
  },
  "support": {
    "uiOps": {
      "model": "google/gemini-2.0-flash",
      "styleSystem": "tailwind"
    }
  },
  "seamless": {
    "keywords": {
      "councilXhigh": ["redesign", "accessibility", "responsive"]
    }
  }
}
```

---

## CLI Usage Examples

### Mission Status

```bash
# Quick status overview
$ delta9 status

Mission: User Authentication System
Status: in_progress
Progress: 8/18 tasks (44%)

Objectives:
  ✓ Core Auth Infrastructure (4/4)
  → API Endpoints (3/5)
  ○ OAuth Integration (0/3)
  ○ Frontend Integration (0/4)
  ○ Testing & Security (1/3)

Budget: $2.34 / $10.00 (23%)
```

```bash
# Detailed task breakdown
$ delta9 status --verbose

Mission: User Authentication System
...
Current Task: POST /auth/refresh
  Agent: operator
  Status: in_progress
  Started: 2 minutes ago

Next Tasks:
  1. POST /auth/logout (blocked by: POST /auth/refresh)
  2. Google OAuth setup
  3. GitHub OAuth setup
```

```bash
# JSON output for scripting
$ delta9 status --format json | jq '.progress'
{
  "completed": 8,
  "total": 18,
  "percentage": 44
}
```

---

### Event History

```bash
# Recent events
$ delta9 history

Timeline:
  2024-01-15 10:23:45  mission.created    User Authentication System
  2024-01-15 10:23:47  council.convened   mode=STANDARD oracles=4
  2024-01-15 10:24:12  council.complete   consensus=true
  2024-01-15 10:24:15  task.started       Set up JWT library
  2024-01-15 10:25:03  task.completed     Set up JWT library
  ...
```

```bash
# Filter by category
$ delta9 history --category council

Timeline:
  2024-01-15 10:23:47  council.convened   mode=STANDARD oracles=4
  2024-01-15 10:24:12  council.complete   consensus=true
```

```bash
# Filter by type prefix
$ delta9 history --type validation

Timeline:
  2024-01-15 10:25:05  validation.started   Set up JWT library
  2024-01-15 10:25:08  validation.passed    Set up JWT library
  ...
```

---

### Health Check

```bash
$ delta9 health

Delta9 Health Check
==================

Environment:
  ✓ OpenCode version: 1.2.3
  ✓ Node.js version: 20.10.0
  ✓ npm version: 10.2.3

Configuration:
  ✓ Config loaded from: .delta9/config.json
  ✓ 4 oracles configured
  ✓ Budget tracking enabled

State:
  ✓ State directory: .delta9
  ✓ Active mission: User Authentication System
  ✓ 15 events in history

Models:
  ✓ anthropic/claude-opus-4-5: accessible
  ✓ openai/gpt-4o: accessible
  ✓ google/gemini-2.0-flash: accessible
  ✓ deepseek/deepseek-chat: accessible

Overall: HEALTHY
```

```bash
# Verbose mode shows more details
$ delta9 health --verbose
```

---

### Abort and Resume

```bash
# Abort current mission
$ delta9 abort --reason "Requirements changed"

Mission Aborted
  Mission: User Authentication System
  Previous Status: in_progress
  Tasks Completed: 8
  Tasks Aborted: 10
  Checkpoint: abort_1705312456789

Tip: Use "delta9 resume abort_1705312456789" to resume

# Later, resume the mission
$ delta9 resume abort_1705312456789

Mission Resumed
  Mission: User Authentication System
  Checkpoint: abort_1705312456789
  Tasks Reset: 2 (previously failed)
  New Status: paused

Ready for Commander to continue.
```

---

## Template Usage

### Using Feature Template

```
> Use the complex-feature template for implementing real-time notifications
```

**Variables provided:**
```
feature_name: Real-time Notifications
affected_area: Notification System
```

**Generated Mission:**
```
Mission: Real-time Notifications
├── Objective 1: Analysis & Research
│   ├── Research existing patterns
│   └── Identify affected components
├── Objective 2: Backend Implementation
│   ├── Create notification service
│   ├── Implement real-time transport
│   └── Add persistence layer
├── Objective 3: Frontend Implementation
│   ├── Create notification components
│   ├── Integrate real-time client
│   └── Add notification preferences
├── Objective 4: Integration
│   └── Connect all components
└── Objective 5: Testing & Documentation
    ├── Write unit tests
    ├── Write integration tests
    └── Update documentation
```

---

### Using Bugfix Template

```
> Use the security-bugfix template for the SQL injection in user search
```

**Variables provided:**
```
bug_description: SQL injection vulnerability in user search
root_cause: Unsanitized user input in SQL query
affected_area: User Search API
severity: critical
```

**Generated Mission:**
```
Mission: Fix SQL Injection in User Search
├── Objective 1: Security Analysis
│   ├── Identify all injection points
│   ├── Assess data exposure risk
│   └── Document attack vectors
├── Objective 2: Implement Fix
│   ├── Parameterize queries
│   ├── Add input validation
│   └── Implement output encoding
├── Objective 3: Verification
│   ├── Write security tests
│   ├── Run penetration tests
│   └── Verify fix effectiveness
└── Objective 4: Post-Fix
    ├── Update security documentation
    ├── Add to security checklist
    └── Create incident report
```

---

### Using Refactor Template

```
> Use the large-refactor template to reorganize the API layer into domain modules
```

**Generated Mission (9 objectives, 27 tasks):**
- Analysis and planning phase
- Incremental migration phases
- Comprehensive testing
- Documentation updates

---

## Integration Patterns

### With Git Hooks

`.git/hooks/pre-commit`:
```bash
#!/bin/bash
# Run Delta9 validation before commit
delta9 status --format json | jq -e '.validation.passing' || exit 1
```

### With GitHub Actions

`.github/workflows/delta9.yml`:
```yaml
name: Delta9 CI
on: [push, pull_request]

jobs:
  validate:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
      - uses: actions/setup-node@v4
        with:
          node-version: '20'
      - run: npm install
      - run: delta9 health --format json
      - run: delta9 status --format json
```

### With VS Code

`.vscode/tasks.json`:
```json
{
  "version": "2.0.0",
  "tasks": [
    {
      "label": "Delta9 Status",
      "type": "shell",
      "command": "delta9 status",
      "problemMatcher": []
    },
    {
      "label": "Delta9 Health",
      "type": "shell",
      "command": "delta9 health",
      "problemMatcher": []
    }
  ]
}
```

---

## See Also

- [User Guide](USER_GUIDE.md) - Complete usage guide
- [Configuration](CONFIGURATION.md) - Configuration reference
- [Specification](spec.md) - Technical specification
