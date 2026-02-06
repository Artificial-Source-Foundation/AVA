/**
 * Session Resume
 * Flexible session resolution by UUID, numeric index, or "latest" keyword
 *
 * Identifier formats:
 *   - "latest"  → Most recently updated session
 *   - "1", "2"  → 1-based index into sessions sorted by updatedAt (most recent first)
 *   - UUID      → Exact session ID match
 *   - Partial   → Prefix match against session IDs
 *
 * Usage:
 * ```ts
 * const selector = new SessionSelector(manager)
 * const result = await selector.resolve('latest')
 * if (result.found) {
 *   const session = await manager.get(result.sessionId)
 * }
 * ```
 */

import type { SessionManager } from './manager.js'
import type { SessionMeta } from './types.js'

// ============================================================================
// Constants
// ============================================================================

/** Magic keyword for most recent session */
export const RESUME_LATEST = 'latest'

// ============================================================================
// Types
// ============================================================================

/**
 * Result of session resolution
 */
export interface SessionResolveResult {
  /** Whether a session was found */
  found: boolean
  /** Resolved session ID (if found) */
  sessionId?: string
  /** Session metadata (if found) */
  meta?: SessionMeta
  /** How the session was resolved */
  resolvedBy?: 'latest' | 'index' | 'uuid' | 'prefix' | 'search'
  /** Error message if not found */
  error?: string
  /** Available sessions for disambiguation */
  candidates?: SessionMeta[]
}

/**
 * Session display info for listing
 */
export interface SessionDisplayInfo {
  /** 1-based display index */
  index: number
  /** Session ID */
  id: string
  /** Display name (session name or truncated first message) */
  displayName: string
  /** Message count */
  messageCount: number
  /** Working directory */
  workingDirectory: string
  /** Last updated timestamp */
  updatedAt: number
  /** Session status */
  status: string
  /** Whether this is the current active session */
  isCurrent: boolean
}

// ============================================================================
// Session Selector
// ============================================================================

/**
 * Resolves flexible session identifiers to concrete session IDs.
 * Supports "latest", numeric indices, UUIDs, and prefix matching.
 */
export class SessionSelector {
  private manager: SessionManager
  private currentSessionId: string | null

  constructor(manager: SessionManager, currentSessionId?: string) {
    this.manager = manager
    this.currentSessionId = currentSessionId ?? null
  }

  // ==========================================================================
  // Resolution
  // ==========================================================================

  /**
   * Resolve a session identifier to a concrete session.
   *
   * Resolution order:
   * 1. "latest" keyword → most recently updated
   * 2. UUID exact match
   * 3. 1-based numeric index (into updatedAt-sorted list)
   * 4. Prefix match against session IDs
   *
   * @param identifier - Session identifier string
   * @returns Resolution result with session info or error
   */
  async resolve(identifier: string): Promise<SessionResolveResult> {
    const trimmed = identifier.trim().toLowerCase()

    if (!trimmed) {
      return { found: false, error: 'Empty session identifier' }
    }

    // Get all sessions sorted by updatedAt (most recent first)
    const sessions = await this.manager.list()

    if (sessions.length === 0) {
      return { found: false, error: 'No sessions found' }
    }

    // Strategy 1: "latest" keyword
    if (trimmed === RESUME_LATEST) {
      return this.resolveLatest(sessions)
    }

    // Strategy 2: Exact UUID match
    const exactMatch = sessions.find((s) => s.id === trimmed)
    if (exactMatch) {
      return {
        found: true,
        sessionId: exactMatch.id,
        meta: exactMatch,
        resolvedBy: 'uuid',
      }
    }

    // Strategy 3: Numeric index (1-based)
    const index = Number.parseInt(trimmed, 10)
    if (!Number.isNaN(index) && index.toString() === trimmed && index > 0) {
      return this.resolveByIndex(sessions, index)
    }

    // Strategy 4: Prefix match
    return this.resolveByPrefix(sessions, trimmed)
  }

  /**
   * Resolve to the most recently updated session
   */
  private resolveLatest(sessions: SessionMeta[]): SessionResolveResult {
    // Sessions are already sorted by updatedAt (most recent first)
    const latest = sessions[0]
    if (!latest) {
      return { found: false, error: 'No sessions found' }
    }

    return {
      found: true,
      sessionId: latest.id,
      meta: latest,
      resolvedBy: 'latest',
    }
  }

  /**
   * Resolve by 1-based numeric index into updatedAt-sorted list
   */
  private resolveByIndex(sessions: SessionMeta[], index: number): SessionResolveResult {
    if (index < 1 || index > sessions.length) {
      return {
        found: false,
        error: `Session index ${index} out of range (1-${sessions.length})`,
        candidates: sessions.slice(0, 5),
      }
    }

    const session = sessions[index - 1]!
    return {
      found: true,
      sessionId: session.id,
      meta: session,
      resolvedBy: 'index',
    }
  }

