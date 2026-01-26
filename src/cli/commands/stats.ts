/**
 * Delta9 Stats Command
 *
 * Analytics and statistics command showing:
 * - Mission success rates
 * - Agent performance metrics
 * - Token usage and costs
 * - Task completion times
 */

import { existsSync, readFileSync } from 'node:fs'
import { join } from 'node:path'
import type { StatsOptions, StatsReport } from '../types.js'
import { colorize, symbols } from '../types.js'

// =============================================================================
// Stats Command
// =============================================================================

export async function statsCommand(options: StatsOptions): Promise<void> {
  const cwd = options.cwd || process.cwd()
  const format = options.format || 'summary'

  const report = generateStats(cwd, options)

  switch (format) {
    case 'json':
      console.log(JSON.stringify(report, null, 2))
      break
    case 'summary':
    default:
      printSummaryFormat(report, options)
      break
  }
}

// =============================================================================
// Stats Generation
// =============================================================================

function generateStats(cwd: string, options: StatsOptions): StatsReport {
  const report: StatsReport = {
    period: options.period || 'all',
    missions: {
      total: 0,
      completed: 0,
      failed: 0,
      aborted: 0,
      successRate: 0,
    },
    tasks: {
      total: 0,
      completed: 0,
      failed: 0,
      avgAttempts: 0,
      avgDuration: 0,
    },
    agents: {},
    budget: {
      totalSpent: 0,
      byCategory: {
        council: 0,
        operators: 0,
        validators: 0,
        support: 0,
      },
    },
    decompositions: {
      total: 0,
      byStrategy: {},
      avgSubtaskCount: 0,
      successRate: 0,
    },
    epics: {
      total: 0,
      completed: 0,
      inProgress: 0,
    },
    timestamp: new Date().toISOString(),
  }

  // Load events for analysis
  const eventsFile = join(cwd, '.delta9', 'events.jsonl')
  if (existsSync(eventsFile)) {
    analyzeEvents(eventsFile, report, options)
  }

  // Load mission for current stats
  const missionFile = join(cwd, '.delta9', 'mission.json')
  if (existsSync(missionFile)) {
    analyzeMission(missionFile, report)
  }

  // Load decomposition history
  const decompositionFile = join(cwd, '.delta9', 'decompositions.jsonl')
  if (existsSync(decompositionFile)) {
    analyzeDecompositions(decompositionFile, report)
  }

  // Load epics
  const epicsFile = join(cwd, '.delta9', 'epics.json')
  if (existsSync(epicsFile)) {
    analyzeEpics(epicsFile, report)
  }

  // Calculate rates
  if (report.missions.total > 0) {
    report.missions.successRate = Math.round(
      (report.missions.completed / report.missions.total) * 100
    )
  }

  return report
}

function analyzeEvents(filePath: string, report: StatsReport, options: StatsOptions): void {
  try {
    const content = readFileSync(filePath, 'utf-8')
    const lines = content.trim().split('\n').filter(Boolean)

    // Parse period filter
    const since = options.period ? parsePeriod(options.period) : null

    const taskDurations: number[] = []
    const taskAttempts: Map<string, number> = new Map()
    // Track agent durations separately for calculating averages
    const agentDurations: Map<string, number[]> = new Map()

    for (const line of lines) {
      try {
        const event = JSON.parse(line)

        // Filter by period
        if (since && new Date(event.timestamp) < since) continue

        const data = event.data || {}

        switch (event.type) {
          case 'mission.created':
            report.missions.total++
            break
          case 'mission.completed':
            if (data.success) {
              report.missions.completed++
            } else {
              report.missions.failed++
            }
            break
          case 'mission.aborted':
            report.missions.aborted++
            break

          case 'task.created':
            report.tasks.total++
            break
          case 'task.completed':
            if (data.success) {
              report.tasks.completed++
            } else {
              report.tasks.failed++
            }
            if (data.duration) {
              taskDurations.push(data.duration)
            }
            break
          case 'task.retried': {
            const current = taskAttempts.get(data.taskId) || 1
            taskAttempts.set(data.taskId, current + 1)
            break
          }

          case 'agent.completed': {
            const agent = data.agent as string
            if (!report.agents[agent]) {
              report.agents[agent] = {
                tasksCompleted: 0,
                tasksFailed: 0,
                tokensUsed: 0,
                avgDuration: 0,
              }
            }
            if (data.success) {
              report.agents[agent].tasksCompleted++
            } else {
              report.agents[agent].tasksFailed++
            }
            if (data.tokensUsed) {
              report.agents[agent].tokensUsed += data.tokensUsed as number
            }
            if (data.duration) {
              if (!agentDurations.has(agent)) {
                agentDurations.set(agent, [])
              }
              agentDurations.get(agent)!.push(data.duration as number)
            }
            break
          }
        }
      } catch {
        // Skip invalid lines
      }
    }

    // Calculate averages
    if (taskDurations.length > 0) {
      report.tasks.avgDuration = Math.round(
        taskDurations.reduce((a, b) => a + b, 0) / taskDurations.length
      )
    }

    if (taskAttempts.size > 0) {
      const totalAttempts = Array.from(taskAttempts.values()).reduce((a, b) => a + b, 0)
      report.tasks.avgAttempts = Math.round((totalAttempts / taskAttempts.size) * 10) / 10
    }

    // Calculate agent averages
    for (const agent of Object.keys(report.agents)) {
      const durations = agentDurations.get(agent)
      if (durations && durations.length > 0) {
        report.agents[agent].avgDuration = Math.round(
          durations.reduce((a, b) => a + b, 0) / durations.length
        )
      }
    }
  } catch {
    // File read error
  }
}

