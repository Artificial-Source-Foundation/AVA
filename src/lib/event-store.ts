/**
 * Delta9 Event Store
 *
 * Event sourcing foundation with:
 * - Append-only event log (using existing history.jsonl)
 * - Snapshot support for fast state reconstruction
 * - Event versioning for schema evolution
 * - Rebuild capability from events
 *
 * Pattern from: swarm-plugin event sourcing architecture
 *
 * This module provides the primitives for event sourcing.
 * The existing history.jsonl becomes the event log.
 * mission.json becomes a cached snapshot.
 */

import { existsSync, readFileSync, writeFileSync, appendFileSync } from 'node:fs'
import { join } from 'node:path'
import type { HistoryEvent } from '../types/mission.js'
import { getDelta9Dir, ensureDelta9Dir } from './paths.js'
import { getNamedLogger } from './logger.js'

const log = getNamedLogger('event-store')

// =============================================================================
// Types
// =============================================================================

export interface VersionedEvent<T = unknown> {
  /** Event version for schema evolution */
  version: number
  /** Event type */
  type: string
  /** Event timestamp */
  timestamp: string
  /** Aggregate ID (e.g., mission ID) */
  aggregateId: string
  /** Sequence number within aggregate */
  sequence: number
  /** Event payload */
  payload: T
}

export interface Snapshot<T = unknown> {
  /** Aggregate ID */
  aggregateId: string
  /** Version (event sequence at snapshot time) */
  version: number
  /** Snapshot timestamp */
  timestamp: string
  /** State at snapshot time */
  state: T
}

export interface EventFilter {
  /** Filter by aggregate ID */
  aggregateId?: string
  /** Filter by event type */
  type?: string
  /** Filter events after this sequence number */
  afterSequence?: number
  /** Filter events after this timestamp */
  afterTimestamp?: string
  /** Limit number of events returned */
  limit?: number
}

export interface EventStoreConfig {
  /** Event log file path */
  eventLogPath: string
  /** Snapshots directory path */
  snapshotsDir: string
  /** Current event version (for new events) */
  currentVersion: number
  /** Snapshot interval (create snapshot every N events) */
  snapshotInterval: number
}

// =============================================================================
// Event Store
// =============================================================================

export class EventStore {
  private config: EventStoreConfig
  private eventCache: Map<string, VersionedEvent[]> = new Map()
  private snapshotCache: Map<string, Snapshot> = new Map()
  private sequenceCounters: Map<string, number> = new Map()

  constructor(config: Partial<EventStoreConfig> & { cwd: string }) {
    const delta9Dir = getDelta9Dir(config.cwd)
    this.config = {
      eventLogPath: config.eventLogPath ?? join(delta9Dir, 'events.jsonl'),
      snapshotsDir: config.snapshotsDir ?? join(delta9Dir, 'snapshots'),
      currentVersion: config.currentVersion ?? 1,
      snapshotInterval: config.snapshotInterval ?? 100,
    }
  }

  // ===========================================================================
  // Event Operations
  // ===========================================================================

  /**
   * Append an event to the event log
   */
  append<T>(aggregateId: string, type: string, payload: T): VersionedEvent<T> {
    ensureDelta9Dir(this.config.eventLogPath.replace('/events.jsonl', ''))

    // Get next sequence number
    const sequence = this.getNextSequence(aggregateId)

    const event: VersionedEvent<T> = {
      version: this.config.currentVersion,
      type,
      timestamp: new Date().toISOString(),
      aggregateId,
      sequence,
      payload,
    }

    // Append to file
    const line = JSON.stringify(event) + '\n'
    appendFileSync(this.config.eventLogPath, line, 'utf-8')

    // Update cache
    this.addToCache(event)

    // Check if snapshot needed
    if (sequence % this.config.snapshotInterval === 0) {
      log.debug(`Snapshot interval reached for ${aggregateId} at sequence ${sequence}`)
    }

    return event
  }

  /**
   * Append multiple events in a batch
   */
  appendBatch<T>(events: Array<{ aggregateId: string; type: string; payload: T }>): VersionedEvent<T>[] {
    return events.map(({ aggregateId, type, payload }) =>
      this.append(aggregateId, type, payload)
    )
  }

  /**
   * Read events from the event log
   */
  read(filter?: EventFilter): VersionedEvent[] {
    const events = this.loadAllEvents()
    return this.applyFilter(events, filter)
  }

