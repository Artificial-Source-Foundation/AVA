# Paperclip Backend Reference

Comprehensive reference for the Paperclip server at `docs/reference-code/paperclip/server/`.
Paperclip is an AI-agent orchestration platform that manages coding agents (Claude Code, Codex, Cursor, Gemini CLI, OpenCode, Pi) through a heartbeat-driven execution model.

---

## 1. Architecture

### Stack

- **Runtime**: Node.js + Express.js
- **Database**: PostgreSQL (external or embedded via `embedded-postgres`)
- **ORM**: Drizzle ORM (`@paperclipai/db`)
- **Auth**: Better Auth (email/password, session cookies, trusted origins)
- **Real-time**: WebSocket (ws library, `noServer` mode on the HTTP server)
- **Validation**: Zod schemas from `@paperclipai/shared`
- **Logging**: Pino (`pino-http` for request logging)
- **Plugin system**: JSON-RPC worker processes, job scheduler, event bus
- **Storage**: Local disk or S3 (configurable)

### Entry Point

`src/index.ts` -- `startServer()` bootstraps everything:

1. Loads config from `~/.paperclip/config.json` + `.env` + environment variables
2. Starts embedded PostgreSQL or connects to external Postgres
3. Runs Drizzle migrations (auto-apply on first run, prompt otherwise)
4. Creates the Express app via `createApp()` in `src/app.ts`
5. Sets up WebSocket server for live events
6. Starts the heartbeat scheduler (30s interval by default)
7. Starts the routine cron scheduler
8. Starts the database backup scheduler (hourly by default)
9. Starts the plugin system (loader, worker manager, job scheduler, event bus)

### Middleware Stack (in order)

| Middleware | File | Purpose |
|-----------|------|---------|
| `express.json` | `app.ts` | Parse JSON bodies, 10MB limit, raw body capture |
| `httpLogger` | `middleware/logger.ts` | Pino HTTP request logging |
| `privateHostnameGuard` | `middleware/private-hostname-guard.ts` | Block requests from disallowed hostnames (private deployments) |
| `actorMiddleware` | `middleware/auth.ts` | Resolve request actor (board user, agent, or none) |
| Better Auth handler | `auth/better-auth.ts` | `/api/auth/*` session management |
| `boardMutationGuard` | `middleware/board-mutation-guard.ts` | Prevent mutations from unauthenticated actors |
| `validate` | `middleware/validate.ts` | Zod schema validation for request bodies |
| `errorHandler` | `middleware/error-handler.ts` | Catch-all error handler |

### Deployment Modes

| Mode | Description |
|------|-------------|
| `local_trusted` | Loopback-only, implicit local board user, no auth required |
| `authenticated` | Better Auth sessions + API keys, supports public/private exposure |

### Adapter Registry

Adapters bridge Paperclip to specific coding agents. Registered in `src/adapters/registry.ts`:

| Adapter Type | Agent | Session Support | JWT Auth |
|-------------|-------|-----------------|----------|
| `claude_local` | Claude Code CLI | Yes (session codec) | Yes |
| `codex_local` | OpenAI Codex CLI | Yes | Yes |
| `cursor` | Cursor editor | Yes | Yes |
| `gemini_local` | Gemini CLI | Yes | Yes |
| `opencode_local` | OpenCode CLI | Yes | Yes |
| `pi_local` | Pi CLI | Yes | Yes |
| `hermes_local` | Hermes agent | Yes | Yes |
| `openclaw_gateway` | OpenClaw gateway | No | No |
| `process` | Generic subprocess | No | No |
| `http` | HTTP endpoint | No | No |

Each adapter implements `execute()`, `testEnvironment()`, and optionally `listSkills()`, `syncSkills()`, `sessionCodec`, `models`, `getQuotaWindows()`.

---

## 2. Heartbeat Protocol

The heartbeat is Paperclip's core execution engine in `src/services/heartbeat.ts` (~3,860 lines). It manages the full lifecycle of agent invocations.

### Lifecycle: Wake -> Execute -> Sleep

```
                          +-----------+
                          |  TRIGGER  |
                          |  (timer,  |
                          | assignment|
                          | on_demand)|
                          +-----+-----+
                                |
                    enqueueWakeup()
                                |
                          +-----v-----+
                          |  QUEUED   |
                          |  (run)    |
                          +-----+-----+
                                |
                   claimQueuedRun()
                                |
                          +-----v-----+
                          |  RUNNING  |
                          |  (run)    |
                          +-----+-----+
                                |
                     executeRun()
                     adapter.execute()
                                |
              +---------+-------+---------+
              |         |                 |
        +-----v---+ +---v-----+   +------v----+
        |SUCCEEDED| | FAILED  |   | CANCELLED |
        +---------+ +---------+   +-----------+
```

### Wake Sources

| Source | Trigger Detail | Description |
|--------|---------------|-------------|
| `timer` | `system` | Heartbeat scheduler interval elapsed |
| `assignment` | `manual` / `system` | Issue assigned to agent |
| `on_demand` | `manual` / `ping` / `callback` | Board user or agent-initiated wake |
| `automation` | `system` | Routine trigger, process-loss retry |

### Heartbeat Policy

Per-agent configuration in `runtimeConfig.heartbeat`:

```json
{
  "heartbeat": {
    "enabled": true,
    "intervalSec": 300,
    "wakeOnDemand": true,
    "maxConcurrentRuns": 1
  }
}
```

- `intervalSec`: Timer-based wakeup interval (0 = disabled)
- `maxConcurrentRuns`: 1-10 concurrent runs per agent (default 1)
- Agents with status `paused`, `terminated`, or `pending_approval` are skipped

### Scheduler Loop

The server runs a `setInterval` at `heartbeatSchedulerIntervalMs` (default 30s):

1. **`tickTimers(now)`** -- Iterates all agents, checks if `intervalSec` has elapsed since `lastHeartbeatAt`, enqueues wakeup if so.
2. **`routines.tickScheduledTriggers(now)`** -- Checks cron-based routine triggers, creates issues and wakes agents.
3. **`reapOrphanedRuns({ staleThresholdMs: 5min })`** -- Finds runs in "running" state with no in-memory process handle. Checks PID liveness. Marks detached or failed. Retries once for tracked local adapters.
4. **`resumeQueuedRuns()`** -- Drives forward any queued runs that were waiting.

### Run Execution (`executeRun`)

