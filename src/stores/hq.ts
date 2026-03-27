/**
 * AVA HQ Store — navigation state + live backend-backed HQ data.
 */

import { batch, createMemo, createSignal } from 'solid-js'
import { rustBackend } from '../services/rust-bridge'
import type {
  HqActivityEvent,
  HqAgent,
  HqBreadcrumb,
  HqDashboardMetrics,
  HqDirectorMessage,
  HqEpic,
  HqIssue,
  HqPage,
  HqPlan,
  HqSettings,
  KanbanColumn,
} from '../types/hq'
import type { AgentEvent } from '../types/rust-ipc'

const STORAGE_KEY = 'ava-hq-mode'
const ONBOARDED_KEY = 'ava-hq-onboarded'

const EMPTY_METRICS: HqDashboardMetrics = {
  agentsActive: 0,
  agentsRunning: 0,
  agentsIdle: 0,
  epicsInProgress: 0,
  issuesOpen: 0,
  issuesInProgress: 0,
  issuesInReview: 0,
  issuesDone: 0,
  successRate: 100,
  totalCostUsd: 0,
  paygAgentsTracked: 0,
}

const DEFAULT_SETTINGS: HqSettings = {
  directorModel: '',
  tonePreference: 'technical',
  autoReview: true,
  showCosts: false,
}

const [hqMode, setHqMode] = createSignal(localStorage.getItem(STORAGE_KEY) === 'true')
const [hqPage, setHqPage] = createSignal<HqPage>('dashboard')
const [breadcrumbs, setBreadcrumbs] = createSignal<HqBreadcrumb[]>([
  { page: 'dashboard', label: 'Dashboard' },
])

const [selectedEpicId, setSelectedEpicId] = createSignal<string | null>(null)
const [selectedIssueId, setSelectedIssueId] = createSignal<string | null>(null)
const [selectedAgentId, setSelectedAgentId] = createSignal<string | null>(null)
const [showNewEpicModal, setShowNewEpicModal] = createSignal(false)

const [agents, setAgents] = createSignal<HqAgent[]>([])
const [epics, setEpics] = createSignal<HqEpic[]>([])
const [issues, setIssues] = createSignal<HqIssue[]>([])
const [plan, setPlan] = createSignal<HqPlan | null>(null)
const [activity, setActivity] = createSignal<HqActivityEvent[]>([])
const [metrics, setMetrics] = createSignal<HqDashboardMetrics>(EMPTY_METRICS)
const [directorMessages, setDirectorMessages] = createSignal<HqDirectorMessage[]>([])
const [hqSettings, setHqSettings] = createSignal<HqSettings>(DEFAULT_SETTINGS)
const [isLoading, setIsLoading] = createSignal(false)
const [lastError, setLastError] = createSignal<string | null>(null)

let refreshScheduled = false

function toggleHqMode(): void {
  const next = !hqMode()
  setHqMode(next)
  localStorage.setItem(STORAGE_KEY, String(next))
  if (next) void refreshAll()
}

function isOnboarded(): boolean {
  return localStorage.getItem(ONBOARDED_KEY) === 'true'
}

function markOnboarded(): void {
  localStorage.setItem(ONBOARDED_KEY, 'true')
}

function navigateTo(page: HqPage, label?: string): void {
  batch(() => {
    setHqPage(page)
    setBreadcrumbs([{ page, label: label ?? page }])
    setSelectedEpicId(null)
    setSelectedIssueId(null)
    setSelectedAgentId(null)
  })
  if (page === 'plan-review') void loadCurrentPlan()
}

function navigateToEpic(id: string): void {
  batch(() => {
    setHqPage('epic-detail')
    setSelectedEpicId(id)
    const epic = epics().find((e) => e.id === id)
    setBreadcrumbs([
      { page: 'epics', label: 'Epics' },
      { page: 'epic-detail', label: epic?.title ?? 'Epic', id },
    ])
  })
  void loadEpic(id)
  void loadCurrentPlan(id)
}

function navigateToIssue(id: string): void {
  batch(() => {
    setHqPage('issue-detail')
    setSelectedIssueId(id)
    const issue = issues().find((i) => i.id === id)
    setBreadcrumbs([
      { page: 'issues', label: 'Issues' },
      { page: 'issue-detail', label: issue?.identifier ?? 'Issue', id },
    ])
  })
  void loadIssue(id)
}

