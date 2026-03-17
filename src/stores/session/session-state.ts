/**
 * Session Reactive State
 * Module-level SolidJS signals wrapped in createRoot.
 * All signals are created once and shared via exports.
 */

import { createMemo, createRoot, createSignal } from 'solid-js'
import { DEFAULTS } from '../../config/constants'
import { log } from '../../lib/logger'
import { getCoreBudget } from '../../services/core-bridge'
import type {
  Agent,
  FileOperation,
  MemoryItem,
  Message,
  Session,
  SessionTokenStats,
  SessionWithStats,
  TerminalExecution,
} from '../../types'

const SELECTED_MODEL_KEY = 'ava_selected_model'
const SELECTED_PROVIDER_KEY = 'ava_selected_provider'
const DEFAULT_CONTEXT_WINDOW = 200000

export const {
  currentSession,
  setCurrentSession,
  sessions,
  setSessions,
  isLoadingSessions,
  setIsLoadingSessions,
  messages,
  setMessages,
  isLoadingMessages,
  setIsLoadingMessages,
  agents,
  setAgents,
  fileOperations,
  setFileOperations,
  terminalExecutions,
  setTerminalExecutions,
  memoryItems,
  setMemoryItems,
  archivedSessions,
  setArchivedSessions,
  busySessionIds,
  selectedModel,
  selectedProvider,
  setSelectedModel,
  checkpoints,
  setCheckpoints,
  retryingMessageId,
  setRetryingMessageId,
  editingMessageId,
  setEditingMessageId,
  backgroundPlanActive,
  setBackgroundPlanActive,
  backgroundPlanProgress,
  setBackgroundPlanProgress,
  readOnlyFiles,
  setReadOnlyFiles,
  sessionTokenStats,
  contextUsage,
  agentStats,
} = createRoot(() => {
  const [currentSession, setCurrentSession] = createSignal<Session | null>(null)
  const [sessions, setSessions] = createSignal<SessionWithStats[]>([])
  const [isLoadingSessions, setIsLoadingSessions] = createSignal(false)
  const [messages, _setMessagesRaw] = createSignal<Message[]>([])
  // biome-ignore lint/suspicious/noExplicitAny: signal setter wrapper needs dynamic args
  const setMessages = ((...args: unknown[]) => {
    const prev = messages()
    // biome-ignore lint/suspicious/noExplicitAny: SolidJS signal setter typing
    const result = (_setMessagesRaw as any)(...args)
    const next = messages()
    if (next.length !== prev.length || next.length === 0) {
      console.warn(
        `[ava-debug][setMessages] ${prev.length} -> ${next.length}`,
        new Error().stack?.split('\n').slice(1, 4).join(' <- ')
      )
    }
    return result
  }) as typeof _setMessagesRaw
  const [isLoadingMessages, setIsLoadingMessages] = createSignal(false)
  const [agents, setAgents] = createSignal<Agent[]>([])
  const [fileOperations, setFileOperations] = createSignal<FileOperation[]>([])
  const [terminalExecutions, setTerminalExecutions] = createSignal<TerminalExecution[]>([])
  const [memoryItems, setMemoryItems] = createSignal<MemoryItem[]>([])
  const [archivedSessions, setArchivedSessions] = createSignal<SessionWithStats[]>([])

  // Busy session IDs (tracked from session:status events)
  const [busySessionIds, setBusySessionIds] = createSignal<Set<string>>(new Set())
  if (typeof window !== 'undefined') {
    window.addEventListener('ava:session-status', (e) => {
      const { sessionId, status } = (e as CustomEvent).detail as {
        sessionId: string
        status: string
      }
      setBusySessionIds((prev) => {
        const next = new Set(prev)
        if (status === 'busy') next.add(sessionId)
        else next.delete(sessionId)
        return next
      })
    })
  }

  // Selected model — persisted to localStorage
  const savedModel =
    typeof localStorage !== 'undefined' ? localStorage.getItem(SELECTED_MODEL_KEY) : null
  const savedProvider =
    typeof localStorage !== 'undefined' ? localStorage.getItem(SELECTED_PROVIDER_KEY) : null
  const [selectedModel, _setSelectedModel] = createSignal<string>(savedModel || DEFAULTS.MODEL)
  const [selectedProvider, _setSelectedProvider] = createSignal<string | null>(savedProvider)
  const setSelectedModel = (model: string, providerId?: string): void => {
    _setSelectedModel(model)
    _setSelectedProvider(providerId ?? null)
    log.info('model', `Model changed to ${model}`, { provider: providerId ?? 'default' })
    try {
      localStorage.setItem(SELECTED_MODEL_KEY, model)
      if (providerId) localStorage.setItem(SELECTED_PROVIDER_KEY, providerId)
      else localStorage.removeItem(SELECTED_PROVIDER_KEY)
    } catch {
      /* noop */
    }
  }

  // Checkpoints
  const [checkpoints, setCheckpoints] = createSignal<
    Array<{ id: string; timestamp: number; description: string; messageCount: number }>
  >([])

  // UI state
  const [retryingMessageId, setRetryingMessageId] = createSignal<string | null>(null)
  const [editingMessageId, setEditingMessageId] = createSignal<string | null>(null)

  // Background plan execution
  const [backgroundPlanActive, setBackgroundPlanActive] = createSignal(false)
  const [backgroundPlanProgress, setBackgroundPlanProgress] = createSignal('')

  // Read-only file context
  const [readOnlyFiles, setReadOnlyFiles] = createSignal<string[]>([])

  // Computed: token stats
  const sessionTokenStats = createMemo((): SessionTokenStats => {
    return messages().reduce(
      (stats, msg) => ({
        total: stats.total + (msg.tokensUsed || 0),
        count: stats.count + (msg.tokensUsed ? 1 : 0),
        totalCost: stats.totalCost + (msg.costUSD || 0),
      }),
      { total: 0, count: 0, totalCost: 0 }
    )
  })

  // Budget tick for reactive context usage
  const [budgetTick, setBudgetTick] = createSignal(0)
  if (typeof window !== 'undefined') {
    window.addEventListener('ava:budget-updated', () => setBudgetTick((n) => n + 1))
    window.addEventListener('ava:core-settings-changed', (e) => {
      if ((e as CustomEvent).detail?.category === 'context') setBudgetTick((n) => n + 1)
    })
  }

  // Computed: context window usage
  const contextUsage = createMemo(() => {
    budgetTick()
    const budget = getCoreBudget()
    if (budget) {
      const s = budget.getStats()
      return { used: s.total, total: s.limit, percentage: s.percentUsed }
    }
    const estimated = messages().reduce((sum, m) => sum + Math.ceil(m.content.length / 4), 0)
    return {
      used: estimated,
      total: DEFAULT_CONTEXT_WINDOW,
      percentage: Math.min(100, (estimated / DEFAULT_CONTEXT_WINDOW) * 100),
    }
  })

  // Computed: agent statistics
  const agentStats = createMemo(() => {
    const all = agents()
    return {
      running: all.filter((a) => a.status === 'thinking' || a.status === 'executing').length,
      completed: all.filter((a) => a.status === 'completed').length,
      error: all.filter((a) => a.status === 'error').length,
      total: all.length,
    }
  })

  return {
    currentSession,
    setCurrentSession,
    sessions,
    setSessions,
    isLoadingSessions,
    setIsLoadingSessions,
    messages,
    setMessages,
    isLoadingMessages,
    setIsLoadingMessages,
    agents,
    setAgents,
    fileOperations,
    setFileOperations,
    terminalExecutions,
    setTerminalExecutions,
    memoryItems,
    setMemoryItems,
    archivedSessions,
    setArchivedSessions,
    busySessionIds,
    setBusySessionIds,
    selectedModel,
    selectedProvider,
    setSelectedModel,
    checkpoints,
    setCheckpoints,
    retryingMessageId,
    setRetryingMessageId,
    editingMessageId,
    setEditingMessageId,
    backgroundPlanActive,
    setBackgroundPlanActive,
    backgroundPlanProgress,
    setBackgroundPlanProgress,
    readOnlyFiles,
    setReadOnlyFiles,
    sessionTokenStats,
    budgetTick,
    setBudgetTick,
    contextUsage,
    agentStats,
  }
})