1. Claim the queued run (atomic CAS on status = "queued")
2. Load agent record, runtime state, context snapshot
3. Resolve task key (from `issueId`, `taskId`, or `taskKey` in context/payload)
4. Load or create task session (per adapter-type + task-key)
5. Evaluate session compaction (rotate if maxSessionRuns, maxRawInputTokens, or maxSessionAgeHours exceeded)
6. Resolve workspace (project workspace > task session cwd > agent home)
7. Realize execution workspace (may create git worktree or managed clone)
8. Resolve secrets from adapter config (replace `$secret:name` references)
9. Inject runtime skills from company skills
10. Ensure runtime services (dev servers, etc.)
11. Create agent JWT token for `PAPERCLIP_API_KEY` injection
12. Call `adapter.execute()` with config, context, runtime, callbacks
13. Stream stdout/stderr to run log store + WebSocket live events
14. On completion: update runtime state, record cost event, persist session, release issue execution lock

### Session Persistence

Sessions are tracked at two levels:

- **Agent Runtime State** (`agent_runtime_state`): Global session ID per agent, total token/cost counters
- **Task Sessions** (`agent_task_sessions`): Per-agent, per-adapter, per-task-key session params. Keyed by `(companyId, agentId, adapterType, taskKey)`.

Session IDs flow: `sessionIdBefore` (before run) -> adapter returns `sessionId`/`sessionParams` -> `sessionIdAfter` (after run). The `sessionCodec` per adapter handles serialization/deserialization of session params.

### Session Compaction

When sessions grow too large, automatic rotation occurs:

- `maxSessionRuns` -- number of runs in the same session
- `maxRawInputTokens` -- cumulative raw input tokens
- `maxSessionAgeHours` -- elapsed time since first run in session

On rotation, a handoff markdown summary is injected into the context so the agent can rebuild minimal context.

### Process Recovery

- **Orphan reaping**: Runs stuck in "running" with no in-memory handle and dead PID are failed
- **Process-loss retry**: For tracked local adapters, one automatic retry is enqueued
- **Detached process**: If PID is alive but no handle exists, a warning is attached (cleared when the process reports activity)
- **Startup recovery**: On server start, orphaned running runs are reaped and queued runs are resumed

### Concurrency Control

- `maxConcurrentRuns` per agent (default 1, max 10)
- `withAgentStartLock()` serializes start operations per agent via chained promises
- `activeRunExecutions` set prevents duplicate execution
- Budget checks (`budgets.getInvocationBlock()`) before claiming a run

---

## 3. Database Schema

PostgreSQL via Drizzle ORM. 59 schema files in `packages/db/src/schema/`, 46 migrations.

### Core Entities

#### `companies`
| Column | Type | Description |
|--------|------|-------------|
| `id` | uuid PK | |
| `name` | text | Company name |
| `status` | text | `active` / `paused` |
| `pauseReason` | text | `manual` / `budget` / `system` |
| `issuePrefix` | text | Issue identifier prefix (unique), default `PAP` |
| `issueCounter` | int | Auto-increment for issue numbers |
| `budgetMonthlyCents` | int | Monthly budget ceiling in cents |
| `spentMonthlyCents` | int | Current month spend (materialized) |
| `requireBoardApprovalForNewAgents` | bool | Gate agent hiring |
| `brandColor` | text | Custom brand color |

#### `agents`
| Column | Type | Description |
|--------|------|-------------|
| `id` | uuid PK | |
| `companyId` | uuid FK -> companies | |
| `name` | text | Agent display name |
| `role` | text | `general` / `ceo` / custom |
| `title` | text | Agent title |
| `icon` | text | Agent icon |
| `status` | text | `idle` / `running` / `paused` / `error` / `terminated` / `pending_approval` |
| `reportsTo` | uuid FK -> agents | Org hierarchy (self-referential) |
| `capabilities` | text | Agent capabilities description |
| `adapterType` | text | `claude_local`, `codex_local`, etc. |
| `adapterConfig` | jsonb | Adapter-specific configuration |
| `runtimeConfig` | jsonb | Heartbeat policy, session compaction, etc. |
| `budgetMonthlyCents` | int | Per-agent monthly budget |
| `spentMonthlyCents` | int | Current month spend |
| `pauseReason` | text | Why the agent is paused |
| `permissions` | jsonb | `{ canCreateAgents: bool }` etc. |
| `lastHeartbeatAt` | timestamp | Last heartbeat time |

#### `issues`
| Column | Type | Description |
|--------|------|-------------|
| `id` | uuid PK | |
| `companyId` | uuid FK -> companies | |
| `projectId` | uuid FK -> projects | |
| `goalId` | uuid FK -> goals | |
| `parentId` | uuid FK -> issues | Sub-issue hierarchy |
| `title` | text | |
| `description` | text | |
| `status` | text | `backlog` / `todo` / `in_progress` / `in_review` / `blocked` / `done` / `cancelled` |
| `priority` | text | `low` / `medium` / `high` / `urgent` |
| `assigneeAgentId` | uuid FK -> agents | |
| `assigneeUserId` | text | Human assignee |
| `identifier` | text | e.g., `PAP-42` (unique) |
| `checkoutRunId` | uuid FK -> heartbeat_runs | Run that checked out this issue |
| `executionRunId` | uuid FK -> heartbeat_runs | Currently executing run |
| `executionWorkspaceId` | uuid FK -> execution_workspaces | |
| `originKind` | text | `manual` / `routine_execution` / `agent_created` |
| `requestDepth` | int | Nesting depth for agent-created issues |

#### `projects`
| Column | Type | Description |
|--------|------|-------------|
| `id` | uuid PK | |
| `companyId` | uuid FK -> companies | |
| `goalId` | uuid FK -> goals | |
| `name` | text | |
| `status` | text | `backlog` / `active` / `done` / `archived` |
| `leadAgentId` | uuid FK -> agents | |
| `targetDate` | date | |
| `executionWorkspacePolicy` | jsonb | Workspace strategy config |

#### `goals`
| Column | Type | Description |
|--------|------|-------------|
| `id` | uuid PK | |
| `companyId` | uuid FK -> companies | |
| `title` | text | |
| `level` | text | `task` / `milestone` / `objective` / `strategy` |
| `status` | text | `planned` / `active` / `achieved` / `abandoned` |
| `parentId` | uuid FK -> goals | Hierarchical goals |
| `ownerAgentId` | uuid FK -> agents | |

### Heartbeat & Execution