function analyzeMission(filePath: string, report: StatsReport): void {
  try {
    const mission = JSON.parse(readFileSync(filePath, 'utf-8'))

    if (mission.budget) {
      report.budget.totalSpent = mission.budget.spent || 0
      if (mission.budget.breakdown) {
        report.budget.byCategory = {
          council: mission.budget.breakdown.council || 0,
          operators: mission.budget.breakdown.operators || 0,
          validators: mission.budget.breakdown.validators || 0,
          support: mission.budget.breakdown.support || 0,
        }
      }
    }
  } catch {
    // File read error
  }
}

function analyzeDecompositions(filePath: string, report: StatsReport): void {
  try {
    const content = readFileSync(filePath, 'utf-8')
    const lines = content.trim().split('\n').filter(Boolean)

    let totalSubtasks = 0
    let successCount = 0

    for (const line of lines) {
      try {
        const record = JSON.parse(line)
        const decomp = record.decomposition

        if (decomp) {
          report.decompositions.total++
          totalSubtasks += decomp.subtasks?.length || 0

          const strategy = decomp.strategy
          report.decompositions.byStrategy[strategy] =
            (report.decompositions.byStrategy[strategy] || 0) + 1

          if (record.success) {
            successCount++
          }
        }
      } catch {
        // Skip invalid lines
      }
    }

    if (report.decompositions.total > 0) {
      report.decompositions.avgSubtaskCount =
        Math.round((totalSubtasks / report.decompositions.total) * 10) / 10
      report.decompositions.successRate = Math.round(
        (successCount / report.decompositions.total) * 100
      )
    }
  } catch {
    // File read error
  }
}

function analyzeEpics(filePath: string, report: StatsReport): void {
  try {
    const data = JSON.parse(readFileSync(filePath, 'utf-8'))
    const epics = data.epics || []

    report.epics.total = epics.length
    for (const epic of epics) {
      if (epic.status === 'completed') {
        report.epics.completed++
      } else if (epic.status === 'in_progress') {
        report.epics.inProgress++
      }
    }
  } catch {
    // File read error
  }
}

function parsePeriod(period: string): Date | null {
  const match = period.match(/^(\d+)([hdwm])$/)
  if (!match) return null

  const value = parseInt(match[1], 10)
  const unit = match[2]
  const now = new Date()

  switch (unit) {
    case 'h':
      return new Date(now.getTime() - value * 60 * 60 * 1000)
    case 'd':
      return new Date(now.getTime() - value * 24 * 60 * 60 * 1000)
    case 'w':
      return new Date(now.getTime() - value * 7 * 24 * 60 * 60 * 1000)
    case 'm':
      return new Date(now.getTime() - value * 30 * 24 * 60 * 60 * 1000)
    default:
      return null
  }
}

// =============================================================================
// Output Formatting
// =============================================================================

