/**
 * Session Store
 * Global state management for sessions, messages, and agents
 */

import { createMemo, createRoot, createSignal } from 'solid-js'
import { DEFAULTS, STORAGE_KEYS } from '../config/constants'
import { getCoreBudget, notifySessionOpened } from '../services/core-bridge'
import {
  archiveSession as dbArchiveSession,
  clearFileOperations as dbClearFileOperations,
  clearMemoryItems as dbClearMemoryItems,
  clearTerminalExecutions as dbClearTerminalExecutions,
  createSession as dbCreateSession,
  deleteMemoryItem as dbDeleteMemoryItem,
  deleteMessageFromDb as dbDeleteMessage,
  deleteMessagesFromTimestamp as dbDeleteMessagesFromTimestamp,
  deleteSession as dbDeleteSession,
  deleteSessionMessages as dbDeleteSessionMessages,
  duplicateSessionMessages as dbDuplicateSessionMessages,
  getArchivedSessions as dbGetArchivedSessions,
  insertMessages as dbInsertMessages,
  updateAgentInDb as dbUpdateAgent,
  updateSession as dbUpdateSession,
  updateTerminalExecution as dbUpdateTerminalExecution,
  getAgents,
  getAllMemoryItems,
  getCheckpoints,
  getFileOperations,
  getMemoryItems,
  getMessages,
  getSessionsWithStats,
  getTerminalExecutions,
  saveAgent,
  saveFileOperation,
  saveMemoryItem,
  saveTerminalExecution,
} from '../services/database'
import { readFileContent } from '../services/file-browser'
import {
  clearVersionHistory,
  getVersionCounts,
  redoFileChange,
  undoFileChange,
} from '../services/file-versions'
import { logDebug, logError, logInfo, logWarn } from '../services/logger'
import type {
  Agent,
  FileOperation,
  MemoryItem,
  Message,
  MessageError,
  Session,
  SessionTokenStats,
  SessionWithStats,
  TerminalExecution,
} from '../types'
import { useProject } from './project'
import { getLastSessionForProject, setLastSessionForProject } from './session-persistence'

// ============================================================================
// Session State — wrapped in createRoot to avoid "cleanups outside createRoot" warnings
// ============================================================================

const SELECTED_MODEL_KEY = 'ava_selected_model'
const SELECTED_PROVIDER_KEY = 'ava_selected_provider'
const DEFAULT_CONTEXT_WINDOW = 200000