#### `heartbeat_runs`
| Column | Type | Description |
|--------|------|-------------|
| `id` | uuid PK | |
| `companyId` | uuid FK | |
| `agentId` | uuid FK -> agents | |
| `invocationSource` | text | `timer` / `assignment` / `on_demand` / `automation` |
| `triggerDetail` | text | `manual` / `ping` / `callback` / `system` |
| `status` | text | `queued` / `running` / `succeeded` / `failed` / `cancelled` / `timed_out` |
| `startedAt` | timestamp | |
| `finishedAt` | timestamp | |
| `error` | text | Error message |
| `errorCode` | text | `process_lost`, `agent_not_found`, etc. |
| `wakeupRequestId` | uuid FK -> agent_wakeup_requests | |
| `exitCode` | int | Process exit code |
| `signal` | text | Kill signal |
| `usageJson` | jsonb | Token usage (input, cached, output) |
| `resultJson` | jsonb | Adapter result summary |
| `sessionIdBefore` | text | Session before run |
| `sessionIdAfter` | text | Session after run |
| `logStore` | text | `local_file` |
| `logRef` | text | Path to log file |
| `logBytes` | bigint | |
| `processPid` | int | Child process PID |
| `retryOfRunId` | uuid FK -> heartbeat_runs | |
| `processLossRetryCount` | int | |
| `contextSnapshot` | jsonb | Full context (issueId, projectId, workspace info, etc.) |

#### `heartbeat_run_events`
Ordered lifecycle events per run (seq number, event type, stream, level, message, payload).

#### `agent_wakeup_requests`
Queued wakeup requests with source, status, idempotency key, payload.

#### `agent_runtime_state`
Per-agent singleton: session ID, total token counters, total cost, last run status.

#### `agent_task_sessions`
Per-agent per-task session persistence: `(companyId, agentId, adapterType, taskKey)` -> `sessionParamsJson`, `sessionDisplayId`.

### Cost & Budget

#### `cost_events`
| Column | Type | Description |
|--------|------|-------------|
| `id` | uuid PK | |
| `companyId` | uuid FK | |
| `agentId` | uuid FK -> agents | |
| `issueId` | uuid FK -> issues | (optional) |
| `projectId` | uuid FK -> projects | (optional) |
| `heartbeatRunId` | uuid FK -> heartbeat_runs | |
| `provider` | text | e.g., `anthropic`, `openai` |
| `biller` | text | Billing entity |
| `billingType` | text | `metered_api` / `subscription_included` / `subscription_overage` / `credits` / `fixed` / `unknown` |
| `model` | text | Model name |
| `inputTokens` | int | |
| `cachedInputTokens` | int | |
| `outputTokens` | int | |
| `costCents` | int | Cost in cents |
| `occurredAt` | timestamp | |

#### `budget_policies`
Per-scope budget rules: `(companyId, scopeType, scopeId, metric, windowKind)`. Supports `company`, `agent`, `project` scopes with `calendar_month_utc` or `lifetime` windows. Fields: `amount`, `warnPercent`, `hardStopEnabled`, `notifyEnabled`.

#### `budget_incidents`
Records when a budget threshold is crossed: `warning` or `hard_stop`. Links to an approval for resolution. Tracks `amountLimit`, `amountObserved`, window boundaries.

#### `finance_events`
External billing events (subscription payments, credits, etc.).

### Workspace Management

#### `project_workspaces`
| Column | Type | Description |
|--------|------|-------------|
| `id` | uuid PK | |
| `projectId` | uuid FK -> projects | |
| `name` | text | |
| `sourceType` | text | `local_path` / `remote` |
| `cwd` | text | Local filesystem path |
| `repoUrl` | text | Git repository URL |
| `repoRef` | text | Branch/tag |
| `isPrimary` | bool | |
| `setupCommand` | text | |
| `cleanupCommand` | text | |

#### `execution_workspaces`
Per-issue or per-run workspaces (git worktrees, isolated directories):

| Column | Type | Description |
|--------|------|-------------|
| `mode` | text | `shared_workspace` / `isolated_workspace` / `operator_branch` / `adapter_managed` |
| `strategyType` | text | `git_worktree` / `project_primary` |
| `status` | text | `active` / `idle` / `archived` |
| `cwd` | text | Workspace path |
| `branchName` | text | Git branch |
| `providerType` | text | `git_worktree` / `local_fs` |
| `providerRef` | text | Worktree path |

#### `workspace_operations`
Audit log of workspace operations (create, checkout, cleanup) with command logs.

#### `workspace_runtime_services`
Runtime services started for workspaces (dev servers, etc.) with URLs and lifecycle state.

### Access & Auth

#### `auth_users`, `auth_sessions`, `auth_accounts`, `auth_verifications`
Better Auth managed tables.

#### `instance_user_roles`
Instance-level roles: `instance_admin`.

#### `company_memberships`
`(companyId, principalType, principalId)` -- maps users and agents to companies with `membershipRole` (owner, admin, member).

#### `board_api_keys`
Long-lived API keys for board users (SHA-256 hashed).

#### `agent_api_keys`
Per-agent API keys: `keyHash` (SHA-256), `lastUsedAt`, `revokedAt`.

#### `cli_auth_challenges`
PKCE-like challenge/response for CLI authentication.

#### `principal_permission_grants`
Fine-grained permission grants: `(companyId, principalType, principalId, permissionKey)`.

#### `invites` / `join_requests`
Agent onboarding flow with invite tokens and join request approval.

### Other Tables

| Table | Purpose |
|-------|---------|
| `agent_config_revisions` | Version history for agent configuration changes |
| `approvals` | General approval workflow (type, status, payload, decision) |
| `approval_comments` | Discussion on approvals |
| `issue_approvals` | Links between issues and approvals |
| `issue_comments` | Threaded comments on issues |
| `issue_attachments` | File attachments (stored in storage service) |
| `issue_documents` | Key-value documents per issue (plans, specs) |
| `document_revisions` | Version history for documents |
| `issue_labels` / `labels` | Tagging system |
| `issue_work_products` | Deliverables per issue (PRs, commits, files) |
| `issue_read_states` | Read/unread tracking per user per issue |
| `issue_inbox_archives` | Inbox archive state |
| `activity_log` | Audit trail of all actions |
| `routines` | Recurring task definitions |
| `routine_triggers` | Cron triggers, webhook triggers |
| `routine_runs` | Execution history of routines |
| `company_secrets` / `company_secret_versions` | Encrypted secrets management |
| `company_skills` | Shared skill definitions (markdown instructions) |
| `company_logos` | Company logo assets |
| `instance_settings` | Global instance configuration |
| `plugins` / `plugin_config` / `plugin_state` | Plugin system tables |
| `plugin_entities` / `plugin_jobs` / `plugin_logs` / `plugin_webhooks` | Plugin data |
| `plugin_company_settings` | Per-company plugin configuration |
| `project_goals` | Many-to-many project-goal links |

---

## 4. API Routes

All routes are mounted under `/api`. Authentication is handled by the `actorMiddleware` which sets `req.actor`.

### Health

| Method | Path | Description |
|--------|------|-------------|
| GET | `/api/health` | Server health, deployment mode, auth status |

### Companies