function printSummaryFormat(report: StatsReport, _options: StatsOptions): void {
  const width = 70

  console.log()
  console.log(colorize('═'.repeat(width), 'cyan'))
  console.log(colorize('  DELTA9 STATISTICS', 'bold'))
  console.log(colorize('═'.repeat(width), 'cyan'))
  console.log()

  // Period
  console.log(
    `  ${colorize('Period:', 'dim')} ${report.period === 'all' ? 'All Time' : `Last ${report.period}`}`
  )
  console.log()

  // Mission Stats
  console.log(`${colorize(symbols.bullet, 'cyan')} ${colorize('Missions:', 'bold')}`)
  console.log(`  Total:        ${report.missions.total}`)
  console.log(`  Completed:    ${colorize(String(report.missions.completed), 'green')}`)
  console.log(`  Failed:       ${colorize(String(report.missions.failed), 'red')}`)
  console.log(`  Aborted:      ${colorize(String(report.missions.aborted), 'yellow')}`)
  console.log(`  Success Rate: ${renderPercentage(report.missions.successRate)}`)
  console.log()

  // Task Stats
  console.log(`${colorize(symbols.bullet, 'cyan')} ${colorize('Tasks:', 'bold')}`)
  console.log(`  Total:        ${report.tasks.total}`)
  console.log(`  Completed:    ${colorize(String(report.tasks.completed), 'green')}`)
  console.log(`  Failed:       ${colorize(String(report.tasks.failed), 'red')}`)
  console.log(`  Avg Attempts: ${report.tasks.avgAttempts}`)
  console.log(`  Avg Duration: ${formatDuration(report.tasks.avgDuration)}`)
  console.log()

  // Agent Performance
  const agentNames = Object.keys(report.agents)
  if (agentNames.length > 0) {
    console.log(`${colorize(symbols.bullet, 'cyan')} ${colorize('Agent Performance:', 'bold')}`)

    for (const agent of agentNames) {
      const data = report.agents[agent]
      const total = data.tasksCompleted + data.tasksFailed
      const successRate = total > 0 ? Math.round((data.tasksCompleted / total) * 100) : 0

      console.log(`  ${colorize(agent, 'white')}:`)
      console.log(`    Tasks: ${data.tasksCompleted}/${total} (${successRate}%)`)
      console.log(`    Tokens: ${data.tokensUsed.toLocaleString()}`)
      console.log(`    Avg Duration: ${formatDuration(data.avgDuration)}`)
    }
    console.log()
  }

  // Budget
  if (report.budget.totalSpent > 0) {
    console.log(`${colorize(symbols.bullet, 'cyan')} ${colorize('Budget:', 'bold')}`)
    console.log(`  Total Spent:  $${report.budget.totalSpent.toFixed(4)}`)
    console.log(`  Council:      $${report.budget.byCategory.council.toFixed(4)}`)
    console.log(`  Operators:    $${report.budget.byCategory.operators.toFixed(4)}`)
    console.log(`  Validators:   $${report.budget.byCategory.validators.toFixed(4)}`)
    console.log(`  Support:      $${report.budget.byCategory.support.toFixed(4)}`)
    console.log()
  }

  // Decomposition Stats
  if (report.decompositions.total > 0) {
    console.log(`${colorize(symbols.bullet, 'cyan')} ${colorize('Decompositions:', 'bold')}`)
    console.log(`  Total:          ${report.decompositions.total}`)
    console.log(`  Success Rate:   ${renderPercentage(report.decompositions.successRate)}`)
    console.log(`  Avg Subtasks:   ${report.decompositions.avgSubtaskCount}`)
    console.log(`  By Strategy:`)
    for (const [strategy, count] of Object.entries(report.decompositions.byStrategy)) {
      console.log(`    ${strategy}: ${count}`)
    }
    console.log()
  }

  // Epic Stats
  if (report.epics.total > 0) {
    console.log(`${colorize(symbols.bullet, 'cyan')} ${colorize('Epics:', 'bold')}`)
    console.log(`  Total:       ${report.epics.total}`)
    console.log(`  Completed:   ${colorize(String(report.epics.completed), 'green')}`)
    console.log(`  In Progress: ${colorize(String(report.epics.inProgress), 'blue')}`)
    console.log()
  }

  console.log(colorize('─'.repeat(width), 'gray'))
  console.log(colorize(`  Generated: ${new Date().toLocaleString()}`, 'dim'))
  console.log()
}

// =============================================================================
// Helpers
// =============================================================================

function renderPercentage(value: number): string {
  const color = value >= 80 ? 'green' : value >= 50 ? 'yellow' : 'red'
  return colorize(`${value}%`, color)
}

function formatDuration(ms: number): string {
  if (ms === 0) return 'N/A'
  if (ms < 1000) return `${ms}ms`
  if (ms < 60000) return `${(ms / 1000).toFixed(1)}s`
  if (ms < 3600000) return `${(ms / 60000).toFixed(1)}m`
  return `${(ms / 3600000).toFixed(1)}h`
}