  /**
   * Read events as an async iterable (for large event logs)
   */
  async *readStream(filter?: EventFilter): AsyncIterable<VersionedEvent> {
    const events = this.read(filter)
    for (const event of events) {
      yield event
    }
  }

  /**
   * Get events for a specific aggregate
   */
  getAggregateEvents(aggregateId: string, afterSequence?: number): VersionedEvent[] {
    return this.read({
      aggregateId,
      afterSequence,
    })
  }

  /**
   * Get the latest sequence number for an aggregate
   */
  getLatestSequence(aggregateId: string): number {
    const events = this.getAggregateEvents(aggregateId)
    if (events.length === 0) return 0
    return Math.max(...events.map((e) => e.sequence))
  }

  // ===========================================================================
  // Snapshot Operations
  // ===========================================================================

  /**
   * Save a snapshot for an aggregate
   */
  saveSnapshot<T>(aggregateId: string, state: T, version?: number): Snapshot<T> {
    const actualVersion = version ?? this.getLatestSequence(aggregateId)

    const snapshot: Snapshot<T> = {
      aggregateId,
      version: actualVersion,
      timestamp: new Date().toISOString(),
      state,
    }

    // Ensure snapshots directory exists
    const snapshotsDir = this.config.snapshotsDir
    if (!existsSync(snapshotsDir)) {
      const { mkdirSync } = require('node:fs')
      mkdirSync(snapshotsDir, { recursive: true })
    }

    // Save snapshot file
    const snapshotPath = this.getSnapshotPath(aggregateId)
    writeFileSync(snapshotPath, JSON.stringify(snapshot, null, 2), 'utf-8')

    // Update cache
    this.snapshotCache.set(aggregateId, snapshot)

    log.debug(`Saved snapshot for ${aggregateId} at version ${actualVersion}`)

    return snapshot
  }

  /**
   * Load snapshot for an aggregate
   */
  getSnapshot<T>(aggregateId: string): Snapshot<T> | null {
    // Check cache first
    const cached = this.snapshotCache.get(aggregateId)
    if (cached) return cached as Snapshot<T>

    // Try to load from file
    const snapshotPath = this.getSnapshotPath(aggregateId)
    if (!existsSync(snapshotPath)) return null

    try {
      const content = readFileSync(snapshotPath, 'utf-8')
      const snapshot = JSON.parse(content) as Snapshot<T>
      this.snapshotCache.set(aggregateId, snapshot)
      return snapshot
    } catch (error) {
      log.error(`Failed to load snapshot for ${aggregateId}: ${error instanceof Error ? error.message : String(error)}`)
      return null
    }
  }

  /**
   * Delete snapshot for an aggregate
   */
  deleteSnapshot(aggregateId: string): boolean {
    const snapshotPath = this.getSnapshotPath(aggregateId)

    if (!existsSync(snapshotPath)) return false

    try {
      const { unlinkSync } = require('node:fs')
      unlinkSync(snapshotPath)
      this.snapshotCache.delete(aggregateId)
      return true
    } catch {
      return false
    }
  }

  // ===========================================================================
  // State Reconstruction
  // ===========================================================================

  /**
   * Rebuild state from events using a reducer function
   *
   * @param aggregateId - The aggregate to rebuild
   * @param reducer - Function that applies each event to state
   * @param initialState - Initial state if no snapshot exists
   * @returns The reconstructed state
   */
  rebuild<T>(
    aggregateId: string,
    reducer: (state: T, event: VersionedEvent) => T,
    initialState: T
  ): T {
    // Try to load snapshot
    const snapshot = this.getSnapshot<T>(aggregateId)
    let state = snapshot?.state ?? initialState
    const afterSequence = snapshot?.version ?? 0

    // Apply events after snapshot
    const events = this.getAggregateEvents(aggregateId, afterSequence)

    for (const event of events) {
      state = reducer(state, event)
    }

    log.debug(
      `Rebuilt state for ${aggregateId}: snapshot v${snapshot?.version ?? 0} + ${events.length} events`
    )

    return state
  }

  /**
   * Rebuild and save snapshot
   */
  rebuildAndSnapshot<T>(
    aggregateId: string,
    reducer: (state: T, event: VersionedEvent) => T,
    initialState: T
  ): { state: T; snapshot: Snapshot<T> } {
    const state = this.rebuild(aggregateId, reducer, initialState)
    const snapshot = this.saveSnapshot(aggregateId, state)
    return { state, snapshot }
  }