function navigateToAgent(id: string): void {
  batch(() => {
    setHqPage('agent-detail')
    setSelectedAgentId(id)
    const agent = agents().find((a) => a.id === id)
    setBreadcrumbs([
      { page: 'org-chart', label: 'Org Chart' },
      { page: 'agent-detail', label: agent?.name ?? 'Agent', id },
    ])
  })
  void loadAgent(id)
}

function navigateBack(): void {
  const crumbs = breadcrumbs()
  if (crumbs.length > 1) {
    const parent = crumbs[crumbs.length - 2]!
    batch(() => {
      setHqPage(parent.page)
      setBreadcrumbs(crumbs.slice(0, -1))
      if (parent.id) {
        if (parent.page === 'epic-detail') setSelectedEpicId(parent.id)
        else if (parent.page === 'issue-detail') setSelectedIssueId(parent.id)
        else if (parent.page === 'agent-detail') setSelectedAgentId(parent.id)
      }
    })
  }
}

function openNewEpicModal(): void {
  setShowNewEpicModal(true)
}

function closeNewEpicModal(): void {
  setShowNewEpicModal(false)
}

async function refreshAll(): Promise<void> {
  setIsLoading(true)
  try {
    const [nextEpics, nextIssues, nextAgents, nextActivity, nextMetrics, nextChat, nextSettings] =
      await Promise.all([
        rustBackend.listEpics(),
        rustBackend.listIssues(),
        rustBackend.getAgents(),
        rustBackend.getActivityFeed(),
        rustBackend.getDashboardMetrics(),
        rustBackend.getDirectorChat(),
        rustBackend.getHqSettings(),
      ])

    batch(() => {
      setEpics(nextEpics)
      setIssues(nextIssues)
      setAgents(nextAgents)
      setActivity(nextActivity)
      setMetrics(nextMetrics)
      setDirectorMessages(nextChat)
      setHqSettings(nextSettings)
      setLastError(null)
    })

    await loadCurrentPlan()
  } catch (error) {
    setLastError(error instanceof Error ? error.message : String(error))
  } finally {
    setIsLoading(false)
  }
}

function scheduleRefresh(delay = 150): void {
  if (refreshScheduled) return
  refreshScheduled = true
  window.setTimeout(() => {
    refreshScheduled = false
    void refreshAll()
  }, delay)
}

async function loadCurrentPlan(explicitEpicId?: string | null): Promise<void> {
  const epicId =
    explicitEpicId ?? selectedEpicId() ?? epics().find((epic) => epic.planId)?.id ?? null
  if (!epicId) {
    setPlan(null)
    return
  }
  try {
    setPlan(await rustBackend.getPlan(epicId))
  } catch {
    setPlan(null)
  }
}

async function loadEpic(id: string): Promise<void> {
  try {
    const detail = await rustBackend.getEpic(id)
    if (!detail) return
    batch(() => {
      setEpics((prev) => prev.map((epic) => (epic.id === id ? detail.epic : epic)))
      setIssues((prev) => {
        const filtered = prev.filter((issue) => issue.epicId !== id)
        return [...filtered, ...detail.issues]
      })
    })
  } catch {}
}

async function loadIssue(id: string): Promise<void> {
  try {
    const detail = await rustBackend.getIssue(id)
    if (!detail) return
    setIssues((prev) => prev.map((issue) => (issue.id === id ? detail : issue)))
  } catch {}
}

async function loadAgent(id: string): Promise<void> {
  try {
    const detail = await rustBackend.getAgent(id)
    if (!detail) return
    setAgents((prev) => prev.map((agent) => (agent.id === id ? detail : agent)))
  } catch {}
}

async function createEpic(title: string, description: string): Promise<void> {
  const epic = await rustBackend.createEpic(title, description)
  batch(() => {
    setShowNewEpicModal(false)
    setEpics((prev) => [epic, ...prev])
    setSelectedEpicId(epic.id)
    setHqPage('epic-detail')
    setBreadcrumbs([
      { page: 'epics', label: 'Epics' },
      { page: 'epic-detail', label: epic.title, id: epic.id },
    ])
  })
  scheduleRefresh(50)
}

