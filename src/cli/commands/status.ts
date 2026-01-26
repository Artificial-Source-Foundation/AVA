/**
 * Delta9 Status Command
 *
 * Mission overview dashboard showing:
 * - Active mission state
 * - Task progress
 * - Background task utilization
 */

import { existsSync, readFileSync } from 'node:fs'
import { join } from 'node:path'
import type { StatusOptions, StatusReport } from '../types.js'
import { colorize, colors, symbols } from '../types.js'

// =============================================================================
// Status Command
// =============================================================================

export async function statusCommand(options: StatusOptions): Promise<void> {
  const cwd = options.cwd || process.cwd()
  const format = options.format || 'summary'

  // Load mission state
  const report = loadStatusReport(cwd)

  // Output based on format
  switch (format) {
    case 'json':
      console.log(JSON.stringify(report, null, 2))
      break
    case 'table':
      printTableFormat(report, options.verbose)
      break
    case 'summary':
    default:
      printSummaryFormat(report, options.verbose)
      break
  }
}

// =============================================================================
// Data Loading
// =============================================================================

function loadStatusReport(cwd: string): StatusReport {
  const missionFile = join(cwd, '.delta9', 'mission.json')

  const report: StatusReport = {
    mission: {
      active: false,
      progress: { completed: 0, total: 0, percentage: 0 },
    },
    tasks: { pending: 0, inProgress: 0, completed: 0, failed: 0 },
    background: { active: 0, pending: 0, capacity: 3, utilization: '0%' },
    uptime: calculateUptime(cwd),
    timestamp: new Date().toISOString(),
  }

  if (!existsSync(missionFile)) {
    return report
  }

  try {
    const mission = JSON.parse(readFileSync(missionFile, 'utf-8'))

    report.mission = {
      active: true,
      id: mission.id,
      status: mission.status,
      title: mission.title,
      objective: mission.objectives?.[0]?.description,
      progress: { completed: 0, total: 0, percentage: 0 },
    }

    // Count tasks across all objectives
    let completed = 0
    let total = 0
    let pending = 0
    let inProgress = 0
    let failed = 0

    for (const objective of mission.objectives || []) {
      for (const task of objective.tasks || []) {
        total++
        switch (task.status) {
          case 'completed':
            completed++
            break
          case 'pending':
            pending++
            break
          case 'in_progress':
            inProgress++
            break
          case 'failed':
            failed++
            break
        }
      }
    }

    report.mission.progress = {
      completed,
      total,
      percentage: total > 0 ? Math.round((completed / total) * 100) : 0,
    }
    report.tasks = { pending, inProgress, completed, failed }
  } catch {
    // Invalid mission file
  }

  // Load background task state
  const bgFile = join(cwd, '.delta9', 'background.json')
  if (existsSync(bgFile)) {
    try {
      const bg = JSON.parse(readFileSync(bgFile, 'utf-8'))
      report.background = {
        active: bg.active || 0,
        pending: bg.pending || 0,
        capacity: bg.capacity || 3,
        utilization: `${Math.round(((bg.active || 0) / (bg.capacity || 3)) * 100)}%`,
      }
    } catch {
      // Invalid background file
    }
  }

  return report
}

function calculateUptime(cwd: string): string {
  const pidFile = join(cwd, '.delta9', 'session.pid')
  if (!existsSync(pidFile)) {
    return 'N/A'
  }

  try {
    const content = readFileSync(pidFile, 'utf-8')
    const startTime = parseInt(content.trim(), 10)
    if (isNaN(startTime)) return 'N/A'

    const ms = Date.now() - startTime
    if (ms < 1000) return `${ms}ms`
    if (ms < 60000) return `${(ms / 1000).toFixed(1)}s`
    if (ms < 3600000) return `${(ms / 60000).toFixed(1)}m`
    return `${(ms / 3600000).toFixed(1)}h`
  } catch {
    return 'N/A'
  }
}

// =============================================================================
// Output Formatting
// =============================================================================

