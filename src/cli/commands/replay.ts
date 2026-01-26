/**
 * Delta9 Replay Command
 *
 * Event replay command for debugging and analysis:
 * - Replay mission events with timing
 * - Adjustable playback speed
 * - Filter by event type/category
 */

import { existsSync, readFileSync } from 'node:fs'
import { join } from 'node:path'
import type { ReplayOptions, ReplayResult } from '../types.js'
import { colorize, colors, symbols } from '../types.js'
import { EVENT_CATEGORIES } from '../../events/types.js'

// =============================================================================
// Speed Multipliers
// =============================================================================

const SPEED_MULTIPLIERS: Record<string, number> = {
  '0.5x': 2.0, // Slower = multiply delay by 2
  '1x': 1.0,
  '2x': 0.5, // Faster = multiply delay by 0.5
  instant: 0, // No delay
}

// =============================================================================
// Replay Command
// =============================================================================

export async function replayCommand(options: ReplayOptions): Promise<void> {
  const cwd = options.cwd || process.cwd()
  const format = options.format || 'timeline'
  const speed = options.speed || '1x'

  // Pass format to executeReplay so it knows whether to print during replay
  const result = await executeReplay(cwd, options, speed, format)

  switch (format) {
    case 'json':
      console.log(JSON.stringify(result, null, 2))
      break
    case 'timeline':
    default:
      // Timeline format is printed during replay
      printReplaySummary(result)
      break
  }
}

// =============================================================================
// Replay Execution
// =============================================================================

async function executeReplay(
  cwd: string,
  options: ReplayOptions,
  speed: string,
  format: string
): Promise<ReplayResult> {
  const eventsFile = join(cwd, '.delta9', 'events.jsonl')
  const result: ReplayResult = {
    missionId: options.missionId,
    events: [],
    stats: {
      total: 0,
      duration: 0,
      categories: {},
    },
    timestamp: new Date().toISOString(),
  }

  const isTimelineFormat = format === 'timeline'

  if (!existsSync(eventsFile)) {
    if (isTimelineFormat) {
      console.log(colorize('No events file found', 'yellow'))
    }
    return result
  }

  try {
    const content = readFileSync(eventsFile, 'utf-8')
    const lines = content.trim().split('\n').filter(Boolean)

    // Get category events if filtering by category
    const categoryTypes: readonly string[] | null = options.category
      ? EVENT_CATEGORIES[options.category as keyof typeof EVENT_CATEGORIES]
      : null

    // Parse all matching events
    interface ParsedEvent {
      index: number
      type: string
      timestamp: string
      missionId?: string
      data?: Record<string, unknown>
    }

    const allEvents: ParsedEvent[] = []
    let index = 0

    for (const line of lines) {
      try {
        const event = JSON.parse(line) as Record<string, unknown>
        index++

        // Apply filters
        if (options.missionId && event.missionId !== options.missionId) continue
        if (options.type && event.type !== options.type) continue
        if (categoryTypes && !categoryTypes.includes(event.type as string)) continue
        if (options.start && index < options.start) continue
        if (options.end && index > options.end) continue

        allEvents.push({
          index,
          type: event.type as string,
          timestamp: event.timestamp as string,
          missionId: event.missionId as string | undefined,
          data: event.data as Record<string, unknown> | undefined,
        })
      } catch {
        // Skip invalid lines
      }
    }

    if (allEvents.length === 0) {
      if (isTimelineFormat) {
        console.log(colorize('No events matched the filters', 'yellow'))
      }
      return result
    }

    // Print header (only in timeline format)
    if (isTimelineFormat) {
      const width = 80
      console.log()
      console.log(colorize('═'.repeat(width), 'cyan'))
      console.log(
        colorize(`  REPLAYING EVENTS (${speed})`, 'bold') +
          (options.missionId ? colorize(` - Mission: ${options.missionId}`, 'dim') : '')
      )
      console.log(colorize('═'.repeat(width), 'cyan'))
      console.log()
    }

    // Calculate first timestamp for elapsed time
    const firstTimestamp = new Date(allEvents[0].timestamp).getTime()
    let previousTimestamp = firstTimestamp
    const speedMultiplier = SPEED_MULTIPLIERS[speed] ?? 1

    // Replay events
    for (let i = 0; i < allEvents.length; i++) {
      const event = allEvents[i]
      const eventTime = new Date(event.timestamp).getTime()
      const elapsed = eventTime - firstTimestamp
      const gap = eventTime - previousTimestamp

      // Track stats
      result.stats.total++
      const category = getEventCategory(event.type)
      result.stats.categories[category] = (result.stats.categories[category] || 0) + 1

      // Add to result
      result.events.push({
        index: event.index,
        type: event.type,
        timestamp: event.timestamp,
        elapsed,
        summary: summarizeEvent(event),
        data: event.data,
      })

      // Wait based on gap and speed (only in timeline format)
      if (isTimelineFormat && speedMultiplier > 0 && gap > 0) {
        const waitTime = Math.min(gap * speedMultiplier, 2000) // Cap at 2 seconds
        await sleep(waitTime)
      }

      // Print event (only in timeline format)
      if (isTimelineFormat) {
        printReplayEvent(event, elapsed, category)
      }

      previousTimestamp = eventTime
    }

    // Calculate total duration
    if (allEvents.length > 0) {
      const lastTimestamp = new Date(allEvents[allEvents.length - 1].timestamp).getTime()
      result.stats.duration = lastTimestamp - firstTimestamp
    }
  } catch (error) {
    console.error(colorize(`Error reading events: ${error}`, 'red'))
  }

  return result
}

