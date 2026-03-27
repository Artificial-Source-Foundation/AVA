/**
 * AVA HQ — Type definitions for the multi-agent orchestration UI.
 */

// ── Navigation ────────────────────────────────────────────────────────

export type HqPage =
  | 'dashboard'
  | 'org-chart'
  | 'director-chat'
  | 'plan-review'
  | 'epics'
  | 'epic-detail'
  | 'issues'
  | 'issue-detail'
  | 'agent-detail'

export interface HqBreadcrumb {
  page: HqPage
  label: string
  id?: string
}

// ── Epics ─────────────────────────────────────────────────────────────

export type EpicStatus = 'planning' | 'in-progress' | 'completed' | 'paused'

export interface HqEpic {
  id: string
  title: string
  description: string
  status: EpicStatus
  progress: number // 0-100
  issueIds: string[]
  planId?: string
  createdAt: number
}

// ── Issues ────────────────────────────────────────────────────────────

export type KanbanColumn = 'backlog' | 'in-progress' | 'review' | 'done'
export type IssuePriority = 'low' | 'medium' | 'high' | 'urgent'

export interface HqIssue {
  id: string
  identifier: string // e.g. "HQ-1"
  title: string
  description: string
  status: KanbanColumn
  priority: IssuePriority
  assigneeId?: string
  assigneeName?: string
  epicId: string
  phaseLabel?: string
  comments: HqComment[]
  filesChanged: HqFileChange[]
  agentProgress?: { turn: number; maxTurns: number }
  agentLiveAction?: string // e.g. "editing src/auth/oauth2.rs"
  isLive: boolean
  createdAt: number
}

export interface HqComment {
  id: string
  authorName: string
  authorRole: 'director' | 'agent' | 'user'
  authorIcon?: string
  content: string
  timestamp: number
}

export interface HqFileChange {
  path: string
  additions: number
  deletions: number
  isNew: boolean
}

// ── Plans ─────────────────────────────────────────────────────────────

export type PlanStatus = 'awaiting-approval' | 'approved' | 'rejected' | 'executing'
export type PhaseExecution = 'parallel' | 'sequential'
export type TaskComplexity = 'simple' | 'medium' | 'complex'

export interface HqPlan {
  id: string
  epicId: string
  title: string
  status: PlanStatus
  directorDescription: string
  phases: HqPhase[]
}

export interface HqPhase {
  id: string
  number: number
  name: string
  description: string
  execution: PhaseExecution
  dependsOn: string[] // phase IDs
  tasks: HqPlanTask[]
  reviewEnabled: boolean
  reviewAssignee?: string
}

export interface HqPlanTask {
  id: string
  title: string
  domain: string
  complexity: TaskComplexity
  assigneeId?: string
  assigneeName?: string
  assigneeModel?: string
  steps: string[]
  fileHints: string[]
  expanded: boolean
}

// ── Agents ─────────────────────────────────────────────────────────────

export type AgentStatus = 'active' | 'running' | 'idle' | 'paused' | 'error'
export type AgentTier = 'director' | 'lead' | 'worker' | 'scout'

export interface HqAgent {
  id: string
  name: string
  role: string
  tier: AgentTier
  model: string
  status: AgentStatus
  icon: string
  parentId?: string
  currentTask?: string
  currentIssueId?: string
  turn?: number
  maxTurns?: number
  transcript: HqTranscriptEntry[]
  assignedIssueIds: string[]
  filesTouched: string[]
  totalCostUsd: number
}

export interface HqTranscriptEntry {
  id: string
  type: 'tool-call' | 'message' | 'thinking'
  toolName?: string
  toolPath?: string
  toolStatus?: 'running' | 'done' | 'error'
  content: string
  timestamp: number
}

// ── Activity ──────────────────────────────────────────────────────────

export type ActivityType =
  | 'delegation'
  | 'completion'
  | 'review'
  | 'error'
  | 'comment'
  | 'status-change'

export interface HqActivityEvent {
  id: string
  type: ActivityType
  agentName?: string
  message: string
  color: string
  timestamp: number
}

// ── Dashboard ─────────────────────────────────────────────────────────

export interface HqDashboardMetrics {
  agentsActive: number
  agentsRunning: number
  agentsIdle: number
  epicsInProgress: number
  issuesOpen: number
  issuesInProgress: number
  issuesInReview: number
  issuesDone: number
  successRate: number
  totalCostUsd: number
  paygAgentsTracked: number
}

// -- Director Chat -----------------------------------------------------

export interface HqDelegationCard {
  agentName: string
  task: string
  status: 'running' | 'done' | 'waiting' | string
}

export interface HqDirectorMessage {
  id: string
  role: 'user' | 'director' | string
  content: string
  delegations: HqDelegationCard[]
  timestamp: number
}

// -- Settings ----------------------------------------------------------

export interface HqSettings {
  directorModel: string
  tonePreference: 'technical' | 'simple' | string
  autoReview: boolean
  showCosts: boolean
}
