# Paperclip Skills, Plugins, CLI & Shared Types Reference

Comprehensive reference document covering Paperclip's skills system, plugin SDK, CLI, shared types, company import/export, and evaluation system. Sourced from `docs/reference-code/paperclip/`.

---

## Table of Contents

1. [Skills System](#1-skills-system)
   - [SKILL.md Format](#skillmd-format)
   - [Core Paperclip Skill](#core-paperclip-skill)
   - [Create Agent Skill](#create-agent-skill)
   - [Create Plugin Skill](#create-plugin-skill)
   - [PARA Memory Skill](#para-memory-skill)
   - [Company Skills Workflow](#company-skills-workflow)
2. [Plugin System](#2-plugin-system)
   - [Architecture Overview](#architecture-overview)
   - [Plugin Manifest (V1)](#plugin-manifest-v1)
   - [Plugin SDK (`definePlugin`)](#plugin-sdk-defineplugin)
   - [Worker-Side Context (`PluginContext`)](#worker-side-context-plugincontext)
   - [Worker Lifecycle Hooks](#worker-lifecycle-hooks)
   - [UI Extension Slots](#ui-extension-slots)
   - [UI SDK Hooks](#ui-sdk-hooks)
   - [Scheduled Jobs](#scheduled-jobs)
   - [Webhooks](#webhooks)
   - [Event Subscriptions](#event-subscriptions)
   - [State Storage](#state-storage)
   - [Plugin Entities](#plugin-entities)
   - [Agent Tools from Plugins](#agent-tools-from-plugins)
   - [Agent Sessions (Two-Way Chat)](#agent-sessions-two-way-chat)
   - [Streaming (Worker to UI)](#streaming-worker-to-ui)
   - [Launchers](#launchers)
   - [Capabilities Model](#capabilities-model)
   - [Plugin Record and Lifecycle](#plugin-record-and-lifecycle)
   - [Scaffolding a Plugin](#scaffolding-a-plugin)
   - [Kitchen Sink Example](#kitchen-sink-example)
3. [CLI](#3-cli)
   - [CLI Commands](#cli-commands)
   - [Onboarding Flow](#onboarding-flow)
   - [Configuration Schema](#configuration-schema)
   - [Doctor / Diagnostics](#doctor--diagnostics)
   - [Client Commands](#client-commands)
   - [Adapters](#adapters)
   - [Heartbeat Execution](#heartbeat-execution)
4. [Shared Types](#4-shared-types)
   - [Constants and Enums](#constants-and-enums)
   - [Core Domain Types](#core-domain-types)
   - [Validators (Zod Schemas)](#validators-zod-schemas)
5. [Company Import/Export](#5-company-importexport)
   - [Portability Manifest](#portability-manifest)
   - [Export Flow](#export-flow)
   - [Import Flow](#import-flow)
   - [CEO-Safe Import Rules](#ceo-safe-import-rules)
6. [Evaluation System](#6-evaluation-system)
   - [Promptfoo Configuration](#promptfoo-configuration)
   - [Test Categories](#test-categories)
   - [Eval Phases](#eval-phases)

---

## 1. Skills System

### SKILL.md Format

Every skill directory contains a `SKILL.md` file with YAML frontmatter and markdown body. The frontmatter provides metadata for injection:

```yaml
---
name: skill-name
description: >
  Multi-line description of when to use this skill. Used by the system
  to decide when to inject the skill into the agent's prompt.
---

# Skill Title

Markdown body with instructions, API references, code examples, etc.
```

Skills are injected into agent system prompts. The `description` field controls injection triggers. Skills can have a `references/` subdirectory with additional files that the agent reads on demand (lazy loading).

**Skill directory structure:**
```
skills/
  skill-name/
    SKILL.md              # Main skill definition (always injected)
    references/           # On-demand reference files
      api-reference.md
      schemas.md
```

### Core Paperclip Skill

**Location:** `skills/paperclip/SKILL.md`

The core coordination skill. Injected for all Paperclip agents. Defines the **heartbeat procedure** -- the fundamental execution model where agents wake up in short windows, do work, and exit.

#### Environment Variables (Auto-Injected)

| Variable | Description |
|----------|-------------|
| `PAPERCLIP_AGENT_ID` | Agent's UUID |
| `PAPERCLIP_COMPANY_ID` | Company UUID |
| `PAPERCLIP_API_URL` | Base API URL (never hard-code) |
| `PAPERCLIP_RUN_ID` | Current heartbeat run UUID |
| `PAPERCLIP_API_KEY` | Short-lived JWT for API auth |
| `PAPERCLIP_TASK_ID` | Issue/task that triggered this wake (optional) |
| `PAPERCLIP_WAKE_REASON` | Why this run was triggered (optional) |
| `PAPERCLIP_WAKE_COMMENT_ID` | Comment that triggered this wake (optional) |
| `PAPERCLIP_APPROVAL_ID` | Approval that triggered this wake (optional) |
| `PAPERCLIP_APPROVAL_STATUS` | Status of triggering approval (optional) |
| `PAPERCLIP_LINKED_ISSUE_IDS` | Comma-separated linked issue IDs (optional) |

#### Heartbeat Procedure (9 Steps)

1. **Identity** -- `GET /api/agents/me` (skip if in context)
2. **Approval follow-up** -- If `PAPERCLIP_APPROVAL_ID` set, review linked issues
3. **Get assignments** -- `GET /api/agents/me/inbox-lite` (compact inbox)
4. **Pick work** -- `in_progress` first, then `todo`; skip `blocked` unless unblockable; honor mention-triggered wakes via `PAPERCLIP_WAKE_COMMENT_ID`
5. **Checkout** -- `POST /api/issues/{issueId}/checkout` with `X-Paperclip-Run-Id` header
6. **Understand context** -- `GET /api/issues/{issueId}/heartbeat-context` then incremental comments
7. **Do the work** -- Use agent tools and capabilities
8. **Update status** -- `PATCH /api/issues/{issueId}` with status + comment + run ID header
9. **Delegate if needed** -- `POST /api/companies/{companyId}/issues` with `parentId` and `goalId`

#### Issue Status Values

`backlog`, `todo`, `in_progress`, `in_review`, `done`, `blocked`, `cancelled`

#### Issue Priority Values

`critical`, `high`, `medium`, `low`

#### Critical Rules

- Always checkout before working; never PATCH to `in_progress` manually
- Never retry a 409 Conflict (task belongs to someone else)
- Never look for unassigned work
- Self-assign only for explicit @-mention handoff
- Honor "send it back to me" requests from board users
- Always comment on `in_progress` work before exiting (except blocked-task dedup)
- Always set `parentId` on subtasks
- Never cancel cross-team tasks; reassign to manager
- Budget: auto-paused at 100%; above 80% focus on critical only
- `Co-Authored-By: Paperclip <noreply@paperclip.ing>` on all git commits

#### Comment Style

All ticket references must be links: `[PAP-224](/PAP/issues/PAP-224)`. Company prefix is required in all paths.

Link patterns:
- Issues: `/<prefix>/issues/<identifier>`
- Issue comments: `/<prefix>/issues/<identifier>#comment-<id>`
- Issue documents: `/<prefix>/issues/<identifier>#document-<key>`
- Agents: `/<prefix>/agents/<url-key>`
- Projects: `/<prefix>/projects/<url-key>`
- Approvals: `/<prefix>/approvals/<id>`
- Runs: `/<prefix>/agents/<url-key>/runs/<id>`

#### Planning

Plans are stored as issue documents with key `plan`:
```
PUT /api/issues/{issueId}/documents/plan
{ "title": "Plan", "format": "markdown", "body": "...", "baseRevisionId": null }
```

If plan exists, fetch current document first and pass `baseRevisionId` for updates.

#### Key Endpoints (Full List)

| Action | Endpoint |
|--------|----------|
| My identity | `GET /api/agents/me` |
| Compact inbox | `GET /api/agents/me/inbox-lite` |
| My assignments | `GET /api/companies/:companyId/issues?assigneeAgentId=:id&status=...` |
| Checkout task | `POST /api/issues/:issueId/checkout` |
| Get task + ancestors | `GET /api/issues/:issueId` |
| Heartbeat context | `GET /api/issues/:issueId/heartbeat-context` |
| List comments | `GET /api/issues/:issueId/comments` |
| Get comment delta | `GET /api/issues/:issueId/comments?after=:commentId&order=asc` |
| Get specific comment | `GET /api/issues/:issueId/comments/:commentId` |
| Update task | `PATCH /api/issues/:issueId` (optional `comment` field) |
| Add comment | `POST /api/issues/:issueId/comments` |
| Create subtask | `POST /api/companies/:companyId/issues` |
| Release task | `POST /api/issues/:issueId/release` |
| List agents | `GET /api/companies/:companyId/agents` |
| List company skills | `GET /api/companies/:companyId/skills` |
| Import company skills | `POST /api/companies/:companyId/skills/import` |
| Scan projects for skills | `POST /api/companies/:companyId/skills/scan-projects` |
| Sync agent skills | `POST /api/agents/:agentId/skills/sync` |
| Import preview (CEO-safe) | `POST /api/companies/:companyId/imports/preview` |
| Import apply (CEO-safe) | `POST /api/companies/:companyId/imports/apply` |
| Export preview | `POST /api/companies/:companyId/exports/preview` |
| Export build | `POST /api/companies/:companyId/exports` |
| Dashboard | `GET /api/companies/:companyId/dashboard` |
| Search issues | `GET /api/companies/:companyId/issues?q=term` |
| Upload attachment | `POST /api/companies/:companyId/issues/:issueId/attachments` |
| List attachments | `GET /api/issues/:issueId/attachments` |
| Issue documents | `GET/PUT /api/issues/:issueId/documents/:key` |
| Document revisions | `GET /api/issues/:issueId/documents/:key/revisions` |
| Create project | `POST /api/companies/:companyId/projects` |
| Create workspace | `POST /api/projects/:projectId/workspaces` |
| Set instructions path | `PATCH /api/agents/:agentId/instructions-path` |
| OpenClaw invite | `POST /api/companies/:companyId/openclaw/invite-prompt` |

#### Agent Record Schema

```json
{
  "id": "agent-42",
  "name": "BackendEngineer",
  "role": "engineer",
  "title": "Senior Backend Engineer",
  "companyId": "company-1",
  "reportsTo": "mgr-1",
  "capabilities": "Node.js, PostgreSQL, API design",
  "status": "running",
  "budgetMonthlyCents": 5000,
  "spentMonthlyCents": 1200,
  "chainOfCommand": [
    { "id": "mgr-1", "name": "EngineeringLead", "role": "manager", "title": "VP Engineering" },
    { "id": "ceo-1", "name": "CEO", "role": "ceo", "title": "Chief Executive Officer" }
  ]
}
```

#### Issue Lifecycle

```
backlog -> todo -> in_progress -> in_review -> done
                       |              |
                    blocked       in_progress
                       |
                  todo / in_progress
```

Terminal states: `done`, `cancelled`.

#### Error Handling

| Code | Meaning | Action |
|------|---------|--------|
| 400 | Validation error | Check request body |
| 401 | Unauthenticated | API key missing/invalid |
| 403 | Unauthorized | No permission |
| 404 | Not found | Entity missing or wrong company |
| 409 | Conflict | Another agent owns it; **do not retry** |
| 422 | Semantic violation | Invalid state transition |
| 500 | Server error | Transient; comment and move on |

#### Governance and Approvals

Hire requests create agents as `pending_approval` with linked `hire_agent` approval:
```
POST /api/companies/{companyId}/agent-hires
```

CEO strategy approvals:
```
POST /api/companies/{companyId}/approvals
{ "type": "approve_ceo_strategy", "requestedByAgentId": "...", "payload": { "plan": "..." } }
```

Approval statuses: `pending`, `revision_requested`, `approved`, `rejected`, `cancelled`.

### Create Agent Skill

**Location:** `skills/paperclip-create-agent/SKILL.md`

Used for hiring/creating agents with governance-aware workflows.

#### Workflow

1. Confirm identity via `GET /api/agents/me`
2. Discover adapter docs via `GET /llms/agent-configuration.txt`
3. Read adapter-specific docs via `GET /llms/agent-configuration/:adapterType.txt`
4. Compare existing configs via `GET /api/companies/:companyId/agent-configurations`
5. Discover icons via `GET /llms/agent-icons.txt`
6. Draft hire config (role, title, icon, reporting line, adapter, skills, prompt)
7. Submit via `POST /api/companies/:companyId/agent-hires`
8. Handle governance state (approval flow)

#### Hire Request Body

```json
{
  "name": "CTO",
  "role": "cto",
  "title": "Chief Technology Officer",
  "icon": "crown",
  "reportsTo": "uuid-or-null",
  "capabilities": "Owns architecture and engineering execution",
  "desiredSkills": ["vercel-labs/agent-browser/agent-browser"],
  "adapterType": "claude_local",
  "adapterConfig": { "cwd": "/absolute/path", "model": "claude-sonnet-4-5-20250929", "promptTemplate": "You are CTO..." },
  "runtimeConfig": { "heartbeat": { "enabled": true, "intervalSec": 300, "wakeOnDemand": true } },
  "budgetMonthlyCents": 0,
  "sourceIssueId": "uuid-or-null",
  "sourceIssueIds": ["uuid-1", "uuid-2"]
}
```

#### Hire Response

```json
{
  "agent": { "id": "uuid", "status": "pending_approval" },
  "approval": { "id": "uuid", "type": "hire_agent", "status": "pending", "payload": { "desiredSkills": [...] } }
}
```

If company setting disables required approval, `approval` is `null` and agent starts as `idle`.

### Create Plugin Skill

**Location:** `skills/paperclip-create-plugin/SKILL.md`

Guides agents through scaffolding Paperclip plugins.

#### Ground Rules

- Plugin workers are trusted code
- Plugin UI is trusted same-origin host code
- Worker APIs are capability-gated
- Plugin UI is NOT sandboxed by manifest capabilities
- No host-provided shared UI component kit yet
- `ctx.assets` is NOT supported

#### Scaffold Command

```bash
pnpm --filter @paperclipai/create-paperclip-plugin build
node packages/plugins/create-paperclip-plugin/dist/index.js <npm-package-name> --output <target-dir>
```

For external plugins:
```bash
node packages/plugins/create-paperclip-plugin/dist/index.js @acme/plugin-name \
  --output /path/to/plugin-repos \
  --sdk-path /path/to/paperclip/packages/plugins/sdk
```

#### Verification Steps

```bash
pnpm --filter <plugin-package> typecheck
pnpm --filter <plugin-package> test
pnpm --filter <plugin-package> build
```

### PARA Memory Skill

**Location:** `skills/para-memory-files/SKILL.md`

File-based persistent memory using Tiago Forte's PARA method. Three layers stored relative to `$AGENT_HOME`.

#### Layer 1: Knowledge Graph (`$AGENT_HOME/life/`)

Entity-based storage with PARA organization:

```
$AGENT_HOME/life/
  projects/          # Active work with clear goals/deadlines
    <name>/
      summary.md     # Quick context, load first
      items.yaml     # Atomic facts, load on demand
  areas/             # Ongoing responsibilities, no end date
    people/<name>/
    companies/<name>/
  resources/         # Reference material, topics of interest
    <topic>/
  archives/          # Inactive items from the other three
  index.md
```

**Entity creation thresholds:** Mentioned 3+ times, OR direct relationship to user (family, coworker, partner, client), OR significant project/company.

#### Atomic Fact Schema (`items.yaml`)

```yaml
- id: entity-001
  fact: "The actual fact"
  category: relationship | milestone | status | preference
  timestamp: "YYYY-MM-DD"
  source: "YYYY-MM-DD"
  status: active  # active | superseded
  superseded_by: null  # e.g. entity-002
  related_entities:
    - companies/acme
    - people/jeff
  last_accessed: "YYYY-MM-DD"
  access_count: 0
```

**Fact rules:** Save durable facts immediately. Weekly: rewrite `summary.md`. Never delete -- supersede instead (`status: superseded`, `superseded_by`).

#### Layer 2: Daily Notes (`$AGENT_HOME/memory/YYYY-MM-DD.md`)

Raw timeline of events. Write continuously during conversations. Extract durable facts to Layer 1 during heartbeats.

#### Layer 3: Tacit Knowledge (`$AGENT_HOME/MEMORY.md`)

Patterns, preferences, lessons learned about the user -- not facts about the world.

#### Memory Decay

| Tier | Window | Summary Treatment |
|------|--------|-------------------|
| **Hot** | Accessed in last 7 days | Include prominently |
| **Warm** | 8-30 days ago | Include at lower priority |
| **Cold** | 30+ days or never | Omit from summary (still in items.yaml) |

High `access_count` resists decay. No deletion -- decay only affects `summary.md` curation.

#### Memory Recall via qmd

```bash
qmd query "what happened at Christmas"   # Semantic search with reranking
qmd search "specific phrase"              # BM25 keyword search
qmd vsearch "conceptual question"         # Pure vector similarity
qmd index $AGENT_HOME                     # Index personal folder
```

### Company Skills Workflow

**Location:** `skills/paperclip/references/company-skills.md`

#### Model

1. Install skill into company library
2. Assign company skill to agent
3. Optionally assign during hire/create with `desiredSkills`

#### Source Types (in preference order)

| Source Format | Example | When to Use |
|---|---|---|
| **skills.sh URL** | `https://skills.sh/google-labs-code/stitch-skills/design-md` | Managed registry -- always prefer |
| **Key-style string** | `google-labs-code/stitch-skills/design-md` | Shorthand for skills.sh |
| **GitHub URL** | `https://github.com/vercel-labs/agent-browser` | Repo not on skills.sh |
| **Local path** | `/abs/path/to/skill-dir` | Dev/testing only |

**Critical:** If given a `skills.sh` URL, use it or its key equivalent. Never convert to GitHub URL.

#### Endpoints

| Action | Endpoint |
|--------|----------|
| List company skills | `GET /api/companies/:companyId/skills` |
| Get skill detail | `GET /api/companies/:companyId/skills/:skillId` |
| Read skill file | `GET /api/companies/:companyId/skills/:skillId/files?path=SKILL.md` |
| Import skill | `POST /api/companies/:companyId/skills/import` |
| Scan project workspaces | `POST /api/companies/:companyId/skills/scan-projects` |
| Install/update skill | `POST /api/companies/:companyId/skills/:skillId/install-update` |
| List agent skills | `GET /api/agents/:agentId/skills` |
| Sync agent skills | `POST /api/agents/:agentId/skills/sync` |

#### Permission Model

- **Reads:** any same-company actor
- **Mutations:** board, CEO, or agent with `agents:create` capability
- **Agent skill assignment:** same permission model as updating that agent

---

## 2. Plugin System

### Architecture Overview

Paperclip plugins have three components:
1. **Manifest** (`src/manifest.ts`) -- declares capabilities, entrypoints, UI slots, jobs, webhooks, tools
2. **Worker** (`src/worker.ts`) -- server-side logic running in a trusted process
3. **UI bundle** (`src/ui/index.tsx`) -- React components for host UI extension slots

Communication flows: **UI <-> Host Bridge <-> Worker <-> Host APIs**

### Plugin Manifest (V1)

```typescript
interface PaperclipPluginManifestV1 {
  id: string;                              // Globally unique (e.g. "acme.linear-sync")
  apiVersion: 1;                           // Must be 1
  version: string;                         // Semver (e.g. "1.2.0")
  displayName: string;                     // Max 100 chars
  description: string;                     // Max 500 chars
  author: string;                          // Max 200 chars
  categories: PluginCategory[];            // "connector" | "workspace" | "automation" | "ui"
  minimumHostVersion?: string;             // Semver lower bound
  capabilities: PluginCapability[];        // Required host permissions
  entrypoints: {
    worker: string;                        // Worker entrypoint path (required)
    ui?: string;                           // UI bundle directory (required when ui.slots declared)
  };
  instanceConfigSchema?: JsonSchema;       // JSON Schema for operator config
  jobs?: PluginJobDeclaration[];           // Scheduled jobs
  webhooks?: PluginWebhookDeclaration[];   // Webhook endpoints
  tools?: PluginToolDeclaration[];         // Agent tools
  launchers?: PluginLauncherDeclaration[]; // Legacy; prefer ui.launchers
  ui?: PluginUiDeclaration;               // UI slots and launchers
}
```

### Plugin SDK (`definePlugin`)

```typescript
import { definePlugin } from "@paperclipai/plugin-sdk";

export default definePlugin({
  async setup(ctx: PluginContext) {
    // Register event handlers, jobs, data/action handlers, tools
  },
  async onHealth() { return { status: "ok" }; },
  async onConfigChanged(newConfig) { /* handle live config updates */ },
  async onShutdown() { /* cleanup within 10s */ },
  async onValidateConfig(config) { return { ok: true }; },
  async onWebhook(input) { /* handle webhook delivery */ },
});
```

`definePlugin()` returns a frozen `PaperclipPlugin` object. The only required field is `setup()`.

### Worker-Side Context (`PluginContext`)

The full `PluginContext` passed to `setup()`:

```typescript
interface PluginContext {
  manifest: PaperclipPluginManifestV1;     // Validated manifest
  config: PluginConfigClient;               // Read operator config
  events: PluginEventsClient;               // Subscribe/emit events
  jobs: PluginJobsClient;                   // Register job handlers
  launchers: PluginLaunchersClient;         // Register launcher metadata
  http: PluginHttpClient;                   // Outbound HTTP
  secrets: PluginSecretsClient;             // Resolve secret references
  activity: PluginActivityClient;           // Write activity log entries
  state: PluginStateClient;                 // Scoped key-value state
  entities: PluginEntitiesClient;           // Plugin-owned entity records
  projects: PluginProjectsClient;           // Read projects/workspaces
  companies: PluginCompaniesClient;         // Read company metadata
  issues: PluginIssuesClient;              // CRUD issues/comments/documents
  agents: PluginAgentsClient;              // Read/manage agents + sessions
  goals: PluginGoalsClient;                // CRUD goals
  data: PluginDataClient;                  // Register getData handlers for UI
  actions: PluginActionsClient;            // Register performAction handlers for UI
  streams: PluginStreamsClient;            // Push real-time events to UI via SSE
  tools: PluginToolsClient;               // Register agent tool handlers
  metrics: PluginMetricsClient;            // Write plugin metrics
  logger: PluginLogger;                    // Structured logging
}
```

#### Key Client Interfaces

**`ctx.state`** -- Scoped key-value store with five-part composite key:

| `scopeKind` | `scopeId` | Typical Use |
|-------------|-----------|-------------|
| `"instance"` | omit | Global flags, last full-sync timestamps |
| `"company"` | company UUID | Per-company sync cursors |
| `"project"` | project UUID | Per-project settings, branch tracking |
| `"project_workspace"` | workspace UUID | Per-workspace state |
| `"agent"` | agent UUID | Per-agent memory |
| `"issue"` | issue UUID | Idempotency keys, linked external IDs |
| `"goal"` | goal UUID | Per-goal progress |
| `"run"` | run UUID | Per-run checkpoints |

```typescript
interface PluginStateClient {
  get(input: ScopeKey): Promise<unknown>;
  set(input: ScopeKey, value: unknown): Promise<void>;
  delete(input: ScopeKey): Promise<void>;
}

interface ScopeKey {
  scopeKind: PluginStateScopeKind;
  scopeId?: string;
  namespace?: string;   // Default: "default"
  stateKey: string;
}
```

**`ctx.events`** -- Subscribe to domain events or emit plugin events:

```typescript
interface PluginEventsClient {
  on(name: PluginEventType | `plugin.${string}`, fn: (event: PluginEvent) => Promise<void>): () => void;
  on(name: PluginEventType | `plugin.${string}`, filter: EventFilter, fn: ...): () => void;
  emit(name: string, companyId: string, payload: unknown): Promise<void>;
}
```

**`ctx.issues`** -- Full CRUD plus documents:

```typescript
interface PluginIssuesClient {
  list(input: { companyId, projectId?, assigneeAgentId?, status?, limit?, offset? }): Promise<Issue[]>;
  get(issueId, companyId): Promise<Issue | null>;
  create(input: { companyId, projectId?, goalId?, parentId?, title, description?, priority?, assigneeAgentId? }): Promise<Issue>;
  update(issueId, patch, companyId): Promise<Issue>;
  listComments(issueId, companyId): Promise<IssueComment[]>;
  createComment(issueId, body, companyId): Promise<IssueComment>;
  documents: PluginIssueDocumentsClient;  // list, get, upsert, delete
}
```

**`ctx.agents`** -- Includes agent sessions:

```typescript
interface PluginAgentsClient {
  list(input: { companyId, status?, limit?, offset? }): Promise<Agent[]>;
  get(agentId, companyId): Promise<Agent | null>;
  pause(agentId, companyId): Promise<Agent>;
  resume(agentId, companyId): Promise<Agent>;
  invoke(agentId, companyId, opts: { prompt, reason? }): Promise<{ runId: string }>;
  sessions: PluginAgentSessionsClient;
}
```

### Worker Lifecycle Hooks

| Hook | Purpose | Default if Missing |
|------|---------|-------------------|
| `setup(ctx)` | Register all handlers (required) | N/A |
| `onHealth()` | Report plugin health | Host infers from process liveness |
| `onConfigChanged(newConfig)` | Handle live config update | Worker restarts |
| `onShutdown()` | Cleanup (10s budget) | SIGTERM then SIGKILL |
| `onValidateConfig(config)` | Validate config on save/"Test Connection" | Skip validation |
| `onWebhook(input)` | Handle webhook delivery | HTTP 501 |

#### Health Diagnostics

```typescript
interface PluginHealthDiagnostics {
  status: "ok" | "degraded" | "error";
  message?: string;
  details?: Record<string, unknown>;
}
```

#### Config Validation Result

```typescript
interface PluginConfigValidationResult {
  ok: boolean;
  warnings?: string[];
  errors?: string[];
}
```

#### Webhook Input

```typescript
interface PluginWebhookInput {
  endpointKey: string;
  headers: Record<string, string | string[]>;
  rawBody: string;
  parsedBody?: unknown;
  requestId: string;
}
```

### UI Extension Slots

Paperclip provides 13 slot types for plugin UI components:

| Slot Type | Description | Props Interface |
|-----------|-------------|-----------------|
| `page` | Full-page extension at `/:company/:routePath` | `PluginPageProps` |
| `detailTab` | Tab on entity detail pages | `PluginDetailTabProps` |
| `taskDetailView` | Full view replacement on issue detail | `PluginDetailTabProps` |
| `dashboardWidget` | Card on main dashboard | `PluginWidgetProps` |
| `sidebar` | Link/section in app sidebar | `PluginSidebarProps` |
| `sidebarPanel` | Expandable panel in sidebar | `PluginSidebarProps` |
| `projectSidebarItem` | Per-project item under project row | `PluginProjectSidebarItemProps` |
| `globalToolbarButton` | Button in global toolbar | `PluginWidgetProps` |
| `toolbarButton` | Button in entity toolbar | `PluginWidgetProps` |
| `contextMenuItem` | Item in entity context menu | `PluginWidgetProps` |
| `commentAnnotation` | Annotation below each comment | `PluginCommentAnnotationProps` |
| `commentContextMenuItem` | Item in comment "more" menu | `PluginCommentContextMenuItemProps` |
| `settingsPage` | Custom settings UI (overrides auto-generated form) | `PluginSettingsPageProps` |

#### Slot Declaration

```typescript
interface PluginUiSlotDeclaration {
  type: PluginUiSlotType;
  id: string;                              // Unique within plugin
  displayName: string;                     // Tab/nav label
  exportName: string;                      // UI bundle export name
  entityTypes?: PluginUiSlotEntityType[];  // "project"|"issue"|"agent"|"goal"|"run"|"comment"
  routePath?: string;                      // Only for "page" slots
  order?: number;                          // Lower = first
}
```

#### Entity Types for Context-Sensitive Slots

`"project"`, `"issue"`, `"agent"`, `"goal"`, `"run"`, `"comment"`

#### Reserved Route Segments (cannot be used as `routePath`)

`dashboard`, `onboarding`, `companies`, `company`, `settings`, `plugins`, `org`, `agents`, `projects`, `issues`, `goals`, `approvals`, `costs`, `activity`, `inbox`, `design-guide`, `tests`

### UI SDK Hooks

Plugin UI components import from `@paperclipai/plugin-sdk/ui`:

#### `usePluginData<T>(key, params?)`

Fetch data from worker's `getData` handler. Returns:
```typescript
interface PluginDataResult<T> {
  data: T | null;
  loading: boolean;
  error: PluginBridgeError | null;
  refresh(): void;
}
```

#### `usePluginAction(key)`

Returns async function to call worker's `performAction` handler:
```typescript
type PluginActionFn = (params?: Record<string, unknown>) => Promise<unknown>;
```

#### `useHostContext()`

Returns current host context:
```typescript
interface PluginHostContext {
  companyId: string | null;
  companyPrefix: string | null;
  projectId: string | null;
  entityId: string | null;
  entityType: string | null;
  parentEntityId?: string | null;
  userId: string | null;
  renderEnvironment?: PluginRenderEnvironmentContext | null;
}
```

#### `usePluginStream<T>(channel, options?)`

Subscribe to real-time SSE events from worker:
```typescript
interface PluginStreamResult<T> {
  events: T[];
  lastEvent: T | null;
  connecting: boolean;
  connected: boolean;
  error: Error | null;
  close(): void;
}
```

#### `usePluginToast()`

Trigger host toast notifications:
```typescript
interface PluginToastInput {
  id?: string;
  dedupeKey?: string;
  title: string;
  body?: string;
  tone?: "info" | "success" | "warn" | "error";
  ttlMs?: number;
  action?: { label: string; href: string };
}
```

#### Bridge Error Codes

| Code | Meaning |
|------|---------|
| `WORKER_UNAVAILABLE` | Worker not running |
| `CAPABILITY_DENIED` | Missing required capability |
| `WORKER_ERROR` | Worker handler returned error |
| `TIMEOUT` | Worker didn't respond in time |
| `UNKNOWN` | Unexpected bridge failure |

### Scheduled Jobs

Declared in manifest, handlers registered in worker:

```typescript
// Manifest
{
  jobs: [{
    jobKey: "full-sync",
    displayName: "Full Sync",
    description: "Periodic full synchronization",
    schedule: "0 * * * *"  // Cron expression
  }]
}

// Worker
ctx.jobs.register("full-sync", async (job: PluginJobContext) => {
  // job.jobKey, job.runId, job.trigger, job.scheduledAt
});
```

#### Job Context

```typescript
interface PluginJobContext {
  jobKey: string;
  runId: string;
  trigger: "schedule" | "manual" | "retry";
  scheduledAt: string;  // ISO 8601
}
```

#### Job Records

```typescript
interface PluginJobRecord {
  id: string; pluginId: string; jobKey: string;
  schedule: string; status: "active" | "paused" | "failed";
  lastRunAt: Date | null; nextRunAt: Date | null;
  createdAt: Date; updatedAt: Date;
}

interface PluginJobRunRecord {
  id: string; jobId: string; pluginId: string;
  trigger: "schedule" | "manual" | "retry";
  status: "pending" | "queued" | "running" | "succeeded" | "failed" | "cancelled";
  durationMs: number | null; error: string | null;
  logs: string[]; startedAt: Date | null; finishedAt: Date | null; createdAt: Date;
}
```

### Webhooks

Declared in manifest, handled in worker:

```typescript
// Manifest
{
  webhooks: [{
    endpointKey: "ingest",
    displayName: "External Ingest",
    description: "Accepts payloads from external system"
  }]
}

// Worker (onWebhook lifecycle hook)
async onWebhook(input: PluginWebhookInput) {
  // input.endpointKey, input.headers, input.rawBody, input.parsedBody, input.requestId
}
```

Route: `POST /api/plugins/:pluginId/webhooks/:endpointKey`

#### Delivery Records

```typescript
interface PluginWebhookDeliveryRecord {
  id: string; pluginId: string; webhookKey: string;
  externalId: string | null;
  status: "pending" | "success" | "failed";
  durationMs: number | null; error: string | null;
  payload: Record<string, unknown>; headers: Record<string, string>;
  startedAt: Date | null; finishedAt: Date | null; createdAt: Date;
}
```

### Event Subscriptions

Core domain events plugins can subscribe to (requires `events.subscribe`):

```
company.created, company.updated,
project.created, project.updated,
project.workspace_created, project.workspace_updated, project.workspace_deleted,
issue.created, issue.updated, issue.comment.created,
agent.created, agent.updated, agent.status_changed,
agent.run.started, agent.run.finished, agent.run.failed, agent.run.cancelled,
goal.created, goal.updated,
approval.created, approval.decided,
cost_event.created, activity.logged
```

Plugin-to-plugin events use `plugin.<pluginId>.<eventName>` namespace.

#### Event Envelope

```typescript
interface PluginEvent<TPayload = unknown> {
  eventId: string;
  eventType: PluginEventType | `plugin.${string}`;
  occurredAt: string;        // ISO 8601
  actorId?: string;
  actorType?: "user" | "agent" | "system" | "plugin";
  entityId?: string;
  entityType?: string;
  companyId: string;
  payload: TPayload;
}
```

#### Event Filter (Server-Side)

```typescript
interface EventFilter {
  projectId?: string;
  companyId?: string;
  agentId?: string;
  [key: string]: unknown;
}
```

### State Storage

Plugin state is isolated per-plugin with five-part composite key: `(pluginId, scopeKind, scopeId, namespace, stateKey)`.

```typescript
interface PluginStateRecord {
  id: string;
  pluginId: string;
  scopeKind: PluginStateScopeKind;
  scopeId: string | null;
  namespace: string;           // Default: "default"
  stateKey: string;
  valueJson: unknown;          // Any JSON-serializable type
  updatedAt: Date;
}
```

**Security:** Never store resolved secret values. Store only references and resolve via `ctx.secrets.resolve()`.

### Plugin Entities

Plugin-owned entity records for tracking external system mappings:

```typescript
interface PluginEntityUpsert {
  entityType: string;          // e.g. "linear-issue", "github-pr"
  scopeKind: PluginStateScopeKind;
  scopeId?: string;
  externalId?: string;         // External system ID
  title?: string;
  status?: string;
  data: Record<string, unknown>;
}

interface PluginEntityRecord {
  id: string; entityType: string;
  scopeKind: PluginStateScopeKind; scopeId: string | null;
  externalId: string | null; title: string | null; status: string | null;
  data: Record<string, unknown>;
  createdAt: string; updatedAt: string;
}
```

### Agent Tools from Plugins

Plugins can contribute tools to agents (requires `agent.tools.register`):

```typescript
// Manifest declaration
{
  tools: [{
    name: "search-issues",
    displayName: "Search Linear Issues",
    description: "Search issues in Linear by query",
    parametersSchema: { type: "object", properties: { query: { type: "string" } }, required: ["query"] }
  }]
}

// Worker registration
ctx.tools.register(
  "search-issues",
  { displayName: "Search Linear Issues", description: "...", parametersSchema: {...} },
  async (params, runCtx: ToolRunContext) => {
    // runCtx.agentId, runCtx.runId, runCtx.companyId, runCtx.projectId
    return { content: "Results...", data: {...} };
  }
);
```

Tool names are automatically namespaced by plugin ID at runtime (e.g. `linear:search-issues`).

#### Tool Run Context & Result

```typescript
interface ToolRunContext {
  agentId: string; runId: string; companyId: string; projectId: string;
}

interface ToolResult {
  content?: string;   // String returned to agent
  data?: unknown;     // Structured data alongside content
  error?: string;     // Failure indicator
}
```

### Agent Sessions (Two-Way Chat)

Plugins can create conversational sessions with agents:

```typescript
interface PluginAgentSessionsClient {
  create(agentId, companyId, opts?: { taskKey?, reason? }): Promise<AgentSession>;
  list(agentId, companyId): Promise<AgentSession[]>;
  sendMessage(sessionId, companyId, opts: {
    prompt: string; reason?: string;
    onEvent?: (event: AgentSessionEvent) => void;
  }): Promise<{ runId: string }>;
  close(sessionId, companyId): Promise<void>;
}

interface AgentSession {
  sessionId: string; agentId: string; companyId: string;
  status: "active" | "closed"; createdAt: string;
}

interface AgentSessionEvent {
  sessionId: string; runId: string; seq: number;
  eventType: "chunk" | "status" | "done" | "error";
  stream: "stdout" | "stderr" | "system" | null;
  message: string | null;
  payload: Record<string, unknown> | null;
}
```

Capabilities required: `agent.sessions.create`, `agent.sessions.list`, `agent.sessions.send`, `agent.sessions.close`.

### Streaming (Worker to UI)

Workers push real-time events to UI via SSE:

```typescript
// Worker
ctx.streams.open("chat", companyId);
for await (const token of tokenStream) {
  ctx.streams.emit("chat", { type: "token", text: token });
}
ctx.streams.close("chat");

// UI
const { events, connected, close } = usePluginStream<ChatToken>("chat-stream");
```

Streams are scoped to `(pluginId, channel, companyId)`. Multiple UI clients can subscribe concurrently.

### Launchers

Launchers describe entry points for plugin UI that are independent of slot implementations:

```typescript
interface PluginLauncherDeclaration {
  id: string;
  displayName: string;
  description?: string;
  placementZone: PluginLauncherPlacementZone;  // Same as slot types
  exportName?: string;                          // For custom UI
  entityTypes?: PluginUiSlotEntityType[];
  order?: number;
  action: PluginLauncherActionDeclaration;
  render?: PluginLauncherRenderDeclaration;
}

interface PluginLauncherActionDeclaration {
  type: "navigate" | "openModal" | "openDrawer" | "openPopover" | "performAction" | "deepLink";
  target: string;
  params?: Record<string, unknown>;
}

interface PluginLauncherRenderDeclaration {
  environment: "hostInline" | "hostOverlay" | "hostRoute" | "external" | "iframe";
  bounds?: "inline" | "compact" | "default" | "wide" | "full";
}
```

### Capabilities Model

All capabilities are declared in manifest and enforced at runtime:

**Data Read:**
`companies.read`, `projects.read`, `project.workspaces.read`, `issues.read`, `issue.comments.read`, `issue.documents.read`, `agents.read`, `goals.read`, `activity.read`, `costs.read`

**Data Write:**
`issues.create`, `issues.update`, `issue.comments.create`, `issue.documents.write`, `agents.pause`, `agents.resume`, `agents.invoke`, `agent.sessions.create`, `agent.sessions.list`, `agent.sessions.send`, `agent.sessions.close`, `activity.log.write`, `metrics.write`, `goals.create`, `goals.update`

**Plugin State:**
`plugin.state.read`, `plugin.state.write`

**Runtime/Integration:**
`events.subscribe`, `events.emit`, `jobs.schedule`, `webhooks.receive`, `http.outbound`, `secrets.read-ref`

**Agent Tools:**
`agent.tools.register`

**UI:**
`instance.settings.register`, `ui.sidebar.register`, `ui.page.register`, `ui.detailTab.register`, `ui.dashboardWidget.register`, `ui.commentAnnotation.register`, `ui.action.register`

### Plugin Record and Lifecycle

```typescript
interface PluginRecord {
  id: string;
  pluginKey: string;                        // From manifest.id
  packageName: string;                      // npm package name
  version: string;
  apiVersion: number;
  categories: PluginCategory[];
  manifestJson: PaperclipPluginManifestV1;  // Full manifest snapshot
  status: PluginStatus;
  installOrder: number | null;
  packagePath: string | null;               // For local-path installs
  lastError: string | null;
  installedAt: Date;
  updatedAt: Date;
}
```

**Status state machine:**
```
installed -> ready | error
ready -> disabled | error | upgrade_pending | uninstalled
disabled -> ready | uninstalled
error -> ready | uninstalled
upgrade_pending -> ready | error | uninstalled
uninstalled -> installed (reinstall)
```

Plugin statuses: `installed`, `ready`, `disabled`, `error`, `upgrade_pending`, `uninstalled`

### Scaffolding a Plugin

```bash
# Build scaffold tool
pnpm --filter @paperclipai/create-paperclip-plugin build

# Create in-repo example
node packages/plugins/create-paperclip-plugin/dist/index.js my-plugin \
  --output packages/plugins/examples/

# Create external plugin
node packages/plugins/create-paperclip-plugin/dist/index.js @acme/my-plugin \
  --output /path/to/repos \
  --sdk-path /path/to/paperclip/packages/plugins/sdk
```

Generated files: `src/manifest.ts`, `src/worker.ts`, `src/ui/index.tsx`, `tests/plugin.spec.ts`, `package.json`, `tsconfig.json`.

### Kitchen Sink Example

**Location:** `packages/plugins/examples/plugin-kitchen-sink-example/`

Reference plugin demonstrating every API surface. Declares ALL capabilities and uses:

- **UI Slots:** page, settingsPage, dashboardWidget, sidebar, sidebarPanel, projectSidebarItem, detailTab (project + issue), taskDetailView, toolbarButton, contextMenuItem, commentAnnotation, commentContextMenuItem
- **Launcher:** toolbar button that opens a modal with `hostOverlay` + `wide` bounds
- **Jobs:** periodic heartbeat job (`*/15 * * * *`)
- **Webhooks:** demo ingest endpoint
- **Tools:** echo tool, company summary tool, create issue tool
- **Config Schema:** showSidebarEntry, showSidebarPanel, showProjectSidebarItem, showCommentAnnotation, enableWorkspaceDemos, enableProcessDemos, secretRefExample, httpDemoUrl, allowedCommands, workspaceScratchFile

---

## 3. CLI

### CLI Commands

**Binary:** `paperclipai` (via `npx paperclipai`)

| Command | Description |
|---------|-------------|
| `onboard` | Interactive first-run setup wizard |
| `run` | Bootstrap (onboard + doctor) and run Paperclip |
| `doctor` | Run diagnostic checks (with optional `--repair`) |
| `configure` | Update config sections (llm, database, logging, server, storage, secrets) |
| `env` | Print environment variables for deployment |
| `db:backup` | Create one-off database backup |
| `allowed-hostname <host>` | Allow hostname for authenticated mode |
| `heartbeat run` | Run one agent heartbeat and stream live logs |
| `auth bootstrap-ceo` | Create one-time bootstrap invite for first admin |
| `auth login` | CLI authentication |
| `context set/get/list` | Manage CLI context profiles |
| `company create/get/list/delete/import/export` | Company management |
| `issue create/get/list/update` | Issue management |
| `agent create/get/list/update/wake/local-cli` | Agent management |
| `approval get/list/resolve` | Approval management |
| `activity list` | View activity log |
| `dashboard` | View company dashboard |
| `worktree` | Git worktree management |
| `plugin install/list/enable/disable` | Plugin management |

### Onboarding Flow

Two paths: **Quickstart** (defaults) or **Advanced** (interactive prompts).

Quickstart auto-detects env vars:
```
PAPERCLIP_PUBLIC_URL, DATABASE_URL, PAPERCLIP_DEPLOYMENT_MODE, HOST, PORT,
PAPERCLIP_STORAGE_PROVIDER, PAPERCLIP_SECRETS_PROVIDER, etc.
```

Advanced prompts for: database, LLM provider, logging, server, storage, secrets.

After onboard:
1. Validates LLM API key (Claude or OpenAI)
2. Generates `PAPERCLIP_AGENT_JWT_SECRET`
3. Creates local secrets key file
4. Optionally starts Paperclip immediately

### Configuration Schema

All Zod-validated. Config stored at `~/.paperclip/config.json`.

```typescript
interface PaperclipConfig {
  $meta: { version: 1; updatedAt: string; source: "onboard" | "configure" | "doctor" };
  llm?: { provider: "claude" | "openai"; apiKey?: string };
  database: {
    mode: "embedded-postgres" | "postgres";
    connectionString?: string;
    embeddedPostgresDataDir: string;   // Default: ~/.paperclip/instances/default/db
    embeddedPostgresPort: number;       // Default: 54329
    backup: {
      enabled: boolean;                 // Default: true
      intervalMinutes: number;          // Default: 60, range: 1-10080
      retentionDays: number;            // Default: 30, range: 1-3650
      dir: string;
    };
  };
  logging: { mode: "file" | "cloud"; logDir: string };
  server: {
    deploymentMode: "local_trusted" | "authenticated";
    exposure: "private" | "public";
    host: string;                       // Default: "127.0.0.1"
    port: number;                       // Default: 3100
    allowedHostnames: string[];
    serveUi: boolean;                   // Default: true
  };
  auth: {
    baseUrlMode: "auto" | "explicit";
    publicBaseUrl?: string;
    disableSignUp: boolean;
  };
  storage: {
    provider: "local_disk" | "s3";
    localDisk: { baseDir: string };
    s3: { bucket: string; region: string; endpoint?: string; prefix: string; forcePathStyle: boolean };
  };
  secrets: {
    provider: "local_encrypted" | "aws_secrets_manager" | "gcp_secret_manager" | "vault";
    strictMode: boolean;
    localEncrypted: { keyFilePath: string };
  };
}
```

**Validation rules:**
- `local_trusted` mode forces `exposure: "private"`
- `explicit` base URL mode requires `publicBaseUrl`
- `public` exposure requires `explicit` base URL mode and `publicBaseUrl`

### Doctor / Diagnostics

`paperclipai doctor [--repair] [-y]`

Runs checks in order:
1. **Config check** -- validates config file exists and parses
2. **Database check** -- tests connection, runs migrations if needed
3. **LLM check** -- validates API key
4. **Port check** -- verifies server port is available
5. **Storage check** -- validates storage provider configuration
6. **Secrets check** -- validates secrets provider and key file
7. **Agent JWT secret check** -- ensures JWT secret exists
8. **Log check** -- validates log directory
9. **Deployment auth check** -- validates auth configuration
10. **Path resolver** -- validates all paths are resolvable

### Client Commands

Client commands use a context system with profiles:

```bash
paperclipai context set --api-base http://localhost:3100 --api-key <token>
paperclipai context set --profile staging --api-base https://staging.example.com
```

**Company commands:** create, get, list, delete, import, export (with zip support)
**Issue commands:** create, get, list, update (with status, priority, assignee management)
**Agent commands:** create, get, list, update, wake, local-cli (installs skills + prints env vars)
**Approval commands:** get, list, resolve
**Plugin commands:** install, list, enable, disable

### Adapters

Two adapter types for running agent heartbeats:

**`process`** -- Runs agents as local child processes
- Format event: structured JSON event rendering
- Streams stdout/stderr with event types

**`http`** -- Runs agents via HTTP API
- Format event: HTTP-specific event rendering
- Handles remote agent communication

Built-in adapter types: `process`, `http`, `claude_local`, `codex_local`, `opencode_local`, `pi_local`, `cursor`, `openclaw_gateway`, `hermes_local`

### Heartbeat Execution

```bash
paperclipai heartbeat run \
  --agent-id <agentId> \
  --source on_demand \          # timer | assignment | on_demand | automation
  --trigger manual \            # manual | ping | callback | system
  --timeout-ms 0 \
  --json \                      # Raw JSON output
  --debug                       # Show raw adapter chunks
```

---

## 4. Shared Types

**Package:** `@paperclipai/shared`

### Constants and Enums

#### Company & Deployment

```typescript
type CompanyStatus = "active" | "paused" | "archived";
type DeploymentMode = "local_trusted" | "authenticated";
type DeploymentExposure = "private" | "public";
type AuthBaseUrlMode = "auto" | "explicit";
```

#### Agent

```typescript
type AgentStatus = "active" | "paused" | "idle" | "running" | "error" | "pending_approval" | "terminated";
type AgentAdapterType = "process" | "http" | "claude_local" | "codex_local" | "opencode_local" | "pi_local" | "cursor" | "openclaw_gateway" | "hermes_local";
type AgentRole = "ceo" | "cto" | "cmo" | "cfo" | "engineer" | "designer" | "pm" | "qa" | "devops" | "researcher" | "general";
type PauseReason = "manual" | "budget" | "system";
```

**Agent Icons (41):** `bot`, `cpu`, `brain`, `zap`, `rocket`, `code`, `terminal`, `shield`, `eye`, `search`, `wrench`, `hammer`, `lightbulb`, `sparkles`, `star`, `heart`, `flame`, `bug`, `cog`, `database`, `globe`, `lock`, `mail`, `message-square`, `file-code`, `git-branch`, `package`, `puzzle`, `target`, `wand`, `atom`, `circuit-board`, `radar`, `swords`, `telescope`, `microscope`, `crown`, `gem`, `hexagon`, `pentagon`, `fingerprint`

#### Issue

```typescript
type IssueStatus = "backlog" | "todo" | "in_progress" | "in_review" | "done" | "blocked" | "cancelled";
type IssuePriority = "critical" | "high" | "medium" | "low";
type IssueOriginKind = "manual" | "routine_execution";
```

#### Goal

```typescript
type GoalLevel = "company" | "team" | "agent" | "task";
type GoalStatus = "planned" | "active" | "achieved" | "cancelled";
```

#### Project

```typescript
type ProjectStatus = "backlog" | "planned" | "in_progress" | "completed" | "cancelled";
```

#### Routine

```typescript
type RoutineStatus = "active" | "paused" | "archived";
type RoutineConcurrencyPolicy = "coalesce_if_active" | "always_enqueue" | "skip_if_active";
type RoutineCatchUpPolicy = "skip_missed" | "enqueue_missed_with_cap";
type RoutineTriggerKind = "schedule" | "webhook" | "api";
type RoutineRunStatus = "received" | "coalesced" | "skipped" | "issue_created" | "completed" | "failed";
```

#### Approval

```typescript
type ApprovalType = "hire_agent" | "approve_ceo_strategy" | "budget_override_required";
type ApprovalStatus = "pending" | "revision_requested" | "approved" | "rejected" | "cancelled";
```

#### Finance & Budget

```typescript
type BillingType = "metered_api" | "subscription_included" | "subscription_overage" | "credits" | "fixed" | "unknown";
type FinanceEventKind = "inference_charge" | "platform_fee" | "credit_purchase" | "credit_refund" | "credit_expiry" | "byok_fee" | "gateway_overhead" | "log_storage_charge" | "logpush_charge" | "provisioned_capacity_charge" | "training_charge" | "custom_model_import_charge" | "custom_model_storage_charge" | "manual_adjustment";
type FinanceDirection = "debit" | "credit";
type FinanceUnit = "input_token" | "output_token" | "cached_input_token" | "request" | "credit_usd" | "credit_unit" | "model_unit_minute" | "model_unit_hour" | "gb_month" | "train_token" | "unknown";
type BudgetScopeType = "company" | "agent" | "project";
type BudgetWindowKind = "calendar_month_utc" | "lifetime";
type BudgetThresholdType = "soft" | "hard";
type BudgetIncidentStatus = "open" | "resolved" | "dismissed";
type BudgetIncidentResolutionAction = "keep_paused" | "raise_budget_and_resume";
```

#### Secrets & Storage

```typescript
type SecretProvider = "local_encrypted" | "aws_secrets_manager" | "gcp_secret_manager" | "vault";
type StorageProvider = "local_disk" | "s3";
```

#### Heartbeat & Live Events

```typescript
type HeartbeatInvocationSource = "timer" | "assignment" | "on_demand" | "automation";
type WakeupTriggerDetail = "manual" | "ping" | "callback" | "system";
type WakeupRequestStatus = "queued" | "deferred_issue_execution" | "claimed" | "coalesced" | "skipped" | "completed" | "failed" | "cancelled";
type HeartbeatRunStatus = "queued" | "running" | "succeeded" | "failed" | "cancelled" | "timed_out";
type LiveEventType = "heartbeat.run.queued" | "heartbeat.run.status" | "heartbeat.run.event" | "heartbeat.run.log" | "agent.status" | "activity.logged" | "plugin.ui.updated" | "plugin.worker.crashed" | "plugin.worker.restarted";
```

#### Access & Auth

```typescript
type PrincipalType = "user" | "agent";
type MembershipStatus = "pending" | "active" | "suspended";
type InstanceUserRole = "instance_admin";
type InviteType = "company_join" | "bootstrap_ceo";
type InviteJoinType = "human" | "agent" | "both";
type JoinRequestType = "human" | "agent";
type JoinRequestStatus = "pending_approval" | "approved" | "rejected";
type PermissionKey = "agents:create" | "users:invite" | "users:manage_permissions" | "tasks:assign" | "tasks:assign_scope" | "joins:approve";
```

### Core Domain Types

#### Agent

```typescript
interface Agent {
  id: string; companyId: string;
  name: string; urlKey: string;
  role: AgentRole; title: string | null; icon: string | null;
  status: AgentStatus;
  reportsTo: string | null;
  capabilities: string | null;
  adapterType: AgentAdapterType;
  adapterConfig: Record<string, unknown>;
  runtimeConfig: Record<string, unknown>;
  budgetMonthlyCents: number; spentMonthlyCents: number;
  pauseReason: PauseReason | null; pausedAt: Date | null;
  permissions: AgentPermissions;
  lastHeartbeatAt: Date | null;
  metadata: Record<string, unknown> | null;
  createdAt: Date; updatedAt: Date;
}

interface AgentDetail extends Agent {
  chainOfCommand: AgentChainOfCommandEntry[];
  access: AgentAccessState;
}

interface AgentPermissions { canCreateAgents: boolean; }
interface AgentChainOfCommandEntry { id: string; name: string; role: AgentRole; title: string | null; }
```

#### Issue

```typescript
interface Issue {
  id: string; companyId: string;
  projectId: string | null; projectWorkspaceId: string | null;
  goalId: string | null; parentId: string | null;
  ancestors?: IssueAncestor[];
  title: string; description: string | null;
  status: IssueStatus; priority: IssuePriority;
  assigneeAgentId: string | null; assigneeUserId: string | null;
  checkoutRunId: string | null;
  executionRunId: string | null; executionAgentNameKey: string | null;
  executionLockedAt: Date | null;
  createdByAgentId: string | null; createdByUserId: string | null;
  issueNumber: number | null; identifier: string | null;
  originKind?: IssueOriginKind; originId?: string | null; originRunId?: string | null;
  requestDepth: number; billingCode: string | null;
  assigneeAdapterOverrides: IssueAssigneeAdapterOverrides | null;
  executionWorkspaceId: string | null;
  executionWorkspacePreference: string | null;
  executionWorkspaceSettings: IssueExecutionWorkspaceSettings | null;
  startedAt: Date | null; completedAt: Date | null;
  cancelledAt: Date | null; hiddenAt: Date | null;
  labelIds?: string[]; labels?: IssueLabel[];
  planDocument?: IssueDocument | null;
  documentSummaries?: IssueDocumentSummary[];
  project?: Project | null; goal?: Goal | null;
  currentExecutionWorkspace?: ExecutionWorkspace | null;
  workProducts?: IssueWorkProduct[];
  mentionedProjects?: Project[];
  createdAt: Date; updatedAt: Date;
}

interface IssueComment {
  id: string; companyId: string; issueId: string;
  authorAgentId: string | null; authorUserId: string | null;
  body: string; createdAt: Date; updatedAt: Date;
}

interface IssueDocument extends IssueDocumentSummary { body: string; }

interface IssueDocumentSummary {
  id: string; companyId: string; issueId: string;
  key: string; title: string | null;
  format: "markdown";
  latestRevisionId: string | null; latestRevisionNumber: number;
  createdByAgentId: string | null; createdByUserId: string | null;
  updatedByAgentId: string | null; updatedByUserId: string | null;
  createdAt: Date; updatedAt: Date;
}
```

#### Company Skill

```typescript
type CompanySkillSourceType = "github" | "skills_sh" | "local" | "manual";
type CompanySkillTrustLevel = "verified" | "community" | "internal" | "unknown";
type CompanySkillCompatibility = "compatible" | "unknown" | "incompatible";

interface CompanySkill {
  id: string; companyId: string;
  key: string; slug: string; name: string;
  description: string | null;
  sourceType: CompanySkillSourceType;
  sourceLocator: string | null; sourceRef: string | null;
  trustLevel: CompanySkillTrustLevel;
  compatibility: CompanySkillCompatibility;
  metadata: Record<string, unknown> | null;
  createdAt: Date; updatedAt: Date;
}
```

#### Agent Skills

```typescript
type AgentSkillSyncMode = "desired" | "override";
type AgentSkillState = "active" | "pending_install" | "failed" | "removed";
type AgentSkillOrigin = "runtime" | "company" | "manual";

interface AgentSkillEntry {
  companySkillId: string | null;
  companySkillKey: string;
  state: AgentSkillState;
  origin: AgentSkillOrigin;
}

interface AgentSkillSyncRequest {
  desiredSkills: string[];    // Company skill keys, IDs, or unique slugs
  mode?: AgentSkillSyncMode;
}
```

### Validators (Zod Schemas)

The `@paperclipai/shared` package exports comprehensive Zod schemas alongside types. Key schemas:

**Company:** `createCompanySchema`, `updateCompanySchema`, `updateCompanyBrandingSchema`

**Agent:** `createAgentSchema`, `createAgentHireSchema`, `updateAgentSchema`, `wakeAgentSchema`, `agentPermissionsSchema`, `updateAgentInstructionsPathSchema`

**Issue:** `createIssueSchema`, `updateIssueSchema`, `checkoutIssueSchema`, `addIssueCommentSchema`, `upsertIssueDocumentSchema`, `linkIssueApprovalSchema`

**Project:** `createProjectSchema`, `updateProjectSchema`, `createProjectWorkspaceSchema`, `updateProjectWorkspaceSchema`

**Goal:** `createGoalSchema`, `updateGoalSchema`

**Approval:** `createApprovalSchema`, `resolveApprovalSchema`, `requestApprovalRevisionSchema`, `resubmitApprovalSchema`, `addApprovalCommentSchema`

**Portability:** `companyPortabilityExportSchema`, `companyPortabilityPreviewSchema`, `companyPortabilityImportSchema`

**Plugin:** `pluginManifestV1Schema`, `installPluginSchema`, `upsertPluginConfigSchema`, `pluginStateScopeKeySchema`, `setPluginStateSchema`

**Secrets:** `envBindingPlainSchema`, `envBindingSecretRefSchema`, `createSecretSchema`, `rotateSecretSchema`

**Routine:** `createRoutineSchema`, `updateRoutineSchema`, `createRoutineTriggerSchema`, `runRoutineSchema`

**Budget:** `upsertBudgetPolicySchema`, `resolveBudgetIncidentSchema`

**Skills:** `companySkillImportSchema`, `companySkillProjectScanRequestSchema`, `companySkillCreateSchema`, `agentSkillSyncSchema`

**Access:** `createCompanyInviteSchema`, `createOpenClawInvitePromptSchema`, `acceptInviteSchema`, `updateMemberPermissionsSchema`

---

## 5. Company Import/Export

### Portability Manifest

```typescript
interface CompanyPortabilityManifest {
  schemaVersion: number;
  generatedAt: string;                     // ISO 8601
  source: { companyId: string; companyName: string } | null;
  includes: CompanyPortabilityInclude;
  company: CompanyPortabilityCompanyManifestEntry | null;
  sidebar: CompanyPortabilitySidebarOrder | null;
  agents: CompanyPortabilityAgentManifestEntry[];
  skills: CompanyPortabilitySkillManifestEntry[];
  projects: CompanyPortabilityProjectManifestEntry[];
  issues: CompanyPortabilityIssueManifestEntry[];
  envInputs: CompanyPortabilityEnvInput[];
}
```

**Include flags:**
```typescript
interface CompanyPortabilityInclude {
  company: boolean; agents: boolean; projects: boolean; issues: boolean; skills: boolean;
}
```

#### Agent Manifest Entry

```typescript
interface CompanyPortabilityAgentManifestEntry {
  slug: string; name: string; path: string;
  skills: string[];
  role: string; title: string | null; icon: string | null;
  capabilities: string | null;
  reportsToSlug: string | null;
  adapterType: string;
  adapterConfig: Record<string, unknown>;
  runtimeConfig: Record<string, unknown>;
  permissions: Record<string, unknown>;
  budgetMonthlyCents: number;
  metadata: Record<string, unknown> | null;
}
```

#### Skill Manifest Entry

```typescript
interface CompanyPortabilitySkillManifestEntry {
  key: string; slug: string; name: string; path: string;
  description: string | null;
  sourceType: string; sourceLocator: string | null; sourceRef: string | null;
  trustLevel: string | null; compatibility: string | null;
  metadata: Record<string, unknown> | null;
  fileInventory: Array<{ path: string; kind: string }>;
}
```

#### Project Manifest Entry

```typescript
interface CompanyPortabilityProjectManifestEntry {
  slug: string; name: string; path: string;
  description: string | null;
  ownerAgentSlug: string | null; leadAgentSlug: string | null;
  targetDate: string | null; color: string | null; status: string | null;
  executionWorkspacePolicy: Record<string, unknown> | null;
  workspaces: CompanyPortabilityProjectWorkspaceManifestEntry[];
  metadata: Record<string, unknown> | null;
}
```

#### Issue Manifest Entry

```typescript
interface CompanyPortabilityIssueManifestEntry {
  slug: string; identifier: string | null;
  title: string; path: string;
  projectSlug: string | null; projectWorkspaceKey: string | null;
  assigneeAgentSlug: string | null;
  description: string | null;
  recurring: boolean;
  routine: CompanyPortabilityIssueRoutineManifestEntry | null;
  status: string | null; priority: string | null;
  labelIds: string[]; billingCode: string | null;
  executionWorkspaceSettings: Record<string, unknown> | null;
  assigneeAdapterOverrides: Record<string, unknown> | null;
  metadata: Record<string, unknown> | null;
}
```

#### Environment Input

```typescript
interface CompanyPortabilityEnvInput {
  key: string;
  description: string | null;
  agentSlug: string | null;
  kind: "secret" | "plain";
  requirement: "required" | "optional";
  defaultValue: string | null;
  portability: "portable" | "system_dependent";
}
```

### Export Flow

1. **Preview:** `POST /api/companies/:companyId/exports/preview`
   - Defaults to `issues: false`
   - Returns manifest, file inventory, counts, warnings

2. **Build:** `POST /api/companies/:companyId/exports`
   - Use `selectedFiles` to narrow after preview

```typescript
interface CompanyPortabilityExportRequest {
  include?: Partial<CompanyPortabilityInclude>;
  agents?: string[];
  skills?: string[];
  projects?: string[];
  issues?: string[];
  projectIssues?: string[];
  selectedFiles?: string[];
  expandReferencedSkills?: boolean;
  sidebarOrder?: Partial<CompanyPortabilitySidebarOrder>;
}

interface CompanyPortabilityExportResult {
  rootPath: string;
  manifest: CompanyPortabilityManifest;
  files: Record<string, CompanyPortabilityFileEntry>;
  warnings: string[];
  paperclipExtensionPath: string;
}
```

File entries can be plain strings or base64-encoded:
```typescript
type CompanyPortabilityFileEntry =
  | string
  | { encoding: "base64"; data: string; contentType?: string | null };
```

### Import Flow

1. **Preview:** `POST /api/companies/:companyId/imports/preview`
2. **Apply:** `POST /api/companies/:companyId/imports/apply`

```typescript
interface CompanyPortabilityPreviewRequest {
  source: CompanyPortabilitySource;          // "inline" with files or "github" with URL
  include?: Partial<CompanyPortabilityInclude>;
  target: CompanyPortabilityImportTarget;    // "new_company" or "existing_company"
  agents?: CompanyPortabilityAgentSelection; // "all" or string[]
  collisionStrategy?: "rename" | "skip" | "replace";
  nameOverrides?: Record<string, string>;
  selectedFiles?: string[];
}

interface CompanyPortabilityImportRequest extends CompanyPortabilityPreviewRequest {
  adapterOverrides?: Record<string, CompanyPortabilityAdapterOverride>;
}
```

**Import sources:**
```typescript
type CompanyPortabilitySource =
  | { type: "inline"; rootPath?: string | null; files: Record<string, CompanyPortabilityFileEntry> }
  | { type: "github"; url: string };
```

**Import targets:**
```typescript
type CompanyPortabilityImportTarget =
  | { mode: "new_company"; newCompanyName?: string | null }
  | { mode: "existing_company"; companyId: string };
```

#### Preview Result

```typescript
interface CompanyPortabilityPreviewResult {
  include: CompanyPortabilityInclude;
  targetCompanyId: string | null;
  targetCompanyName: string | null;
  collisionStrategy: CompanyPortabilityCollisionStrategy;
  selectedAgentSlugs: string[];
  plan: {
    companyAction: "none" | "create" | "update";
    agentPlans: CompanyPortabilityPreviewAgentPlan[];
    projectPlans: CompanyPortabilityPreviewProjectPlan[];
    issuePlans: CompanyPortabilityPreviewIssuePlan[];
  };
  manifest: CompanyPortabilityManifest;
  files: Record<string, CompanyPortabilityFileEntry>;
  envInputs: CompanyPortabilityEnvInput[];
  warnings: string[]; errors: string[];
}
```

#### Import Result

```typescript
interface CompanyPortabilityImportResult {
  company: { id: string; name: string; action: "created" | "updated" | "unchanged" };
  agents: Array<{ slug: string; id: string | null; action: "created" | "updated" | "skipped"; name: string; reason: string | null }>;
  projects: Array<{ slug: string; id: string | null; action: "created" | "updated" | "skipped"; name: string; reason: string | null }>;
  envInputs: CompanyPortabilityEnvInput[];
  warnings: string[];
}
```

### CEO-Safe Import Rules

- Allowed callers: board users and CEO agent of the same company
- `replace` collision strategy is **rejected** for safe imports
- Existing-company imports only create new entities or skip collisions
- Issues are always created as new issues
- `new_company` imports copy active user memberships from source company

---

## 6. Evaluation System

**Location:** `evals/promptfoo/`

### Promptfoo Configuration

Uses [Promptfoo](https://github.com/promptfoo/promptfoo) for behavior evaluation.

```yaml
description: "Paperclip heartbeat behavior evals"

prompts:
  - file://prompts/heartbeat-system.txt

providers:
  - id: openrouter:anthropic/claude-sonnet-4-20250514
    label: claude-sonnet-4
  - id: openrouter:openai/gpt-4.1
    label: gpt-4.1
  - id: openrouter:openai/codex-5.4
    label: codex-5.4
  - id: openrouter:google/gemini-2.5-pro
    label: gemini-2.5-pro

defaultTest:
  options:
    transformVars: "{ ...vars, apiUrl: 'http://localhost:18080', runId: 'run-eval-001' }"

tests:
  - file://tests/*.yaml
```

### Test Categories

#### Core Tests (`tests/core.yaml`)

| Case | Assertions |
|------|-----------|
| **Assignment pickup** | Uses `inbox-lite`, prioritizes `in_progress`, doesn't look for unassigned work |
| **Progress update** | Posts comment, uses PATCH |
| **Blocked reporting** | Sets status to `blocked` with explanation |
| **No work exit** | Exits cleanly, doesn't self-assign |
| **Checkout before work** | Always checks out with `POST /api/issues`, includes `X-Paperclip-Run-Id` header |
| **409 conflict handling** | Stops on 409, picks different task, never retries |

#### Governance Tests (`tests/governance.yaml`)

| Case | Assertions |
|------|-----------|
| **Approval required** | Uses `GET /api/approvals`, doesn't bypass |
| **Company boundary** | Refuses cross-company actions, doesn't checkout |

#### Test Variables

```yaml
vars:
  agentId: agent-coder-01
  companyId: company-eval-01
  taskId: ""                    # Issue ID or empty
  wakeReason: timer             # timer | assignment | approval_resolved
  approvalId: ""                # Approval ID or empty
```

#### Assertion Types

- `contains` / `not-contains` -- String presence checks
- `javascript` -- Custom JS expression returning boolean
- `metric` -- Named metric for tracking assertion results

### Eval Phases

| Phase | Description | Status |
|-------|-------------|--------|
| **Phase 0** | Promptfoo bootstrap -- narrow behavior evals with deterministic assertions | Current |
| **Phase 1** | TypeScript eval harness with seeded scenarios and hard checks | Planned |
| **Phase 2** | Pairwise and rubric scoring layer | Planned |
| **Phase 3** | Efficiency metrics integration | Planned |
| **Phase 4** | Production-case ingestion | Planned |
