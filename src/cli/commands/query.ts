/**
 * Delta9 Query Command
 *
 * Event query command for filtering and searching event history:
 * - Filter by event type
 * - Filter by category
 * - Filter by time range
 * - Full-text search
 */

import { existsSync, readFileSync } from 'node:fs'
import { join } from 'node:path'
import type { QueryOptions, QueryResult } from '../types.js'
import { colorize, colors, symbols } from '../types.js'
import { EVENT_CATEGORIES } from '../../events/types.js'

// =============================================================================
// Query Presets
// =============================================================================

const QUERY_PRESETS: Record<string, Partial<QueryOptions>> = {
  /** Recent errors and failures */
  errors: {
    search: 'failed|error|failure',
    limit: 25,
  },
  /** Decision traces from council */
  decisions: {
    category: 'trace',
    limit: 20,
  },
  /** Budget and cost events */
  budget: {
    category: 'budget',
    limit: 50,
  },
  /** Agent activity */
  agents: {
    category: 'agent',
    limit: 30,
  },
  /** Full timeline (recent activity) */
  timeline: {
    since: '1h',
    limit: 100,
  },
}

// =============================================================================
// Query Command
// =============================================================================

export async function queryCommand(options: QueryOptions): Promise<void> {
  const cwd = options.cwd || process.cwd()
  const format = options.format || 'table'

  // Apply preset if specified
  let effectiveOptions = { ...options }
  if (options.preset && QUERY_PRESETS[options.preset]) {
    effectiveOptions = { ...QUERY_PRESETS[options.preset], ...options }
    // Keep the preset name for display
    effectiveOptions.preset = options.preset
  }

  const result = executeQuery(cwd, effectiveOptions)

  switch (format) {
    case 'json':
      console.log(JSON.stringify(result, null, 2))
      break
    case 'table':
    default:
      printTableFormat(result, effectiveOptions)
      break
  }
}

// =============================================================================
// Query Execution
// =============================================================================

function executeQuery(cwd: string, options: QueryOptions): QueryResult {
  const eventsFile = join(cwd, '.delta9', 'events.jsonl')
  const result: QueryResult = {
    query: {
      type: options.type,
      category: options.category,
      since: options.since,
      until: options.until,
      search: options.search,
      limit: options.limit || 50,
    },
    events: [],
    stats: {
      total: 0,
      matched: 0,
      categories: {},
    },
    timestamp: new Date().toISOString(),
  }

  if (!existsSync(eventsFile)) {
    return result
  }

  try {
    const content = readFileSync(eventsFile, 'utf-8')
    const lines = content.trim().split('\n').filter(Boolean)

    // Parse time filters
    const sinceTime = options.since ? parseTimeFilter(options.since) : null
    const untilTime = options.until ? parseTimeFilter(options.until) : null

    // Get category events if filtering by category
    const categoryTypes: readonly string[] | null = options.category
      ? EVENT_CATEGORIES[options.category as keyof typeof EVENT_CATEGORIES]
      : null

    const matchedEvents: Array<{
      id: string
      type: string
      timestamp: string
      category: string
      summary: string
      data?: Record<string, unknown>
    }> = []

    for (const line of lines) {
      try {
        const event = JSON.parse(line)
        result.stats.total++

        // Track category stats
        const category = getEventCategory(event.type)
        result.stats.categories[category] = (result.stats.categories[category] || 0) + 1

        // Apply filters
        const eventType = event.type as string
        if (options.type && eventType !== options.type) continue
        if (categoryTypes && !categoryTypes.includes(eventType)) continue
        if (sinceTime && new Date(event.timestamp) < sinceTime) continue
        if (untilTime && new Date(event.timestamp) > untilTime) continue
        if (options.search && !matchesSearch(event, options.search)) continue

        matchedEvents.push({
          id: event.id,
          type: event.type,
          timestamp: event.timestamp,
          category,
          summary: summarizeEvent(event),
          data: options.verbose ? event.data : undefined,
        })
      } catch {
        // Skip invalid lines
      }
    }

    result.stats.matched = matchedEvents.length

    // Apply limit (get most recent)
    result.events = matchedEvents.slice(-1 * (options.limit || 50)).reverse()
  } catch {
    // File read error
  }

  return result
}

// =============================================================================
// Time Filter Parsing
// =============================================================================