// =============================================================================
// Output Formatting
// =============================================================================

function printReplayEvent(
  event: { index: number; type: string; timestamp: string; data?: Record<string, unknown> },
  elapsed: number,
  category: string
): void {
  const time = formatElapsed(elapsed)
  const typeColor = getTypeColor(category)
  const summary = summarizeEvent(event)

  console.log(
    `${colorize(time, 'dim')} ${colorize(`[${event.index}]`, 'gray')} ` +
      `${colorize(event.type, typeColor)}`
  )
  console.log(`         ${summary}`)
  console.log()
}

function printReplaySummary(result: ReplayResult): void {
  const width = 80

  console.log(colorize('─'.repeat(width), 'gray'))
  console.log()
  console.log(`${colorize(symbols.bullet, 'cyan')} ${colorize('Replay Complete:', 'bold')}`)
  console.log(`  Events: ${result.stats.total}`)
  console.log(`  Duration: ${formatDuration(result.stats.duration)}`)
  console.log()

  if (Object.keys(result.stats.categories).length > 0) {
    console.log(`${colorize(symbols.bullet, 'cyan')} ${colorize('By Category:', 'bold')}`)
    for (const [category, count] of Object.entries(result.stats.categories)) {
      console.log(`  ${category}: ${count}`)
    }
    console.log()
  }
}

// =============================================================================
// Helpers
// =============================================================================

function getEventCategory(type: string): string {
  for (const [category, types] of Object.entries(EVENT_CATEGORIES)) {
    if ((types as readonly string[]).includes(type)) {
      return category
    }
  }
  return 'unknown'
}

function summarizeEvent(event: { type: string; data?: Record<string, unknown> }): string {
  const data = event.data

  if (!data) return ''

  switch (event.type) {
    case 'mission.created':
      return `Mission: ${data.name}`
    case 'mission.completed':
      return `Result: ${data.success ? 'SUCCESS' : 'FAILED'}`
    case 'task.created':
      return `Task: ${data.title || data.taskId}`
    case 'task.completed':
      return `${data.taskId}: ${data.success ? 'SUCCESS' : 'FAILED'}`
    case 'task.failed':
      return `${data.taskId}: ${data.error}`
    case 'agent.dispatched':
      return `${data.agent} -> ${data.taskId}`
    case 'validation.completed':
      return `${data.taskId}: ${data.passed ? 'PASSED' : 'FAILED'}`
    case 'trace.decision_recorded':
      return `Decision: ${data.outcome} (confidence: ${data.confidence})`
    case 'budget.threshold_warning':
      return `Budget warning: ${data.percentage}% used`
    default:
      return JSON.stringify(data).substring(0, 60)
  }
}

function formatElapsed(ms: number): string {
  if (ms < 1000) return `+${ms}ms`.padStart(10)
  if (ms < 60000) return `+${(ms / 1000).toFixed(1)}s`.padStart(10)
  if (ms < 3600000) return `+${(ms / 60000).toFixed(1)}m`.padStart(10)
  return `+${(ms / 3600000).toFixed(1)}h`.padStart(10)
}

function formatDuration(ms: number): string {
  if (ms === 0) return 'N/A'
  if (ms < 1000) return `${ms}ms`
  if (ms < 60000) return `${(ms / 1000).toFixed(1)}s`
  if (ms < 3600000) return `${(ms / 60000).toFixed(1)}m`
  return `${(ms / 3600000).toFixed(1)}h`
}

function getTypeColor(category: string): keyof typeof colors {
  switch (category) {
    case 'mission':
      return 'cyan'
    case 'task':
      return 'blue'
    case 'council':
      return 'magenta'
    case 'agent':
      return 'green'
    case 'validation':
      return 'yellow'
    case 'trace':
      return 'white'
    case 'budget':
      return 'yellow'
    case 'messaging':
      return 'cyan'
    default:
      return 'white'
  }
}

function sleep(ms: number): Promise<void> {
  return new Promise((resolve) => setTimeout(resolve, ms))
}
