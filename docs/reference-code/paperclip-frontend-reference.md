# Paperclip Frontend Architecture Reference

Complete architectural reference for the Paperclip web frontend. Covers every page, component, context provider, API module, real-time system, plugin framework, and design token. Intended to allow a full rebuild from scratch.

---

## Table of Contents

1. [Technology Stack](#1-technology-stack)
2. [Entry Point and Provider Tree](#2-entry-point-and-provider-tree)
3. [Routing System](#3-routing-system)
4. [Layout System](#4-layout-system)
5. [All Pages/Routes](#5-all-pagesroutes)
6. [State Management (Contexts)](#6-state-management-contexts)
7. [API Layer](#7-api-layer)
8. [Real-Time System](#8-real-time-system)
9. [Design System](#9-design-system)
10. [All Components](#10-all-components)
11. [Adapter System (Agent Runtimes)](#11-adapter-system-agent-runtimes)
12. [Plugin System](#12-plugin-system)
13. [Key Interactions](#13-key-interactions)
14. [Agent System](#14-agent-system)
15. [Issue System](#15-issue-system)
16. [Cost and Budget System](#16-cost-and-budget-system)
17. [Hooks](#17-hooks)
18. [Utility Libraries](#18-utility-libraries)
19. [Data Flow Patterns](#19-data-flow-patterns)
20. [File Index](#20-file-index)

---

## 1. Technology Stack

| Layer | Technology |
|-------|-----------|
| Framework | React 18 (StrictMode) |
| Routing | react-router-dom (wrapped with company-prefix logic) |
| Data Fetching | TanStack React Query (`@tanstack/react-query`) |
| Styling | Tailwind CSS v4 with `@tailwindcss/typography` plugin |
| Component Variants | `class-variance-authority` (CVA) |
| UI Primitives | Radix UI (Slot, Dialog, Popover, Tooltip, Select, Tabs, Collapsible, Sheet, ScrollArea, DropdownMenu, Checkbox, Avatar, Command) |
| Drag and Drop | `@dnd-kit/core` + `@dnd-kit/sortable` |
| Icons | Lucide React |
| Markdown | `@mdxeditor/editor` (rich editor), custom `MarkdownBody` renderer |
| Build | Vite |
| Testing | Vitest |
| Color System | OKLCH-based CSS custom properties, light/dark mode |
| Type Sharing | `@paperclipai/shared` (shared types between server and UI) |

### Package Structure

```
ui/
  package.json
  vite.config.ts
  vitest.config.ts
  components.json          # shadcn/ui configuration
  public/
    sw.js                  # Service worker
  src/
    main.tsx               # Entry point
    App.tsx                # Route definitions
    index.css              # Design tokens + global styles
    adapters/              # Agent runtime adapters (9 types)
    api/                   # REST API client modules (24 files)
    components/            # All UI components (~80 files)
      ui/                  # Low-level primitives (21 files)
      transcript/          # Run transcript viewer
    context/               # React contexts (8 providers)
    fixtures/              # Test fixtures
    hooks/                 # Custom hooks (8 files)
    lib/                   # Utilities and helpers (~40 files)
    pages/                 # Page-level components (39 files)
    plugins/               # Plugin UI framework (4 files)
```

---

## 2. Entry Point and Provider Tree

**File:** `src/main.tsx`

The app bootstraps with a deeply nested provider tree. Order matters -- inner providers can consume outer ones.

```
StrictMode
  QueryClientProvider          -- TanStack React Query (staleTime: 30s, refetchOnWindowFocus: true)
    ThemeProvider               -- Light/dark theme toggle, persisted to localStorage
      BrowserRouter             -- Company-prefix-aware router wrapper
        CompanyProvider          -- Active company selection, company list from API
          ToastProvider          -- Toast notification queue (max 5, dedup, TTL by tone)
            LiveUpdatesProvider  -- WebSocket connection for real-time events
              TooltipProvider    -- Radix tooltip context
                BreadcrumbProvider -- Page breadcrumbs + document.title sync
                  SidebarProvider  -- Sidebar open/close + mobile detection (768px breakpoint)
                    PanelProvider  -- Right-side properties panel visibility
                      PluginLauncherProvider -- Plugin launcher modal/drawer/popover hosting
                        DialogProvider -- Global dialog state (new issue, project, goal, agent, onboarding)
                          App
```

Before rendering, `initPluginBridge(React, ReactDOM)` registers host React/ReactDOM on `globalThis.__paperclipPluginBridge__` for plugin isolation.

A service worker (`/sw.js`) is registered on page load.

---

## 3. Routing System

### Company-Prefix Router Wrapper

**File:** `src/lib/router.tsx`

All routing goes through a custom wrapper around `react-router-dom` that automatically prepends the active company's issue prefix (e.g., `/ACME/dashboard`) to all navigation:

- `Link`, `NavLink`, `Navigate`, `useNavigate` -- all wrapped to auto-prefix absolute paths
- Company prefix resolved from: URL params > path extraction > selected company context
- `applyCompanyPrefix()`, `extractCompanyPrefixFromPath()`, `normalizeCompanyPrefix()` from `lib/company-routes.ts`

### Top-Level Routes (from `App.tsx`)

**Public routes (no auth required):**
| Path | Component | Description |
|------|-----------|-------------|
| `/auth` | `AuthPage` | Email sign-in/sign-up |
| `/board-claim/:token` | `BoardClaimPage` | Board seat claim flow |
| `/cli-auth/:id` | `CliAuthPage` | CLI authentication approval |
| `/invite/:token` | `InviteLandingPage` | Invite acceptance |

**Protected routes (behind `CloudAccessGate`):**

`CloudAccessGate` checks `/api/health` for deployment mode:
- `local_trusted` -- no auth needed
- `authenticated` -- checks session via `authApi.getSession()`, redirects to `/auth` if missing
- `bootstrap_pending` -- shows admin bootstrap instructions

**Instance settings routes (`/instance/settings/*` inside `Layout`):**
| Path | Component |
|------|-----------|
| `/instance/settings/general` | `InstanceGeneralSettings` |
| `/instance/settings/heartbeats` | `InstanceSettings` |
| `/instance/settings/experimental` | `InstanceExperimentalSettings` |
| `/instance/settings/plugins` | `PluginManager` |
| `/instance/settings/plugins/:pluginId` | `PluginSettings` |

**Board routes (`/:companyPrefix/*` inside `Layout`):**
| Path | Component | Description |
|------|-----------|-------------|
| `/dashboard` | `Dashboard` | Company dashboard with metrics, charts, activity |
| `/onboarding` | `OnboardingRoutePage` | Re-run onboarding wizard |
| `/companies` | `Companies` | Company list |
| `/company/settings` | `CompanySettings` | Company settings |
| `/company/export/*` | `CompanyExport` | Export company data |
| `/company/import` | `CompanyImport` | Import company data |
| `/skills/*` | `CompanySkills` | Company skills management |
| `/org` | `OrgChart` | Organization chart |
| `/agents` | `Agents` | Agent list (sub-routes: `/all`, `/active`, `/paused`, `/error`) |
| `/agents/new` | `NewAgent` | Create new agent |
| `/agents/:agentId` | `AgentDetail` | Agent detail (sub-routes: `/:tab`, `/runs/:runId`) |
| `/projects` | `Projects` | Project list |
| `/projects/:projectId` | `ProjectDetail` | Project detail (sub-routes: `/overview`, `/issues`, `/issues/:filter`, `/configuration`, `/budget`) |
| `/issues` | `Issues` | Issue list with filters |
| `/issues/:issueId` | `IssueDetail` | Full issue detail page |
| `/routines` | `Routines` | Routine list |
| `/routines/:routineId` | `RoutineDetail` | Routine detail |
| `/execution-workspaces/:workspaceId` | `ExecutionWorkspaceDetail` | Workspace detail |
| `/goals` | `Goals` | Goal list |
| `/goals/:goalId` | `GoalDetail` | Goal detail |
| `/approvals` | `Approvals` | Approval list (sub-routes: `/pending`, `/all`) |
| `/approvals/:approvalId` | `ApprovalDetail` | Approval detail |
| `/costs` | `Costs` | Cost analytics and budgets |
| `/activity` | `Activity` | Company activity feed |
| `/inbox` | `Inbox` | Inbox (sub-routes: `/mine`, `/recent`, `/unread`, `/all`) |
| `/design-guide` | `DesignGuide` | Internal design reference |
| `/tests/ux/runs` | `RunTranscriptUxLab` | Transcript viewer test page |
| `/plugins/:pluginId` | `PluginPage` | Plugin-contributed page |
| `/:pluginRoutePath` | `PluginPage` | Plugin custom route |
| `*` | `NotFoundPage` | 404 page |

**Redirect logic:**
- `/` -- redirects to `/:companyPrefix/dashboard` (or onboarding if no companies)
- Unprefixed board routes (e.g., `/issues`) -- redirected to `/:companyPrefix/issues`
- `/settings` -- redirected to `/instance/settings/general`
- `/inbox` -- redirected to `/inbox/{lastTab}` (persisted via `loadLastInboxTab()`)

---

## 4. Layout System

### Main Layout (`Layout.tsx`)

The `Layout` component provides the full application shell:

```
<div class="bg-background text-foreground">            -- Full viewport
  <a href="#main-content" class="sr-only">              -- Skip link (a11y)
  <WorktreeBanner />                                     -- Git worktree indicator
  <DevRestartBanner />                                   -- Dev server restart notification
  <div class="flex">                                     -- Main row
    [Mobile overlay backdrop]                            -- Black 50% overlay when sidebar open
    <CompanyRail />                                      -- 72px vertical icon rail
    <Sidebar /> or <InstanceSidebar />                   -- 240px nav sidebar (collapsible)
    <div class="flex-1 flex-col">                        -- Content area
      <BreadcrumbBar />                                  -- Sticky on mobile, static on desktop
      <div class="flex">
        <main id="main-content" class="flex-1 p-4 md:p-6">
          <Outlet />                                     -- Page content
        </main>
        <PropertiesPanel />                              -- 320px right panel (desktop only)
      </div>
    </div>
  </div>
  [MobileBottomNav]                                      -- Mobile-only bottom tab bar
  <CommandPalette />                                     -- Cmd+K search dialog
  <NewIssueDialog />                                     -- Global "new issue" dialog
  <NewProjectDialog />
  <NewGoalDialog />
  <NewAgentDialog />
  <ToastViewport />                                      -- Toast notification stack
</div>
```

### Company Rail (`CompanyRail.tsx`)

Left-most 72px vertical strip:
- **Paperclip icon** at top (brand)
- **Company icons** in a sortable list (drag-and-drop via `@dnd-kit`)
  - Circular icons with rounded corners that morph on hover/selection
  - Selection indicator: vertical pill on left edge
  - Live agent indicator: pulsing blue dot (top-right)
  - Unread inbox indicator: red dot (bottom-right)
  - Tooltip on hover showing company name
- **Add company button** (dashed circle with `+`)
- Company order persisted in `localStorage` under `paperclip.companyOrder`, synced across tabs via `StorageEvent`

### Sidebar (`Sidebar.tsx`)

240px wide, collapsible to 0px with smooth transition:

```
Company Name (bold) + Search button
---
New Issue button
Dashboard (with live agent count badge)
Inbox (with unread badge, danger tone on failed runs)
[Plugin sidebar slots]
---
Work section:
  Issues
  Routines (Beta badge)
  Goals
---
Projects section (SidebarProjects -- collapsible, draggable order)
---
Agents section (SidebarAgents -- collapsible, draggable order)
---
Company section:
  Org
  Skills
  Costs
  Activity
  Settings
---
[Plugin sidebar panel slots]
```

Bottom bar (below sidebar): Documentation link, version tooltip, Instance Settings gear icon, Theme toggle (sun/moon).

### Instance Sidebar (`InstanceSidebar.tsx`)

Replaces the company sidebar when on `/instance/*` routes:
- General, Heartbeats, Experimental, Plugins
- Plugin sub-items indented under Plugins

### Mobile Adaptations

- **Breakpoint**: 768px (`MOBILE_BREAKPOINT`)
- **Sidebar**: Slides in from left as overlay (z-50) with backdrop, swipe gestures (edge swipe to open, swipe left to close)
- **Bottom nav**: Fixed 5-column grid: Home, Issues, Create (action), Agents, Inbox
  - Auto-hides on scroll down, shows on scroll up
  - Badge on Inbox icon
  - Safe area inset padding for notched devices
- **Body overflow**: `visible` on mobile (native scroll), `hidden` on desktop (internal scroll areas)
- **BreadcrumbBar**: Sticky with backdrop blur on mobile

### Properties Panel (`PropertiesPanel.tsx`)

Right-side panel, 320px wide, desktop only:
- Content injected via `PanelContext.openPanel(reactNode)`
- Collapsible with smooth width/opacity transition
- Close button in header
- `ScrollArea` for content

---

## 5. All Pages/Routes

### Dashboard (`pages/Dashboard.tsx`)
- Metric cards grid (2-col, 4-col on XL): Agents Enabled, Tasks In Progress, Month Spend, Pending Approvals
- Budget incident banner (red gradient) when active incidents exist
- No-agents banner with onboarding link
- `ActiveAgentsPanel` -- live running agents
- Chart grid (4 charts): Run Activity, Issues by Priority, Issues by Status, Success Rate
- Plugin dashboard widget slots
- Recent Activity list (10 items) with animated new-item highlight
- Recent Tasks list (10 items) with status icons

### Issues (`pages/Issues.tsx`)
- Delegates entirely to `IssuesList` component
- Supports search via URL `?q=` parameter
- Supports `?participantAgentId=` filter
- Live run indicators via heartbeat polling (5s interval)

### Issue Detail (`pages/IssueDetail.tsx`)
- Inline editable title
- Status/Priority icons and badges
- Comment thread with markdown editor
- Mentions support (`@agent-name`)
- Document attachments section
- Live run widget showing active agent work
- Properties panel (status, priority, assignee, project, labels)
- Activity timeline
- Approval links
- Work products section
- Plugin slots: `detailTab`, `taskDetailView`, `contextMenuItem`, `commentAnnotation`
- Plugin launchers

### Agents (`pages/Agents.tsx`)
- Agent list with filter tabs: All, Active, Paused, Error

### Agent Detail (`pages/AgentDetail.tsx`)
- Tabbed interface: Overview, Configuration, Runs, Instructions, Skills, Keys, Runtime
- Overview: status badge, role, adapter type, description
- Configuration: `AgentConfigForm` with adapter-specific fields
- Runs: list with status icons (succeeded/failed/running/queued/timed_out/cancelled), transcript viewer
- Instructions: file tree, markdown editor, managed vs external mode
- Skills: skill list with sync capability
- Keys: API key management (create, revoke, copy)
- Runtime: state viewer, session reset, task sessions
- Budget policy card
- Config revision history with rollback
- Pause/Resume/Terminate controls

### Projects (`pages/Projects.tsx`)
- Project list with create dialog

### Project Detail (`pages/ProjectDetail.tsx`)
- Sub-tabs: Overview, Issues, Configuration, Budget
- Workspace management

### Goals (`pages/Goals.tsx`)
- Goal list with tree view (`GoalTree` component)

### Goal Detail (`pages/GoalDetail.tsx`)
- Goal properties, child goals, linked issues

### Approvals (`pages/Approvals.tsx`)
- Filter tabs: Pending, All
- `ApprovalCard` with approve/reject/request-revision actions

### Approval Detail (`pages/ApprovalDetail.tsx`)
- Full approval with payload renderer, comments, linked issues

### Costs (`pages/Costs.tsx`)
- Date range picker
- Cost summary, by-agent, by-provider, by-project breakdowns
- Finance views: by-biller, by-kind, events timeline
- Budget overview with policy management
- Provider quota bars
- Window spend tracking

### Activity (`pages/Activity.tsx`)
- Chronological activity feed for the company

### Inbox (`pages/Inbox.tsx`)
- Tabs: Mine, Recent, Unread, All
- Swipe-to-archive on mobile
- Mark as read functionality

### Routines (`pages/Routines.tsx`)
- Routine list with triggers and run summaries

### Routine Detail (`pages/RoutineDetail.tsx`)
- Trigger management (create, update, delete, rotate secret)
- Run history
- Activity timeline
- Schedule editor

### Company Settings (`pages/CompanySettings.tsx`)
- Name, description, branding, budget
- Archive/delete company

### Company Skills (`pages/CompanySkills.tsx`)
- Skill library management

### Company Export/Import (`pages/CompanyExport.tsx`, `pages/CompanyImport.tsx`)
- Portable company data export/import with selection preview

### Org Chart (`pages/OrgChart.tsx`)
- Hierarchical agent organization view

### New Agent (`pages/NewAgent.tsx`)
- Agent creation wizard with adapter selection

### Instance Settings Pages
- **General** (`InstanceGeneralSettings.tsx`): Core instance configuration
- **Heartbeats** (`InstanceSettings.tsx`): Scheduler heartbeat monitoring
- **Experimental** (`InstanceExperimentalSettings.tsx`): Feature flags
- **Plugin Manager** (`PluginManager.tsx`): Install, uninstall, enable/disable plugins
- **Plugin Settings** (`PluginSettings.tsx`): Per-plugin config, health, dashboard, logs

### Auth Pages
- **Auth** (`pages/Auth.tsx`): Email sign-in/sign-up
- **Board Claim** (`pages/BoardClaim.tsx`): Board seat claim via token
- **CLI Auth** (`pages/CliAuth.tsx`): CLI authentication challenge approval
- **Invite Landing** (`pages/InviteLanding.tsx`): Invite acceptance (human or agent)

### Other Pages
- **Companies** (`pages/Companies.tsx`): Multi-company switcher
- **My Issues** (`pages/MyIssues.tsx`): Personal issue view
- **Execution Workspace Detail** (`pages/ExecutionWorkspaceDetail.tsx`): Workspace inspection
- **Design Guide** (`pages/DesignGuide.tsx`): Internal design reference
- **Run Transcript UX Lab** (`pages/RunTranscriptUxLab.tsx`): Transcript viewer test page
- **Plugin Page** (`pages/PluginPage.tsx`): Renders plugin-contributed pages
- **Not Found** (`pages/NotFound.tsx`): 404 with scoped messages (board, global, invalid_company_prefix)

---

## 6. State Management (Contexts)

### CompanyContext (`context/CompanyContext.tsx`)

Central context for multi-company support.

```typescript
interface CompanyContextValue {
  companies: Company[];
  selectedCompanyId: string | null;
  selectedCompany: Company | null;
  selectionSource: CompanySelectionSource;  // "bootstrap" | "manual" | "route_sync"
  loading: boolean;
  error: Error | null;
  setSelectedCompanyId: (companyId: string, options?) => void;
  reloadCompanies: () => Promise<void>;
  createCompany: (data) => Promise<Company>;
}
```

- Fetches companies via `companiesApi.list()`
- Persists selected company in `localStorage` under `paperclip.selectedCompanyId`
- Auto-selects first company on load
- Handles 401 errors gracefully (returns empty array)
- `createCompany` mutation auto-selects the new company

### LiveUpdatesProvider (`context/LiveUpdatesProvider.tsx`)

WebSocket-based real-time event system. See [Section 8](#8-real-time-system).

### PanelContext (`context/PanelContext.tsx`)

```typescript
interface PanelContextValue {
  panelContent: ReactNode | null;
  panelVisible: boolean;
  openPanel: (content: ReactNode) => void;
  closePanel: () => void;
  setPanelVisible: (visible: boolean) => void;
  togglePanelVisible: () => void;
}
```

- Visibility persisted in `localStorage` under `paperclip:panel-visible`
- Default: visible

### SidebarContext (`context/SidebarContext.tsx`)

```typescript
interface SidebarContextValue {
  sidebarOpen: boolean;
  setSidebarOpen: (open: boolean) => void;
  toggleSidebar: () => void;
  isMobile: boolean;
}
```

- Mobile breakpoint: 768px
- Uses `matchMedia` for responsive detection
- Sidebar auto-closes when switching to mobile

### DialogContext (`context/DialogContext.tsx`)

Global dialog state for creation dialogs:

```typescript
interface DialogContextValue {
  newIssueOpen: boolean;
  newIssueDefaults: { status?, priority?, projectId?, assigneeAgentId?, assigneeUserId?, title?, description? };
  openNewIssue: (defaults?) => void;
  closeNewIssue: () => void;
  newProjectOpen: boolean;
  openNewProject: () => void;
  closeNewProject: () => void;
  newGoalOpen: boolean;
  newGoalDefaults: { parentId? };
  openNewGoal: (defaults?) => void;
  closeNewGoal: () => void;
  newAgentOpen: boolean;
  openNewAgent: () => void;
  closeNewAgent: () => void;
  onboardingOpen: boolean;
  onboardingOptions: { initialStep?: 1|2|3|4, companyId? };
  openOnboarding: (options?) => void;
  closeOnboarding: () => void;
}
```

### BreadcrumbContext (`context/BreadcrumbContext.tsx`)

```typescript
interface Breadcrumb { label: string; href?: string; }
interface BreadcrumbContextValue {
  breadcrumbs: Breadcrumb[];
  setBreadcrumbs: (crumbs: Breadcrumb[]) => void;
}
```

- Auto-updates `document.title` from breadcrumbs: `"Label1 . Label2 . Paperclip"`

### ToastContext (`context/ToastContext.tsx`)

```typescript
type ToastTone = "info" | "success" | "warn" | "error";
interface ToastInput {
  id?: string;
  dedupeKey?: string;
  title: string;
  body?: string;
  tone?: ToastTone;
  ttlMs?: number;
  action?: { label: string; href: string };
}
```

- Max 5 visible toasts
- Default TTL by tone: info=4s, success=3.5s, warn=8s, error=10s
- Min TTL: 1.5s, Max TTL: 15s
- Deduplication window: 3.5s, max age: 20s
- Auto-dismiss via `setTimeout`

### ThemeContext (`context/ThemeContext.tsx`)

```typescript
type Theme = "light" | "dark";
interface ThemeContextValue {
  theme: Theme;
  setTheme: (theme: Theme) => void;
  toggleTheme: () => void;
}
```

- Persisted in `localStorage` under `paperclip.theme`
- Applies `.dark` class on `document.documentElement`
- Updates `color-scheme` CSS property
- Updates `<meta name="theme-color">` (dark: `#18181b`, light: `#ffffff`)

---

## 7. API Layer

### HTTP Client (`api/client.ts`)

```typescript
const BASE = "/api";

class ApiError extends Error {
  status: number;
  body: unknown;
}

const api = {
  get: <T>(path) => request<T>(path),
  post: <T>(path, body) => request<T>(path, { method: "POST", body: JSON.stringify(body) }),
  postForm: <T>(path, body: FormData) => request<T>(path, { method: "POST", body }),
  put: <T>(path, body) => request<T>(path, { method: "PUT", body: JSON.stringify(body) }),
  patch: <T>(path, body) => request<T>(path, { method: "PATCH", body: JSON.stringify(body) }),
  delete: <T>(path) => request<T>(path, { method: "DELETE" }),
};
```

- All requests include `credentials: "include"` (cookie-based auth)
- Content-Type defaults to `application/json` (skipped for `FormData`)
- 204 responses return `undefined`
- Error responses parsed as JSON, thrown as `ApiError`

### All API Modules

| Module | File | Key Endpoints |
|--------|------|---------------|
| **auth** | `api/auth.ts` | `getSession`, `signInEmail`, `signUpEmail`, `signOut` |
| **health** | `api/health.ts` | `get` -- deployment mode, version, features, dev server status |
| **access** | `api/access.ts` | `createCompanyInvite`, `getInvite`, `acceptInvite`, `listJoinRequests`, `approveJoinRequest`, `rejectJoinRequest`, `getBoardClaimStatus`, `claimBoard`, `getCliAuthChallenge`, `approveCliAuthChallenge`, `cancelCliAuthChallenge`, `createOpenClawInvitePrompt` |
| **companies** | `api/companies.ts` | `list`, `get`, `stats`, `create`, `update`, `updateBranding`, `archive`, `remove`, `exportBundle`, `exportPreview`, `exportPackage`, `importPreview`, `importBundle` |
| **agents** | `api/agents.ts` | `list`, `org`, `get`, `create`, `hire`, `update`, `updatePermissions`, `instructionsBundle`, `updateInstructionsBundle`, `instructionsFile`, `saveInstructionsFile`, `deleteInstructionsFile`, `pause`, `resume`, `terminate`, `remove`, `listKeys`, `createKey`, `revokeKey`, `skills`, `syncSkills`, `runtimeState`, `taskSessions`, `resetSession`, `adapterModels`, `testEnvironment`, `invoke`, `wakeup`, `loginWithClaude`, `availableSkills`, `getConfiguration`, `listConfigRevisions`, `getConfigRevision`, `rollbackConfigRevision` |
| **projects** | `api/projects.ts` | `list`, `get`, `create`, `update`, `listWorkspaces`, `createWorkspace`, `updateWorkspace`, `removeWorkspace`, `remove` |
| **issues** | `api/issues.ts` | `list` (with 12+ filters: status, projectId, assigneeAgentId, participantAgentId, assigneeUserId, touchedByUserId, inboxArchivedByUserId, unreadForUserId, labelId, originKind, originId, q), `listLabels`, `createLabel`, `deleteLabel`, `get`, `markRead`, `archiveFromInbox`, `unarchiveFromInbox`, `create`, `update`, `remove`, `checkout`, `release`, `listComments`, `addComment` (with reopen/interrupt flags), `listDocuments`, `getDocument`, `upsertDocument`, `listDocumentRevisions`, `deleteDocument`, `listAttachments`, `uploadAttachment`, `deleteAttachment`, `listApprovals`, `linkApproval`, `unlinkApproval`, `listWorkProducts`, `createWorkProduct`, `updateWorkProduct`, `deleteWorkProduct` |
| **routines** | `api/routines.ts` | `list`, `create`, `get`, `update`, `listRuns`, `createTrigger`, `updateTrigger`, `deleteTrigger`, `rotateTriggerSecret`, `run`, `activity` |
| **goals** | `api/goals.ts` | `list`, `get`, `create`, `update`, `remove` |
| **approvals** | `api/approvals.ts` | `list`, `create`, `get`, `approve`, `reject`, `requestRevision`, `resubmit`, `listComments`, `addComment`, `listIssues` |
| **costs** | `api/costs.ts` | `summary`, `byAgent`, `byAgentModel`, `byProject`, `byProvider`, `byBiller`, `financeSummary`, `financeByBiller`, `financeByKind`, `financeEvents`, `windowSpend`, `quotaWindows` |
| **budgets** | `api/budgets.ts` | `overview`, `upsertPolicy`, `resolveIncident` |
| **activity** | `api/activity.ts` | `list` (with entityType, entityId, agentId filters), `forIssue`, `runsForIssue`, `issuesForRun` |
| **dashboard** | `api/dashboard.ts` | `summary` |
| **heartbeats** | `api/heartbeats.ts` | `list`, `get`, `events`, `log`, `workspaceOperations`, `workspaceOperationLog`, `cancel`, `liveRunsForIssue`, `activeRunForIssue`, `liveRunsForCompany`, `listInstanceSchedulerAgents` |
| **secrets** | `api/secrets.ts` | `list`, `providers`, `create`, `rotate`, `update`, `remove` |
| **instanceSettings** | `api/instanceSettings.ts` | `getGeneral`, `updateGeneral`, `getExperimental`, `updateExperimental` |
| **executionWorkspaces** | `api/execution-workspaces.ts` | `list` (with projectId, projectWorkspaceId, issueId, status, reuseEligible filters), `get`, `update` |
| **sidebarBadges** | `api/sidebarBadges.ts` | `get` -- unread inbox count per company |
| **companySkills** | `api/companySkills.ts` | Company skill management |
| **assets** | `api/assets.ts` | Asset/file management |
| **plugins** | `api/plugins.ts` | `list`, `listExamples`, `get`, `install`, `uninstall`, `enable`, `disable`, `health`, `dashboard`, `logs`, `upgrade`, `listUiContributions`, `getConfig`, `saveConfig`, `testConfig`, `bridgeGetData`, `bridgePerformAction` |

### Query Key System (`lib/queryKeys.ts`)

Comprehensive hierarchical query key structure for cache management:

```typescript
queryKeys.companies.all             // ["companies"]
queryKeys.agents.list(companyId)    // ["agents", companyId]
queryKeys.agents.detail(id)         // ["agents", "detail", id]
queryKeys.issues.list(companyId)    // ["issues", companyId]
queryKeys.issues.detail(id)         // ["issues", "detail", id]
queryKeys.issues.comments(id)       // ["issues", "comments", id]
queryKeys.issues.documents(id)      // ["issues", "documents", id]
queryKeys.issues.runs(id)           // ["issues", "runs", id]
queryKeys.issues.liveRuns(id)       // ["issues", "live-runs", id]
// ... ~60 total key factories
```

---

## 8. Real-Time System

### WebSocket Live Updates (`context/LiveUpdatesProvider.tsx`)

**Connection:**
- URL: `ws[s]://{host}/api/companies/{companyId}/events/ws`
- Auto-reconnects with exponential backoff: `min(15s, 1s * 2^(attempt-1))`, capped at 4 doublings
- Reconnect suppresses toasts for 2s after reconnection

**Event Types Handled:**
| Event Type | Actions |
|-----------|---------|
| `heartbeat.run.status` | Invalidates heartbeat + agent + dashboard + cost + sidebar badge queries; shows run status toast |
| `heartbeat.run.queued` | Invalidates heartbeat queries |
| `heartbeat.run.log` | Ignored (log streaming handled separately) |
| `heartbeat.run.event` | Ignored |
| `agent.status` | Invalidates agent + dashboard + org queries; shows agent status toast |
| `activity.logged` | Invalidates activity + dashboard + entity-specific queries; shows toast for issue/agent/join events |

**Toast Intelligence:**
- Self-activity suppressed (won't toast your own actions)
- Visible issue suppression: if you're viewing the issue detail page, toasts for that issue are suppressed
- Cooldown rate limiting: max 3 toasts per category per 10s window
- Deduplication via `dedupeKey` per toast

**Query Invalidation on Activity Events:**
- Issues: list, detail, comments, activity, runs, documents, attachments, approvals, live-runs, active-run
- Agents: list, detail, heartbeats
- Projects: list, detail
- Goals: list, detail
- Approvals: list
- Join requests: list
- Cost events: costs, usage-by-provider, window-spend
- Routines: all routine queries
- Companies: company list

### Live Run Transcripts (`components/transcript/useLiveRunTranscripts.ts`)

Polls agent run logs every 2s (configurable `LOG_POLL_INTERVAL_MS`):
- Fetches log chunks from `/heartbeat-runs/{runId}/log?offset={offset}&limitBytes=256000`
- Parses NDJSON log records: `{ ts, stream: "stdout"|"stderr"|"system", chunk }`
- Builds transcript entries via adapter-specific parsers
- Handles partial line buffering across chunks
- Stops polling when run reaches terminal status

---

## 9. Design System

### Color System (`index.css`)

Uses OKLCH color space for all design tokens. Two themes: light and dark.

**Semantic tokens (CSS custom properties):**
| Token | Light | Dark |
|-------|-------|------|
| `--background` | `oklch(1 0 0)` (white) | `oklch(0.145 0 0)` (near-black) |
| `--foreground` | `oklch(0.145 0 0)` | `oklch(0.985 0 0)` |
| `--card` | `oklch(1 0 0)` | `oklch(0.205 0 0)` |
| `--primary` | `oklch(0.205 0 0)` | `oklch(0.985 0 0)` |
| `--secondary` | `oklch(0.97 0 0)` | `oklch(0.269 0 0)` |
| `--muted` | `oklch(0.97 0 0)` | `oklch(0.269 0 0)` |
| `--muted-foreground` | `oklch(0.556 0 0)` | `oklch(0.708 0 0)` |
| `--accent` | `oklch(0.97 0 0)` | `oklch(0.269 0 0)` |
| `--destructive` | `oklch(0.577 0.245 27.325)` | `oklch(0.637 0.237 25.331)` |
| `--border` | `oklch(0.922 0 0)` | `oklch(0.269 0 0)` |
| `--ring` | `oklch(0.708 0 0)` | `oklch(0.439 0 0)` |
| `--chart-1..5` | 5 chart colors | 5 chart colors |
| `--sidebar-*` | Sidebar-specific tokens | Sidebar-specific tokens |

**Radius system:** All radii set to 0 (`--radius: 0`, `--radius-lg: 0px`, `--radius-xl: 0px`) with small inner radii (`--radius-sm: 0.375rem`, `--radius-md: 0.5rem`). This gives a deliberately sharp/industrial aesthetic.

**Global base styles:**
- Antialiased text
- Touch manipulation on interactive elements
- Min 44px height for touch targets on coarse pointer devices
- Balanced text wrap on headings

### UI Primitives (`components/ui/`)

21 primitive components, all based on Radix UI + CVA:

| Component | File | Variants/Notes |
|-----------|------|---------------|
| **Button** | `button.tsx` | Variants: `default`, `destructive`, `outline`, `secondary`, `ghost`, `link`. Sizes: `default`, `xs`, `sm`, `lg`, `icon`, `icon-xs`, `icon-sm`, `icon-lg`. Supports `asChild` via Radix Slot. |
| **Badge** | `badge.tsx` | Variants: `default`, `secondary`, `destructive`, `outline`, `ghost`, `link`. Pill-shaped (`rounded-full`). |
| **Input** | `input.tsx` | Standard text input with focus ring |
| **Textarea** | `textarea.tsx` | Multi-line input |
| **Label** | `label.tsx` | Form label |
| **Checkbox** | `checkbox.tsx` | Radix checkbox |
| **Select** | `select.tsx` | Radix select with trigger, content, items |
| **Tabs** | `tabs.tsx` | Radix tabs (list, trigger, content) |
| **Dialog** | `dialog.tsx` | Radix dialog with overlay, close button |
| **Sheet** | `sheet.tsx` | Slide-out panel (side: top, bottom, left, right) |
| **Popover** | `popover.tsx` | Radix popover |
| **Tooltip** | `tooltip.tsx` | Radix tooltip (requires `TooltipProvider`) |
| **DropdownMenu** | `dropdown-menu.tsx` | Radix dropdown menu |
| **Command** | `command.tsx` | cmdk-based command palette (input, list, group, item, separator) |
| **ScrollArea** | `scroll-area.tsx` | Radix scroll area |
| **Avatar** | `avatar.tsx` | Radix avatar with image and fallback |
| **Breadcrumb** | `breadcrumb.tsx` | Breadcrumb navigation |
| **Separator** | `separator.tsx` | Horizontal/vertical separator |
| **Card** | `card.tsx` | Card container |
| **Skeleton** | `skeleton.tsx` | Loading placeholder (`bg-accent/75 rounded-md`) |
| **Collapsible** | `collapsible.tsx` | Radix collapsible sections |

---

## 10. All Components

### Navigation Components

| Component | File | Description |
|-----------|------|-------------|
| `Layout` | `Layout.tsx` | Full application shell |
| `Sidebar` | `Sidebar.tsx` | Main navigation sidebar |
| `InstanceSidebar` | `InstanceSidebar.tsx` | Instance settings sidebar |
| `CompanyRail` | `CompanyRail.tsx` | Vertical company icon strip with drag-and-drop |
| `SidebarSection` | `SidebarSection.tsx` | Collapsible sidebar section with label |
| `SidebarNavItem` | `SidebarNavItem.tsx` | Sidebar navigation link with icon, badge, live count |
| `SidebarProjects` | `SidebarProjects.tsx` | Draggable project list in sidebar |
| `SidebarAgents` | `SidebarAgents.tsx` | Draggable agent list in sidebar |
| `MobileBottomNav` | `MobileBottomNav.tsx` | Mobile bottom tab bar (5 items) |
| `BreadcrumbBar` | `BreadcrumbBar.tsx` | Top breadcrumb navigation bar |
| `PageTabBar` | `PageTabBar.tsx` | Horizontal tab bar for page sub-navigation |
| `CommandPalette` | `CommandPalette.tsx` | Cmd+K search dialog (issues, agents, projects, pages, actions) |

### Entity Display Components

| Component | File | Description |
|-----------|------|-------------|
| `StatusIcon` | `StatusIcon.tsx` | Issue status indicator icon |
| `StatusBadge` | `StatusBadge.tsx` | Status badge with color coding |
| `PriorityIcon` | `PriorityIcon.tsx` | Issue priority icon |
| `Identity` | `Identity.tsx` | Agent/user identity display (avatar + name) |
| `EntityRow` | `EntityRow.tsx` | Generic entity list row |
| `IssueRow` | `IssueRow.tsx` | Issue list item with status, priority, assignee, time |
| `ActivityRow` | `ActivityRow.tsx` | Activity event row with actor, action, entity |
| `MetricCard` | `MetricCard.tsx` | Dashboard metric card with icon, value, label, description |
| `EmptyState` | `EmptyState.tsx` | Empty state placeholder with icon, message, optional action |
| `PageSkeleton` | `PageSkeleton.tsx` | Loading skeleton with page-specific variants |
| `CopyText` | `CopyText.tsx` | Copyable text with click-to-copy |
| `QuotaBar` | `QuotaBar.tsx` | Progress bar for quota visualization |

### Issue Components

| Component | File | Description |
|-----------|------|-------------|
| `IssuesList` | `IssuesList.tsx` | Full issue list with filtering, sorting, grouping, search, list/board view toggle |
| `KanbanBoard` | `KanbanBoard.tsx` | Drag-and-drop Kanban board (7 status columns) |
| `IssueProperties` | `IssueProperties.tsx` | Issue detail properties panel |
| `IssueDocumentsSection` | `IssueDocumentsSection.tsx` | Issue attached documents |
| `IssueWorkspaceCard` | `IssueWorkspaceCard.tsx` | Execution workspace card for issues |
| `NewIssueDialog` | `NewIssueDialog.tsx` | Global new issue creation dialog |
| `CommentThread` | `CommentThread.tsx` | Issue comment thread with markdown |
| `InlineEditor` | `InlineEditor.tsx` | Click-to-edit inline text/textarea |
| `LiveRunWidget` | `LiveRunWidget.tsx` | Live agent run status widget |
| `SwipeToArchive` | `SwipeToArchive.tsx` | Swipe gesture for inbox archiving |

### Agent Components

| Component | File | Description |
|-----------|------|-------------|
| `AgentConfigForm` | `AgentConfigForm.tsx` | Agent configuration editor with adapter fields |
| `AgentProperties` | `AgentProperties.tsx` | Agent detail properties |
| `AgentActionButtons` | `AgentActionButtons.tsx` | Run/Pause/Resume buttons |
| `AgentIconPicker` | `AgentIconPicker.tsx` | Agent icon selector/editor |
| `ActiveAgentsPanel` | `ActiveAgentsPanel.tsx` | Dashboard panel showing running agents |
| `NewAgentDialog` | `NewAgentDialog.tsx` | Global new agent creation dialog |
| `ReportsToPicker` | `ReportsToPicker.tsx` | Agent hierarchy "reports to" selector |
| `agent-config-defaults.ts` | N/A | Default configuration values per adapter type |
| `agent-config-primitives.tsx` | N/A | Shared config field labels, help text, adapter metadata |

### Transcript Components

| Component | File | Description |
|-----------|------|-------------|
| `RunTranscriptView` | `transcript/RunTranscriptView.tsx` | Streaming transcript viewer with blocks: message, thinking, tool, activity, command_group, stdout, event |
| `useLiveRunTranscripts` | `transcript/useLiveRunTranscripts.ts` | Hook for polling and parsing live run logs |

### Approval Components

| Component | File | Description |
|-----------|------|-------------|
| `ApprovalCard` | `ApprovalCard.tsx` | Approval display with actions |
| `ApprovalPayload` | `ApprovalPayload.tsx` | Type-specific approval payload renderer |

### Cost/Budget Components

| Component | File | Description |
|-----------|------|-------------|
| `BudgetPolicyCard` | `BudgetPolicyCard.tsx` | Budget policy editor card |
| `BudgetIncidentCard` | `BudgetIncidentCard.tsx` | Budget incident display |
| `BudgetSidebarMarker` | `BudgetSidebarMarker.tsx` | Sidebar budget status indicator |
| `BillerSpendCard` | `BillerSpendCard.tsx` | Biller spend breakdown card |
| `FinanceBillerCard` | `FinanceBillerCard.tsx` | Finance by biller card |
| `FinanceKindCard` | `FinanceKindCard.tsx` | Finance by kind card |
| `FinanceTimelineCard` | `FinanceTimelineCard.tsx` | Finance event timeline |
| `ProviderQuotaCard` | `ProviderQuotaCard.tsx` | Provider quota display |
| `AccountingModelCard` | `AccountingModelCard.tsx` | Accounting model display |

### Chart Components

| Component | File | Description |
|-----------|------|-------------|
| `ActivityCharts` | `ActivityCharts.tsx` | Container for dashboard charts: `ChartCard`, `RunActivityChart`, `PriorityChart`, `IssueStatusChart`, `SuccessRateChart` |

### Project/Goal Components

| Component | File | Description |
|-----------|------|-------------|
| `ProjectProperties` | `ProjectProperties.tsx` | Project detail properties |
| `NewProjectDialog` | `NewProjectDialog.tsx` | Project creation dialog |
| `GoalProperties` | `GoalProperties.tsx` | Goal detail properties |
| `GoalTree` | `GoalTree.tsx` | Hierarchical goal tree view |
| `NewGoalDialog` | `NewGoalDialog.tsx` | Goal creation dialog |

### Content Components

| Component | File | Description |
|-----------|------|-------------|
| `MarkdownBody` | `MarkdownBody.tsx` | Markdown renderer |
| `MarkdownEditor` | `MarkdownEditor.tsx` | Rich markdown editor (uses `@mdxeditor/editor`) |
| `JsonSchemaForm` | `JsonSchemaForm.tsx` | Dynamic form from JSON Schema |
| `PackageFileTree` | `PackageFileTree.tsx` | File tree viewer for packages |
| `PathInstructionsModal` | `PathInstructionsModal.tsx` | Instructions file path modal |

### Other Components

| Component | File | Description |
|-----------|------|-------------|
| `CompanySwitcher` | `CompanySwitcher.tsx` | Company selection dropdown |
| `CompanyPatternIcon` | `CompanyPatternIcon.tsx` | Company avatar with pattern/logo/color |
| `ClaudeSubscriptionPanel` | `ClaudeSubscriptionPanel.tsx` | Claude subscription management |
| `CodexSubscriptionPanel` | `CodexSubscriptionPanel.tsx` | Codex subscription management |
| `FilterBar` | `FilterBar.tsx` | Active filter badges with remove/clear |
| `InlineEntitySelector` | `InlineEntitySelector.tsx` | Inline entity picker |
| `OnboardingWizard` | `OnboardingWizard.tsx` | 4-step wizard: Company, Agent, Project/Issue, Launch |
| `PropertiesPanel` | `PropertiesPanel.tsx` | Right-side properties panel shell |
| `ScheduleEditor` | `ScheduleEditor.tsx` | Cron schedule editor for routines |
| `ScrollToBottom` | `ScrollToBottom.tsx` | Auto-scroll-to-bottom for streaming content |
| `ToastViewport` | `ToastViewport.tsx` | Toast notification render area |
| `WorktreeBanner` | `WorktreeBanner.tsx` | Git worktree indicator banner |
| `DevRestartBanner` | `DevRestartBanner.tsx` | Dev server restart notification |
| `AsciiArtAnimation` | `AsciiArtAnimation.tsx` | Animated ASCII art (used in onboarding) |
| `OpenCodeLogoIcon` | `OpenCodeLogoIcon.tsx` | OpenCode logo SVG |

---

## 11. Adapter System (Agent Runtimes)

**Directory:** `src/adapters/`

Adapters bridge between the frontend and different agent runtime backends. Each adapter provides:
- A stdout line parser for transcript rendering
- Configuration UI fields for agent setup
- Config builder for API payloads

### Adapter Types

| Adapter | Key | Description |
|---------|-----|-------------|
| Claude Local | `claude-local` | Claude Code CLI running locally |
| Codex Local | `codex-local` | OpenAI Codex CLI running locally |
| Gemini Local | `gemini-local` | Google Gemini CLI running locally |
| OpenCode Local | `opencode-local` | OpenCode CLI running locally |
| Pi Local | `pi-local` | Pi agent running locally |
| Cursor | `cursor` | Cursor editor agent |
| OpenClaw Gateway | `openclaw-gateway` | OpenClaw protocol gateway |
| Process | `process` | Generic subprocess agent |
| HTTP | `http` | HTTP-based agent endpoint |

### Architecture

```typescript
interface UIAdapterModule {
  type: string;                      // Adapter key
  label: string;                     // Display name
  parseStdoutLine: (line, ts) => TranscriptEntry[];  // Log parser
  ConfigFields: ComponentType<AdapterConfigFieldsProps>;  // Config form
  buildAdapterConfig: (values) => Record<string, unknown>;  // Config builder
}
```

**Registry** (`adapters/registry.ts`): Maps adapter type strings to `UIAdapterModule` instances. Unknown types fall back to the `process` adapter.

**Transcript Builder** (`adapters/transcript.ts`): Converts raw log chunks into structured `TranscriptEntry` arrays via adapter-specific parsers.

**Transcript Entry Types:**
```typescript
type TranscriptEntry = {
  ts: string;
  type: "assistant" | "user" | "tool_use" | "tool_result" | "thinking" | "event" | "stdout" | "activity";
  // ... type-specific fields
};
```

### Shared Config Fields

- `local-workspace-runtime-fields.tsx` -- Common fields for local workspace adapters
- `runtime-json-fields.tsx` -- JSON runtime configuration fields
- Process adapters share `build-config.ts` and `parse-stdout.ts` utilities

---

## 12. Plugin System

**Directory:** `src/plugins/`

Paperclip has a full plugin UI extension system with dynamic loading, bridge communication, and error isolation.

### Architecture Overview

```
Plugin Server (Worker Process)
     |
     | HTTP REST (getData, performAction)
     | SSE (stream)
     |
Plugin UI Module (ESM bundle)
     |
     | Dynamic import() with bare-specifier rewriting
     |
Host React Tree
     |
     +-- PluginBridgeScope (context: pluginId + hostContext)
         +-- PluginSlotErrorBoundary (per-plugin error isolation)
             +-- Plugin React Component
```

### Bridge Initialization (`plugins/bridge-init.ts`)

Called once at startup:
```typescript
initPluginBridge(React, ReactDOM);
// Sets globalThis.__paperclipPluginBridge__ = { react, reactDom, sdkUi: { hooks } }
```

### Bridge Runtime (`plugins/bridge.ts`)

Provides concrete implementations of SDK hooks:

| Hook | Purpose | Transport |
|------|---------|-----------|
| `usePluginData(key, params)` | Fetch data from plugin worker | `POST /api/plugins/:id/data/:key` |
| `usePluginAction(key)` | Execute action on plugin worker | `POST /api/plugins/:id/actions/:key` |
| `useHostContext()` | Read host context (company, entity, user) | React context |
| `usePluginStream(channel)` | SSE streaming from plugin | `EventSource /api/plugins/:id/bridge/stream/:channel` |
| `usePluginToast()` | Show host toast notifications | React context |

**Error handling:** `PluginBridgeError` with codes: `WORKER_UNAVAILABLE`, `TIMEOUT`, `UNKNOWN`. Auto-retry (2x) for `WORKER_UNAVAILABLE` and `TIMEOUT`.

### Slot System (`plugins/slots.tsx`)

**Registration flow:**
1. Host fetches `GET /api/plugins/ui-contributions` to discover plugin slots/launchers
2. For each contribution, dynamically imports the plugin's UI entry module via `/_plugins/:pluginId/ui/:entryFile`
3. Bare specifier imports (`react`, `react-dom`, `@paperclipai/plugin-sdk/ui`) are rewritten to blob URLs pointing to the host bridge registry
4. Named exports matching manifest `exportName` declarations are registered as React components or web component tag names

**Slot Types:**
| Type | Description | Entity-scoped |
|------|-------------|--------------|
| `sidebar` | Items in main sidebar nav | No |
| `sidebarPanel` | Panels in sidebar below nav | No |
| `dashboardWidget` | Widgets on dashboard page | No |
| `detailTab` | Tabs on entity detail pages | Yes |
| `taskDetailView` | Views within task/issue detail | Yes |
| `contextMenuItem` | Context menu items on entities | Yes |
| `commentAnnotation` | Annotations on comments | Yes |
| `commentContextMenuItem` | Context menu on comments | Yes |
| `projectSidebarItem` | Items in project sidebar | Yes |
| `toolbarButton` | Toolbar buttons | Yes |

**Components:**
- `usePluginSlots(filters)` -- Hook to discover/filter plugin slots
- `PluginSlotOutlet` -- Renders all matching slots with error boundary isolation
- `PluginSlotMount` -- Mounts a single slot (React or web component)
- `PluginBridgeScope` -- Wraps plugin components with bridge context
- `PluginSlotErrorBoundary` -- Isolates plugin render failures

### Launcher System (`plugins/launchers.tsx`)

Launchers are plugin-provided buttons/entries that open modals, drawers, or popovers:

| Placement Zone | Description |
|---------------|-------------|
| `toolbarButton` | Toolbar action buttons |
| Others | Entity-scoped buttons |

**Action Types:** `openModal`, `openDrawer`, `openPopover`, `navigate`

**Provider:** `PluginLauncherProvider` in the context tree manages active launcher rendering, bounds negotiation, and close lifecycle (`onBeforeClose`, `onClose`).

**Component:** `PluginLauncherOutlet` renders launcher buttons for specific placement zones and entity contexts.

---

## 13. Key Interactions

### Drag-and-Drop

Two DnD implementations, both using `@dnd-kit`:

**1. Kanban Board (`KanbanBoard.tsx`)**
- 7 status columns: backlog, todo, in_progress, in_review, blocked, done, cancelled
- Cards are `SortableContext` items within droppable columns
- `DragOverlay` shows ghost card while dragging
- On drop: determines target status from column or target card, calls `onUpdateIssue(id, { status })`
- 5px activation distance to prevent accidental drags on click
- Click-through to issue detail still works (prevented during drag)

**2. Company Rail (`CompanyRail.tsx`)**
- Vertical sortable list of company icons
- `PointerSensor` with 8px activation distance
- Order persisted in `localStorage`, synced across tabs via `StorageEvent`
- Visual feedback: opacity 0.8 during drag, `scale-105`, shadow

### Command Palette (`CommandPalette.tsx`)

- Activated by `Cmd+K` / `Ctrl+K`
- Powered by cmdk (`CommandDialog`)
- Sections: Actions (New Issue, New Agent, New Project), Pages (8 navigation targets), Issues (top 10, with search), Agents (top 10), Projects (top 10)
- Live search queries issues API with `?q=` parameter
- Auto-closes on selection, clears query on close
- On mobile: auto-closes sidebar when opened

### Keyboard Shortcuts (`useKeyboardShortcuts`)

| Key | Action | Context |
|-----|--------|---------|
| `Cmd/Ctrl+K` | Open command palette | Global |
| `C` | New issue | Not in input/textarea |
| `[` | Toggle sidebar | Not in input/textarea |
| `]` | Toggle properties panel | Not in input/textarea |

### Inline Editing (`InlineEditor.tsx`)

- Click-to-edit pattern for issue titles and other text
- Switches between display and edit mode
- Auto-saves on blur or Enter

### Approval Flow

Approval lifecycle:
1. Agent requests approval (`approval.created`)
2. Board member reviews (`ApprovalCard` with payload display)
3. Actions: Approve, Reject, Request Revision
4. Agent can Resubmit after revision request
5. Comments on approvals
6. Linked to issues

### Swipe-to-Archive (`SwipeToArchive.tsx`)

Mobile gesture for inbox items -- horizontal swipe triggers archive action.

### Company Switching

- Click company icon in rail -> selects company, navigates to company dashboard
- Route-based sync: URL prefix sets selection source to `"route_sync"`
- Manual selection: click sets source to `"manual"`

---

## 14. Agent System

### Agent Lifecycle States

Agents have these statuses: `active`, `running`, `paused`, `error`, `terminated`

### Agent Adapter Types

9 adapter types (see [Section 11](#11-adapter-system-agent-runtimes)):
- `claude-local`, `codex-local`, `gemini-local`, `opencode-local`, `pi-local`, `cursor`, `openclaw-gateway`, `process`, `http`

### Agent Configuration

`AgentConfigForm` renders adapter-specific fields:
- Adapter type selection
- Model selection (fetched from `/companies/:id/adapters/:type/models`)
- Runtime environment fields
- Instructions mode: managed (in-app editor) vs external (filesystem)
- Instructions file tree with markdown editing
- Environment testing (`testEnvironment` API)

### Agent Actions

- **Run/Invoke**: Triggers a heartbeat run via `agentsApi.invoke()`
- **Wakeup**: More flexible invocation with source, trigger detail, payload, idempotency key
- **Pause/Resume**: Toggles agent activity
- **Terminate**: Permanently stops agent
- **Remove**: Deletes agent

### Run Transcript

The `RunTranscriptView` renders structured blocks:

| Block Type | Display |
|-----------|---------|
| `message` | Assistant/user message with markdown |
| `thinking` | Collapsible thinking block |
| `tool` | Tool call with name, input (collapsible), result, error state |
| `command_group` | Grouped sequential commands |
| `activity` | Activity events (running/completed) |
| `stdout` | Raw stdout output (collapsible) |
| `event` | System events with tone (info/warn/error/neutral) |

**Modes:** `"nice"` (formatted) and `"raw"` (unformatted)
**Densities:** `"comfortable"` and `"compact"`

### Agent Skills

- `agentsApi.skills(id)` -- Get current skill snapshot
- `agentsApi.syncSkills(id, desiredSkills)` -- Sync desired skills
- `agentsApi.availableSkills()` -- List all available skills
- UI: checkboxes for enabling/disabling skills, read-only display for unmanaged skills

### Agent Keys

API key management per agent:
- Create keys with names
- Copy key on creation (shown once)
- Revoke keys

### Agent Permissions

```typescript
interface AgentPermissionUpdate {
  canCreateAgents: boolean;
  canAssignTasks: boolean;
}
```

### Config Revisions

Full revision history for agent configurations with rollback capability:
- `listConfigRevisions` -- History list
- `getConfigRevision` -- Specific revision detail
- `rollbackConfigRevision` -- Restore previous config

---

## 15. Issue System

### Issue Lifecycle

Status flow: `backlog` -> `todo` -> `in_progress` -> `in_review` -> `done`/`cancelled`/`blocked`

Priorities: `critical`, `high`, `medium`, `low`

### Issue List (`IssuesList.tsx`)

Comprehensive list component with:

**View State** (persisted in `localStorage`):
```typescript
type IssueViewState = {
  statuses: string[];       // Status filters
  priorities: string[];     // Priority filters
  assignees: string[];      // Assignee filters
  labels: string[];         // Label filters
  projects: string[];       // Project filters
  sortField: "status" | "priority" | "title" | "created" | "updated";
  sortDir: "asc" | "desc";
  groupBy: "status" | "priority" | "assignee" | "none";
  viewMode: "list" | "board";
  collapsedGroups: string[];
};
```

**Quick filter presets:** All, Active, Backlog, Done

**Features:**
- Full-text search with 150ms debounce, URL sync via `?q=`
- Multi-select filters for status, priority, assignee, label, project
- Sort by 5 fields with ascending/descending
- Group by status, priority, or assignee with collapsible groups
- Toggle between list view and Kanban board view
- Inline issue creation
- Live agent run indicators (pulsing blue dot on active issues)

### Issue Detail

- Inline editable title
- Status and priority with change controls
- Assignee management (agent or user)
- Project assignment
- Labels (colored pills)
- Comment thread with markdown editor, `@mentions`, file attachments
- Comment-driven status changes (reopen, interrupt)
- Documents section (markdown documents attached to issues)
- Document revision history
- File attachments (drag-and-drop upload)
- Linked approvals
- Work products
- Execution workspace card
- Live run widget
- Activity timeline
- Plugin slots for custom tabs, views, and context menus

### Kanban Board (`KanbanBoard.tsx`)

7 columns matching status values. Cards show:
- Issue identifier (mono font)
- Live indicator (blue pulse)
- Title (2-line clamp)
- Priority icon
- Assignee identity

Drag a card to another column to change status.

---

## 16. Cost and Budget System

### Cost Analytics (`pages/Costs.tsx`)

Date-range filtered cost views:
- **Summary**: Total spend, budget utilization
- **By Agent**: Per-agent cost breakdown
- **By Agent Model**: Cost by model within agents
- **By Provider**: Cost by LLM provider/model
- **By Project**: Cost by project
- **By Biller**: Cost by billing source
- **Finance Summary**: Higher-level financial view
- **Finance by Kind**: Cost categories
- **Finance Events**: Event timeline

### Budget System

**Budget Policies** (`budgetsApi.upsertPolicy`):
- Set spending limits per company, project, or agent
- Monthly budget caps in cents

**Budget Incidents** (`budgetsApi.resolveIncident`):
- Triggered when budgets are exceeded
- Pause agents/projects automatically
- Dashboard shows active incident count with red gradient banner
- Board member resolves incidents

**Provider Quotas**:
- `costsApi.quotaWindows(companyId)` -- External provider rate limits
- `QuotaBar` component for visualization
- `ProviderQuotaCard` for detailed quota display

**Window Spend**:
- `costsApi.windowSpend(companyId)` -- Rolling window spend tracking

---

## 17. Hooks

| Hook | File | Description |
|------|------|-------------|
| `useKeyboardShortcuts` | `hooks/useKeyboardShortcuts.ts` | Global keyboard shortcut handler (C, [, ]) |
| `useCompanyPageMemory` | `hooks/useCompanyPageMemory.ts` | Remembers last visited page per company |
| `useAgentOrder` | `hooks/useAgentOrder.ts` | Persistent agent ordering in sidebar |
| `useProjectOrder` | `hooks/useProjectOrder.ts` | Persistent project ordering in sidebar |
| `useInboxBadge` | `hooks/useInboxBadge.ts` | Inbox unread/failed-run badge counts |
| `useAutosaveIndicator` | `hooks/useAutosaveIndicator.ts` | Autosave status indicator |
| `useDateRange` | `hooks/useDateRange.ts` | Date range picker state |

---

## 18. Utility Libraries

| Module | File | Purpose |
|--------|------|---------|
| `utils.ts` | `lib/utils.ts` | `cn()` (classname merge), `formatCents`, `formatDate`, `relativeTime`, `formatTokens`, `visibleRunCostUsd`, `agentUrl`, `projectUrl` |
| `queryKeys.ts` | `lib/queryKeys.ts` | All TanStack Query cache keys (~60 factories) |
| `timeAgo.ts` | `lib/timeAgo.ts` | Relative time formatting |
| `groupBy.ts` | `lib/groupBy.ts` | Array grouping utility |
| `company-routes.ts` | `lib/company-routes.ts` | Company prefix URL manipulation |
| `company-selection.ts` | `lib/company-selection.ts` | Company selection sync logic |
| `company-page-memory.ts` | `lib/company-page-memory.ts` | Per-company page memory |
| `company-export-selection.ts` | `lib/company-export-selection.ts` | Export entity selection logic |
| `company-portability-sidebar.ts` | `lib/company-portability-sidebar.ts` | Import/export sidebar helpers |
| `status-colors.ts` | `lib/status-colors.ts` | Agent status dot colors |
| `color-contrast.ts` | `lib/color-contrast.ts` | `pickTextColorForPillBg()` for label contrast |
| `inbox.ts` | `lib/inbox.ts` | Inbox tab persistence |
| `assignees.ts` | `lib/assignees.ts` | Assignee formatting and selection |
| `recent-assignees.ts` | `lib/recent-assignees.ts` | Recent assignee tracking |
| `agent-icons.ts` | `lib/agent-icons.ts` | Agent icon definitions |
| `agent-order.ts` | `lib/agent-order.ts` | Agent ordering persistence |
| `agent-skills-state.ts` | `lib/agent-skills-state.ts` | Skill state management |
| `project-order.ts` | `lib/project-order.ts` | Project ordering persistence |
| `model-utils.ts` | `lib/model-utils.ts` | Model name/provider extraction |
| `mention-chips.ts` | `lib/mention-chips.ts` | `@mention` rendering |
| `mention-deletion.ts` | `lib/mention-deletion.ts` | Mention deletion handling |
| `mention-aware-link-node.ts` | `lib/mention-aware-link-node.ts` | Mention-aware markdown links |
| `onboarding-goal.ts` | `lib/onboarding-goal.ts` | Onboarding goal parsing |
| `onboarding-launch.ts` | `lib/onboarding-launch.ts` | Onboarding launch helpers |
| `onboarding-route.ts` | `lib/onboarding-route.ts` | Onboarding route redirect logic |
| `instance-settings.ts` | `lib/instance-settings.ts` | Instance settings path normalization |
| `issueDetailBreadcrumb.ts` | `lib/issueDetailBreadcrumb.ts` | Issue detail breadcrumb builder |
| `legacy-agent-config.ts` | `lib/legacy-agent-config.ts` | Legacy config migration |
| `portable-files.ts` | `lib/portable-files.ts` | Portable file format handling |
| `routine-trigger-patch.ts` | `lib/routine-trigger-patch.ts` | Routine trigger update helpers |
| `worktree-branding.ts` | `lib/worktree-branding.ts` | Git worktree branding |
| `zip.ts` | `lib/zip.ts` | ZIP file utilities |
| `router.tsx` | `lib/router.tsx` | Company-prefix-aware router wrapper |

---

## 19. Data Flow Patterns

### Query Pattern

All data fetching uses TanStack React Query with the pattern:

```typescript
const { data, isLoading, error } = useQuery({
  queryKey: queryKeys.issues.list(selectedCompanyId!),
  queryFn: () => issuesApi.list(selectedCompanyId!),
  enabled: !!selectedCompanyId,
});
```

### Mutation Pattern

All writes use `useMutation` with cache invalidation:

```typescript
const mutation = useMutation({
  mutationFn: (data) => issuesApi.update(id, data),
  onSuccess: () => {
    queryClient.invalidateQueries({ queryKey: queryKeys.issues.list(companyId) });
    queryClient.invalidateQueries({ queryKey: queryKeys.issues.detail(id) });
  },
});
```

### Real-Time Invalidation

WebSocket events trigger targeted query invalidations (not full refetches):
1. Live event arrives via WebSocket
2. `handleLiveEvent` dispatches to type-specific handlers
3. Handlers call `queryClient.invalidateQueries()` for affected query keys
4. React Query refetches affected queries automatically
5. Optional toast notification shown (with dedup and suppression logic)

### Company Scoping

Nearly all API calls and query keys are scoped by `selectedCompanyId`:
- API paths: `/companies/{companyId}/issues`, etc.
- Query keys: `["issues", companyId]`
- URL routing: `/:companyPrefix/issues`
- WebSocket: per-company connection

### Persistence Pattern

Multiple values persist in `localStorage`:
| Key | Purpose |
|-----|---------|
| `paperclip.selectedCompanyId` | Active company |
| `paperclip.companyOrder` | Company rail sort order |
| `paperclip.theme` | Light/dark theme |
| `paperclip:panel-visible` | Properties panel visibility |
| `paperclip.lastInstanceSettingsPath` | Last visited instance settings tab |
| `paperclip:issues-view` | Issue list view state |
| Various per-page keys | Sidebar collapse, sort preferences |

---

## 20. File Index

### Pages (39 files)
`src/pages/Activity.tsx`, `AgentDetail.tsx`, `Agents.tsx`, `ApprovalDetail.tsx`, `Approvals.tsx`, `Auth.tsx`, `BoardClaim.tsx`, `CliAuth.tsx`, `Companies.tsx`, `CompanyExport.tsx`, `CompanyImport.tsx`, `CompanySettings.tsx`, `CompanySkills.tsx`, `Costs.tsx`, `Dashboard.tsx`, `DesignGuide.tsx`, `ExecutionWorkspaceDetail.tsx`, `GoalDetail.tsx`, `Goals.tsx`, `Inbox.tsx`, `InstanceExperimentalSettings.tsx`, `InstanceGeneralSettings.tsx`, `InstanceSettings.tsx`, `InviteLanding.tsx`, `IssueDetail.tsx`, `Issues.tsx`, `MyIssues.tsx`, `NewAgent.tsx`, `NotFound.tsx`, `Org.tsx`, `OrgChart.tsx`, `PluginManager.tsx`, `PluginPage.tsx`, `PluginSettings.tsx`, `ProjectDetail.tsx`, `Projects.tsx`, `RoutineDetail.tsx`, `Routines.tsx`, `RunTranscriptUxLab.tsx`

### Components (~80 files)
`src/components/AccountingModelCard.tsx`, `ActiveAgentsPanel.tsx`, `ActivityCharts.tsx`, `ActivityRow.tsx`, `AgentActionButtons.tsx`, `AgentConfigForm.tsx`, `AgentIconPicker.tsx`, `AgentProperties.tsx`, `ApprovalCard.tsx`, `ApprovalPayload.tsx`, `AsciiArtAnimation.tsx`, `BillerSpendCard.tsx`, `BreadcrumbBar.tsx`, `BudgetIncidentCard.tsx`, `BudgetPolicyCard.tsx`, `BudgetSidebarMarker.tsx`, `ClaudeSubscriptionPanel.tsx`, `CodexSubscriptionPanel.tsx`, `CommandPalette.tsx`, `CommentThread.tsx`, `CompanyPatternIcon.tsx`, `CompanyRail.tsx`, `CompanySwitcher.tsx`, `CopyText.tsx`, `DevRestartBanner.tsx`, `EmptyState.tsx`, `EntityRow.tsx`, `FilterBar.tsx`, `FinanceBillerCard.tsx`, `FinanceKindCard.tsx`, `FinanceTimelineCard.tsx`, `GoalProperties.tsx`, `GoalTree.tsx`, `Identity.tsx`, `InlineEditor.tsx`, `InlineEntitySelector.tsx`, `InstanceSidebar.tsx`, `IssueDocumentsSection.tsx`, `IssueProperties.tsx`, `IssueRow.tsx`, `IssueWorkspaceCard.tsx`, `IssuesList.tsx`, `JsonSchemaForm.tsx`, `KanbanBoard.tsx`, `Layout.tsx`, `LiveRunWidget.tsx`, `MarkdownBody.tsx`, `MarkdownEditor.tsx`, `MetricCard.tsx`, `MobileBottomNav.tsx`, `NewAgentDialog.tsx`, `NewGoalDialog.tsx`, `NewIssueDialog.tsx`, `NewProjectDialog.tsx`, `OnboardingWizard.tsx`, `OpenCodeLogoIcon.tsx`, `PackageFileTree.tsx`, `PageSkeleton.tsx`, `PageTabBar.tsx`, `PathInstructionsModal.tsx`, `PriorityIcon.tsx`, `ProjectProperties.tsx`, `PropertiesPanel.tsx`, `ProviderQuotaCard.tsx`, `QuotaBar.tsx`, `ReportsToPicker.tsx`, `ScheduleEditor.tsx`, `ScrollToBottom.tsx`, `Sidebar.tsx`, `SidebarAgents.tsx`, `SidebarNavItem.tsx`, `SidebarProjects.tsx`, `SidebarSection.tsx`, `StatusBadge.tsx`, `StatusIcon.tsx`, `SwipeToArchive.tsx`, `ToastViewport.tsx`, `WorktreeBanner.tsx`, `agent-config-defaults.ts`, `agent-config-primitives.tsx`

### UI Primitives (21 files)
`src/components/ui/avatar.tsx`, `badge.tsx`, `breadcrumb.tsx`, `button.tsx`, `card.tsx`, `checkbox.tsx`, `collapsible.tsx`, `command.tsx`, `dialog.tsx`, `dropdown-menu.tsx`, `input.tsx`, `label.tsx`, `popover.tsx`, `scroll-area.tsx`, `select.tsx`, `separator.tsx`, `sheet.tsx`, `skeleton.tsx`, `tabs.tsx`, `textarea.tsx`, `tooltip.tsx`

### API Modules (24 files)
`src/api/access.ts`, `activity.ts`, `agents.ts`, `approvals.ts`, `assets.ts`, `auth.ts`, `budgets.ts`, `client.ts`, `companies.ts`, `companySkills.ts`, `costs.ts`, `dashboard.ts`, `execution-workspaces.ts`, `goals.ts`, `health.ts`, `heartbeats.ts`, `index.ts`, `instanceSettings.ts`, `issues.ts`, `plugins.ts`, `projects.ts`, `routines.ts`, `secrets.ts`, `sidebarBadges.ts`

### Context Providers (8 files)
`src/context/BreadcrumbContext.tsx`, `CompanyContext.tsx`, `DialogContext.tsx`, `LiveUpdatesProvider.tsx`, `PanelContext.tsx`, `SidebarContext.tsx`, `ThemeContext.tsx`, `ToastContext.tsx`

### Adapters (29 files across 9 adapter types)
`src/adapters/index.ts`, `registry.ts`, `transcript.ts`, `types.ts`, `runtime-json-fields.tsx`, `local-workspace-runtime-fields.tsx`, `claude-local/`, `codex-local/`, `cursor/`, `gemini-local/`, `http/`, `openclaw-gateway/`, `opencode-local/`, `pi-local/`, `process/`

### Plugins (4 files)
`src/plugins/bridge-init.ts`, `bridge.ts`, `launchers.tsx`, `slots.tsx`

### Hooks (8 files)
`src/hooks/useAgentOrder.ts`, `useAutosaveIndicator.ts`, `useCompanyPageMemory.ts`, `useDateRange.ts`, `useInboxBadge.ts`, `useKeyboardShortcuts.ts`, `useProjectOrder.ts`

### Libraries (~40 files)
`src/lib/agent-icons.ts`, `agent-order.ts`, `agent-skills-state.ts`, `assignees.ts`, `color-contrast.ts`, `company-export-selection.ts`, `company-page-memory.ts`, `company-portability-sidebar.ts`, `company-routes.ts`, `company-selection.ts`, `groupBy.ts`, `inbox.ts`, `instance-settings.ts`, `issueDetailBreadcrumb.ts`, `legacy-agent-config.ts`, `mention-aware-link-node.ts`, `mention-chips.ts`, `mention-deletion.ts`, `model-utils.ts`, `onboarding-goal.ts`, `onboarding-launch.ts`, `onboarding-route.ts`, `portable-files.ts`, `project-order.ts`, `queryKeys.ts`, `recent-assignees.ts`, `router.tsx`, `routine-trigger-patch.ts`, `status-colors.ts`, `timeAgo.ts`, `utils.ts`, `worktree-branding.ts`, `zip.ts`