function printSummaryFormat(report: StatusReport, verbose?: boolean): void {
  const width = 60

  // Header
  console.log()
  console.log(colorize('═'.repeat(width), 'cyan'))
  console.log(colorize('  DELTA9 STATUS', 'bold'))
  console.log(colorize('═'.repeat(width), 'cyan'))
  console.log()

  // Mission Status
  if (report.mission.active) {
    const statusColor = getStatusColor(report.mission.status)
    console.log(
      `${colorize(symbols.bullet, 'cyan')} ${colorize('Mission:', 'bold')} ${report.mission.title || report.mission.id}`
    )
    console.log(`  Status: ${colorize(report.mission.status || 'unknown', statusColor)}`)
    console.log(`  Progress: ${renderProgressBar(report.mission.progress.percentage, 30)}`)
    if (report.mission.objective) {
      console.log(
        `  Objective: ${report.mission.objective.slice(0, 50)}${report.mission.objective.length > 50 ? '...' : ''}`
      )
    }
  } else {
    console.log(
      `${colorize(symbols.bullet, 'gray')} ${colorize('Mission:', 'bold')} ${colorize('No active mission', 'dim')}`
    )
    console.log(`  Use ${colorize('mission_create', 'cyan')} to start a new mission`)
  }
  console.log()

  // Task Summary
  console.log(`${colorize(symbols.bullet, 'cyan')} ${colorize('Tasks:', 'bold')}`)
  const taskTotal =
    report.tasks.pending + report.tasks.inProgress + report.tasks.completed + report.tasks.failed
  if (taskTotal === 0) {
    console.log(`  ${colorize('No tasks', 'dim')}`)
  } else {
    console.log(`  ${colorize(symbols.pending, 'yellow')} Pending:     ${report.tasks.pending}`)
    console.log(`  ${colorize(symbols.inProgress, 'blue')} In Progress: ${report.tasks.inProgress}`)
    console.log(`  ${colorize(symbols.completed, 'green')} Completed:   ${report.tasks.completed}`)
    if (report.tasks.failed > 0) {
      console.log(`  ${colorize(symbols.cross, 'red')} Failed:      ${report.tasks.failed}`)
    }
  }
  console.log()

  // Background Tasks
  console.log(`${colorize(symbols.bullet, 'cyan')} ${colorize('Background:', 'bold')}`)
  console.log(
    `  Active: ${report.background.active}/${report.background.capacity} (${report.background.utilization})`
  )
  if (report.background.pending > 0) {
    console.log(`  Queued: ${report.background.pending}`)
  }
  console.log()

  // Footer
  console.log(colorize('─'.repeat(width), 'gray'))
  console.log(colorize(`  Uptime: ${report.uptime} | ${new Date().toLocaleTimeString()}`, 'dim'))
  console.log()

  if (verbose) {
    console.log(colorize('Verbose details not available in summary mode.', 'dim'))
    console.log(colorize('Use --format=json for full data.', 'dim'))
    console.log()
  }
}

function printTableFormat(report: StatusReport, verbose?: boolean): void {
  console.log()
  console.log(colorize('DELTA9 STATUS', 'bold'))
  console.log()

  // Mission table
  console.log(colorize('Mission', 'cyan'))
  console.log('┌──────────────┬──────────────────────────────────────┐')
  console.log(`│ Active       │ ${padRight(report.mission.active ? 'Yes' : 'No', 36)} │`)
  if (report.mission.active) {
    console.log(`│ ID           │ ${padRight(report.mission.id || '-', 36)} │`)
    console.log(`│ Status       │ ${padRight(report.mission.status || '-', 36)} │`)
    console.log(`│ Progress     │ ${padRight(`${report.mission.progress.percentage}%`, 36)} │`)
  }
  console.log('└──────────────┴──────────────────────────────────────┘')
  console.log()

  // Tasks table
  console.log(colorize('Tasks', 'cyan'))
  console.log('┌──────────────┬──────┐')
  console.log(`│ Pending      │ ${padRight(String(report.tasks.pending), 4)} │`)
  console.log(`│ In Progress  │ ${padRight(String(report.tasks.inProgress), 4)} │`)
  console.log(`│ Completed    │ ${padRight(String(report.tasks.completed), 4)} │`)
  console.log(`│ Failed       │ ${padRight(String(report.tasks.failed), 4)} │`)
  console.log('└──────────────┴──────┘')
  console.log()

  if (verbose) {
    console.log(colorize('Background', 'cyan'))
    console.log('┌──────────────┬──────────────────────────────────────┐')
    console.log(`│ Active       │ ${padRight(String(report.background.active), 36)} │`)
    console.log(`│ Pending      │ ${padRight(String(report.background.pending), 36)} │`)
    console.log(`│ Capacity     │ ${padRight(String(report.background.capacity), 36)} │`)
    console.log(`│ Utilization  │ ${padRight(report.background.utilization, 36)} │`)
    console.log('└──────────────┴──────────────────────────────────────┘')
    console.log()
  }
}

// =============================================================================
// Helpers
// =============================================================================

function getStatusColor(status?: string): keyof typeof colors {
  switch (status) {
    case 'active':
    case 'completed':
      return 'green'
    case 'in_progress':
      return 'blue'
    case 'planning':
      return 'cyan'
    case 'failed':
    case 'blocked':
      return 'red'
    case 'paused':
      return 'yellow'
    default:
      return 'white'
  }
}

function renderProgressBar(percentage: number, width: number): string {
  const filled = Math.round((percentage / 100) * width)
  const empty = width - filled
  const bar = `${'█'.repeat(filled)}${'░'.repeat(empty)}`
  const color = percentage === 100 ? 'green' : percentage >= 50 ? 'blue' : 'yellow'
  return `${colorize(bar, color)} ${percentage}%`
}

function padRight(str: string, length: number): string {
  return str.padEnd(length)
}
