/**
 * Delta9 Export Command
 *
 * Export event data for external analysis:
 * - JSON format (full data)
 * - CSV format (tabular)
 * - JSONL format (streaming)
 */

import { existsSync, readFileSync, writeFileSync } from 'node:fs'
import { join } from 'node:path'
import type { ExportOptions, ExportResult } from '../types.js'
import { colorize, symbols } from '../types.js'
import { EVENT_CATEGORIES } from '../../events/types.js'

// =============================================================================
// Export Command
// =============================================================================

export async function exportCommand(options: ExportOptions): Promise<void> {
  const cwd = options.cwd || process.cwd()
  const format = options.format || 'json'

  const result = executeExport(cwd, format, options)

  // Print result summary
  if (result.success) {
    console.log()
    console.log(`${colorize(symbols.check, 'green')} Export successful`)
    console.log(`  Format: ${result.format}`)
    console.log(`  Events: ${result.eventsExported}`)
    if (result.outputPath) {
      console.log(`  Output: ${result.outputPath}`)
    }
    console.log()
  } else {
    console.log()
    console.log(`${colorize(symbols.cross, 'red')} Export failed`)
    console.log(`  Error: ${result.error}`)
    console.log()
  }
}

// =============================================================================
// Export Execution
// =============================================================================

function executeExport(
  cwd: string,
  format: 'json' | 'csv' | 'jsonl',
  options: ExportOptions
): ExportResult {
  const eventsFile = join(cwd, '.delta9', 'events.jsonl')
  const result: ExportResult = {
    success: false,
    format,
    eventsExported: 0,
    timestamp: new Date().toISOString(),
  }

  if (!existsSync(eventsFile)) {
    result.error = 'No events file found'
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

    // Parse and filter events
    const events: Array<Record<string, unknown>> = []

    for (const line of lines) {
      try {
        const event = JSON.parse(line) as Record<string, unknown>

        // Apply filters
        if (options.type && event.type !== options.type) continue
        if (categoryTypes && !categoryTypes.includes(event.type as string)) continue
        if (sinceTime && new Date(event.timestamp as string) < sinceTime) continue
        if (untilTime && new Date(event.timestamp as string) > untilTime) continue

        events.push(event)
      } catch {
        // Skip invalid lines
      }
    }

    result.eventsExported = events.length

    // Generate output
    let outputContent: string

    switch (format) {
      case 'json':
        outputContent = JSON.stringify(events, null, 2)
        break
      case 'csv':
        outputContent = convertToCsv(events)
        break
      case 'jsonl':
        outputContent = events.map((e) => JSON.stringify(e)).join('\n')
        break
      default:
        outputContent = JSON.stringify(events, null, 2)
    }

    // Write to file or stdout
    if (options.output) {
      writeFileSync(options.output, outputContent, 'utf-8')
      result.outputPath = options.output
    } else {
      // Generate default filename
      const timestamp = new Date().toISOString().replace(/[:.]/g, '-')
      const defaultPath = join(cwd, `.delta9/export-${timestamp}.${format}`)
      writeFileSync(defaultPath, outputContent, 'utf-8')
      result.outputPath = defaultPath
    }

    result.success = true
  } catch (error) {
    result.error = error instanceof Error ? error.message : String(error)
  }

  return result
}

// =============================================================================
// CSV Conversion
// =============================================================================

function convertToCsv(events: Array<Record<string, unknown>>): string {
  if (events.length === 0) {
    return 'timestamp,type,missionId,taskId,summary'
  }

  // Define columns
  const columns = ['timestamp', 'type', 'missionId', 'taskId', 'objectiveId', 'summary', 'data']

  // Header row
  const header = columns.join(',')

  // Data rows
  const rows = events.map((event) => {
    return columns
      .map((col) => {
        let value: unknown

        if (col === 'summary') {
          value = summarizeEvent(event)
        } else if (col === 'data') {
          value = event.data ? JSON.stringify(event.data) : ''
        } else {
          value = event[col]
        }

        // Escape for CSV
        return escapeCsvValue(value)
      })
      .join(',')
  })

  return [header, ...rows].join('\n')
}

function escapeCsvValue(value: unknown): string {
  if (value === undefined || value === null) return ''

  const str = String(value)

  // Escape quotes and wrap in quotes if contains comma, quote, or newline
  if (str.includes(',') || str.includes('"') || str.includes('\n')) {
    return `"${str.replace(/"/g, '""')}"`
  }

  return str
}

function summarizeEvent(event: Record<string, unknown>): string {
  const type = event.type as string
  const data = event.data as Record<string, unknown> | undefined

  if (!data) return type

  switch (type) {
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
    default:
      return type
  }
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
