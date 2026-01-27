/**
 * Delta9 Session State Manager
 *
 * Tracks session states (active/idle) and enables auto-resumption
 * when messages arrive for idle agents.
 *
 * Key Features:
 * - Session state tracking (active, idle, completed)
 * - Resume callback registration
 * - Auto-wake on message arrival
 * - Integration with MessageStore events
 */

import type { MessageEvent, MessageEventListener } from './types.js'
import { getNamedLogger } from '../lib/logger.js'

// Logger for session state (silent in TUI mode)
const log = getNamedLogger('session')

// =============================================================================
// Types
// =============================================================================

export type SessionState = 'active' | 'idle' | 'completed'

export interface SessionInfo {
  /** Session ID */
  sessionId: string

  /** Agent ID owning this session */
  agentId: string

  /** Current state */
  state: SessionState

  /** When session was created */
  createdAt: string

  /** When state last changed */
  lastStateChange: string

  /** Count of messages received while idle */
  pendingMessages: number

  /** Last activity timestamp */
  lastActivity: string
}

export interface ResumeCallback {
  /** Callback to invoke when resuming */
  onResume: (sessionId: string, reason: ResumeReason) => void | Promise<void>

  /** Optional filter for which agents to resume */
  agentFilter?: string[]
}

export interface ResumeReason {
  /** Why the session is being resumed */
  type: 'new_message' | 'pending_messages' | 'manual'

  /** Message ID if applicable */
  messageId?: string

  /** Sender if applicable */
  from?: string

  /** Subject if applicable */
  subject?: string
}

export interface SessionStateConfig {
  /** Auto-resume idle sessions on new messages */
  autoResume?: boolean

  /** Delay before auto-resuming (ms) - allows batching */
  resumeDelayMs?: number

  /** Max idle time before session is considered stale (ms) */
  maxIdleTimeMs?: number
}

// =============================================================================
// Constants
// =============================================================================

const DEFAULT_CONFIG: Required<SessionStateConfig> = {
  autoResume: true,
  resumeDelayMs: 100, // Small delay to batch rapid messages
  maxIdleTimeMs: 30 * 60 * 1000, // 30 minutes
}

// =============================================================================
// Session State Manager
// =============================================================================

export class SessionStateManager {
  private sessions = new Map<string, SessionInfo>()
  private agentToSession = new Map<string, string>() // agentId -> sessionId
  private resumeCallbacks = new Set<ResumeCallback>()
  private pendingResumes = new Map<string, ReturnType<typeof setTimeout>>()
  private config: Required<SessionStateConfig>

  constructor(config?: SessionStateConfig) {
    this.config = { ...DEFAULT_CONFIG, ...config }
  }

  // ===========================================================================
  // Session Registration
  // ===========================================================================

  /**
   * Register a new session
   */
  registerSession(sessionId: string, agentId: string): SessionInfo {
    const now = new Date().toISOString()

    const session: SessionInfo = {
      sessionId,
      agentId,
      state: 'active',
      createdAt: now,
      lastStateChange: now,
      pendingMessages: 0,
      lastActivity: now,
    }

    this.sessions.set(sessionId, session)
    this.agentToSession.set(agentId, sessionId)

    return session
  }

  /**
   * Unregister a session
   */
  unregisterSession(sessionId: string): void {
    const session = this.sessions.get(sessionId)
    if (session) {
      this.agentToSession.delete(session.agentId)
    }
    this.sessions.delete(sessionId)

    // Cancel any pending resume
    const pendingResume = this.pendingResumes.get(sessionId)
    if (pendingResume) {
      clearTimeout(pendingResume)
      this.pendingResumes.delete(sessionId)
    }
  }

  // ===========================================================================
  // State Management
  // ===========================================================================

  /**
   * Set session state
   */
  setState(sessionId: string, state: SessionState): void {
    const session = this.sessions.get(sessionId)
    if (!session) return

    const previousState = session.state
    session.state = state
    session.lastStateChange = new Date().toISOString()

    // If transitioning to idle, don't clear pending messages
    // If transitioning to active, clear pending messages
    if (state === 'active') {
      session.pendingMessages = 0
      session.lastActivity = new Date().toISOString()
    }

    // Cancel pending resume if becoming active
    if (state === 'active' || state === 'completed') {
      const pendingResume = this.pendingResumes.get(sessionId)
      if (pendingResume) {
        clearTimeout(pendingResume)
        this.pendingResumes.delete(sessionId)
      }
    }

    // Emit state change for debugging
    if (previousState !== state) {
      log.info(`[session] Session ${sessionId} state: ${previousState} -> ${state}`)
    }
  }

  /**
   * Mark session as idle
   */
  markIdle(sessionId: string): void {
    this.setState(sessionId, 'idle')
  }

  /**
   * Mark session as active
   */
  markActive(sessionId: string): void {
    this.setState(sessionId, 'active')
  }

  /**
   * Mark session as completed
   */
  markCompleted(sessionId: string): void {
    this.setState(sessionId, 'completed')
  }

  /**
   * Record activity (keeps session alive)
   */
  recordActivity(sessionId: string): void {
    const session = this.sessions.get(sessionId)
    if (session) {
      session.lastActivity = new Date().toISOString()
    }
  }

  // ===========================================================================
  // Query
  // ===========================================================================

