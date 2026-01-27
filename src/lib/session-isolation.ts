/**
 * Delta9 Session Isolation Manager
 *
 * Tracks session hierarchies and isolates background tasks by root session:
 * - Maps each session to its root session
 * - Filters background tasks/agents by root session
 * - Provides cleanup when root session ends
 *
 * Pattern from: oh-my-opencode sessionToRootId
 *
 * Session Tree Example:
 * ```
 * root-1 (user session)
 *   ├── agent-1 (operator)
 *   ├── agent-2 (operator)
 *   │   └── agent-3 (sub-operator)
 *   └── agent-4 (validator)
 *
 * root-2 (different user session)
 *   └── agent-5 (operator)
 * ```
 *
 * Agents from root-1 cannot see agents from root-2.
 */

import { getNamedLogger } from './logger.js'

const log = getNamedLogger('session-isolation')

// =============================================================================
// Types
// =============================================================================

export interface SessionInfo {
  /** Session ID */
  id: string
  /** Parent session ID (null for root sessions) */
  parentId: string | null
  /** Root session ID (same as id for root sessions) */
  rootId: string
  /** Session creation timestamp */
  createdAt: number
  /** Session metadata */
  metadata?: Record<string, unknown>
}

export interface SessionStats {
  /** Total sessions tracked */
  totalSessions: number
  /** Number of root sessions */
  rootSessions: number
  /** Sessions per root */
  sessionsPerRoot: Map<string, number>
}

// =============================================================================
// Session Isolation Manager
// =============================================================================

export class SessionIsolationManager {
  /** Map: sessionId -> SessionInfo */
  private sessions: Map<string, SessionInfo> = new Map()
  /** Map: sessionId -> rootSessionId (for fast lookup) */
  private sessionToRoot: Map<string, string> = new Map()
  /** Map: rootSessionId -> Set<sessionId> (all sessions in tree) */
  private rootToSessions: Map<string, Set<string>> = new Map()

  // ===========================================================================
  // Session Registration
  // ===========================================================================

  /**
   * Register a new session
   *
   * @param sessionId - The session ID
   * @param parentSessionId - Parent session ID (null/undefined for root sessions)
   * @param metadata - Optional session metadata
   */
  registerSession(
    sessionId: string,
    parentSessionId?: string | null,
    metadata?: Record<string, unknown>
  ): SessionInfo {
    // If already registered, return existing
    const existing = this.sessions.get(sessionId)
    if (existing) {
      log.debug(`Session ${sessionId} already registered`)
      return existing
    }

    // Determine root session
    let rootId: string
    if (parentSessionId) {
      // Look up parent's root
      const parentRoot = this.sessionToRoot.get(parentSessionId)
      rootId = parentRoot ?? parentSessionId
    } else {
      // This is a root session
      rootId = sessionId
    }

    const info: SessionInfo = {
      id: sessionId,
      parentId: parentSessionId ?? null,
      rootId,
      createdAt: Date.now(),
      metadata,
    }

    // Store session info
    this.sessions.set(sessionId, info)
    this.sessionToRoot.set(sessionId, rootId)

    // Add to root's session set
    const rootSessions = this.rootToSessions.get(rootId) ?? new Set()
    rootSessions.add(sessionId)
    this.rootToSessions.set(rootId, rootSessions)

    log.debug(`Registered session ${sessionId} under root ${rootId}`)

    return info
  }

  /**
   * Unregister a session
   *
   * Note: This does NOT cascade to child sessions.
   * Use cleanup() for full tree cleanup.
   */
  unregisterSession(sessionId: string): boolean {
    const info = this.sessions.get(sessionId)
    if (!info) return false

    // Remove from maps
    this.sessions.delete(sessionId)
    this.sessionToRoot.delete(sessionId)

    // Remove from root's session set
    const rootSessions = this.rootToSessions.get(info.rootId)
    if (rootSessions) {
      rootSessions.delete(sessionId)
      if (rootSessions.size === 0) {
        this.rootToSessions.delete(info.rootId)
      }
    }

    log.debug(`Unregistered session ${sessionId}`)
    return true
  }

  // ===========================================================================
  // Root Session Lookup
  // ===========================================================================

  /**
   * Get the root session for any session
   *
   * Returns the session ID itself if it's a root session.
   */
  getRootSession(sessionId: string): string | null {
    return this.sessionToRoot.get(sessionId) ?? null
  }

  /**
   * Check if a session is a root session
   */
  isRootSession(sessionId: string): boolean {
    const rootId = this.sessionToRoot.get(sessionId)
    return rootId === sessionId
  }

  /**
   * Get session info
   */
  getSession(sessionId: string): SessionInfo | null {
    return this.sessions.get(sessionId) ?? null
  }

  // ===========================================================================
  // Session Tree Operations
  // ===========================================================================