| Method | Path | Description |
|--------|------|-------------|
| GET | `/api/companies` | List all companies |
| GET | `/api/companies/stats` | Company-level statistics |
| GET | `/api/companies/:companyId` | Get single company |
| POST | `/api/companies` | Create company |
| PATCH | `/api/companies/:companyId` | Update company |
| PATCH | `/api/companies/:companyId/branding` | Update brand color |
| POST | `/api/companies/:companyId/archive` | Archive company |
| DELETE | `/api/companies/:companyId` | Delete company (if enabled) |
| POST | `/api/companies/:companyId/export` | Export company as portable package |
| POST | `/api/companies/import` | Import company from package |
| POST | `/api/companies/:companyId/exports/preview` | Preview export |
| POST | `/api/companies/:companyId/imports/preview` | Preview import |
| POST | `/api/companies/:companyId/imports/apply` | Apply import |

### Agents

| Method | Path | Description |
|--------|------|-------------|
| GET | `/api/companies/:companyId/agents` | List agents |
| GET | `/api/companies/:companyId/org` | Org chart JSON |
| GET | `/api/companies/:companyId/org.svg` | Org chart SVG |
| GET | `/api/companies/:companyId/org.png` | Org chart PNG |
| GET | `/api/companies/:companyId/agent-configurations` | Available adapter configs |
| GET | `/api/companies/:companyId/adapters/:type/models` | List models for adapter |
| GET | `/api/agents/me` | Current agent (from API key) |
| GET | `/api/agents/me/inbox-lite` | Agent inbox summary |
| GET | `/api/agents/:id` | Get agent details |
| GET | `/api/agents/:id/configuration` | Agent configuration doc |
| GET | `/api/agents/:id/config-revisions` | Configuration history |
| GET | `/api/agents/:id/config-revisions/:revisionId` | Specific revision |
| POST | `/api/agents/:id/config-revisions/:revisionId/rollback` | Rollback config |
| GET | `/api/agents/:id/runtime-state` | Runtime state (session, counters) |
| GET | `/api/agents/:id/task-sessions` | List task sessions |
| POST | `/api/agents/:id/runtime-state/reset-session` | Reset session |
| POST | `/api/companies/:companyId/agent-hires` | Hire agent (approval flow) |
| POST | `/api/companies/:companyId/agents` | Create agent directly |
| PATCH | `/api/agents/:id` | Update agent |
| PATCH | `/api/agents/:id/permissions` | Update permissions |
| PATCH | `/api/agents/:id/instructions-path` | Update instructions file path |
| GET | `/api/agents/:id/instructions-bundle` | Get instructions bundle |
| PATCH | `/api/agents/:id/instructions-bundle` | Update instructions bundle |
| GET | `/api/agents/:id/instructions-bundle/file` | Read instructions file |
| PUT | `/api/agents/:id/instructions-bundle/file` | Write instructions file |
| DELETE | `/api/agents/:id/instructions-bundle/file` | Delete instructions file |
| POST | `/api/agents/:id/pause` | Pause agent |
| POST | `/api/agents/:id/resume` | Resume agent |
| POST | `/api/agents/:id/terminate` | Terminate agent |
| DELETE | `/api/agents/:id` | Delete agent |
| GET | `/api/agents/:id/keys` | List API keys |
| POST | `/api/agents/:id/keys` | Create API key |
| DELETE | `/api/agents/:id/keys/:keyId` | Revoke API key |
| POST | `/api/agents/:id/wakeup` | Wake agent (enqueue heartbeat) |
| POST | `/api/agents/:id/heartbeat/invoke` | Invoke heartbeat directly |
| POST | `/api/agents/:id/claude-login` | Run Claude CLI login |
| GET | `/api/agents/:id/skills` | List agent skills |
| POST | `/api/agents/:id/skill-sync` | Sync skills to agent |

### Agent Skills

| Method | Path | Description |
|--------|------|-------------|
| POST | `/api/agents/:id/test-environment` | Test adapter environment |

### Heartbeat Runs

| Method | Path | Description |
|--------|------|-------------|
| GET | `/api/companies/:companyId/heartbeat-runs` | List runs |
| GET | `/api/companies/:companyId/live-runs` | Currently running/queued runs |
| GET | `/api/instance/scheduler-heartbeats` | All agents with heartbeat status |
| GET | `/api/heartbeat-runs/:runId` | Get run details |
| POST | `/api/heartbeat-runs/:runId/cancel` | Cancel run |
| GET | `/api/heartbeat-runs/:runId/events` | Run lifecycle events |
| GET | `/api/heartbeat-runs/:runId/log` | Full run log |
| GET | `/api/heartbeat-runs/:runId/workspace-operations` | Workspace ops for run |
| GET | `/api/heartbeat-runs/:runId/issues` | Issues touched by run |
| GET | `/api/workspace-operations/:operationId/log` | Operation log |
| GET | `/api/issues/:issueId/live-runs` | Active runs for issue |
| GET | `/api/issues/:issueId/active-run` | Current active run for issue |

### Issues

| Method | Path | Description |
|--------|------|-------------|
| GET | `/api/companies/:companyId/issues` | List issues (filters: status, assignee, project, goal, label) |
| GET | `/api/issues/:id` | Get issue |
| GET | `/api/issues/:id/heartbeat-context` | Context for heartbeat run |
| GET | `/api/issues/:id/comments` | List comments |
| GET | `/api/issues/:id/comments/:commentId` | Get comment |
| POST | `/api/issues/:id/comments` | Add comment (supports agent @mentions -> wakeup) |
| GET | `/api/issues/:id/attachments` | List attachments |
| POST | `/api/companies/:companyId/issues/:issueId/attachments` | Upload attachment |
| GET | `/api/attachments/:attachmentId/content` | Download attachment |
| DELETE | `/api/attachments/:attachmentId` | Delete attachment |
| GET | `/api/issues/:id/work-products` | List deliverables |
| POST | `/api/issues/:id/work-products` | Create work product |
| PATCH | `/api/work-products/:id` | Update work product |
| DELETE | `/api/work-products/:id` | Delete work product |
| GET | `/api/issues/:id/documents` | List documents |
| GET | `/api/issues/:id/documents/:key` | Get document by key |
| PUT | `/api/issues/:id/documents/:key` | Upsert document |
| GET | `/api/issues/:id/documents/:key/revisions` | Document revision history |
| DELETE | `/api/issues/:id/documents/:key` | Delete document |
| GET | `/api/issues/:id/approvals` | Linked approvals |
| POST | `/api/issues/:id/approvals` | Link approval |
| DELETE | `/api/issues/:id/approvals/:approvalId` | Unlink approval |
| POST | `/api/companies/:companyId/issues` | Create issue |
| PATCH | `/api/issues/:id` | Update issue |
| DELETE | `/api/issues/:id` | Delete issue |
| POST | `/api/issues/:id/checkout` | Checkout issue (agent claims work) |
| POST | `/api/issues/:id/release` | Release issue checkout |
| POST | `/api/issues/:id/read` | Mark as read |
| POST | `/api/issues/:id/inbox-archive` | Archive from inbox |
| DELETE | `/api/issues/:id/inbox-archive` | Unarchive from inbox |

