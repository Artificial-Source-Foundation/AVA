/**
 * Delta9 Injection Tracker
 *
 * Prevents duplicate context injection within sessions.
 * Tracks what context types have been injected to avoid redundant prompts.
 *
 * Pattern from: oh-my-opencode context injection guards
 *
 * Context Types:
 * - Static: Should only be injected once per session (mission briefing, agent roles)
 * - Dynamic: Should be injected every turn (current progress, active tasks)
 *
 * This module tracks STATIC context injection to prevent duplicates.
 * Dynamic context should NOT use this tracker.
 */

import { getNamedLogger } from './logger.js'

const log = getNamedLogger('injection-tracker')

// =============================================================================
// Types
// =============================================================================

export interface InjectionRecord {
  /** Context type that was injected */
  contextType: string
  /** When it was injected */
  timestamp: number
  /** Size of injected content (chars) */
  size?: number
}

export interface InjectionStats {
  /** Total injections tracked */
  totalInjections: number
  /** Duplicates prevented */
  duplicatesPrevented: number
  /** Active sessions */
  activeSessions: number
}

// =============================================================================
// Injection Tracker
// =============================================================================

/**
 * Tracks context injection to prevent duplicates.
 *
 * Use this for STATIC context that should only be injected once:
 * - Mission briefing
 * - Agent role instructions
 * - Configuration context
 * - Initial tool hints
 *
 * Do NOT use for dynamic context that should update each turn:
 * - Current task progress
 * - Learning insights (these change per-turn)
 * - Active warnings
 */
export class InjectionTracker {
  /** Map: sessionId -> Set<contextType> */
  private injected: Map<string, Set<string>> = new Map()
  /** Map: sessionId -> InjectionRecord[] */
  private records: Map<string, InjectionRecord[]> = new Map()
  /** Stats */
  private stats: InjectionStats = {
    totalInjections: 0,
    duplicatesPrevented: 0,
    activeSessions: 0,
  }

  // ===========================================================================
  // Core Methods
  // ===========================================================================

  /**
   * Check if a context type has been injected for this session
   */
  hasInjected(sessionId: string, contextType: string): boolean {
    const injectedTypes = this.injected.get(sessionId)
    return injectedTypes?.has(contextType) ?? false
  }

  /**
   * Mark a context type as injected for this session
   */
  markInjected(sessionId: string, contextType: string, size?: number): void {
    // Get or create set for session
    let injectedTypes = this.injected.get(sessionId)
    if (!injectedTypes) {
      injectedTypes = new Set()
      this.injected.set(sessionId, injectedTypes)
      this.stats.activeSessions++
    }

    // Add context type
    injectedTypes.add(contextType)
    this.stats.totalInjections++

    // Record for debugging
    let sessionRecords = this.records.get(sessionId)
    if (!sessionRecords) {
      sessionRecords = []
      this.records.set(sessionId, sessionRecords)
    }
    sessionRecords.push({
      contextType,
      timestamp: Date.now(),
      size,
    })

    log.debug(`Marked ${contextType} as injected for ${sessionId}`)
  }

  /**
   * Attempt to inject, returning whether injection should proceed
   *
   * This is the main convenience method that combines hasInjected + markInjected.
   * Returns true if injection should proceed, false if already injected.
   */
  tryInject(sessionId: string, contextType: string, size?: number): boolean {
    if (this.hasInjected(sessionId, contextType)) {
      this.stats.duplicatesPrevented++
      log.debug(`Prevented duplicate injection of ${contextType} for ${sessionId}`)
      return false
    }

    this.markInjected(sessionId, contextType, size)
    return true
  }

  // ===========================================================================
  // Session Management
  // ===========================================================================

  /**
   * Clear injection tracking for a session
   */
  clearSession(sessionId: string): void {
    const had = this.injected.has(sessionId)
    this.injected.delete(sessionId)
    this.records.delete(sessionId)

    if (had) {
      this.stats.activeSessions--
      log.debug(`Cleared injection tracking for ${sessionId}`)
    }
  }

  /**
   * Get all context types injected for a session
   */
  getInjectedTypes(sessionId: string): string[] {
    const injectedTypes = this.injected.get(sessionId)
    return injectedTypes ? Array.from(injectedTypes) : []
  }

  /**
   * Get injection records for a session
   */
  getInjectionRecords(sessionId: string): InjectionRecord[] {
    return this.records.get(sessionId) ?? []
  }

  /**
   * Check if session has any injections
   */
  hasSession(sessionId: string): boolean {
    return this.injected.has(sessionId)
  }

  // ===========================================================================
  // Stats & Management
  // ===========================================================================

  /**
   * Get tracker statistics
   */
  getStats(): InjectionStats {
    return { ...this.stats }
  }

  /**
   * Get all active session IDs
   */
  getActiveSessions(): string[] {
    return Array.from(this.injected.keys())
  }

  /**
   * Clear all tracking (for testing/reset)
   */
  clear(): void {
    this.injected.clear()
    this.records.clear()
    this.stats = {
      totalInjections: 0,
      duplicatesPrevented: 0,
      activeSessions: 0,
    }
    log.debug('Cleared all injection tracking')
  }

  /**
   * Get total size of all injections for a session
   */
  getSessionInjectionSize(sessionId: string): number {
    const records = this.records.get(sessionId)
    if (!records) return 0

    return records.reduce((total, record) => total + (record.size ?? 0), 0)
  }
}

// =============================================================================
// Singleton & Exports
// =============================================================================

let instance: InjectionTracker | null = null

/**
 * Get the global injection tracker
 */
export function getInjectionTracker(): InjectionTracker {
  if (!instance) {
    instance = new InjectionTracker()
    log.info('Injection tracker initialized')
  }
  return instance
}

/**
 * Clear the global injection tracker (for testing)
 */
export function clearInjectionTracker(): void {
  if (instance) {
    instance.clear()
    instance = null
  }
}

// =============================================================================
// Convenience Functions
// =============================================================================

/**
 * Check if context has been injected
 */
export function hasInjected(sessionId: string, contextType: string): boolean {
  return getInjectionTracker().hasInjected(sessionId, contextType)
}

/**
 * Try to inject context (returns true if should proceed)
 */
export function tryInject(sessionId: string, contextType: string, size?: number): boolean {
  return getInjectionTracker().tryInject(sessionId, contextType, size)
}

/**
 * Clear injection tracking for a session
 */
export function clearSessionInjections(sessionId: string): void {
  getInjectionTracker().clearSession(sessionId)
}

// =============================================================================
// Common Context Types (Constants)
// =============================================================================

/**
 * Predefined context type constants for consistent usage
 */
export const CONTEXT_TYPES = {
  /** Mission briefing - injected once at mission start */
  MISSION_BRIEFING: 'mission_briefing',
  /** Agent role instructions - injected once per agent activation */
  AGENT_ROLE: 'agent_role',
  /** Tool hints - injected once per tool category */
  TOOL_HINTS: 'tool_hints',
  /** Project configuration - injected once per session */
  PROJECT_CONFIG: 'project_config',
  /** Workflow instructions - injected once per workflow */
  WORKFLOW_INSTRUCTIONS: 'workflow_instructions',
  /** Memory context - injected once per relevant memory set */
  MEMORY_CONTEXT: 'memory_context',
  /** Guardian warnings - injected once per warning type */
  GUARDIAN_WARNING: 'guardian_warning',
} as const

export type ContextType = (typeof CONTEXT_TYPES)[keyof typeof CONTEXT_TYPES]