  /**
   * Get all sessions in a root session's tree
   */
  getSessionsInTree(rootSessionId: string): string[] {
    const sessions = this.rootToSessions.get(rootSessionId)
    return sessions ? Array.from(sessions) : []
  }

  /**
   * Check if two sessions are in the same tree
   */
  isInSameTree(sessionA: string, sessionB: string): boolean {
    const rootA = this.sessionToRoot.get(sessionA)
    const rootB = this.sessionToRoot.get(sessionB)
    return rootA !== undefined && rootA === rootB
  }

  /**
   * Get direct children of a session
   */
  getChildren(sessionId: string): string[] {
    const children: string[] = []
    for (const [id, info] of this.sessions) {
      if (info.parentId === sessionId) {
        children.push(id)
      }
    }
    return children
  }

  /**
   * Get all descendants of a session (recursive children)
   */
  getDescendants(sessionId: string): string[] {
    const descendants: string[] = []
    const queue = this.getChildren(sessionId)

    while (queue.length > 0) {
      const childId = queue.shift()!
      descendants.push(childId)
      queue.push(...this.getChildren(childId))
    }

    return descendants
  }

  // ===========================================================================
  // Cleanup
  // ===========================================================================

  /**
   * Clean up all sessions in a root session's tree
   *
   * Returns the number of sessions cleaned up.
   */
  cleanup(rootSessionId: string): number {
    const sessions = this.rootToSessions.get(rootSessionId)
    if (!sessions || sessions.size === 0) {
      return 0
    }

    const count = sessions.size

    // Remove all sessions in tree
    for (const sessionId of sessions) {
      this.sessions.delete(sessionId)
      this.sessionToRoot.delete(sessionId)
    }

    // Remove root entry
    this.rootToSessions.delete(rootSessionId)

    log.info(`Cleaned up ${count} sessions for root ${rootSessionId}`)
    return count
  }

  /**
   * Clean up all sessions (for shutdown)
   */
  clear(): void {
    const total = this.sessions.size
    this.sessions.clear()
    this.sessionToRoot.clear()
    this.rootToSessions.clear()
    log.info(`Cleared all ${total} sessions`)
  }

  // ===========================================================================
  // Filtering
  // ===========================================================================

  /**
   * Filter items by root session
   *
   * Generic filter function that returns items belonging to the same root session.
   *
   * @param items - Items to filter
   * @param getSessionId - Function to extract session ID from item
   * @param rootSessionId - Root session to filter by
   */
  filterByRoot<T>(
    items: T[],
    getSessionId: (item: T) => string | undefined,
    rootSessionId: string
  ): T[] {
    return items.filter((item) => {
      const itemSessionId = getSessionId(item)
      if (!itemSessionId) return false
      const itemRoot = this.sessionToRoot.get(itemSessionId)
      return itemRoot === rootSessionId
    })
  }

  // ===========================================================================
  // Stats
  // ===========================================================================

  /**
   * Get session statistics
   */
  getStats(): SessionStats {
    const sessionsPerRoot = new Map<string, number>()

    for (const [rootId, sessions] of this.rootToSessions) {
      sessionsPerRoot.set(rootId, sessions.size)
    }

    return {
      totalSessions: this.sessions.size,
      rootSessions: this.rootToSessions.size,
      sessionsPerRoot,
    }
  }

  /**
   * Get all root session IDs
   */
  getRootSessionIds(): string[] {
    return Array.from(this.rootToSessions.keys())
  }
}

// =============================================================================
// Singleton Instance
// =============================================================================

let instance: SessionIsolationManager | null = null

/**
 * Get the global session isolation manager
 */
export function getSessionIsolationManager(): SessionIsolationManager {
  if (!instance) {
    instance = new SessionIsolationManager()
    log.info('Session isolation manager initialized')
  }
  return instance
}

/**
 * Clear the global instance (for testing)
 */
export function clearSessionIsolationManager(): void {
  if (instance) {
    instance.clear()
    instance = null
  }
}

// =============================================================================
// Utility Functions
// =============================================================================

/**
 * Register a session with the global manager
 */
export function registerSession(
  sessionId: string,
  parentSessionId?: string | null,
  metadata?: Record<string, unknown>
): SessionInfo {
  return getSessionIsolationManager().registerSession(sessionId, parentSessionId, metadata)
}

/**
 * Get root session ID for any session
 */
export function getRootSession(sessionId: string): string | null {
  return getSessionIsolationManager().getRootSession(sessionId)
}

/**
 * Check if two sessions are in the same tree
 */
export function areSessionsRelated(sessionA: string, sessionB: string): boolean {
  return getSessionIsolationManager().isInSameTree(sessionA, sessionB)
}

/**
 * Clean up a root session tree
 */
export function cleanupSessionTree(rootSessionId: string): number {
  return getSessionIsolationManager().cleanup(rootSessionId)
}