### Labels

| Method | Path | Description |
|--------|------|-------------|
| GET | `/api/companies/:companyId/labels` | List labels |
| POST | `/api/companies/:companyId/labels` | Create label |
| DELETE | `/api/labels/:labelId` | Delete label |

### Projects

| Method | Path | Description |
|--------|------|-------------|
| GET | `/api/companies/:companyId/projects` | List projects |
| GET | `/api/projects/:id` | Get project |
| POST | `/api/companies/:companyId/projects` | Create project |
| PATCH | `/api/projects/:id` | Update project |
| DELETE | `/api/projects/:id` | Delete project |
| GET | `/api/projects/:id/workspaces` | List project workspaces |
| POST | `/api/projects/:id/workspaces` | Create workspace |
| PATCH | `/api/projects/:id/workspaces/:workspaceId` | Update workspace |
| DELETE | `/api/projects/:id/workspaces/:workspaceId` | Delete workspace |

### Goals

| Method | Path | Description |
|--------|------|-------------|
| GET | `/api/companies/:companyId/goals` | List goals |
| GET | `/api/goals/:id` | Get goal |
| POST | `/api/companies/:companyId/goals` | Create goal |
| PATCH | `/api/goals/:id` | Update goal |
| DELETE | `/api/goals/:id` | Delete goal |

### Routines

| Method | Path | Description |
|--------|------|-------------|
| GET | `/api/companies/:companyId/routines` | List routines |
| POST | `/api/companies/:companyId/routines` | Create routine |
| GET | `/api/routines/:id` | Get routine |
| PATCH | `/api/routines/:id` | Update routine |
| GET | `/api/routines/:id/runs` | List routine runs |
| POST | `/api/routines/:id/triggers` | Create trigger |
| PATCH | `/api/routine-triggers/:id` | Update trigger |
| DELETE | `/api/routine-triggers/:id` | Delete trigger |
| POST | `/api/routines/:id/run` | Manually trigger routine |
| POST | `/api/routine-triggers/public/:publicId/fire` | Fire webhook trigger |
| POST | `/api/routine-triggers/:id/rotate-secret` | Rotate trigger secret |

### Execution Workspaces

| Method | Path | Description |
|--------|------|-------------|
| GET | `/api/companies/:companyId/execution-workspaces` | List |
| GET | `/api/execution-workspaces/:id` | Get workspace |
| PATCH | `/api/execution-workspaces/:id` | Update workspace |

### Approvals

| Method | Path | Description |
|--------|------|-------------|
| GET | `/api/companies/:companyId/approvals` | List approvals |
| GET | `/api/approvals/:id` | Get approval |
| POST | `/api/companies/:companyId/approvals` | Create approval |
| GET | `/api/approvals/:id/issues` | Linked issues |
| POST | `/api/approvals/:id/approve` | Approve |
| POST | `/api/approvals/:id/reject` | Reject |
| POST | `/api/approvals/:id/request-revision` | Request changes |
| POST | `/api/approvals/:id/resubmit` | Resubmit |
| GET | `/api/approvals/:id/comments` | List comments |
| POST | `/api/approvals/:id/comments` | Add comment |

### Costs & Budgets

| Method | Path | Description |
|--------|------|-------------|
| POST | `/api/companies/:companyId/cost-events` | Record cost event |
| POST | `/api/companies/:companyId/finance-events` | Record finance event |
| GET | `/api/companies/:companyId/costs/summary` | Cost summary |
| GET | `/api/companies/:companyId/costs/by-agent` | Costs by agent |
| GET | `/api/companies/:companyId/costs/by-agent-model` | Costs by agent+model |
| GET | `/api/companies/:companyId/costs/by-provider` | Costs by provider |
| GET | `/api/companies/:companyId/costs/by-biller` | Costs by biller |
| GET | `/api/companies/:companyId/costs/by-project` | Costs by project |
| GET | `/api/companies/:companyId/costs/window-spend` | Current window spend |
| GET | `/api/companies/:companyId/costs/quota-windows` | Provider quota windows |
| GET | `/api/companies/:companyId/costs/finance-summary` | Finance summary |
| GET | `/api/companies/:companyId/costs/finance-by-biller` | Finance by biller |
| GET | `/api/companies/:companyId/costs/finance-by-kind` | Finance by kind |
| GET | `/api/companies/:companyId/costs/finance-events` | Finance events |
| GET | `/api/companies/:companyId/budgets/overview` | All budget policies and incidents |
| POST | `/api/companies/:companyId/budgets/policies` | Upsert budget policy |
| POST | `/api/companies/:companyId/budgets/incidents/:incidentId/resolve` | Resolve incident |
| PATCH | `/api/companies/:companyId/budgets` | Update company budget |
| PATCH | `/api/agents/:agentId/budgets` | Update agent budget |

### Secrets

| Method | Path | Description |
|--------|------|-------------|
| GET | `/api/companies/:companyId/secret-providers` | Available providers |
| GET | `/api/companies/:companyId/secrets` | List secrets (names only) |
| POST | `/api/companies/:companyId/secrets` | Create secret |
| POST | `/api/secrets/:id/rotate` | Rotate secret value |
| PATCH | `/api/secrets/:id` | Update secret metadata |
| DELETE | `/api/secrets/:id` | Delete secret |

### Company Skills

| Method | Path | Description |
|--------|------|-------------|
| GET | `/api/companies/:companyId/skills` | List skills |
| GET | `/api/companies/:companyId/skills/:skillId` | Get skill |
| GET | `/api/companies/:companyId/skills/:skillId/update-status` | Check for updates |
| GET | `/api/companies/:companyId/skills/:skillId/files` | List skill files |
| POST | `/api/companies/:companyId/skills` | Create/import skill |
| PATCH | `/api/companies/:companyId/skills/:skillId` | Update skill |
| POST | `/api/companies/:companyId/skills/sync-from-path` | Sync from filesystem |
| POST | `/api/companies/:companyId/skills/import-from-url` | Import from URL |
| DELETE | `/api/companies/:companyId/skills/:skillId` | Delete skill |
| POST | `/api/companies/:companyId/skills/:skillId/install-update` | Install update |

### Activity

