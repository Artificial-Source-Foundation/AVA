/**
 * Delta9 History Command
 *
 * Event log viewer showing:
 * - Recent events with filtering
 * - Event statistics
 * - Timeline view
 */

import { existsSync, readFileSync } from 'node:fs'
import { join } from 'node:path'
import type { HistoryOptions } from '../types.js'
import { colorize, colors, symbols } from '../types.js'

// =============================================================================
// Types
// =============================================================================

interface EventRecord {
  id: string
  type: string
  timestamp: string
  sessionId?: string
  missionId?: string
  correlationId?: string
  data: Record<string, unknown>
}

// =============================================================================
// History Command
// =============================================================================

export async function historyCommand(options: HistoryOptions): Promise<void> {
  const cwd = options.cwd || process.cwd()
  const format = options.format || 'timeline'
  const limit = options.limit || 20

  // Load events
  const events = loadEvents(cwd, {
    limit,
    type: options.type,
    category: options.category,
    session: options.session,
  })

  // Get stats
  const stats = calculateStats(cwd)

  // Output based on format
  switch (format) {
    case 'json':
      console.log(JSON.stringify({ events, stats }, null, 2))
      break
    case 'table':
      printTableFormat(events, stats)
      break
    case 'timeline':
    default:
      printTimelineFormat(events, stats)
      break
  }
}

// =============================================================================
// Data Loading
// =============================================================================

function loadEvents(
  cwd: string,
  filters: { limit: number; type?: string; category?: string; session?: string }
): EventRecord[] {
  const eventsFile = join(cwd, '.delta9', 'events.jsonl')

  if (!existsSync(eventsFile)) {
    return []
  }

  try {
    const content = readFileSync(eventsFile, 'utf-8')
    const lines = content.trim().split('\n').filter(Boolean)

    let events: EventRecord[] = []

    for (const line of lines) {
      try {
        const event = JSON.parse(line) as EventRecord
        events.push(event)
      } catch {
        // Skip invalid lines
      }
    }

    // Apply filters
    if (filters.type) {
      events = events.filter((e) => e.type === filters.type || e.type.startsWith(filters.type + '.'))
    }

    if (filters.category) {
      const categoryPrefixes: Record<string, string[]> = {
        mission: ['mission.'],
        task: ['task.'],
        council: ['council.'],
        agent: ['agent.'],
        validation: ['validation.'],
        learning: ['learning.'],
        file: ['file.'],
        system: ['system.'],
      }
      const prefixes = categoryPrefixes[filters.category] || []
      if (prefixes.length > 0) {
        events = events.filter((e) => prefixes.some((p) => e.type.startsWith(p)))
      }
    }

    if (filters.session) {
      events = events.filter((e) => e.sessionId === filters.session)
    }

    // Reverse to show newest first, then limit
    events.reverse()
    if (filters.limit > 0) {
      events = events.slice(0, filters.limit)
    }

    return events
  } catch {
    return []
  }
}

function calculateStats(cwd: string): { total: number; byCategory: Record<string, number>; sessions: number } {
  const eventsFile = join(cwd, '.delta9', 'events.jsonl')

  const stats = {
    total: 0,
    byCategory: {} as Record<string, number>,
    sessions: 0,
  }

  if (!existsSync(eventsFile)) {
    return stats
  }

  try {
    const content = readFileSync(eventsFile, 'utf-8')
    const lines = content.trim().split('\n').filter(Boolean)

    const sessions = new Set<string>()

    for (const line of lines) {
      try {
        const event = JSON.parse(line) as EventRecord
        stats.total++

        // Count by category
        const category = event.type.split('.')[0]
        stats.byCategory[category] = (stats.byCategory[category] || 0) + 1

        // Track sessions
        if (event.sessionId) {
          sessions.add(event.sessionId)
        }
      } catch {
        // Skip invalid
      }
    }

    stats.sessions = sessions.size
  } catch {
    // Error reading file
  }

  return stats
}

// =============================================================================
// Output Formatting
// =============================================================================

