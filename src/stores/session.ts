/**
 * Session Store
 * Global state management for sessions, messages, and agents
 */

import { createMemo, createSignal } from 'solid-js'
import { DEFAULTS, STORAGE_KEYS } from '../config/constants'
import { getCoreTracker } from '../services/core-bridge'
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
  insertMessages as dbInsertMessages,
  updateAgentInDb as dbUpdateAgent,
  updateSession as dbUpdateSession,
  updateTerminalExecution as dbUpdateTerminalExecution,
  getAgents,
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

// ============================================================================
// Session State
// ============================================================================

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

// Selected model for chat
const [selectedModel, setSelectedModel] = createSignal<string>(DEFAULTS.MODEL)

// Checkpoints
const [checkpoints, setCheckpoints] = createSignal<
  Array<{ id: string; timestamp: number; description: string; messageCount: number }>
>([])

// UI state
const [retryingMessageId, setRetryingMessageId] = createSignal<string | null>(null)
const [editingMessageId, setEditingMessageId] = createSignal<string | null>(null)

// ============================================================================
// Computed Values
// ============================================================================

// Session token statistics
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

// Context window usage — uses core tracker when available, falls back to rough estimate
const DEFAULT_CONTEXT_WINDOW = 200000
const contextUsage = createMemo(() => {
  const tracker = getCoreTracker()
  if (tracker) {
    const s = tracker.getStats()
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

// ============================================================================
// Session Store Hook
// ============================================================================

export function useSession() {
  return {
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
    setSelectedModel,
    retryingMessageId,
    editingMessageId,
    sessionTokenStats,
    contextUsage,
    agentStats,

    // ========================================================================
    // Session List Management
    // ========================================================================

    /**
     * Load all sessions from database
     * Filters by current project if one is selected
     */
    loadAllSessions: async () => {
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
     * Create a new session and switch to it
     * Automatically assigns to current project if one is selected
     */
    createNewSession: async (name?: string): Promise<Session> => {
      const { currentProject } = useProject()
      const projectId = currentProject()?.id

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

      logInfo('session', 'Session created', { id: session.id })

      // Persist last session
      localStorage.setItem(STORAGE_KEYS.LAST_SESSION, session.id)

      return session
    },

    /**
     * Switch to a different session
     */
    switchSession: async (id: string): Promise<void> => {
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
        logInfo('session', 'Session switched', { id, messageCount: dbMessages.length })
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

      // Persist last session
      localStorage.setItem(STORAGE_KEYS.LAST_SESSION, id)
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
    },

    /**
     * Archive a session (soft delete)
     */
    archiveSession: async (id: string): Promise<void> => {
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
        } else {
          // Create new session
          const newSession = await dbCreateSession(DEFAULTS.SESSION_NAME)
          const sessionWithStats: SessionWithStats = {
            ...newSession,
            messageCount: 0,
            totalTokens: 0,
          }
          setSessions([sessionWithStats])
          setCurrentSession(newSession)
          setMessages([])
          localStorage.setItem(STORAGE_KEYS.LAST_SESSION, newSession.id)
        }
      }
    },

    /**
     * Delete a session permanently
     */
    deleteSessionPermanently: async (id: string): Promise<void> => {
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
        } else {
          const newSession = await dbCreateSession(DEFAULTS.SESSION_NAME)
          const sessionWithStats: SessionWithStats = {
            ...newSession,
            messageCount: 0,
            totalTokens: 0,
          }
          setSessions([sessionWithStats])
          setCurrentSession(newSession)
          setMessages([])
          localStorage.setItem(STORAGE_KEYS.LAST_SESSION, newSession.id)
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
      const newSession = await dbCreateSession(forkName, projectId)

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
      setMessages((prev) => prev.map((msg) => (msg.id === id ? { ...msg, ...updates } : msg)))
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
        } catch (err) {
          logError('Session', 'Failed to clear file operations', err)
        }
      }
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
    },

    /**
     * Get the last session ID from localStorage
     */
    getLastSessionId: (): string | null => {
      return localStorage.getItem(STORAGE_KEYS.LAST_SESSION)
    },
  }
}