| Method | Path | Description |
|--------|------|-------------|
| GET | `/api/companies/:companyId/activity` | Activity log |
| POST | `/api/companies/:companyId/activity` | Create activity entry |
| GET | `/api/issues/:id/activity` | Activity for issue |
| GET | `/api/issues/:id/runs` | Runs for issue |

### Dashboard & Sidebar

| Method | Path | Description |
|--------|------|-------------|
| GET | `/api/companies/:companyId/dashboard` | Dashboard summary |
| GET | `/api/companies/:companyId/sidebar-badges` | Sidebar badge counts |

### Instance Settings

| Method | Path | Description |
|--------|------|-------------|
| GET | `/api/instance/settings/general` | General settings |
| PATCH | `/api/instance/settings/general` | Update general settings |
| GET | `/api/instance/settings/experimental` | Experimental features |
| PATCH | `/api/instance/settings/experimental` | Update experimental |

### Access & Invites

| Method | Path | Description |
|--------|------|-------------|
| GET | `/api/board-claim/:token` | Check board claim token |
| POST | `/api/board-claim/:token/claim` | Claim board ownership |
| POST | `/api/cli-auth/challenges` | Create CLI auth challenge |
| GET | `/api/cli-auth/challenges/:id` | Check challenge status |
| POST | `/api/cli-auth/challenges/:id/approve` | Approve CLI auth |
| POST | `/api/cli-auth/challenges/:id/exchange` | Exchange for token |
| GET | `/api/cli-auth/me` | Current CLI user |
| POST | `/api/cli-auth/revoke-current` | Revoke current CLI token |
| GET | `/api/invites/:token` | Get invite details |
| GET | `/api/invites/:token/onboarding` | Onboarding instructions |
| POST | `/api/invites/:token/accept` | Accept invite |
| POST | `/api/invites/:inviteId/revoke` | Revoke invite |
| GET | `/api/companies/:companyId/join-requests` | List join requests |
| POST | `/api/companies/:companyId/join-requests/:requestId/approve` | Approve join |
| POST | `/api/companies/:companyId/join-requests/:requestId/reject` | Reject join |
| GET | `/api/companies/:companyId/members` | List members |
| PATCH | `/api/companies/:companyId/members/:membershipId` | Update membership |
| POST | `/api/companies/:companyId/members/:membershipId/suspend` | Suspend member |
| POST | `/api/companies/:companyId/members/:membershipId/reinstate` | Reinstate member |
| GET | `/api/admin/users/:userId/company-access` | Admin: user access |
| PUT | `/api/admin/users/:userId/company-access` | Admin: set access |
| GET | `/api/skills/available` | Available skill templates |
| GET | `/api/skills/index` | Skill index |
| GET | `/api/skills/:skillName` | Get skill template |
| POST | `/api/companies/:companyId/skills/create-from-template` | Create skill from template |
| POST | `/api/companies/:companyId/invites` | Create invite |

### LLM Configuration

| Method | Path | Description |
|--------|------|-------------|
| GET | `/llms/agent-configuration.txt` | Agent configuration docs (plain text) |
| GET | `/llms/agent-icons.txt` | Available agent icons |
| GET | `/llms/agent-configuration/:adapterType.txt` | Per-adapter docs |

### Plugins

| Method | Path | Description |
|--------|------|-------------|
| GET | `/api/plugins` | List plugins |
| GET | `/api/plugins/examples` | Example plugins |
| GET | `/api/plugins/ui-contributions` | UI contributions from plugins |
| GET | `/api/plugins/tools` | Plugin-provided tools |
| POST | `/api/plugins/tools/execute` | Execute plugin tool |
| POST | `/api/plugins/install` | Install plugin |
| GET | `/api/plugins/:pluginId` | Get plugin |
| DELETE | `/api/plugins/:pluginId` | Uninstall plugin |
| POST | `/api/plugins/:pluginId/enable` | Enable plugin |
| POST | `/api/plugins/:pluginId/disable` | Disable plugin |
| GET | `/api/plugins/:pluginId/health` | Plugin health |
| GET | `/api/plugins/:pluginId/logs` | Plugin logs |
| POST | `/api/plugins/:pluginId/upgrade` | Upgrade plugin |
| GET | `/api/plugins/:pluginId/config` | Plugin config |
| POST | `/api/plugins/:pluginId/config` | Update config |
| POST | `/api/plugins/:pluginId/config/test` | Test config |
| GET | `/api/plugins/:pluginId/jobs` | Plugin jobs |
| GET | `/api/plugins/:pluginId/jobs/:jobId/runs` | Job runs |
| POST | `/api/plugins/:pluginId/jobs/:jobId/trigger` | Trigger job |
| POST | `/api/plugins/:pluginId/webhooks/:endpointKey` | Webhook endpoint |
| GET | `/api/plugins/:pluginId/dashboard` | Plugin dashboard |
| POST | `/api/plugins/:pluginId/bridge/data` | Bridge data request |
| POST | `/api/plugins/:pluginId/bridge/action` | Bridge action request |
| GET | `/api/plugins/:pluginId/bridge/stream/:channel` | SSE stream |

### Assets

| Method | Path | Description |
|--------|------|-------------|
| POST | `/api/companies/:companyId/assets/images` | Upload image |
| POST | `/api/companies/:companyId/logo` | Upload company logo |
| GET | `/api/assets/:assetId/content` | Get asset content |

### WebSocket

| Endpoint | Description |
|----------|-------------|
| `ws://host/api/companies/:companyId/events/ws` | Live events stream |

---

## 5. Services

All business logic is in `src/services/`. Services are factory functions that receive `db: Db` and return method objects.

### `heartbeatService(db)` -- `heartbeat.ts`
Core execution engine. See Section 2.

### `agentService(db)` -- `agents.ts`
CRUD for agents. Config revision tracking. API key generation (`pcp_` prefix, SHA-256 hashed). Short-name collision detection. Permission normalization.

### `issueService(db)` -- `issues.ts`
Issue CRUD with filtering (status, assignee, project, goal, label, origin). Issue checkout/release (execution locking). Comment system with agent @mention detection. Sub-issue hierarchy. Inbox read/archive state.

### `costService(db, budgetHooks)` -- `costs.ts`
Records cost events from adapter execution results. Updates materialized spend counters on agents and companies. Triggers budget evaluation after each event. Provides summary/breakdown queries by agent, model, provider, biller, project.

### `budgetService(db, hooks)` -- `budgets.ts`
Budget policy CRUD. Evaluates cost events against policies. Creates incidents when thresholds are crossed. Hard-stop enforcement pauses scope (agent/company/project). Creates approval requests. `getInvocationBlock()` checks if a scope is budget-paused before allowing runs.