async function moveIssue(issueId: string, toColumn: KanbanColumn): Promise<void> {
  const updated = await rustBackend.moveIssue(issueId, toColumn)
  if (!updated) return
  setIssues((prev) => prev.map((issue) => (issue.id === issueId ? updated : issue)))
  scheduleRefresh(50)
}

async function steerAgent(agentId: string, message: string): Promise<void> {
  const content = message.trim()
  if (!content) return
  await rustBackend.steerLead(agentId, content)
}

async function approveCurrentPlan(): Promise<void> {
  const current = plan()
  if (!current) return
  const updated = await rustBackend.approvePlan(current.id)
  if (updated) setPlan(updated)
  scheduleRefresh(50)
}

async function rejectCurrentPlan(feedback: string): Promise<void> {
  const current = plan()
  if (!current) return
  const updated = await rustBackend.rejectPlan(current.id, feedback)
  if (updated) setPlan(updated)
  scheduleRefresh(50)
}

async function addIssueComment(issueId: string, content: string): Promise<void> {
  const updated = await rustBackend.addComment(issueId, content)
  if (!updated) return
  setIssues((prev) => prev.map((issue) => (issue.id === issueId ? updated : issue)))
  scheduleRefresh(50)
}

async function sendDirectorMessage(message: string): Promise<void> {
  await rustBackend.sendDirectorMessage(message, selectedEpicId())
  scheduleRefresh(50)
}

async function updateSettings(patch: Partial<HqSettings>): Promise<void> {
  const updated = await rustBackend.updateHqSettings(patch)
  setHqSettings(updated)
}

function ingestEvent(event: AgentEvent): void {
  switch (event.type) {
    case 'plan_created':
    case 'hq_worker_started':
    case 'hq_worker_completed':
    case 'hq_worker_failed':
    case 'hq_all_complete':
    case 'hq_phase_started':
    case 'hq_phase_completed':
      scheduleRefresh(100)
      break
    case 'hq_worker_progress':
    case 'hq_worker_token':
    case 'hq_external_worker_started':
    case 'hq_external_worker_completed':
    case 'hq_external_worker_failed':
      scheduleRefresh(500)
      break
  }
}

const selectedEpic = createMemo(() => {
  const id = selectedEpicId()
  return id ? (epics().find((epic) => epic.id === id) ?? null) : null
})

const selectedIssue = createMemo(() => {
  const id = selectedIssueId()
  return id ? (issues().find((issue) => issue.id === id) ?? null) : null
})

const selectedAgent = createMemo(() => {
  const id = selectedAgentId()
  return id ? (agents().find((agent) => agent.id === id) ?? null) : null
})

const runningAgents = createMemo(() => agents().filter((agent) => agent.status === 'running'))

const issuesByColumn = createMemo(() => {
  const all = issues()
  return {
    backlog: all.filter((issue) => issue.status === 'backlog'),
    'in-progress': all.filter((issue) => issue.status === 'in-progress'),
    review: all.filter((issue) => issue.status === 'review'),
    done: all.filter((issue) => issue.status === 'done'),
  }
})

if (hqMode()) void refreshAll()

export function useHq() {
  return {
    hqMode,
    toggleHqMode,
    isOnboarded,
    markOnboarded,
    hqPage,
    breadcrumbs,
    navigateTo,
    navigateToEpic,
    navigateToIssue,
    navigateToAgent,
    navigateBack,
    showNewEpicModal,
    openNewEpicModal,
    closeNewEpicModal,
    agents,
    epics,
    issues,
    plan,
    activity,
    metrics,
    directorMessages,
    hqSettings,
    isLoading,
    lastError,
    selectedEpicId,
    selectedIssueId,
    selectedAgentId,
    selectedEpic,
    selectedIssue,
    selectedAgent,
    runningAgents,
    issuesByColumn,
    refreshAll,
    createEpic,
    moveIssue,
    steerAgent,
    approveCurrentPlan,
    rejectCurrentPlan,
    addIssueComment,
    sendDirectorMessage,
    updateSettings,
    ingestEvent,
  }
}
