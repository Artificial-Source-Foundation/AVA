/**
 * Delta9 Event Store
 *
 * Append-only event log with persistence and querying.
 * Events are stored in .delta9/events.jsonl (JSON Lines format).
 *
 * Features:
 * - Append-only (immutable history)
 * - Session-scoped queries
 * - Type-filtered queries
 * - Time-range queries
 * - Event replay for state reconstruction
 */

import { existsSync, mkdirSync, readFileSync, appendFileSync, writeFileSync } from 'node:fs'
import { join } from 'node:path'
import { nanoid } from 'nanoid'
import {
  Delta9Event,
  Delta9EventSchema,
  EventType,
  EventCategory,
  EVENT_CATEGORIES,
} from './types.js'
import { getVersion } from '../lib/version.js'

// =============================================================================
// Types
// =============================================================================

export interface EventStoreOptions {
  /** Base directory for event storage */
  baseDir?: string
  /** Maximum events to keep in memory */
  maxMemoryEvents?: number
}

export interface EventQuery {
  /** Filter by event types */
  types?: EventType[]
  /** Filter by event category */
  category?: EventCategory
  /** Filter by session ID */
  sessionId?: string
  /** Filter by mission ID */
  missionId?: string
  /** Filter by correlation ID */
  correlationId?: string
  /** Start time (inclusive) */
  after?: Date | string
  /** End time (inclusive) */
  before?: Date | string
  /** Maximum events to return */
  limit?: number
  /** Skip first N events */
  offset?: number
  /** Sort order */
  order?: 'asc' | 'desc'
}

export interface EventStats {
  totalEvents: number
  eventsByType: Record<string, number>
  eventsByCategory: Record<string, number>
  firstEvent?: string
  lastEvent?: string
  sessions: number
  missions: number
}

// =============================================================================
// Event Store Class
// =============================================================================

export class EventStore {
  private events: Delta9Event[] = []
  private baseDir: string
  private eventsFile: string
  private maxMemoryEvents: number
  private currentSessionId: string | null = null

  constructor(options: EventStoreOptions = {}) {
    this.baseDir = options.baseDir || process.cwd()
    this.eventsFile = join(this.baseDir, '.delta9', 'events.jsonl')
    this.maxMemoryEvents = options.maxMemoryEvents || 10000

    this.ensureDirectory()
    this.loadEvents()
  }

  // ===========================================================================
  // Core Operations
  // ===========================================================================

  /**
   * Append a new event to the store
   */
  append<T extends Delta9Event['type']>(
    type: T,
    data: Extract<Delta9Event, { type: T }>['data'],
    options: {
      sessionId?: string
      missionId?: string
      correlationId?: string
    } = {}
  ): Delta9Event {
    const event = {
      id: nanoid(12),
      type,
      timestamp: new Date().toISOString(),
      sessionId: options.sessionId || this.currentSessionId || undefined,
      missionId: options.missionId,
      correlationId: options.correlationId,
      data,
    } as Delta9Event

    // Validate event
    const parsed = Delta9EventSchema.parse(event)

    // Add to memory
    this.events.push(parsed)

    // Trim memory if needed
    if (this.events.length > this.maxMemoryEvents) {
      this.events = this.events.slice(-this.maxMemoryEvents)
    }

    // Persist to disk
    this.persistEvent(parsed)

    return parsed
  }

  /**
   * Query events with filters
   */
  query(query: EventQuery = {}): Delta9Event[] {
    let results = [...this.events]

    // Filter by types
    if (query.types && query.types.length > 0) {
      results = results.filter((e) => query.types!.includes(e.type as EventType))
    }

    // Filter by category
    if (query.category) {
      const categoryTypes = EVENT_CATEGORIES[query.category] as readonly string[]
      results = results.filter((e) => categoryTypes.includes(e.type))
    }

    // Filter by session
    if (query.sessionId) {
      results = results.filter((e) => e.sessionId === query.sessionId)
    }

    // Filter by mission
    if (query.missionId) {
      results = results.filter((e) => e.missionId === query.missionId)
    }

    // Filter by correlation
    if (query.correlationId) {
      results = results.filter((e) => e.correlationId === query.correlationId)
    }

    // Filter by time range
    if (query.after) {
      const afterTime = new Date(query.after).getTime()
      results = results.filter((e) => new Date(e.timestamp).getTime() >= afterTime)
    }

    if (query.before) {
      const beforeTime = new Date(query.before).getTime()
      results = results.filter((e) => new Date(e.timestamp).getTime() <= beforeTime)
    }

    // Sort
    if (query.order === 'desc') {
      results.reverse()
    }

    // Pagination
    if (query.offset) {
      results = results.slice(query.offset)
    }

    if (query.limit) {
      results = results.slice(0, query.limit)
    }

    return results
  }

  /**
   * Get the last event of a specific type
   */
  getLastOfType<T extends EventType>(type: T): Extract<Delta9Event, { type: T }> | null {
    for (let i = this.events.length - 1; i >= 0; i--) {
      if (this.events[i].type === type) {
        return this.events[i] as Extract<Delta9Event, { type: T }>
      }
    }
    return null
  }

  /**
   * Get all events for a specific task
   */
  getTaskEvents(taskId: string): Delta9Event[] {
    return this.events.filter((e) => {
      const data = e.data as Record<string, unknown>
      return data.taskId === taskId
    })
  }

  /**
   * Get all events for a specific mission
   */
  getMissionEvents(missionId: string): Delta9Event[] {
    return this.events.filter((e) => e.missionId === missionId)
  }