### `approvalService(db)` -- `approvals.ts`
Generic approval workflow (approve/reject/request-revision/resubmit). Types: `agent_hire`, `budget_breach`, custom. Links to issues via `issue_approvals`.

### `routineService(db)` -- `routines.ts`
Recurring tasks with cron triggers. Creates issues on trigger. Concurrency policies: `coalesce_if_active` (skip if issue already open), `always_create`. Catch-up policy: `skip_missed`. Webhook triggers with HMAC signing.

### `goalService(db)` -- `goals.ts`
Hierarchical goal CRUD with levels (task, milestone, objective, strategy).

### `projectService(db)` -- `projects.ts`
Project CRUD. Workspace management. Execution workspace policy (shared, isolated, operator branch).

### `companyService(db)` -- `companies.ts`
Company CRUD with archive/pause.

### `companySkillService(db)` -- `company-skills.ts`
Shared skill definitions (markdown instructions injected into agent contexts). Sync from filesystem paths or URLs. File inventory tracking.

### `secretService(db)` -- `secrets.ts`
Secret management with providers (`local_encrypted`, external). Version tracking. Resolution of `$secret:name` references in adapter configs at runtime.

### `accessService(db)` -- `access.ts`
Membership management. Permission grants. Company access checks.

### `boardAuthService(db)` -- `board-auth.ts`
Board API key management. Token resolution.

### `instanceSettingsService(db)` -- `instance-settings.ts`
Global settings (general, experimental).

### `executionWorkspaceService(db)` -- `execution-workspaces.ts`
CRUD for execution workspaces.

### `workspaceOperationService(db)` -- `workspace-operations.ts`
Records workspace setup/teardown operations with command logs.

### `workProductService(db)` -- `work-products.ts`
Issue deliverable tracking (PRs, commits, files).

### `documentService(db)` -- `documents.ts`
Key-value document store per issue with revision history.

### `activityService(db)` -- `activity.ts`
Activity log queries with filters.

### `dashboardService(db)` -- `dashboard.ts`
Aggregated dashboard data.

### `sidebarBadgeService(db)` -- `sidebar-badges.ts`
Sidebar notification badge counts.

### `financeService(db)` -- `finance.ts`
External billing event tracking (subscriptions, credits).

### `liveEvents` -- `live-events.ts`
In-memory EventEmitter for real-time events. Per-company channels. `publishLiveEvent()` and `subscribeCompanyLiveEvents()`.

### `companyPortabilityService(db)` -- `company-portability.ts`
Export/import companies as portable JSON packages.

### Plugin Services
- `pluginLoader` -- Load plugins from local directories
- `pluginWorkerManager` -- JSON-RPC worker process lifecycle
- `pluginJobScheduler` / `pluginJobStore` -- Cron job scheduling
- `pluginToolDispatcher` -- Tool execution through plugins
- `pluginLifecycleManager` -- Enable/disable/upgrade
- `pluginEventBus` -- Inter-plugin event routing
- `pluginDevWatcher` -- Hot-reload during development
- `pluginHostServices` -- Host capabilities exposed to plugins

---

## 6. Authentication

### Three Actor Types

| Actor Type | Source | Capabilities |
|-----------|--------|-------------|
| `board` | Session cookie, Board API key, local_trusted implicit | Full access to all companies (scoped by membership) |
| `agent` | Agent API key (`Bearer pcp_...`), Local Agent JWT | Scoped to own company, permission-gated |
| `none` | No credentials | Read-only public routes only |

### Authentication Flow (`middleware/auth.ts`)

1. Check `Authorization: Bearer <token>` header
2. If no bearer: check Better Auth session cookie (authenticated mode) or grant implicit local board (local_trusted mode)
3. If bearer: try Board API key lookup (`board_api_keys` table, SHA-256 hash)
4. If not board key: try Agent API key lookup (`agent_api_keys` table, SHA-256 hash)
5. If not agent key: try Local Agent JWT verification
6. Set `req.actor` with type, IDs, and source

### Agent JWT (`agent-auth-jwt.ts`)

Per-run JWTs issued by the heartbeat service:

- Algorithm: HS256
- Secret: `PAPERCLIP_AGENT_JWT_SECRET` env var
- TTL: 48 hours (configurable)
- Claims: `sub` (agentId), `company_id`, `adapter_type`, `run_id`, `iat`, `exp`, `iss`, `aud`
- Injected as `PAPERCLIP_API_KEY` environment variable into adapter processes

### Better Auth (`auth/better-auth.ts`)

- Email/password authentication
- Drizzle adapter for PostgreSQL
- Trusted origins derived from config + env
- Session resolution from request headers
- Optional sign-up disable

### Board Claim

For instances transitioning from `local_trusted` to `authenticated` mode, a one-time board claim URL allows the first real user to claim admin ownership.

---

## 7. Real-time (WebSocket Live Events)

### Architecture

```
Service Layer ──> publishLiveEvent() ──> EventEmitter (per-company channel)
                                                │
                                    subscribeCompanyLiveEvents()
                                                │
                                         WebSocket Server
                                                │
                                    ┌───────────┼───────────┐
                                 Client 1    Client 2    Client N
```

### WebSocket Endpoint

`GET /api/companies/:companyId/events/ws`

- Authentication: Bearer token (query param `?token=` or Authorization header) or session cookie
- Ping/pong keepalive every 30 seconds
- Client receives JSON-encoded `LiveEvent` objects

### Event Types

| Event Type | Triggered By |
|-----------|-------------|
| `heartbeat.run.status` | Run status changes (queued, running, succeeded, failed, cancelled) |
| `heartbeat.run.event` | Run lifecycle events (adapter invoke, errors) |
| `heartbeat.run.log` | Stdout/stderr chunks from running agents |
| `heartbeat.run.queued` | New run enqueued |
| `agent.status` | Agent status changes (idle, running, error, paused) |
| Various CRUD events | Issue, project, goal, approval changes |

### Event Shape

```typescript
interface LiveEvent {
  id: number;           // monotonic sequence
  companyId: string;
  type: string;         // e.g., "heartbeat.run.log"
  createdAt: string;    // ISO 8601
  payload: Record<string, unknown>;
}
```

---

## 8. Budget System

### Budget Scopes

Budgets cascade across three scope types:

| Scope | Entity | Example |
|-------|--------|---------|
| `company` | Company | Company-wide $500/month |
| `agent` | Agent | Agent limited to $100/month |
| `project` | Project | Project capped at $200/month |

### Budget Policies (`budget_policies`)

Each policy defines:
- `metric`: `billed_cents` (only metric currently)
- `windowKind`: `calendar_month_utc` or `lifetime`
- `amount`: Threshold in cents
- `warnPercent`: Warning threshold (default 80%)
- `hardStopEnabled`: Whether to pause the scope on breach
- `notifyEnabled`: Whether to create an approval/notification