function parseTimeFilter(filter: string): Date | null {
  // Handle relative times: 1h, 30m, 2d, 1w
  const relativeMatch = filter.match(/^(\d+)([smhdw])$/)
  if (relativeMatch) {
    const value = parseInt(relativeMatch[1], 10)
    const unit = relativeMatch[2]
    const now = new Date()

    switch (unit) {
      case 's':
        return new Date(now.getTime() - value * 1000)
      case 'm':
        return new Date(now.getTime() - value * 60 * 1000)
      case 'h':
        return new Date(now.getTime() - value * 60 * 60 * 1000)
      case 'd':
        return new Date(now.getTime() - value * 24 * 60 * 60 * 1000)
      case 'w':
        return new Date(now.getTime() - value * 7 * 24 * 60 * 60 * 1000)
    }
  }

  // Handle ISO dates
  const date = new Date(filter)
  return isNaN(date.getTime()) ? null : date
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

function matchesSearch(event: Record<string, unknown>, search: string): boolean {
  const searchLower = search.toLowerCase()
  const json = JSON.stringify(event).toLowerCase()
  return json.includes(searchLower)
}

function summarizeEvent(event: Record<string, unknown>): string {
  const type = event.type as string
  const data = event.data as Record<string, unknown> | undefined

  if (!data) return type

  // Generate summary based on event type
  switch (type) {
    case 'mission.created':
      return `Mission created: ${data.name}`
    case 'mission.completed':
      return `Mission completed: ${data.success ? 'SUCCESS' : 'FAILED'}`
    case 'task.created':
      return `Task created: ${data.title || data.taskId}`
    case 'task.completed':
      return `Task ${data.taskId} completed: ${data.success ? 'SUCCESS' : 'FAILED'}`
    case 'task.failed':
      return `Task ${data.taskId} failed: ${data.error}`
    case 'agent.dispatched':
      return `Agent ${data.agent} dispatched for ${data.taskId}`
    case 'validation.completed':
      return `Validation ${data.passed ? 'PASSED' : 'FAILED'} for ${data.taskId}`
    case 'messaging.sent':
      return `Message from ${data.from} to ${data.to}: ${data.subject}`
    case 'decomposition.created':
      return `Decomposition ${data.decompositionId}: ${data.subtaskCount} subtasks`
    case 'epic.created':
      return `Epic created: ${data.title}`
    default:
      return type.replace('.', ': ')
  }
}

// =============================================================================
// Output Formatting
// =============================================================================

function printTableFormat(result: QueryResult, options: QueryOptions): void {
  const width = 80

  console.log()
  console.log(colorize('═'.repeat(width), 'cyan'))
  const title = options.preset
    ? `  EVENT QUERY: ${options.preset.toUpperCase()}`
    : '  EVENT QUERY RESULTS'
  console.log(colorize(title, 'bold'))
  console.log(colorize('═'.repeat(width), 'cyan'))
  console.log()

  // Query summary
  console.log(`${colorize(symbols.bullet, 'cyan')} ${colorize('Query:', 'bold')}`)
  if (options.preset) console.log(`  Preset: ${colorize(options.preset, 'cyan')}`)
  if (result.query.type) console.log(`  Type: ${result.query.type}`)
  if (result.query.category) console.log(`  Category: ${result.query.category}`)
  if (result.query.since) console.log(`  Since: ${result.query.since}`)
  if (result.query.until) console.log(`  Until: ${result.query.until}`)
  if (result.query.search) console.log(`  Search: "${result.query.search}"`)
  console.log(`  Limit: ${result.query.limit}`)
  console.log()

  // Stats
  console.log(`${colorize(symbols.bullet, 'cyan')} ${colorize('Stats:', 'bold')}`)
  console.log(`  Total events: ${result.stats.total}`)
  console.log(`  Matched: ${result.stats.matched}`)
  console.log(`  Showing: ${result.events.length}`)
  console.log()

  // Category breakdown
  if (Object.keys(result.stats.categories).length > 0) {
    console.log(`${colorize(symbols.bullet, 'cyan')} ${colorize('By Category:', 'bold')}`)
    for (const [category, count] of Object.entries(result.stats.categories)) {
      const bar = renderMiniBar(count, result.stats.total, 20)
      console.log(`  ${padRight(category, 15)} ${bar} ${count}`)
    }
    console.log()
  }

  // Events
  if (result.events.length === 0) {
    console.log(colorize('  No events matched the query', 'dim'))
  } else {
    console.log(`${colorize(symbols.bullet, 'cyan')} ${colorize('Events:', 'bold')}`)
    console.log()

    for (const event of result.events) {
      const time = new Date(event.timestamp).toLocaleTimeString()
      const typeColor = getTypeColor(event.category)

      console.log(`  ${colorize(time, 'dim')} ${colorize(`[${event.type}]`, typeColor)}`)
      console.log(`    ${event.summary}`)

      if (options.verbose && event.data) {
        console.log(colorize(`    ${JSON.stringify(event.data)}`, 'dim'))
      }
      console.log()
    }
  }

  console.log(colorize('─'.repeat(width), 'gray'))
  console.log()
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
    case 'learning':
      return 'white'
    case 'messaging':
      return 'cyan'
    case 'decomposition':
      return 'blue'
    case 'epic':
      return 'magenta'
    case 'system':
      return 'gray'
    default:
      return 'white'
  }
}

function renderMiniBar(value: number, max: number, width: number): string {
  const filled = Math.round((value / max) * width)
  const empty = width - filled
  return colorize('█'.repeat(filled), 'cyan') + colorize('░'.repeat(empty), 'gray')
}

function padRight(str: string, length: number): string {
  return str.padEnd(length)
}