  /**
   * Get event statistics
   */
  getStats(): EventStats {
    const stats: EventStats = {
      totalEvents: this.events.length,
      eventsByType: {},
      eventsByCategory: {},
      sessions: 0,
      missions: 0,
    }

    const sessions = new Set<string>()
    const missions = new Set<string>()

    for (const event of this.events) {
      // Count by type
      stats.eventsByType[event.type] = (stats.eventsByType[event.type] || 0) + 1

      // Count by category
      for (const [category, types] of Object.entries(EVENT_CATEGORIES)) {
        if ((types as readonly string[]).includes(event.type)) {
          stats.eventsByCategory[category] = (stats.eventsByCategory[category] || 0) + 1
          break
        }
      }

      // Track unique sessions and missions
      if (event.sessionId) sessions.add(event.sessionId)
      if (event.missionId) missions.add(event.missionId)
    }

    stats.sessions = sessions.size
    stats.missions = missions.size

    if (this.events.length > 0) {
      stats.firstEvent = this.events[0].timestamp
      stats.lastEvent = this.events[this.events.length - 1].timestamp
    }

    return stats
  }

  // ===========================================================================
  // Session Management
  // ===========================================================================

  /**
   * Start a new session
   */
  startSession(sessionId?: string): string {
    this.currentSessionId = sessionId || nanoid(12)

    this.append('system.session_started', {
      version: getVersion(),
    })

    return this.currentSessionId
  }

  /**
   * End the current session
   */
  endSession(reason: 'completed' | 'aborted' | 'error' | 'timeout'): void {
    if (!this.currentSessionId) return

    const sessionEvents = this.query({ sessionId: this.currentSessionId })
    const firstEvent = sessionEvents[0]
    const duration = firstEvent
      ? Date.now() - new Date(firstEvent.timestamp).getTime()
      : 0

    this.append('system.session_ended', {
      reason,
      duration,
    })

    this.currentSessionId = null
  }

  /**
   * Get current session ID
   */
  getCurrentSessionId(): string | null {
    return this.currentSessionId
  }

  // ===========================================================================
  // Replay & Projections
  // ===========================================================================

  /**
   * Replay events through a reducer to build state
   */
  replay<T>(
    reducer: (state: T, event: Delta9Event) => T,
    initialState: T,
    query?: EventQuery
  ): T {
    const events = this.query({ ...query, order: 'asc' })
    return events.reduce(reducer, initialState)
  }

  /**
   * Get events since a checkpoint
   */
  getEventsSinceCheckpoint(checkpointId: string): Delta9Event[] {
    const checkpointIndex = this.events.findIndex(
      (e) =>
        e.type === 'system.checkpoint_created' &&
        (e.data as { checkpointId: string }).checkpointId === checkpointId
    )

    if (checkpointIndex === -1) {
      return []
    }

    return this.events.slice(checkpointIndex + 1)
  }

  // ===========================================================================
  // Persistence
  // ===========================================================================

  private ensureDirectory(): void {
    const dir = join(this.baseDir, '.delta9')
    if (!existsSync(dir)) {
      mkdirSync(dir, { recursive: true })
    }
  }

  private loadEvents(): void {
    if (!existsSync(this.eventsFile)) {
      return
    }

    try {
      const content = readFileSync(this.eventsFile, 'utf-8')
      const lines = content.trim().split('\n').filter(Boolean)

      for (const line of lines) {
        try {
          const event = JSON.parse(line)
          const parsed = Delta9EventSchema.parse(event)
          this.events.push(parsed)
        } catch {
          // Skip invalid events
        }
      }

      // Trim if too many
      if (this.events.length > this.maxMemoryEvents) {
        this.events = this.events.slice(-this.maxMemoryEvents)
      }
    } catch {
      // File read error, start fresh
      this.events = []
    }
  }

  private persistEvent(event: Delta9Event): void {
    try {
      appendFileSync(this.eventsFile, JSON.stringify(event) + '\n')
    } catch {
      // Persistence error, log but don't throw
    }
  }

  /**
   * Compact the event log (remove old events)
   */
  compact(keepDays: number = 30): number {
    const cutoff = new Date()
    cutoff.setDate(cutoff.getDate() - keepDays)

    const originalCount = this.events.length
    this.events = this.events.filter((e) => new Date(e.timestamp) >= cutoff)
    const removedCount = originalCount - this.events.length

    // Rewrite file
    if (removedCount > 0) {
      writeFileSync(
        this.eventsFile,
        this.events.map((e) => JSON.stringify(e)).join('\n') + '\n'
      )
    }

    return removedCount
  }

  /**
   * Export events to JSON
   */
  export(query?: EventQuery): string {
    const events = this.query(query)
    return JSON.stringify(events, null, 2)
  }

  /**
   * Clear all events (for testing)
   */
  clear(): void {
    this.events = []
    if (existsSync(this.eventsFile)) {
      writeFileSync(this.eventsFile, '')
    }
  }
}

// =============================================================================
// Singleton Instance
// =============================================================================

let defaultStore: EventStore | null = null

/**
 * Get the default event store instance
 */
export function getEventStore(options?: EventStoreOptions): EventStore {
  if (!defaultStore) {
    defaultStore = new EventStore(options)
  }
  return defaultStore
}

/**
 * Reset the default event store (for testing)
 */
export function resetEventStore(): void {
  defaultStore = null
}
