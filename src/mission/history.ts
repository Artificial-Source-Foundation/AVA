/**
 * Delta9 History Logger
 *
 * Append-only audit log for all mission events.
 * Stored as JSONL (JSON Lines) for easy streaming.
 */

import { appendFileSync, readFileSync, existsSync } from 'node:fs'
import type { HistoryEvent, HistoryEventType } from '../types/mission.js'
import { validateHistoryEvent } from '../schemas/mission.schema.js'
import { getHistoryPath, ensureDelta9Dir } from '../lib/paths.js'
import { getNamedLogger } from '../lib/logger.js'

const log = getNamedLogger('history')

// =============================================================================
// Append History
// =============================================================================

/**
 * Append an event to the history log
 */
export function appendHistory(cwd: string, event: HistoryEvent): void {
  try {
    ensureDelta9Dir(cwd)

    const validated = validateHistoryEvent(event)
    const line = JSON.stringify(validated) + '\n'

    appendFileSync(getHistoryPath(cwd), line, 'utf-8')
  } catch (error) {
    log.error(`Failed to append history: ${error instanceof Error ? error.message : String(error)}`)
  }
}

/**
 * Create and append a history event
 */
export function logEvent(
  cwd: string,
  type: HistoryEventType,
  missionId: string,
  options: {
    objectiveId?: string
    taskId?: string
    data?: Record<string, unknown>
  } = {}
): void {
  appendHistory(cwd, {
    type,
    timestamp: new Date().toISOString(),
    missionId,
    ...options,
  })
}

// =============================================================================
// Read History
// =============================================================================

/**
 * Read all history events
 */
export function readHistory(cwd: string): HistoryEvent[] {
  const historyPath = getHistoryPath(cwd)

  if (!existsSync(historyPath)) {
    return []
  }

  try {
    const content = readFileSync(historyPath, 'utf-8')
    const lines = content
      .trim()
      .split('\n')
      .filter((line) => line.length > 0)

    return lines
      .map((line) => {
        try {
          return validateHistoryEvent(JSON.parse(line))
        } catch {
          return null
        }
      })
      .filter((e): e is HistoryEvent => e !== null)
  } catch (error) {
    log.error(`Failed to read history: ${error instanceof Error ? error.message : String(error)}`)
    return []
  }
}

/**
 * Read history events for a specific mission
 */
export function readMissionHistory(cwd: string, missionId: string): HistoryEvent[] {
  return readHistory(cwd).filter((e) => e.missionId === missionId)
}

/**
 * Read history events of a specific type
 */
export function readHistoryByType(cwd: string, type: HistoryEventType): HistoryEvent[] {
  return readHistory(cwd).filter((e) => e.type === type)
}

/**
 * Read recent history events
 */
export function readRecentHistory(cwd: string, limit: number = 50): HistoryEvent[] {
  const all = readHistory(cwd)
  return all.slice(-limit)
}

// =============================================================================
// History Stats
// =============================================================================

export interface HistoryStats {
  totalEvents: number
  byType: Record<HistoryEventType, number>
  firstEvent?: string
  lastEvent?: string
}

/**
 * Get history statistics
 */
export function getHistoryStats(cwd: string): HistoryStats {
  const events = readHistory(cwd)

  const stats: HistoryStats = {
    totalEvents: events.length,
    byType: {} as Record<HistoryEventType, number>,
  }

  for (const event of events) {
    stats.byType[event.type] = (stats.byType[event.type] || 0) + 1
  }

  if (events.length > 0) {
    stats.firstEvent = events[0].timestamp
    stats.lastEvent = events[events.length - 1].timestamp
  }

  return stats
}

// =============================================================================
// History Search
// =============================================================================

/**
 * Search history events
 */
export function searchHistory(
  cwd: string,
  query: {
    type?: HistoryEventType
    missionId?: string
    objectiveId?: string
    taskId?: string
    after?: string
    before?: string
  }
): HistoryEvent[] {
  let events = readHistory(cwd)

  if (query.type) {
    events = events.filter((e) => e.type === query.type)
  }

  if (query.missionId) {
    events = events.filter((e) => e.missionId === query.missionId)
  }

  if (query.objectiveId) {
    events = events.filter((e) => e.objectiveId === query.objectiveId)
  }

  if (query.taskId) {
    events = events.filter((e) => e.taskId === query.taskId)
  }

  if (query.after) {
    events = events.filter((e) => e.timestamp > query.after!)
  }

  if (query.before) {
    events = events.filter((e) => e.timestamp < query.before!)
  }

  return events
}
