/**
 * AVA HQ Store — navigation state + live backend-backed HQ data.
 */

import { batch, createEffect, createMemo, createSignal } from 'solid-js'
import type { ThinkingSegment } from '../hooks/use-rust-agent'
import { rustBackend } from '../services/rust-bridge'
import type { ToolCall } from '../types'
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
  HqWorkspaceBootstrapResult,
} from '../types/hq'
import type { AgentEvent } from '../types/rust-ipc'
import { useSettings } from './settings'

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
const [hqPage, setHqPage] = createSignal<HqPage>('director-chat')
const [breadcrumbs, setBreadcrumbs] = createSignal<HqBreadcrumb[]>([
  { page: 'director-chat', label: 'Director Chat' },
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
const [liveDirectorWorkerId, setLiveDirectorWorkerId] = createSignal<string | null>(null)
const [liveDirectorStartedAt, setLiveDirectorStartedAt] = createSignal<number | null>(null)
const [liveDirectorContent, setLiveDirectorContent] = createSignal('')
const [liveDirectorThinking, setLiveDirectorThinking] = createSignal('')
const [liveDirectorThinkingSegments, setLiveDirectorThinkingSegments] = createSignal<
  ThinkingSegment[]
>([])
const [liveDirectorToolCalls, setLiveDirectorToolCalls] = createSignal<ToolCall[]>([])
const [liveDirectorStreaming, setLiveDirectorStreaming] = createSignal(false)
const [hqSettings, setHqSettings] = createSignal<HqSettings>(DEFAULT_SETTINGS)
const [lastBootstrapResult, setLastBootstrapResult] =
  createSignal<HqWorkspaceBootstrapResult | null>(null)
const [isLoading, setIsLoading] = createSignal(false)
const [lastError, setLastError] = createSignal<string | null>(null)

let refreshScheduled = false

function preferredEpicId(list: HqEpic[], explicitEpicId?: string | null): string | null {
  if (explicitEpicId) return explicitEpicId

  const sorted = [...list].sort((a, b) => b.createdAt - a.createdAt)
  return (
    selectedEpicId() ??
    sorted.find((epic) => !!epic.planId)?.id ??
    sorted.find((epic) => epic.status === 'planning' || epic.status === 'in-progress')?.id ??
    sorted[0]?.id ??
    null
  )
}

function resetLiveDirectorStream(): void {
  batch(() => {
    setLiveDirectorWorkerId(null)
    setLiveDirectorStartedAt(null)
    setLiveDirectorContent('')
    setLiveDirectorThinking('')
    setLiveDirectorThinkingSegments([])
    setLiveDirectorToolCalls([])
    setLiveDirectorStreaming(false)
  })
}

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

async function bootstrapWorkspace(args?: {
  directorModel?: string | null
  force?: boolean
}): Promise<HqWorkspaceBootstrapResult> {
  const result = await rustBackend.bootstrapHqWorkspace(args)
  setLastBootstrapResult(result)
  markOnboarded()
  return result
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

function navigateToAgent(id: string): void {
  batch(() => {
    setHqPage('team')
    setSelectedAgentId(id)
    const agent = agents().find((a) => a.id === id)
    setBreadcrumbs([
      { page: 'team', label: 'Team' },
      { page: 'team', label: agent?.name ?? 'Agent', id },
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
        else if (parent.page === 'agent-detail' || parent.page === 'team')
          setSelectedAgentId(parent.id)
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

      const startedAt = liveDirectorStartedAt()
      if (
        !liveDirectorStreaming() &&
        startedAt &&
        nextChat.some((msg) => msg.role === 'director' && msg.timestamp >= startedAt)
      ) {
        resetLiveDirectorStream()
      }
    })

    await loadCurrentPlan()
  } catch (error) {
    setLastError(error instanceof Error ? error.message : String(error))
  } finally {
    setIsLoading(false)
  }
}

function triggerRefresh(): void {
  void refreshAll()
}

function scheduleRefresh(delay = 150): void {
  if (refreshScheduled) return
  refreshScheduled = true
  // eslint-disable-next-line solid/reactivity
  window.setTimeout(() => {
    refreshScheduled = false
    triggerRefresh()
  }, delay)
}

function scheduleRefreshBurst(delays: number[]): void {
  for (const delay of delays) {
    window.setTimeout(() => {
      scheduleRefresh(0)
    }, delay)
  }
}

async function loadCurrentPlan(explicitEpicId?: string | null): Promise<void> {
  const epicId = preferredEpicId(epics(), explicitEpicId)
  if (!epicId) {
    setPlan(null)
    return
  }
  try {
    const nextPlan = await rustBackend.getPlan(epicId)
    batch(() => {
      setSelectedEpicId(epicId)
      setPlan(nextPlan)
    })
  } catch {
    setPlan(null)
  }
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
    setHqPage('plan-review')
    setBreadcrumbs([
      { page: 'plan-review', label: 'Plan Review' },
      { page: 'plan-review', label: epic.title, id: epic.id },
    ])
  })
  scheduleRefreshBurst([100, 1000, 4000, 10000, 22000])
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

async function sendDirectorMessage(message: string): Promise<void> {
  const { settings } = useSettings()
  const team = settings().team
  await rustBackend.sendDirectorMessage(message, selectedEpicId(), {
    defaultDirectorModel: team.defaultDirectorModel,
    defaultLeadModel: team.defaultLeadModel,
    defaultWorkerModel: team.defaultWorkerModel,
    defaultScoutModel: team.defaultScoutModel,
    workerNames: team.workerNames,
    leads: team.leads.map((lead) => ({
      domain: lead.domain,
      enabled: lead.enabled,
      model: lead.model,
      maxWorkers: lead.maxWorkers,
      customPrompt: lead.customPrompt,
    })),
  })
  scheduleRefresh(50)
  scheduleRefreshBurst([750, 2000, 5000, 9000])
}

async function updateSettings(patch: Partial<HqSettings>): Promise<void> {
  const updated = await rustBackend.updateHqSettings(patch)
  setHqSettings(updated)
}

async function refreshSettings(): Promise<void> {
  try {
    setHqSettings(await rustBackend.getHqSettings())
  } catch (error) {
    setLastError(error instanceof Error ? error.message : String(error))
  }
}

function ingestEvent(event: AgentEvent): void {
  switch (event.type) {
    case 'plan_created':
      setHqPage('plan-review')
      setBreadcrumbs([{ page: 'plan-review', label: 'Plan Review' }])
      scheduleRefresh(100)
      break
    case 'hq_worker_started':
      batch(() => {
        setLiveDirectorWorkerId(event.worker_id)
        setLiveDirectorStartedAt(Date.now())
        setLiveDirectorContent('')
        setLiveDirectorThinking('')
        setLiveDirectorThinkingSegments([])
        setLiveDirectorToolCalls([])
        setLiveDirectorStreaming(true)
      })
      scheduleRefresh(100)
      break
    case 'hq_worker_completed':
    case 'hq_worker_failed':
    case 'hq_all_complete':
    case 'hq_phase_started':
    case 'hq_phase_completed':
      scheduleRefresh(100)
      if ('worker_id' in event && event.worker_id === liveDirectorWorkerId()) {
        batch(() => {
          setLiveDirectorStreaming(false)
        })
      }
      if (event.type === 'hq_all_complete') {
        batch(() => {
          setLiveDirectorStreaming(false)
        })
      }
      break
    case 'hq_worker_token':
      if (event.worker_id === liveDirectorWorkerId()) {
        setLiveDirectorContent((prev) => prev + event.token)
      }
      break
    case 'hq_worker_thinking':
      if (event.worker_id === liveDirectorWorkerId()) {
        setLiveDirectorThinking((prev) => prev + event.content)
        setLiveDirectorThinkingSegments((prev) => {
          if (prev.length === 0) return [{ thinking: event.content, toolCallIds: [] }]
          const next = [...prev]
          const last = next[next.length - 1]!
          next[next.length - 1] = { ...last, thinking: last.thinking + event.content }
          return next
        })
      }
      break
    case 'hq_worker_tool_call':
      if (event.worker_id === liveDirectorWorkerId()) {
        const startedAt = Date.now()
        setLiveDirectorToolCalls((prev) => [
          ...prev,
          {
            id: event.call_id,
            name: event.name,
            args: event.args,
            status: 'running',
            startedAt,
          },
        ])
        setLiveDirectorThinkingSegments((prev) => {
          if (prev.length === 0) return [{ thinking: '', toolCallIds: [event.call_id] }]
          const next = [...prev]
          const last = next[next.length - 1]!
          next[next.length - 1] = { ...last, toolCallIds: [...last.toolCallIds, event.call_id] }
          return next
        })
      }
      break
    case 'hq_worker_tool_result':
      if (event.worker_id === liveDirectorWorkerId()) {
        setLiveDirectorToolCalls((prev) =>
          prev.map((toolCall) =>
            toolCall.id === event.call_id
              ? {
                  ...toolCall,
                  status: event.is_error ? 'error' : 'success',
                  output: event.content,
                  completedAt: Date.now(),
                  error: event.is_error ? event.content : undefined,
                }
              : toolCall
          )
        )
      }
      break
    case 'hq_worker_progress':
    case 'hq_summary':
    case 'hq_spec_created':
    case 'hq_artifact_created':
    case 'hq_conflict_detected':
    case 'hq_external_worker_started':
    case 'hq_external_worker_completed':
    case 'hq_external_worker_failed':
      scheduleRefresh(500)
      break
  }
}

const selectedAgent = createMemo(() => {
  const id = selectedAgentId()
  return id ? (agents().find((agent) => agent.id === id) ?? null) : null
})

const runningAgents = createMemo(() => agents().filter((agent) => agent.status === 'running'))

createEffect(() => {
  if (hqMode()) {
    triggerRefresh()
  }
})

createEffect(() => {
  const page = hqPage()
  const currentEpics = epics()
  if (page !== 'plan-review' || currentEpics.length === 0 || plan()) return
  const epicId = preferredEpicId(currentEpics)
  if (epicId) void loadCurrentPlan(epicId)
})

export function useHq() {
  return {
    hqMode,
    toggleHqMode,
    isOnboarded,
    markOnboarded,
    bootstrapWorkspace,
    hqPage,
    breadcrumbs,
    navigateTo,
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
    liveDirectorStartedAt,
    liveDirectorContent,
    liveDirectorThinking,
    liveDirectorThinkingSegments,
    liveDirectorToolCalls,
    liveDirectorStreaming,
    hqSettings,
    lastBootstrapResult,
    isLoading,
    lastError,
    selectedEpicId,
    selectedIssueId,
    selectedAgentId,
    selectedAgent,
    runningAgents,
    refreshAll,
    refreshSettings,
    createEpic,
    approveCurrentPlan,
    rejectCurrentPlan,
    sendDirectorMessage,
    updateSettings,
    ingestEvent,
  }
}