const {
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
  // Current active session
  const [currentSession, setCurrentSession] = createSignal<Session | null>(null)

  // All sessions (for sidebar)
  const [sessions, setSessions] = createSignal<SessionWithStats[]>([])
  const [isLoadingSessions, setIsLoadingSessions] = createSignal(false)

  // Messages in current session
  const [messages, setMessages] = createSignal<Message[]>([])
  const [isLoadingMessages, setIsLoadingMessages] = createSignal(false)

  // Agents in current session
  const [agents, setAgents] = createSignal<Agent[]>([])

  // File operations in current session
  const [fileOperations, setFileOperations] = createSignal<FileOperation[]>([])

  // Terminal executions in current session
  const [terminalExecutions, setTerminalExecutions] = createSignal<TerminalExecution[]>([])

  // Memory/context items in current session
  const [memoryItems, setMemoryItems] = createSignal<MemoryItem[]>([])

  // Archived sessions (lazy-loaded)
  const [archivedSessions, setArchivedSessions] = createSignal<SessionWithStats[]>([])

  // Busy session IDs (tracked from session:status events)
  const [busySessionIds, setBusySessionIds] = createSignal<Set<string>>(new Set())

  // Listen for session busy/idle events from core-v2
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

  // Selected model for chat — persisted to localStorage
  const savedModel =
    typeof localStorage !== 'undefined' ? localStorage.getItem(SELECTED_MODEL_KEY) : null
  const savedProvider =
    typeof localStorage !== 'undefined' ? localStorage.getItem(SELECTED_PROVIDER_KEY) : null
  const [selectedModel, _setSelectedModel] = createSignal<string>(savedModel || DEFAULTS.MODEL)
  const [selectedProvider, _setSelectedProvider] = createSignal<string | null>(savedProvider)
  const setSelectedModel = (model: string, providerId?: string) => {
    _setSelectedModel(model)
    _setSelectedProvider(providerId ?? null)
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

  // Background plan execution — plan runs in background while user continues working
  const [backgroundPlanActive, setBackgroundPlanActive] = createSignal(false)
  const [backgroundPlanProgress, setBackgroundPlanProgress] = createSignal('')

  // Read-only file context — files attached to context but protected from edits
  const [readOnlyFiles, setReadOnlyFiles] = createSignal<string[]>([])

  // Computed Values
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

  // Reactive trigger for context budget updates (from agent events + manual sync)
  const [budgetTick, setBudgetTick] = createSignal(0)
  // Listen for budget updates dispatched by core-bridge context sync
  if (typeof window !== 'undefined') {
    window.addEventListener('ava:budget-updated', () => setBudgetTick((n) => n + 1))
    window.addEventListener('ava:core-settings-changed', (e) => {
      if ((e as CustomEvent).detail?.category === 'context') setBudgetTick((n) => n + 1)
    })
  }

  // Context window usage — uses core budget when available, falls back to rough estimate
  const contextUsage = createMemo(() => {
    budgetTick() // reactive dependency — triggers recalc on budget/context events
    const budget = getCoreBudget()
    if (budget) {
      const s = budget.getStats()
      return { used: s.total, total: s.limit, percentage: s.percentUsed }
    }
    // Fallback: rough estimate (~4 chars per token)
    const estimated = messages().reduce((sum, m) => sum + Math.ceil(m.content.length / 4), 0)
    return {
      used: estimated,
      total: DEFAULT_CONTEXT_WINDOW,
      percentage: Math.min(100, (estimated / DEFAULT_CONTEXT_WINDOW) * 100),
    }
  })

  // Agent statistics
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

// ============================================================================
// Session Store Hook
// ============================================================================

export function useSession() {
  const store = {
    // ========================================================================
    // State Accessors
    // ========================================================================
    currentSession,
    setCurrentSession,
    sessions,
    isLoadingSessions,
    messages,
    setMessages,
    isLoadingMessages,
    agents,
    setAgents,
    fileOperations,
    setFileOperations,
    terminalExecutions,
    setTerminalExecutions,
    memoryItems,
    setMemoryItems,
    selectedModel,
    selectedProvider,
    setSelectedModel,
    retryingMessageId,
    editingMessageId,
    sessionTokenStats,
    contextUsage,
    agentStats,

    // ========================================================================
    // Session Tree (for branching visualization)
    // ========================================================================

    /**
     * Build a tree from the flat sessions list using parentSessionId.
     * Returns root sessions (no parent) with nested children arrays.
     */
    getSessionTree: createMemo(() => {
      const all = sessions()
      const childMap = new Map<string, SessionWithStats[]>()
      const roots: SessionWithStats[] = []

      for (const s of all) {
        if (s.parentSessionId) {
          const siblings = childMap.get(s.parentSessionId) ?? []
          siblings.push(s)
          childMap.set(s.parentSessionId, siblings)
        } else {
          roots.push(s)
        }
      }

      return { roots, childMap }
    }),

    // ========================================================================
    // Session List Management
    // ========================================================================

    /**
     * Load all sessions from database
     * Filters by current project if one is selected
     */
    loadAllSessions: async () => {
      await store.loadSessionsForCurrentProject()
    },

    /**
     * Load sessions for current project
     */
    loadSessionsForCurrentProject: async () => {
      const { currentProject } = useProject()
      const projectId = currentProject()?.id

      setIsLoadingSessions(true)
      try {
        const dbSessions = await getSessionsWithStats(projectId)
        setSessions(dbSessions)
        logDebug('session', 'Loaded sessions', { count: dbSessions.length })
      } catch (err) {
        logError('Session', 'Failed to load sessions', err)
        setSessions([])
      } finally {
        setIsLoadingSessions(false)
      }
    },

    /**
     * Restore best session candidate for current project
     */
    restoreForCurrentProject: async (): Promise<void> => {
      const { currentProject } = useProject()
      const projectId = currentProject()?.id
      const projectSessions = sessions()

      if (projectSessions.length === 0) {
        await store.createNewSession()
        return
      }

      const lastProjectSessionId = getLastSessionForProject(projectId)
      const globalLastSessionId = localStorage.getItem(STORAGE_KEYS.LAST_SESSION)
      const restoreTarget =
        projectSessions.find((session) => session.id === lastProjectSessionId) ||
        projectSessions.find((session) => session.id === globalLastSessionId) ||
        projectSessions[0]

      if (!restoreTarget) {
        await store.createNewSession()
        return
      }

      await store.switchSession(restoreTarget.id)
    },

    /**
     * Create a new session and switch to it
     * Automatically assigns to current project if one is selected
     */
    createNewSession: async (name?: string): Promise<Session> => {
      const { currentProject } = useProject()
      const project = currentProject()
      const projectId = project?.id

      const session = await dbCreateSession(name || DEFAULTS.SESSION_NAME, projectId)
      const sessionWithStats: SessionWithStats = {
        ...session,
        messageCount: 0,
        totalTokens: 0,
      }

      // Add to beginning of session list
      setSessions((prev) => [sessionWithStats, ...prev])

      // Switch to new session
      setCurrentSession(session)
      setMessages([])
      setAgents([])

      logInfo('session', 'Session created', {
        id: session.id,
        name: session.name,
        project: project?.name ?? 'unknown',
      })

      // Notify core-v2 extensions (loads CLAUDE.md, codebase, skills, etc.)
      const { currentProject: getProject } = useProject()
      const cwd = getProject()?.directory || '.'
      notifySessionOpened(session.id, cwd)

      // Persist last session
      localStorage.setItem(STORAGE_KEYS.LAST_SESSION, session.id)
      setLastSessionForProject(projectId, session.id)

      return session
    },

    /**
     * Switch to a different session
     */
    switchSession: async (id: string): Promise<void> => {
      const fromSessionId = currentSession()?.id
      const session = sessions().find((s) => s.id === id)
      if (!session) {
        logWarn('session', 'Session not found', { id })
        return
      }

      // Clear current state
      setEditingMessageId(null)
      setRetryingMessageId(null)

      // Set current session
      setCurrentSession(session)

      // Load messages for session
      setIsLoadingMessages(true)
      try {
        const dbMessages = await getMessages(id)
        setMessages(dbMessages)
        logInfo('session', 'Session switched', {
          from: fromSessionId ?? 'none',
          to: id,
          messageCount: dbMessages.length,
        })
      } catch (err) {
        logError('Session', 'Failed to load messages', err)
        setMessages([])
      } finally {
        setIsLoadingMessages(false)
      }

      // Load session-specific data from database
      try {
        const [dbAgents, dbFileOps, dbTerminalExecs, dbMemItems, dbCheckpoints] = await Promise.all(
          [
            getAgents(id),
            getFileOperations(id),
            getTerminalExecutions(id),
            getMemoryItems(id),
            getCheckpoints(id),
          ]
        )
        setAgents(dbAgents)
        setFileOperations(dbFileOps)
        setTerminalExecutions(dbTerminalExecs)
        setMemoryItems(dbMemItems)
        setCheckpoints(dbCheckpoints)
      } catch (err) {
        logError('Session', 'Failed to load session data', err)
        setAgents([])
        setFileOperations([])
        setTerminalExecutions([])
        setMemoryItems([])
        setCheckpoints([])
      }

      // Notify core-v2 extensions (loads CLAUDE.md, codebase, skills, etc.)
      const { currentProject: getProject } = useProject()
      const cwd = getProject()?.directory || '.'
      notifySessionOpened(id, cwd)

      // Persist last session
      localStorage.setItem(STORAGE_KEYS.LAST_SESSION, id)
      const { currentProject } = useProject()
      setLastSessionForProject(currentProject()?.id, id)
    },

    /**
     * Rename a session
     */
    renameSession: async (id: string, newName: string): Promise<void> => {
      const trimmedName = newName.trim()
      if (!trimmedName) return

      await dbUpdateSession(id, { name: trimmedName })

      // Update in session list
      setSessions((prev) =>
        prev.map((s) => (s.id === id ? { ...s, name: trimmedName, updatedAt: Date.now() } : s))
      )

      // Update current session if it's the one being renamed
      if (currentSession()?.id === id) {
        setCurrentSession((prev) =>
          prev ? { ...prev, name: trimmedName, updatedAt: Date.now() } : null
        )
      }

      logInfo('session', 'Session renamed', { id, name: trimmedName })
    },

    /**
     * Archive a session (soft delete)
     */
    archiveSession: async (id: string): Promise<void> => {
      const { currentProject } = useProject()
      const projectId = currentProject()?.id

      await dbArchiveSession(id)

      // Remove from session list
      const remaining = sessions().filter((s) => s.id !== id)
      setSessions(remaining)

      // If this was the current session, switch to another or create new
      if (currentSession()?.id === id) {
        if (remaining.length > 0) {
          // Switch to most recent
          const mostRecent = remaining[0]
          setCurrentSession(mostRecent)
          setIsLoadingMessages(true)
          try {
            const dbMessages = await getMessages(mostRecent.id)
            setMessages(dbMessages)
          } catch {
            setMessages([])
          } finally {
            setIsLoadingMessages(false)
          }
          localStorage.setItem(STORAGE_KEYS.LAST_SESSION, mostRecent.id)
          setLastSessionForProject(projectId, mostRecent.id)
        } else {
          // Create new session
          const newSession = await dbCreateSession(DEFAULTS.SESSION_NAME, projectId)
          const sessionWithStats: SessionWithStats = {
            ...newSession,
            messageCount: 0,
            totalTokens: 0,
          }
          setSessions([sessionWithStats])
          setCurrentSession(newSession)
          setMessages([])
          localStorage.setItem(STORAGE_KEYS.LAST_SESSION, newSession.id)
          setLastSessionForProject(projectId, newSession.id)
        }
      }
    },

    /**
     * Unarchive a session — restore it to active list
     */
    unarchiveSession: async (id: string): Promise<void> => {
      await dbUpdateSession(id, { status: 'active' })

      // Move from archived to active list
      const archived = archivedSessions().find((s) => s.id === id)
      if (archived) {
        const restored = { ...archived, status: 'active' as const }
        setArchivedSessions((prev) => prev.filter((s) => s.id !== id))
        setSessions((prev) => [restored, ...prev])
      }
    },

    /**
     * Load archived sessions
     */
    loadArchivedSessions: async (): Promise<void> => {
      const { currentProject } = useProject()
      const projectId = currentProject()?.id
      try {
        const archived = await dbGetArchivedSessions(projectId)
        setArchivedSessions(archived)
      } catch (err) {
        logError('Session', 'Failed to load archived sessions', err)
        setArchivedSessions([])
      }
    },

    /** Archived sessions list */
    archivedSessions,

    /** Set of session IDs currently busy */
    busySessionIds,

    /** Check if a session is busy */
    isSessionBusy: (id: string): boolean => busySessionIds().has(id),

    /**
     * Update a session's slug
     */
    updateSessionSlug: async (id: string, slug: string): Promise<void> => {
      await dbUpdateSession(id, { slug })

      setSessions((prev) => prev.map((s) => (s.id === id ? { ...s, slug } : s)))

      if (currentSession()?.id === id) {
        setCurrentSession((prev) => (prev ? { ...prev, slug } : null))
      }
    },

    /**
     * Delete a session permanently
     */
    deleteSessionPermanently: async (id: string): Promise<void> => {
      const { currentProject } = useProject()
      const projectId = currentProject()?.id

      await dbDeleteSession(id)

      // Same logic as archive
      const remaining = sessions().filter((s) => s.id !== id)
      setSessions(remaining)

      if (currentSession()?.id === id) {
        if (remaining.length > 0) {
          const mostRecent = remaining[0]
          setCurrentSession(mostRecent)
          setIsLoadingMessages(true)
          try {
            const dbMessages = await getMessages(mostRecent.id)
            setMessages(dbMessages)
          } catch {
            setMessages([])
          } finally {
            setIsLoadingMessages(false)
          }
          localStorage.setItem(STORAGE_KEYS.LAST_SESSION, mostRecent.id)
          setLastSessionForProject(projectId, mostRecent.id)
        } else {
          const newSession = await dbCreateSession(DEFAULTS.SESSION_NAME, projectId)
          const sessionWithStats: SessionWithStats = {
            ...newSession,
            messageCount: 0,
            totalTokens: 0,
          }
          setSessions([sessionWithStats])
          setCurrentSession(newSession)
          setMessages([])
          localStorage.setItem(STORAGE_KEYS.LAST_SESSION, newSession.id)
          setLastSessionForProject(projectId, newSession.id)
        }
      }
    },

    /**
     * Duplicate a session with all its messages
     */
    duplicateSession: async (sourceSessionId: string): Promise<void> => {
      const source = sessions().find((s) => s.id === sourceSessionId)
      if (!source) return

      const { currentProject } = useProject()
      const projectId = currentProject()?.id

      // Create new session with "(copy)" suffix
      const newSession = await dbCreateSession(`${source.name} (copy)`, projectId)

      // Copy all messages
      await dbDuplicateSessionMessages(sourceSessionId, newSession.id)

      // Add to session list with source's message count
      const sessionWithStats: SessionWithStats = {
        ...newSession,
        messageCount: source.messageCount,
        totalTokens: source.totalTokens,
        lastPreview: source.lastPreview,
      }
      setSessions((prev) => [sessionWithStats, ...prev])

      // Switch to the new session
      setCurrentSession(newSession)
      setIsLoadingMessages(true)
      try {
        const dbMessages = await getMessages(newSession.id)
        setMessages(dbMessages)
      } catch {
        setMessages([])
      } finally {
        setIsLoadingMessages(false)
      }
      localStorage.setItem(STORAGE_KEYS.LAST_SESSION, newSession.id)
      setLastSessionForProject(projectId, newSession.id)
    },

    /**
     * Fork a session — creates a new session with all messages copied
     */
    forkSession: async (sourceSessionId: string, name?: string): Promise<void> => {
      const source = sessions().find((s) => s.id === sourceSessionId)
      if (!source) return

      const { currentProject } = useProject()
      const projectId = currentProject()?.id

      const forkName = name || `${source.name} (fork)`
      const newSession = await dbCreateSession(forkName, projectId, sourceSessionId)

      // Copy all messages up to current point
      await dbDuplicateSessionMessages(sourceSessionId, newSession.id)

      // Add to session list
      const sessionWithStats: SessionWithStats = {
        ...newSession,
        messageCount: source.messageCount,
        totalTokens: source.totalTokens,
        lastPreview: source.lastPreview,
      }
      setSessions((prev) => [sessionWithStats, ...prev])

      // Switch to the forked session
      setCurrentSession(newSession)
      setIsLoadingMessages(true)
      try {
        const dbMessages = await getMessages(newSession.id)
        setMessages(dbMessages)
      } catch {
        setMessages([])
      } finally {
        setIsLoadingMessages(false)
      }
      localStorage.setItem(STORAGE_KEYS.LAST_SESSION, newSession.id)
      setLastSessionForProject(projectId, newSession.id)
    },

    /**
     * Branch at a message — creates a new session with messages up to (and including) the target.
     * Like forkSession but truncated to a specific point.
     */
    branchAtMessage: async (messageId: string): Promise<void> => {
      const session = currentSession()
      if (!session) return

      const msgs = messages()
      const index = msgs.findIndex((m) => m.id === messageId)
      if (index === -1) return

      const { currentProject } = useProject()
      const projectId = currentProject()?.id

      const messagesToCopy = msgs.slice(0, index + 1)
      const branchName = `${session.name} (branch)`
      const newSession = await dbCreateSession(branchName, projectId, session.id)

      // Insert only the messages up to the branch point
      await dbInsertMessages(messagesToCopy.map((m) => ({ ...m, sessionId: newSession.id })))

      const totalTokens = messagesToCopy.reduce((sum, m) => sum + (m.tokensUsed || 0), 0)
      const sessionWithStats: SessionWithStats = {
        ...newSession,
        messageCount: messagesToCopy.length,
        totalTokens,
        lastPreview: messagesToCopy[messagesToCopy.length - 1]?.content.slice(0, 100) || '',
      }
      setSessions((prev) => [sessionWithStats, ...prev])

      // Switch to the branched session
      setCurrentSession(newSession)
      setMessages(messagesToCopy.map((m) => ({ ...m, sessionId: newSession.id })))
      localStorage.setItem(STORAGE_KEYS.LAST_SESSION, newSession.id)
      setLastSessionForProject(projectId, newSession.id)

      logInfo('Session', `Branched at message ${index + 1}/${msgs.length} → ${newSession.id}`)
    },

    /**
     * Update session stats after message changes
     */
    updateSessionStats: (sessionId: string, deltaMessages: number, deltaTokens: number) => {
      setSessions((prev) =>
        prev.map((s) =>
          s.id === sessionId
            ? {
                ...s,
                messageCount: s.messageCount + deltaMessages,
                totalTokens: s.totalTokens + deltaTokens,
                updatedAt: Date.now(),
              }
            : s
        )
      )
    },

    // ========================================================================
    // Message Management
    // ========================================================================

    /**
     * Load messages for a session from database
     */
    loadSessionMessages: async (sessionId: string) => {
      setIsLoadingMessages(true)
      try {
        const dbMessages = await getMessages(sessionId)
        setMessages(dbMessages)
      } catch (err) {
        logError('Session', 'Failed to load messages', err)
        setMessages([])
      } finally {
        setIsLoadingMessages(false)
      }
    },

    /**
     * Add a message to the current session
     */
    addMessage: (message: Message) => {
      setMessages((prev) => [...prev, message])

      // Update session stats
      setSessions((prev) =>
        prev.map((s) =>
          s.id === message.sessionId
            ? {
                ...s,
                messageCount: s.messageCount + 1,
                totalTokens: s.totalTokens + (message.tokensUsed || 0),
                lastPreview: message.content.slice(0, 80),
                updatedAt: Date.now(),
              }
            : s
        )
      )
    },

    /**
     * Update a message's content (for streaming)
     */
    updateMessageContent: (id: string, content: string) => {
      setMessages((prev) => prev.map((msg) => (msg.id === id ? { ...msg, content } : msg)))
    },

    /**
     * Update a message's properties
     */
    updateMessage: (id: string, updates: Partial<Message>) => {
      setMessages((prev) => {
        const idx = prev.findIndex((msg) => msg.id === id)
        if (idx === -1) return prev // same ref = no re-render
        const next = prev.slice()
        next[idx] = { ...prev[idx], ...updates }
        return next
      })
    },

    /**
     * Set error on a specific message
     */
    setMessageError: (messageId: string, error: MessageError | null) => {
      setMessages((prev) =>
        prev.map((msg) => (msg.id === messageId ? { ...msg, error: error || undefined } : msg))
      )
    },

    /**
     * Delete a message (from signal + DB)
     */
    deleteMessage: async (id: string) => {
      setMessages((prev) => prev.filter((msg) => msg.id !== id))
      try {
        await dbDeleteMessage(id)
      } catch (err) {
        logError('Session', 'Failed to delete message from DB', err)
      }
    },

    /**
     * Delete all messages after a specific message (signal only)
     */
    deleteMessagesAfter: (messageId: string) => {
      setMessages((prev) => {
        const index = prev.findIndex((m) => m.id === messageId)
        return index >= 0 ? prev.slice(0, index + 1) : prev
      })
    },

    /**
     * Rollback conversation: delete a message and everything after it.
     * Removes from both reactive signal and database.
     */
    rollbackToMessage: async (messageId: string) => {
      const msgs = messages()
      const index = msgs.findIndex((m) => m.id === messageId)
      if (index === -1) return

      const target = msgs[index]
      const sessionId = target.sessionId
      const removedMessages = msgs.slice(index)

      // Update signal: keep everything before this message
      setMessages((prev) => prev.slice(0, index))

      // Update session stats
      const removedTokens = removedMessages.reduce((sum, m) => sum + (m.tokensUsed || 0), 0)
      setSessions((prev) =>
        prev.map((s) =>
          s.id === sessionId
            ? {
                ...s,
                messageCount: Math.max(0, s.messageCount - removedMessages.length),
                totalTokens: Math.max(0, s.totalTokens - removedTokens),
                updatedAt: Date.now(),
              }
            : s
        )
      )

      // Delete from database
      try {
        await dbDeleteMessagesFromTimestamp(sessionId, target.createdAt)
      } catch (err) {
        logError('Session', 'Failed to delete messages from DB', err)
      }
    },

    // ========================================================================
    // Agent Management
    // ========================================================================

    /**
     * Add a new agent to the session (signal + DB)
     */
    addAgent: (agent: Agent) => {
      setAgents((prev) => [...prev, agent])
      saveAgent(agent).catch((err) => logError('Session', 'Failed to save agent', err))
    },

    /**
     * Update an agent's properties (signal + DB)
     */
    updateAgent: (id: string, updates: Partial<Agent>) => {
      setAgents((prev) => prev.map((agent) => (agent.id === id ? { ...agent, ...updates } : agent)))
      dbUpdateAgent(id, updates).catch((err) =>
        logError('Session', 'Failed to update agent in DB', err)
      )
    },

    /**
     * Remove an agent from the session
     */
    removeAgent: (id: string) => {
      setAgents((prev) => prev.filter((agent) => agent.id !== id))
    },

    // ========================================================================
    // File Operations Management
    // ========================================================================

    /**
     * Add a file operation to the session
     */
    addFileOperation: async (operation: FileOperation) => {
      setFileOperations((prev) => [operation, ...prev])
      try {
        await saveFileOperation(operation)
      } catch (err) {
        logError('Session', 'Failed to save file operation', err)
      }
    },

    /**
     * Clear all file operations
     */
    clearFileOperations: async () => {
      const sessionId = currentSession()?.id
      setFileOperations([])
      if (sessionId) {
        try {
          await dbClearFileOperations(sessionId)
          clearVersionHistory(sessionId)
        } catch (err) {
          logError('Session', 'Failed to clear file operations', err)
        }
      }
    },

    /**
     * Undo the last file change in this session.
     * Returns the affected file path, or null if nothing to undo.
     */
    undoFileChange: async (): Promise<string | null> => {
      const sessionId = currentSession()?.id
      if (!sessionId) return null

      try {
        const result = await undoFileChange(sessionId, readFileContent)
        if (!result) return null

        // Write the reverted content using Tauri FS
        const fs = await import('@tauri-apps/plugin-fs')
        await fs.writeTextFile(result.filePath, result.content)
        logInfo('Session', 'Undid file change', { filePath: result.filePath })
        return result.filePath
      } catch (err) {
        logError('Session', 'Failed to undo file change', err)
        return null
      }
    },

    /**
     * Redo the last undone file change.
     * Returns the affected file path, or null if nothing to redo.
     */
    redoFileChange: async (): Promise<string | null> => {
      const sessionId = currentSession()?.id
      if (!sessionId) return null

      try {
        const result = await redoFileChange(sessionId, readFileContent)
        if (!result) return null

        const fs = await import('@tauri-apps/plugin-fs')
        await fs.writeTextFile(result.filePath, result.content)
        logInfo('Session', 'Redid file change', { filePath: result.filePath })
        return result.filePath
      } catch (err) {
        logError('Session', 'Failed to redo file change', err)
        return null
      }
    },

    /** Get undo/redo counts for current session */
    getVersionCounts: () => {
      const sessionId = currentSession()?.id
      if (!sessionId) return { undoCount: 0, redoCount: 0 }
      return getVersionCounts(sessionId)
    },

    // ========================================================================
    // Terminal Executions Management
    // ========================================================================

    /**
     * Add a terminal execution to the session
     */
    addTerminalExecution: async (execution: TerminalExecution) => {
      setTerminalExecutions((prev) => [execution, ...prev])
      try {
        await saveTerminalExecution(execution)
      } catch (err) {
        logError('Session', 'Failed to save terminal execution', err)
      }
    },

    /**
     * Update a terminal execution (e.g., when it completes)
     */
    updateTerminalExecution: async (id: string, updates: Partial<TerminalExecution>) => {
      setTerminalExecutions((prev) =>
        prev.map((exec) => (exec.id === id ? { ...exec, ...updates } : exec))
      )
      try {
        await dbUpdateTerminalExecution(id, updates)
      } catch (err) {
        logError('Session', 'Failed to update terminal execution', err)
      }
    },

    /**
     * Clear all terminal executions
     */
    clearTerminalExecutions: async () => {
      const sessionId = currentSession()?.id
      setTerminalExecutions([])
      if (sessionId) {
        try {
          await dbClearTerminalExecutions(sessionId)
        } catch (err) {
          logError('Session', 'Failed to clear terminal executions', err)
        }
      }
    },

    // ========================================================================
    // Memory Items Management
    // ========================================================================

    /**
     * Add a memory item to the session
     */
    addMemoryItem: async (item: MemoryItem) => {
      setMemoryItems((prev) => [item, ...prev])
      try {
        await saveMemoryItem(item)
      } catch (err) {
        logError('Session', 'Failed to save memory item', err)
      }
    },

    /**
     * Remove a memory item
     */
    removeMemoryItem: async (id: string) => {
      setMemoryItems((prev) => prev.filter((item) => item.id !== id))
      try {
        await dbDeleteMemoryItem(id)
      } catch (err) {
        logError('Session', 'Failed to delete memory item', err)
      }
    },

    /**
     * Clear all memory items
     */
    clearMemoryItems: async () => {
      const sessionId = currentSession()?.id
      setMemoryItems([])
      if (sessionId) {
        try {
          await dbClearMemoryItems(sessionId)
        } catch (err) {
          logError('Session', 'Failed to clear memory items', err)
        }
      }
    },

    /**
     * Query memory items across all sessions, optionally filtered by project.
     */
    queryMemoriesAcrossSessions: async (projectId?: string) => {
      try {
        return await getAllMemoryItems(projectId)
      } catch (err) {
        logError('Session', 'Failed to query cross-session memories', err)
        return []
      }
    },

    // ========================================================================
    // Checkpoints
    // ========================================================================
    checkpoints,

    /**
     * Create a checkpoint snapshot of the current conversation
     */
    createCheckpoint: async (description: string): Promise<string | null> => {
      const sess = currentSession()
      if (!sess) return null
      const id = `ckpt-${Date.now()}-${Math.random().toString(36).slice(2, 6)}`
      const snapshot = {
        messages: messages().map((m) => ({
          id: m.id,
          role: m.role,
          content: m.content,
          tokensUsed: m.tokensUsed,
          costUSD: m.costUSD,
          model: m.model,
          metadata: m.metadata,
        })),
      }
      await saveMemoryItem({
        id,
        sessionId: sess.id,
        type: 'checkpoint',
        title: description,
        preview: JSON.stringify(snapshot),
        tokens: 0,
        createdAt: Date.now(),
      })
      setCheckpoints((prev) => [
        ...prev,
        { id, timestamp: Date.now(), description, messageCount: messages().length },
      ])
      return id
    },

    /**
     * Rollback conversation to a checkpoint — restores both in-memory and DB state
     */
    rollbackToCheckpoint: async (checkpointId: string): Promise<boolean> => {
      const item = memoryItems().find((m) => m.id === checkpointId)
      if (!item) return false
      const sess = currentSession()
      if (!sess) return false
      try {
        const data = JSON.parse(item.preview) as {
          messages: Array<{
            id: string
            role: string
            content: string
            tokensUsed?: number
            costUSD?: number
            model?: string
            metadata?: Record<string, unknown>
          }>
        }
        const restored = data.messages.map((m) => ({
          ...m,
          sessionId: sess.id,
          createdAt: Date.now(),
          role: m.role as Message['role'],
        })) as Message[]
        // Update in-memory state
        setMessages(restored)
        // Sync database: delete all existing messages, re-insert snapshot
        await dbDeleteSessionMessages(sess.id)
        await dbInsertMessages(restored)
        return true
      } catch {
        return false
      }
    },

    /**
     * Revert file changes made after a given message.
     * Looks at file operations after the target message's timestamp
     * and writes back original content for each.
     */
    revertFilesAfter: async (messageId: string): Promise<number> => {
      const msgs = messages()
      const index = msgs.findIndex((m) => m.id === messageId)
      if (index === -1) return 0

      const targetTimestamp = msgs[index].createdAt
      const ops = fileOperations().filter(
        (op) => op.timestamp > targetTimestamp && op.originalContent
      )

      if (ops.length === 0) return 0

      let reverted = 0
      try {
        const fs = await import('@tauri-apps/plugin-fs')
        for (const op of ops) {
          if (op.originalContent) {
            try {
              await fs.writeTextFile(op.filePath, op.originalContent)
              reverted++
            } catch (err) {
              logError('Session', `Failed to revert ${op.filePath}`, err)
            }
          }
        }
      } catch (err) {
        logError('Session', 'Failed to import Tauri FS for revert', err)
      }
      return reverted
    },

    // ========================================================================
    // Read-Only File Context
    // ========================================================================

    /** Files marked as read-only context */
    readOnlyFiles,

    /** Toggle a file's read-only status */
    toggleReadOnly: (filePath: string) => {
      setReadOnlyFiles((prev) =>
        prev.includes(filePath) ? prev.filter((f) => f !== filePath) : [...prev, filePath]
      )
    },

    /** Check if a file is marked as read-only */
    isReadOnly: (filePath: string): boolean => readOnlyFiles().includes(filePath),

    // ========================================================================
    // Background Plan Execution
    // ========================================================================

    /** Whether a plan is running in the background */
    backgroundPlanActive,

    /** Progress text for the background plan */
    backgroundPlanProgress,

    /** Start background plan execution */
    startBackgroundPlan: () => {
      setBackgroundPlanActive(true)
      setBackgroundPlanProgress('Plan running...')
    },

    /** Update background plan progress text */
    updateBackgroundPlanProgress: (text: string) => {
      setBackgroundPlanProgress(text)
    },

    /** Stop background plan execution */
    stopBackgroundPlan: () => {
      setBackgroundPlanActive(false)
      setBackgroundPlanProgress('')
    },

    // ========================================================================
    // UI State Management
    // ========================================================================

    /**
     * Start editing a message
     */
    startEditing: (id: string) => setEditingMessageId(id),

    /**
     * Stop editing
     */
    stopEditing: () => setEditingMessageId(null),

    /**
     * Set the message being retried
     */
    setRetryingMessageId,

    // ========================================================================
    // Session State Management
    // ========================================================================

    /**
     * Clear all session state
     */
    clearSession: () => {
      setCurrentSession(null)
      setMessages([])
      setAgents([])
      setFileOperations([])
      setTerminalExecutions([])
      setMemoryItems([])
      setEditingMessageId(null)
      setRetryingMessageId(null)
      setBackgroundPlanActive(false)
      setBackgroundPlanProgress('')
    },

    /**
     * Get the last session ID from localStorage
     */
    getLastSessionId: (): string | null => {
      return localStorage.getItem(STORAGE_KEYS.LAST_SESSION)
    },

    /**
     * Get the last session ID for a specific project
     */
    getLastSessionForProject: (projectId: string | null | undefined): string | null => {
      return getLastSessionForProject(projectId)
    },
  }

  return store
}