  /**
   * Get session info
   */
  getSession(sessionId: string): SessionInfo | null {
    return this.sessions.get(sessionId) || null
  }

  /**
   * Get session by agent ID
   */
  getSessionByAgent(agentId: string): SessionInfo | null {
    const sessionId = this.agentToSession.get(agentId)
    if (!sessionId) return null
    return this.sessions.get(sessionId) || null
  }

  /**
   * Get all sessions
   */
  listSessions(filter?: { state?: SessionState }): SessionInfo[] {
    let sessions = Array.from(this.sessions.values())

    if (filter?.state) {
      sessions = sessions.filter((s) => s.state === filter.state)
    }

    return sessions
  }

  /**
   * Get idle sessions with pending messages
   */
  getIdleSessionsWithPendingMessages(): SessionInfo[] {
    return this.listSessions({ state: 'idle' }).filter((s) => s.pendingMessages > 0)
  }

  // ===========================================================================
  // Resume Callbacks
  // ===========================================================================

  /**
   * Register a resume callback
   */
  onResume(callback: ResumeCallback): () => void {
    this.resumeCallbacks.add(callback)
    return () => this.resumeCallbacks.delete(callback)
  }

  /**
   * Manually trigger resume for a session
   */
  async triggerResume(sessionId: string, reason?: ResumeReason): Promise<boolean> {
    const session = this.sessions.get(sessionId)
    if (!session || session.state !== 'idle') {
      return false
    }

    const resumeReason: ResumeReason = reason || { type: 'manual' }

    // Invoke callbacks
    const callbacks = Array.from(this.resumeCallbacks)
    for (const callback of callbacks) {
      // Check agent filter
      if (callback.agentFilter && !callback.agentFilter.includes(session.agentId)) {
        continue
      }

      try {
        await callback.onResume(sessionId, resumeReason)
      } catch (error) {
        log.error(`[session] Resume callback error for ${sessionId}`, { error: String(error) })
      }
    }

    // Mark as active after resume
    this.markActive(sessionId)

    return true
  }

  // ===========================================================================
  // Message Integration
  // ===========================================================================

  /**
   * Create a message event listener for auto-resume
   */
  createMessageListener(): MessageEventListener {
    return (event: MessageEvent) => {
      if (event.type !== 'sent') return

      // Find sessions for recipients
      const recipients = this.resolveRecipients(event.to)

      for (const agentId of recipients) {
        const session = this.getSessionByAgent(agentId)
        if (!session) continue

        // Track pending messages for idle sessions
        if (session.state === 'idle') {
          session.pendingMessages++

          // Schedule auto-resume if enabled
          if (this.config.autoResume) {
            this.scheduleResume(session.sessionId, {
              type: 'new_message',
              messageId: event.messageId,
              from: event.from,
              subject: event.subject,
            })
          }
        }
      }
    }
  }

  /**
   * Schedule a resume with debouncing
   */
  private scheduleResume(sessionId: string, reason: ResumeReason): void {
    // Cancel any existing pending resume
    const existing = this.pendingResumes.get(sessionId)
    if (existing) {
      clearTimeout(existing)
    }

    // Schedule new resume
    const timeout = setTimeout(async () => {
      this.pendingResumes.delete(sessionId)
      await this.triggerResume(sessionId, reason)
    }, this.config.resumeDelayMs)

    this.pendingResumes.set(sessionId, timeout)
  }

  /**
   * Resolve recipient string to agent IDs
   */
  private resolveRecipients(to: string): string[] {
    // Handle group recipients
    switch (to) {
      case 'broadcast':
        return Array.from(this.agentToSession.keys())
      case 'council':
        return ['oracle_claude', 'oracle_gpt', 'oracle_gemini', 'oracle_deepseek']
      case 'operators':
        return ['operator', 'operator_complex', 'ui_ops', 'scribe']
      case 'support':
        return ['scout', 'intel', 'explorer']
      default:
        // Single agent or comma-separated list
        return to.split(',').map((s) => s.trim())
    }
  }

  // ===========================================================================
  // Cleanup
  // ===========================================================================

  /**
   * Clean up stale sessions
   */
  cleanupStale(): number {
    const now = Date.now()
    let cleaned = 0

    for (const [sessionId, session] of this.sessions) {
      const lastActivity = new Date(session.lastActivity).getTime()
      const idleTime = now - lastActivity

      if (idleTime > this.config.maxIdleTimeMs && session.state === 'idle') {
        this.unregisterSession(sessionId)
        cleaned++
      }
    }

    return cleaned
  }

  /**
   * Shutdown manager
   */
  shutdown(): void {
    // Cancel all pending resumes
    for (const timeout of this.pendingResumes.values()) {
      clearTimeout(timeout)
    }
    this.pendingResumes.clear()

    // Clear state
    this.sessions.clear()
    this.agentToSession.clear()
    this.resumeCallbacks.clear()
  }
}

// =============================================================================
// Singleton
// =============================================================================

let globalSessionStateManager: SessionStateManager | null = null

export function getSessionStateManager(config?: SessionStateConfig): SessionStateManager {
  if (!globalSessionStateManager) {
    globalSessionStateManager = new SessionStateManager(config)
  }
  return globalSessionStateManager
}

export function resetSessionStateManager(): void {
  if (globalSessionStateManager) {
    globalSessionStateManager.shutdown()
    globalSessionStateManager = null
  }
}