Unique constraint on `(companyId, scopeType, scopeId, metric, windowKind)`.

### Evaluation Flow

1. Cost event recorded via `costService.createEvent()`
2. `budgetService.evaluateCostEvent()` fires
3. Computes observed amount (sum of `costCents` in window)
4. Checks against all active policies for the scope
5. If `warnPercent` crossed: creates `warning` incident + approval
6. If `amount` crossed and `hardStopEnabled`: creates `hard_stop` incident, pauses scope, cancels active work

### Hard Stop Enforcement

When a hard stop triggers:
1. **Company scope**: Pauses the company, cancels all active agent work
2. **Agent scope**: Pauses the agent, cancels its runs, cancels pending wakeups
3. **Project scope**: Pauses the project, cancels runs for project issues

The `cancelBudgetScopeWork()` hook is called, which:
- Cancels all running/queued heartbeat runs for the scope
- Cancels pending wakeup requests

### Pre-Invocation Check

Before claiming any queued run, `budgets.getInvocationBlock()` checks if the company, agent, or project is budget-paused. If blocked, the run is cancelled with the reason.

### Resolution

Board users resolve budget incidents through the approval workflow:
- Approve (with optional budget increase) -> resumes scope
- Reject -> keeps scope paused
- Can also manually update budget amounts

---

## 9. Cost Tracking

### Cost Event Recording

Each adapter execution produces a cost event with:
- `provider`: LLM provider (anthropic, openai, etc.)
- `biller`: Billing entity (may differ from provider)
- `billingType`: How the cost is classified
- `model`: Model identifier
- Token counts: `inputTokens`, `cachedInputTokens`, `outputTokens`
- `costCents`: Actual cost in cents

### Billing Types

| Type | Description | Cost Treatment |
|------|-------------|----------------|
| `metered_api` | Pay-per-use API | Full cost counted |
| `subscription_included` | Included in subscription | Cost set to 0 |
| `subscription_overage` | Over subscription limit | Full cost counted |
| `credits` | Pre-paid credits | Full cost counted |
| `fixed` | Fixed-price | Full cost counted |
| `unknown` | Unclassified | Full cost counted |

### Usage Normalization

For session-based adapters, raw token counts are cumulative across the session. The heartbeat service computes deltas:

1. Read raw usage from adapter result
2. Find previous run in the same session
3. Compute delta: `current - previous` (fallback to current if negative or no previous)

### Materialized Counters

After each cost event:
- `agents.spentMonthlyCents` updated with current-month sum
- `companies.spentMonthlyCents` updated with current-month sum
- `agentRuntimeState.totalCostCents` incremented (lifetime)

### Provider Quota Windows

Some adapters expose quota windows (e.g., Claude's rate limits). Available via `GET /api/companies/:companyId/costs/quota-windows`.

---

## 10. Workspace Management

### Workspace Resolution Priority

When a heartbeat run starts, the workspace is resolved in this order:

1. **Project workspace** -- If the issue has a project with configured workspaces
   - Check `projectWorkspaces` for the project
   - Prefer the workspace linked to the issue (`projectWorkspaceId`)
   - If `cwd` is missing or sentinel, create a managed workspace (git clone)
   - If the path does not exist, fall back to agent home
2. **Task session workspace** -- If a previous session saved a `cwd`
3. **Agent home** -- `~/.paperclip/workspaces/agents/{agentId}/`

### Execution Workspace Modes

| Mode | Description |
|------|-------------|
| `shared_workspace` | All agents share the project workspace directory |
| `isolated_workspace` | Each issue gets its own git worktree |
| `operator_branch` | Agent works on a branch in a worktree |
| `adapter_managed` | Adapter controls workspace (agent default) |

### Git Worktree Strategy

For `isolated_workspace` and `operator_branch`:
1. Create a git worktree from the project workspace
2. Branch name derived from issue identifier (e.g., `PAP-42`)
3. Worktree path: `{projectCwd}/.paperclip-worktrees/{branchName}`
4. On cleanup: remove worktree, optionally delete branch

### Managed Project Workspaces

When a project workspace has a `repoUrl` but no local `cwd`:
1. Compute path: `~/.paperclip/workspaces/companies/{companyId}/projects/{projectId}/{repoName}`
2. `git clone` the repository (10-minute timeout)
3. Set as the working directory for the run

### Runtime Services

Workspaces can have associated runtime services (dev servers, etc.):
- Configured in `adapterConfig.workspaceRuntime.services`
- Started before adapter execution
- Persisted in `workspace_runtime_services` table
- URLs injected into context as `paperclipRuntimeServices`
- Cleaned up after run completion or on server restart

---

## 11. Company/Org System

### Multi-Company

Paperclip supports multiple companies (organizations) per instance:
- Each company has its own agents, projects, issues, goals, budgets, secrets, skills
- Company isolation enforced via `companyId` foreign keys
- Users can belong to multiple companies via `company_memberships`

### Company Memberships

`company_memberships` links principals to companies:
- `principalType`: `user` or `agent`
- `principalId`: User ID or Agent ID
- `membershipRole`: `owner`, `admin`, `member`
- `status`: `active`, `suspended`

### Agent Hierarchy

Agents form an organizational tree via `agents.reportsTo`:
- **CEO role**: Top of the hierarchy, full permissions, can create agents, can update company branding
- **General role**: Standard agents with configurable permissions
- Org chart visualized as SVG/PNG via `/api/companies/:companyId/org.svg`

### Agent Hiring Flow

1. CEO agent or board user creates a hire request (`POST /api/companies/:companyId/agent-hires`)
2. If `requireBoardApprovalForNewAgents` is true: creates approval
3. Board user approves/rejects
4. On approval: agent is created, API key generated, invite created
5. Agent joins via invite, configures adapter, starts heartbeating

### Permissions

Agent permissions stored in `agents.permissions` (jsonb):
- `canCreateAgents`: Can hire new agents
- Fine-grained grants in `principal_permission_grants`:
  - `tasks:assign`: Can assign issues to other agents

Board users get permissions through:
- Instance admin role (`instance_user_roles`)
- Company membership role (owner/admin)
- Explicit permission grants

### Company Portability

Companies can be exported as JSON packages and imported into other instances:
- Includes agents, projects, issues, goals, skills, secrets (encrypted)
- Preview before import to review changes
- Conflict resolution for existing entities

### Instance Settings

Global settings that apply across all companies:
- **General**: `censorUsernameInLogs` (redact usernames in agent logs)
- **Experimental**: `enableIsolatedWorkspaces` (gate for workspace isolation features)
