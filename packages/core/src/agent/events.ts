/**
 * Agent Events
 * Utilities for managing agent event streams
 */

import type {
  AgentEvent,
  AgentEventCallback,
  AgentEventType,
  AgentFinishEvent,
  AgentStartEvent,
  ErrorEvent,
  ThoughtEvent,
  ToolErrorEvent,
  ToolFinishEvent,
  ToolStartEvent,
  TurnFinishEvent,
  TurnStartEvent,
} from './types.js'

// ============================================================================
// Event Emitter
// ============================================================================

/**
 * Simple typed event emitter for agent events
 */
export class AgentEventEmitter {
  private listeners: AgentEventCallback[] = []
  private typeListeners = new Map<AgentEventType, AgentEventCallback[]>()

  /**
   * Add a listener for all events
   */
  on(callback: AgentEventCallback): () => void {
    this.listeners.push(callback)
    return () => this.off(callback)
  }

  /**
   * Remove a listener
   */
  off(callback: AgentEventCallback): void {
    const index = this.listeners.indexOf(callback)
    if (index !== -1) {
      this.listeners.splice(index, 1)
    }
  }

  /**
   * Add a listener for a specific event type
   */
  onType(type: AgentEventType, callback: AgentEventCallback): () => void {
    const existing = this.typeListeners.get(type) ?? []
    existing.push(callback)
    this.typeListeners.set(type, existing)

    return () => this.offType(type, callback)
  }

  /**
   * Remove a type-specific listener
   */
  offType(type: AgentEventType, callback: AgentEventCallback): void {
    const existing = this.typeListeners.get(type)
    if (existing) {
      const index = existing.indexOf(callback)
      if (index !== -1) {
        existing.splice(index, 1)
      }
    }
  }

  /**
   * Emit an event to all listeners
   */
  emit(event: AgentEvent): void {
    // Call general listeners
    for (const listener of this.listeners) {
      try {
        listener(event)
      } catch {
        // Ignore listener errors
      }
    }

    // Call type-specific listeners
    const typeSpecific = this.typeListeners.get(event.type)
    if (typeSpecific) {
      for (const listener of typeSpecific) {
        try {
          listener(event)
        } catch {
          // Ignore listener errors
        }
      }
    }
  }

  /**
   * Remove all listeners
   */
  clear(): void {
    this.listeners = []
    this.typeListeners.clear()
  }
}

// ============================================================================
// Event Filters
// ============================================================================

/**
 * Filter events by type
 */
export function filterByType<T extends AgentEvent>(
  events: AgentEvent[],
  type: AgentEventType
): T[] {
  return events.filter((e) => e.type === type) as T[]
}

/**
 * Get all tool events from a list
 */
export function getToolEvents(
  events: AgentEvent[]
): (ToolStartEvent | ToolFinishEvent | ToolErrorEvent)[] {
  return events.filter(
    (e) => e.type === 'tool:start' || e.type === 'tool:finish' || e.type === 'tool:error'
  ) as (ToolStartEvent | ToolFinishEvent | ToolErrorEvent)[]
}

/**
 * Get all error events from a list
 */
export function getErrorEvents(events: AgentEvent[]): (ToolErrorEvent | ErrorEvent)[] {
  return events.filter((e) => e.type === 'tool:error' || e.type === 'error') as (
    | ToolErrorEvent
    | ErrorEvent
  )[]
}

/**
 * Get thought events as a single string
 */
export function getThoughts(events: AgentEvent[]): string {
  return filterByType<ThoughtEvent>(events, 'thought')
    .map((e) => e.text)
    .join('')
}

// ============================================================================
// Event Timing
// ============================================================================

/**
 * Calculate time between two events
 */
export function getEventDuration(start: AgentEvent, end: AgentEvent): number {
  return end.timestamp - start.timestamp
}

/**
 * Get total duration from start to finish events
 */
export function getTotalDuration(events: AgentEvent[]): number | null {
  const start = events.find((e) => e.type === 'agent:start') as AgentStartEvent | undefined
  const finish = events.find((e) => e.type === 'agent:finish') as AgentFinishEvent | undefined

  if (start && finish) {
    return finish.timestamp - start.timestamp
  }
  return null
}

/**
 * Get turn durations
 */
export function getTurnDurations(events: AgentEvent[]): Map<number, number> {
  const durations = new Map<number, number>()
  const starts = filterByType<TurnStartEvent>(events, 'turn:start')
  const finishes = filterByType<TurnFinishEvent>(events, 'turn:finish')

  for (const start of starts) {
    const finish = finishes.find((f) => f.turn === start.turn)
    if (finish) {
      durations.set(start.turn, finish.timestamp - start.timestamp)
    }
  }

  return durations
}

// ============================================================================
// Event Statistics
// ============================================================================

/**
 * Calculate event statistics
 */
export function getEventStats(events: AgentEvent[]): {
  totalEvents: number
  eventCounts: Map<AgentEventType, number>
  errorCount: number
  turnCount: number
  toolCallCount: number
} {
  const eventCounts = new Map<AgentEventType, number>()

  for (const event of events) {
    const count = eventCounts.get(event.type) ?? 0
    eventCounts.set(event.type, count + 1)
  }

  const turnStarts = filterByType<TurnStartEvent>(events, 'turn:start')
  const toolStarts = filterByType<ToolStartEvent>(events, 'tool:start')
  const errors = getErrorEvents(events)

  return {
    totalEvents: events.length,
    eventCounts,
    errorCount: errors.length,
    turnCount: turnStarts.length,
    toolCallCount: toolStarts.length,
  }
}

// ============================================================================
// Event Buffer
// ============================================================================

/**
 * Buffer for collecting events during execution
 */
export class EventBuffer {
  private events: AgentEvent[] = []
  private maxSize: number

  constructor(maxSize = 1000) {
    this.maxSize = maxSize
  }

  /**
   * Add an event to the buffer
   */
  push(event: AgentEvent): void {
    this.events.push(event)

    // Trim if over max size (remove oldest)
    while (this.events.length > this.maxSize) {
      this.events.shift()
    }
  }

  /**
   * Get all events
   */
  getAll(): readonly AgentEvent[] {
    return this.events
  }

  /**
   * Get events since a timestamp
   */
  getSince(timestamp: number): AgentEvent[] {
    return this.events.filter((e) => e.timestamp >= timestamp)
  }

  /**
   * Get the last N events
   */
  getLast(n: number): AgentEvent[] {
    return this.events.slice(-n)
  }

  /**
   * Clear the buffer
   */
  clear(): void {
    this.events = []
  }

  /**
   * Get current size
   */
  get size(): number {
    return this.events.length
  }
}

// ============================================================================
// Factory Functions
// ============================================================================

/**
 * Create a new event emitter
 */
export function createEventEmitter(): AgentEventEmitter {
  return new AgentEventEmitter()
}

/**
 * Create an event buffer
 */
export function createEventBuffer(maxSize?: number): EventBuffer {
  return new EventBuffer(maxSize)
}

/**
 * Create an event callback that collects to a buffer
 */
export function createBufferedCallback(buffer: EventBuffer): AgentEventCallback {
  return (event) => buffer.push(event)
}
