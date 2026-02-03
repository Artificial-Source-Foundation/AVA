/**
 * Activity Multiplexing
 * Multiplex activity events from parallel workers
 *
 * Maintains chronological ordering and allows subscription to all events
 */

import type { WorkerActivityCallback, WorkerActivityEvent } from '../types.js'

// ============================================================================
// Types
// ============================================================================

/**
 * Extended activity event for parallel execution
 */
export interface MultiplexedActivity extends WorkerActivityEvent {
  /** Group ID for related parallel tasks */
  parallelGroup?: string
  /** Index within the parallel group */
  executionIndex: number
  /** Task ID that generated this event */
  taskId?: string
}

/**
 * Callback for multiplexed activity events
 */
export type MultiplexedActivityCallback = (event: MultiplexedActivity) => void

// ============================================================================
// Activity Multiplexer
// ============================================================================

/**
 * Multiplexes activity events from parallel workers
 *
 * Features:
 * - Creates worker-specific callbacks that tag events
 * - Maintains chronological ordering via buffering
 * - Allows multiple subscribers
 */
export class ActivityMultiplexer {
  /** Registered listeners */
  private listeners: Set<MultiplexedActivityCallback> = new Set()

  /** Event buffer for ordering (if needed) */
  private eventBuffer: MultiplexedActivity[] = []

  /** Whether buffering is enabled */
  private buffering = false

  /** Counter for execution index */
  private executionCounter = 0

  /** Current parallel group ID */
  private currentGroupId?: string

  /**
   * Create a worker-specific callback that tags events
   *
   * @param workerId - Worker ID for tagging
   * @param groupId - Optional group ID for parallel tasks
   * @returns Worker-specific callback
   */
  createWorkerCallback(workerId: string, groupId?: string): WorkerActivityCallback {
    const index = this.executionCounter++

    return (event: WorkerActivityEvent): void => {
      const multiplexed: MultiplexedActivity = {
        ...event,
        parallelGroup: groupId ?? this.currentGroupId,
        executionIndex: index,
        taskId: workerId,
      }

      if (this.buffering) {
        this.eventBuffer.push(multiplexed)
      } else {
        this.emit(multiplexed)
      }
    }
  }

  /**
   * Subscribe to all activity events
   *
   * @param callback - Callback for events
   * @returns Unsubscribe function
   */
  subscribe(callback: MultiplexedActivityCallback): () => void {
    this.listeners.add(callback)

    return () => {
      this.listeners.delete(callback)
    }
  }

  /**
   * Start buffering events
   *
   * Events are stored until flush() is called
   */
  startBuffering(): void {
    this.buffering = true
  }

  /**
   * Stop buffering and emit all buffered events in order
   */
  flush(): void {
    this.buffering = false

    // Sort by timestamp for chronological order
    this.eventBuffer.sort((a, b) => a.timestamp - b.timestamp)

    // Emit all buffered events
    for (const event of this.eventBuffer) {
      this.emit(event)
    }

    // Clear buffer
    this.eventBuffer = []
  }

  /**
   * Set the current parallel group ID
   */
  setCurrentGroup(groupId: string | undefined): void {
    this.currentGroupId = groupId
  }

  /**
   * Start a new parallel group
   */
  startGroup(): string {
    const groupId = `group-${Date.now()}-${Math.random().toString(36).slice(2, 8)}`
    this.currentGroupId = groupId
    return groupId
  }

  /**
   * End the current parallel group
   */
  endGroup(): void {
    this.currentGroupId = undefined
  }

  /**
   * Get the number of buffered events
   */
  getBufferSize(): number {
    return this.eventBuffer.length
  }

  /**
   * Get the number of listeners
   */
  getListenerCount(): number {
    return this.listeners.size
  }

  /**
   * Clear all listeners and buffer
   */
  clear(): void {
    this.listeners.clear()
    this.eventBuffer = []
    this.buffering = false
    this.executionCounter = 0
    this.currentGroupId = undefined
  }

  /**
   * Emit event to all listeners
   */
  private emit(event: MultiplexedActivity): void {
    for (const listener of this.listeners) {
      try {
        listener(event)
      } catch {
        // Ignore listener errors
      }
    }
  }
}

// ============================================================================
// Convenience Functions
// ============================================================================

/**
 * Create a simple forwarder that tags events with task ID
 */
export function createTaggedCallback(
  taskId: string,
  downstream: WorkerActivityCallback
): WorkerActivityCallback {
  return (event: WorkerActivityEvent): void => {
    downstream({
      ...event,
      data: {
        ...event.data,
        taskId,
      },
    })
  }
}

/**
 * Create a callback that filters events by type
 */
export function createFilteredCallback(
  types: WorkerActivityEvent['type'][],
  downstream: WorkerActivityCallback
): WorkerActivityCallback {
  const typeSet = new Set(types)

  return (event: WorkerActivityEvent): void => {
    if (typeSet.has(event.type)) {
      downstream(event)
    }
  }
}

/**
 * Create a callback that aggregates events
 */
export function createAggregator(): {
  callback: WorkerActivityCallback
  getEvents: () => WorkerActivityEvent[]
  clear: () => void
} {
  const events: WorkerActivityEvent[] = []

  return {
    callback: (event: WorkerActivityEvent): void => {
      events.push(event)
    },
    getEvents: () => [...events],
    clear: () => {
      events.length = 0
    },
  }
}
