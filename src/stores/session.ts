/**
 * Session Store
 * Global state management for sessions, messages, and agents
 */

import { createMemo, createSignal } from 'solid-js'
import { DEFAULTS, STORAGE_KEYS } from '../config/constants'
import {
  archiveSession as dbArchiveSession,
  createSession as dbCreateSession,
  deleteSession as dbDeleteSession,
  updateSession as dbUpdateSession,
  getMessages,
  getSessionsWithStats,
} from '../services/database'
import type {
  Agent,
  Message,
  MessageError,
  Session,
  SessionTokenStats,
  SessionWithStats,
} from '../types'

// ============================================================================
// Tab Navigation
// ============================================================================

export type TabId = 'chat' | 'agents' | 'files' | 'memory' | 'terminal'

const [activeTab, setActiveTab] = createSignal<TabId>('chat')

export { activeTab, setActiveTab }

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

// Selected model for chat
const [selectedModel, setSelectedModel] = createSignal(DEFAULTS.MODEL)

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
    }),
    { total: 0, count: 0 }
  )
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
    selectedModel,
    setSelectedModel,
    retryingMessageId,
    editingMessageId,
    sessionTokenStats,

    // ========================================================================
    // Session List Management
    // ========================================================================

    /**
     * Load all sessions from database
     */
    loadAllSessions: async () => {
      setIsLoadingSessions(true)
      try {
        const dbSessions = await getSessionsWithStats()
        setSessions(dbSessions)
      } catch (err) {
        console.error('Failed to load sessions:', err)
        setSessions([])
      } finally {
        setIsLoadingSessions(false)
      }
    },

    /**
     * Create a new session and switch to it
     */
    createNewSession: async (name?: string): Promise<Session> => {
      const session = await dbCreateSession(name || DEFAULTS.SESSION_NAME)
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
        console.warn(`Session ${id} not found`)
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
      } catch (err) {
        console.error('Failed to load messages:', err)
        setMessages([])
      } finally {
        setIsLoadingMessages(false)
      }

      // Clear agents (would load from DB in future)
      setAgents([])

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
        console.error('Failed to load messages:', err)
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
     * Delete a message
     */
    deleteMessage: (id: string) => {
      setMessages((prev) => prev.filter((msg) => msg.id !== id))
    },

    /**
     * Delete all messages after a specific message
     */
    deleteMessagesAfter: (messageId: string) => {
      setMessages((prev) => {
        const index = prev.findIndex((m) => m.id === messageId)
        return index >= 0 ? prev.slice(0, index + 1) : prev
      })
    },

    // ========================================================================
    // Agent Management
    // ========================================================================

    /**
     * Update an agent's properties
     */
    updateAgent: (id: string, updates: Partial<Agent>) => {
      setAgents((prev) => prev.map((agent) => (agent.id === id ? { ...agent, ...updates } : agent)))
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