function printTimelineFormat(events: EventRecord[], stats: { total: number; byCategory: Record<string, number>; sessions: number }): void {
  const width = 70

  console.log()
  console.log(colorize('═'.repeat(width), 'cyan'))
  console.log(colorize('  DELTA9 EVENT HISTORY', 'bold'))
  console.log(colorize('═'.repeat(width), 'cyan'))
  console.log()

  // Stats summary
  console.log(colorize('Stats:', 'bold'))
  console.log(`  Total Events: ${stats.total}`)
  console.log(`  Sessions: ${stats.sessions}`)

  if (Object.keys(stats.byCategory).length > 0) {
    console.log(`  By Category:`)
    for (const [cat, count] of Object.entries(stats.byCategory).sort((a, b) => b[1] - a[1])) {
      console.log(`    ${colorize(cat, 'cyan')}: ${count}`)
    }
  }
  console.log()

  // Events timeline
  if (events.length === 0) {
    console.log(colorize('No events found.', 'dim'))
    console.log()
    return
  }

  console.log(colorize(`Recent Events (${events.length} shown):`, 'bold'))
  console.log()

  for (const event of events) {
    const time = formatTime(event.timestamp)
    const typeColor = getEventTypeColor(event.type)
    const icon = getEventIcon(event.type)

    console.log(`${colorize(time, 'dim')} ${icon} ${colorize(event.type, typeColor)}`)

    // Show key data based on event type
    const summary = summarizeEventData(event)
    if (summary) {
      console.log(`             ${colorize(summary, 'dim')}`)
    }
  }

  console.log()
  console.log(colorize('─'.repeat(width), 'gray'))
  console.log(colorize('  Use --type or --category to filter events', 'dim'))
  console.log()
}

function printTableFormat(events: EventRecord[], stats: { total: number; byCategory: Record<string, number>; sessions: number }): void {
  console.log()
  console.log(colorize('DELTA9 EVENT HISTORY', 'bold'))
  console.log()

  // Stats table
  console.log(colorize('Statistics', 'cyan'))
  console.log('┌────────────────┬──────────┐')
  console.log(`│ Total Events   │ ${padRight(String(stats.total), 8)} │`)
  console.log(`│ Sessions       │ ${padRight(String(stats.sessions), 8)} │`)
  console.log('└────────────────┴──────────┘')
  console.log()

  // Events table
  if (events.length === 0) {
    console.log('No events found.')
    return
  }

  console.log(colorize('Events', 'cyan'))
  console.log('┌────────────┬──────────────────────────────┬────────────────────────────────┐')
  console.log('│ Time       │ Type                         │ Summary                        │')
  console.log('├────────────┼──────────────────────────────┼────────────────────────────────┤')

  for (const event of events) {
    const time = formatTime(event.timestamp)
    const type = event.type.slice(0, 28).padEnd(28)
    const summary = (summarizeEventData(event) || '').slice(0, 30).padEnd(30)
    console.log(`│ ${time} │ ${type} │ ${summary} │`)
  }

  console.log('└────────────┴──────────────────────────────┴────────────────────────────────┘')
  console.log()
}

// =============================================================================
// Helpers
// =============================================================================

function formatTime(timestamp: string): string {
  try {
    const date = new Date(timestamp)
    return date.toLocaleTimeString('en-US', { hour12: false })
  } catch {
    return timestamp.slice(11, 19)
  }
}

function getEventTypeColor(type: string): keyof typeof colors {
  if (type.startsWith('mission.')) return 'magenta'
  if (type.startsWith('task.')) return 'blue'
  if (type.startsWith('council.')) return 'cyan'
  if (type.startsWith('agent.')) return 'green'
  if (type.startsWith('validation.')) return 'yellow'
  if (type.startsWith('learning.')) return 'magenta'
  if (type.startsWith('file.')) return 'gray'
  if (type.startsWith('system.')) return 'white'
  return 'white'
}

function getEventIcon(type: string): string {
  if (type.includes('created')) return colorize(symbols.bullet, 'green')
  if (type.includes('completed')) return colorize(symbols.check, 'green')
  if (type.includes('failed')) return colorize(symbols.cross, 'red')
  if (type.includes('started')) return colorize(symbols.arrow, 'blue')
  if (type.includes('warning')) return colorize(symbols.warning, 'yellow')
  return colorize(symbols.bullet, 'gray')
}

function summarizeEventData(event: EventRecord): string {
  const data = event.data

  // Try to extract meaningful summary based on event type
  if (data.title) return String(data.title).slice(0, 50)
  if (data.description) return String(data.description).slice(0, 50)
  if (data.taskId) return `task: ${data.taskId}`
  if (data.agent) return `agent: ${data.agent}`
  if (data.reason) return String(data.reason).slice(0, 50)
  if (data.filePath) return String(data.filePath)
  if (data.error) return `error: ${String(data.error).slice(0, 40)}`
  if (data.status) return `status: ${data.status}`

  return ''
}

function padRight(str: string, length: number): string {
  return str.padEnd(length)
}