  /**
   * Resolve by prefix match against session IDs
   */
  private resolveByPrefix(sessions: SessionMeta[], prefix: string): SessionResolveResult {
    const matches = sessions.filter((s) => s.id.toLowerCase().startsWith(prefix))

    if (matches.length === 0) {
      return {
        found: false,
        error: `No session found matching "${prefix}"`,
        candidates: sessions.slice(0, 5),
      }
    }

    if (matches.length === 1) {
      return {
        found: true,
        sessionId: matches[0]!.id,
        meta: matches[0],
        resolvedBy: 'prefix',
      }
    }

    // Multiple matches - ambiguous
    return {
      found: false,
      error: `Ambiguous identifier "${prefix}" matches ${matches.length} sessions`,
      candidates: matches,
    }
  }

  // ==========================================================================
  // Listing
  // ==========================================================================

  /**
   * List all sessions with display info for UI rendering.
   *
   * @param limit - Maximum sessions to return (default: 20)
   * @returns Array of session display info, most recent first
   */
  async listSessions(limit = 20): Promise<SessionDisplayInfo[]> {
    const sessions = await this.manager.list()

    return sessions.slice(0, limit).map((session, i) => ({
      index: i + 1,
      id: session.id,
      displayName: session.name ?? formatSessionId(session.id),
      messageCount: session.messageCount,
      workingDirectory: session.workingDirectory,
      updatedAt: session.updatedAt,
      status: session.status,
      isCurrent: session.id === this.currentSessionId,
    }))
  }

  /**
   * Search sessions by content (name or working directory).
   *
   * @param query - Search string
   * @returns Matching sessions
   */
  async search(query: string): Promise<SessionResolveResult> {
    const sessions = await this.manager.list()
    const lowerQuery = query.toLowerCase()

    const matches = sessions.filter((s) => {
      const name = (s.name ?? '').toLowerCase()
      const dir = s.workingDirectory.toLowerCase()
      return name.includes(lowerQuery) || dir.includes(lowerQuery)
    })

    if (matches.length === 0) {
      return { found: false, error: `No sessions matching "${query}"` }
    }

    if (matches.length === 1) {
      return {
        found: true,
        sessionId: matches[0]!.id,
        meta: matches[0],
        resolvedBy: 'search',
      }
    }

    return {
      found: false,
      error: `${matches.length} sessions match "${query}"`,
      candidates: matches,
    }
  }

  // ==========================================================================
  // Convenience
  // ==========================================================================

  /**
   * Check if a session with the given ID exists.
   */
  async exists(sessionId: string): Promise<boolean> {
    const session = await this.manager.get(sessionId)
    return session !== null
  }

  /**
   * Get the most recent session, or null if none exist.
   */
  async getLatest(): Promise<SessionMeta | null> {
    const result = await this.resolve(RESUME_LATEST)
    return result.meta ?? null
  }

  /**
   * Update the current session ID (e.g., after resuming a new session).
   */
  setCurrentSession(sessionId: string | null): void {
    this.currentSessionId = sessionId
  }
}

// ============================================================================
// Helpers
// ============================================================================

/**
 * Format a session ID for display.
 * Extracts the timestamp portion if the ID follows the session-<timestamp>-<random> format.
 */
function formatSessionId(id: string): string {
  // session-1706123456789-ab3def → "Session ab3def"
  const parts = id.split('-')
  if (parts.length >= 3 && parts[0] === 'session') {
    const suffix = parts.slice(2).join('-')
    return `Session ${suffix}`
  }
  // Truncate long IDs
  return id.length > 20 ? `${id.slice(0, 20)}...` : id
}

/**
 * Format a timestamp for display.
 */
export function formatSessionTimestamp(timestamp: number): string {
  const date = new Date(timestamp)
  const now = new Date()
  const diffMs = now.getTime() - date.getTime()
  const diffMins = Math.floor(diffMs / 60000)
  const diffHours = Math.floor(diffMs / 3600000)
  const diffDays = Math.floor(diffMs / 86400000)

  if (diffMins < 1) return 'just now'
  if (diffMins < 60) return `${diffMins}m ago`
  if (diffHours < 24) return `${diffHours}h ago`
  if (diffDays < 7) return `${diffDays}d ago`

  return date.toLocaleDateString('en-US', {
    month: 'short',
    day: 'numeric',
    year: date.getFullYear() !== now.getFullYear() ? 'numeric' : undefined,
  })
}

// ============================================================================
// Factory
// ============================================================================

/**
 * Create a session selector with a session manager.
 *
 * @param manager - SessionManager instance
 * @param currentSessionId - Currently active session ID (if any)
 * @returns SessionSelector instance
 */
export function createSessionSelector(
  manager: SessionManager,
  currentSessionId?: string
): SessionSelector {
  return new SessionSelector(manager, currentSessionId)
}
