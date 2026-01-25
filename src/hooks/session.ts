/**
 * Delta9 Session Lifecycle Hooks
 *
 * Factory + Closure pattern from oh-my-opencode for managing session state.
 * Handles context compaction recovery and session cleanup via the event handler.
 */

import type { MissionState } from '../mission/state.js'
import { loadConfig, clearConfigCache } from '../lib/config.js'
import { appendHistory } from '../mission/history.js'

// =============================================================================
// Session State
// =============================================================================

/** Per-session state tracking */
interface SessionStateEntry {
  /** Session ID */
  id: string
  /** When session was created */
  createdAt: string
  /** Last activity timestamp */
  lastActivityAt: string
  /** Task IDs dispatched in this session */
  dispatchedTasks: string[]
  /** Background task IDs for this session */
  backgroundTasks: string[]
  /** Whether session has been compacted */
  wasCompacted: boolean
}

/** Map of session ID to session state */
const sessionStateMap = new Map<string, SessionStateEntry>()

// =============================================================================
// Types
// =============================================================================

export interface SessionHooksInput {
  /** Mission state instance */
  state: MissionState
  /** Project root directory */
  cwd: string
  /** Logger function */
  log: (level: string, message: string, data?: Record<string, unknown>) => void
}

/** Event input from OpenCode */
export interface EventInput {
  event: {
    type: string
    properties?: Record<string, unknown>
  }
}

export type SessionEventHandler = (input: EventInput) => Promise<void>

// =============================================================================
// Hook Factory
// =============================================================================

/**
 * Create session lifecycle event handler with closure over state
 *
 * Pattern from oh-my-opencode: Factory + Closure for stateful hooks.
 * Session events come through the generic event handler.
 */
export function createSessionEventHandler(input: SessionHooksInput): SessionEventHandler {
  const { state, cwd, log } = input

  return async ({ event }) => {
    const props = event.properties as Record<string, unknown> | undefined

    switch (event.type) {
      case 'session.created':
      case 'session.updated': {
        const info = props?.info as { id?: string } | undefined
        const sessionId = info?.id as string | undefined
        if (!sessionId) return

        const now = new Date().toISOString()

        // Initialize or update session state
        if (!sessionStateMap.has(sessionId)) {
          sessionStateMap.set(sessionId, {
            id: sessionId,
            createdAt: now,
            lastActivityAt: now,
            dispatchedTasks: [],
            backgroundTasks: [],
            wasCompacted: false,
          })
        }

        // Reload mission state from disk (source of truth)
        const mission = state.load()
        if (mission) {
          log('info', 'Session event, mission state loaded', {
            event: event.type,
            sessionId,
            missionId: mission.id,
            status: mission.status,
          })
        } else {
          log('debug', 'Session event, no existing mission', { event: event.type, sessionId })
        }
        break
      }

      case 'session.compacted': {
        const sessionId = props?.sessionID as string | undefined
        if (!sessionId) return

        // Mark session as compacted
        const sessionState = sessionStateMap.get(sessionId)
        if (sessionState) {
          sessionState.wasCompacted = true
          sessionState.lastActivityAt = new Date().toISOString()
        }

        // CRITICAL: Reload mission state from disk after compaction
        const mission = state.load()

        // Also reload config in case it changed
        clearConfigCache()
        loadConfig(cwd)

        if (mission) {
          log('info', 'Session compacted, mission state reloaded', {
            sessionId,
            missionId: mission.id,
            status: mission.status,
            progress: state.getProgress(),
          })

          // Log compaction event in history
          appendHistory(cwd, {
            type: 'context_compacted',
            timestamp: new Date().toISOString(),
            missionId: mission.id,
            data: { sessionId },
          })
        } else {
          log('debug', 'Session compacted, no mission to reload', { sessionId })
        }
        break
      }

      case 'session.deleted': {
        const info = props?.info as { id?: string } | undefined
        const sessionId = info?.id as string | undefined
        if (!sessionId) return

        const sessionState = sessionStateMap.get(sessionId)

        if (sessionState) {
          log('debug', 'Session deleted, cleaning up', {
            sessionId,
            dispatchedTasks: sessionState.dispatchedTasks.length,
            backgroundTasks: sessionState.backgroundTasks.length,
            wasCompacted: sessionState.wasCompacted,
          })

          // Clean up session state
          sessionStateMap.delete(sessionId)
        }
        break
      }

      case 'session.resumed': {
        const sessionId = props?.sessionID as string | undefined
        if (!sessionId) return

        // Reload mission state
        const mission = state.load()

        if (mission) {
          log('info', 'Session resumed, mission state loaded', {
            sessionId,
            missionId: mission.id,
            status: mission.status,
          })
        }

        // Update or create session state
        const now = new Date().toISOString()
        const existing = sessionStateMap.get(sessionId)

        if (existing) {
          existing.lastActivityAt = now
        } else {
          sessionStateMap.set(sessionId, {
            id: sessionId,
            createdAt: now,
            lastActivityAt: now,
            dispatchedTasks: [],
            backgroundTasks: [],
            wasCompacted: false,
          })
        }
        break
      }

      case 'session.idle': {
        // Could be used for auto-save or cleanup
        const sessionId = props?.sessionID as string | undefined
        if (sessionId) {
          const sessionState = sessionStateMap.get(sessionId)
          if (sessionState) {
            sessionState.lastActivityAt = new Date().toISOString()
          }
        }
        break
      }
    }
  }
}

// =============================================================================
// Session State Accessors
// =============================================================================

/**
 * Get session state for a session ID
 */
export function getSessionState(sessionId: string): SessionStateEntry | undefined {
  return sessionStateMap.get(sessionId)
}

/**
 * Track a dispatched task in session state
 */
export function trackDispatchedTask(sessionId: string, taskId: string): void {
  const state = sessionStateMap.get(sessionId)
  if (state) {
    state.dispatchedTasks.push(taskId)
    state.lastActivityAt = new Date().toISOString()
  }
}

/**
 * Track a background task in session state
 */
export function trackBackgroundTask(sessionId: string, backgroundTaskId: string): void {
  const state = sessionStateMap.get(sessionId)
  if (state) {
    state.backgroundTasks.push(backgroundTaskId)
    state.lastActivityAt = new Date().toISOString()
  }
}

/**
 * Get all active session IDs
 */
export function getActiveSessions(): string[] {
  return Array.from(sessionStateMap.keys())
}

/**
 * Clear all session state (for testing)
 */
export function clearAllSessionState(): void {
  sessionStateMap.clear()
}