  // ===========================================================================
  // Utility
  // ===========================================================================

  /**
   * Get event count for an aggregate
   */
  getEventCount(aggregateId?: string): number {
    if (aggregateId) {
      return this.getAggregateEvents(aggregateId).length
    }
    return this.loadAllEvents().length
  }

  /**
   * Clear all caches (for testing)
   */
  clearCache(): void {
    this.eventCache.clear()
    this.snapshotCache.clear()
    this.sequenceCounters.clear()
  }

  /**
   * Get all aggregate IDs in the event store
   */
  getAggregateIds(): string[] {
    const events = this.loadAllEvents()
    const ids = new Set(events.map((e) => e.aggregateId))
    return Array.from(ids)
  }

  // ===========================================================================
  // Internal Methods
  // ===========================================================================

  private getNextSequence(aggregateId: string): number {
    let sequence = this.sequenceCounters.get(aggregateId)

    if (sequence === undefined) {
      // Load from event log
      sequence = this.getLatestSequence(aggregateId)
    }

    sequence++
    this.sequenceCounters.set(aggregateId, sequence)
    return sequence
  }

  private getSnapshotPath(aggregateId: string): string {
    // Sanitize aggregate ID for filename
    const safeId = aggregateId.replace(/[^a-zA-Z0-9_-]/g, '_')
    return join(this.config.snapshotsDir, `${safeId}.snapshot.json`)
  }

  private loadAllEvents(): VersionedEvent[] {
    if (!existsSync(this.config.eventLogPath)) {
      return []
    }

    try {
      const content = readFileSync(this.config.eventLogPath, 'utf-8')
      const lines = content.trim().split('\n').filter((l) => l.length > 0)

      return lines
        .map((line) => {
          try {
            return JSON.parse(line) as VersionedEvent
          } catch {
            return null
          }
        })
        .filter((e): e is VersionedEvent => e !== null)
    } catch (error) {
      log.error(`Failed to load events: ${error instanceof Error ? error.message : String(error)}`)
      return []
    }
  }

  private addToCache(event: VersionedEvent): void {
    const cached = this.eventCache.get(event.aggregateId) ?? []
    cached.push(event)
    this.eventCache.set(event.aggregateId, cached)
  }

  private applyFilter(events: VersionedEvent[], filter?: EventFilter): VersionedEvent[] {
    if (!filter) return events

    let result = events

    if (filter.aggregateId) {
      result = result.filter((e) => e.aggregateId === filter.aggregateId)
    }

    if (filter.type) {
      result = result.filter((e) => e.type === filter.type)
    }

    if (filter.afterSequence !== undefined) {
      result = result.filter((e) => e.sequence > filter.afterSequence!)
    }

    if (filter.afterTimestamp) {
      result = result.filter((e) => e.timestamp > filter.afterTimestamp!)
    }

    if (filter.limit) {
      result = result.slice(0, filter.limit)
    }

    return result
  }
}

// =============================================================================
// Factory Functions
// =============================================================================

let defaultStore: EventStore | null = null

/**
 * Get or create the default event store
 */
export function getEventStore(cwd: string, config?: Partial<EventStoreConfig>): EventStore {
  if (!defaultStore) {
    defaultStore = new EventStore({ cwd, ...config })
  }
  return defaultStore
}

/**
 * Clear the default event store (for testing)
 */
export function clearEventStore(): void {
  if (defaultStore) {
    defaultStore.clearCache()
    defaultStore = null
  }
}

// =============================================================================
// History Integration
// =============================================================================

/**
 * Convert a HistoryEvent to a VersionedEvent
 *
 * This allows using existing history events in the event store.
 */
export function historyToVersionedEvent(
  event: HistoryEvent,
  sequence: number
): VersionedEvent<HistoryEvent> {
  return {
    version: 1,
    type: event.type,
    timestamp: event.timestamp,
    aggregateId: event.missionId,
    sequence,
    payload: event,
  }
}

/**
 * Import existing history events into the event store
 */
export function importHistoryEvents(
  store: EventStore,
  events: HistoryEvent[]
): number {
  let imported = 0

  for (const event of events) {
    store.append(event.missionId, event.type, event)
    imported++
  }

  log.info(`Imported ${imported} history events into event store`)
  return imported
}
