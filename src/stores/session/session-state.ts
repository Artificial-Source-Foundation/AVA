/**
 * Session Reactive State
 * Module-level SolidJS signals wrapped in createRoot.
 * All signals are created once and shared via exports.
 */

import { createMemo, createRoot, createSignal } from 'solid-js'
import { DEFAULTS } from '../../config/constants'
import { debugLog } from '../../lib/debug-log'
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
  compactionIndex,
  setCompactionIndex,
} = createRoot(() => {
  const [currentSession, setCurrentSession] = createSignal<Session | null>(null)
  const [sessions, setSessions] = createSignal<SessionWithStats[]>([])
  const [isLoadingSessions, setIsLoadingSessions] = createSignal(false)
  const [messages, setMessages] = createSignal<Message[]>([])
  const [isLoadingMessages, setIsLoadingMessages] = createSignal(false)
  const [agents, setAgents] = createSignal<Agent[]>([])
  const [fileOperations, setFileOperations] = createSignal<FileOperation[]>([])
  const [terminalExecutions, setTerminalExecutions] = createSignal<TerminalExecution[]>([])
  const [memoryItems, setMemoryItems] = createSignal<MemoryItem[]>([])
  const [archivedSessions, setArchivedSessions] = createSignal<SessionWithStats[]>([])

  // Compaction divider — index of the first message after the last compaction.
  // -1 means no compaction has occurred in this session view.
  const [compactionIndex, setCompactionIndex] = createSignal<number>(-1)
  if (typeof window !== 'undefined') {
    window.addEventListener('ava:compacted', () => {
      // Snapshot the current message count so we know where the divider sits.
      setCompactionIndex(messages().length)
    })
  }

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
    debugLog('model', 'switched to:', model, 'provider:', providerId ?? 'default')
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
    compactionIndex,
    setCompactionIndex,
  }
})
