/**
 * Delta9 Health Command
 *
 * Environment diagnostics similar to oh-my-opencode doctor.
 * Performs health checks across multiple categories.
 */

import { existsSync, readFileSync, statSync } from 'node:fs'
import { join } from 'node:path'
import type { HealthOptions, HealthReport } from '../types.js'
import { colorize, symbols } from '../types.js'

// =============================================================================
// Types
// =============================================================================

interface HealthCheck {
  name: string
  category: 'config' | 'mission' | 'background' | 'sdk' | 'files'
  status: 'pass' | 'warn' | 'fail'
  message: string
  details?: string
}

// =============================================================================
// Health Command
// =============================================================================

export async function healthCommand(options: HealthOptions): Promise<void> {
  const cwd = options.cwd || process.cwd()
  const format = options.format || 'summary'

  // Run all health checks
  const checks = await runHealthChecks(cwd, options.verbose)

  // Build report
  const report = buildHealthReport(checks)

  // Output based on format
  switch (format) {
    case 'json':
      console.log(JSON.stringify({ report, checks }, null, 2))
      break
    case 'summary':
    default:
      printSummaryFormat(checks, report)
      break
  }

  // Exit with non-zero if unhealthy
  if (report.status === 'unhealthy') {
    process.exitCode = 1
  }
}

// =============================================================================
// Health Checks
// =============================================================================

async function runHealthChecks(cwd: string, verbose?: boolean): Promise<HealthCheck[]> {
  const checks: HealthCheck[] = []

  // Configuration Checks
  checks.push(checkConfigFile(cwd))
  checks.push(checkDelta9Directory(cwd))

  // Mission Checks
  checks.push(checkMissionState(cwd))
  checks.push(checkMissionValidity(cwd))

  // Background Task Checks
  checks.push(checkBackgroundCapacity(cwd))

  // File System Checks
  checks.push(checkEventsFile(cwd))
  checks.push(checkKnowledgeFile(cwd))

  // SDK/Runtime Checks
  checks.push(checkNodeVersion())

  if (verbose) {
    checks.push(checkDiskSpace(cwd))
    checks.push(checkRecentActivity(cwd))
  }

  return checks
}

function checkConfigFile(cwd: string): HealthCheck {
  const configPaths = [
    join(cwd, 'delta9.json'),
    join(cwd, '.delta9', 'config.json'),
    join(cwd, 'opencode.json'),
  ]

  for (const path of configPaths) {
    if (existsSync(path)) {
      try {
        const content = readFileSync(path, 'utf-8')
        JSON.parse(content)
        return {
          name: 'Configuration file',
          category: 'config',
          status: 'pass',
          message: `Found and valid: ${path.replace(cwd, '.')}`,
        }
      } catch (e) {
        return {
          name: 'Configuration file',
          category: 'config',
          status: 'fail',
          message: 'Invalid JSON in config file',
          details: e instanceof Error ? e.message : String(e),
        }
      }
    }
  }

  return {
    name: 'Configuration file',
    category: 'config',
    status: 'warn',
    message: 'No delta9.json found - using defaults',
    details: 'Create delta9.json for custom configuration',
  }
}

function checkDelta9Directory(cwd: string): HealthCheck {
  const dir = join(cwd, '.delta9')

  if (!existsSync(dir)) {
    return {
      name: '.delta9 directory',
      category: 'config',
      status: 'warn',
      message: 'Directory not created yet',
      details: 'Will be created on first mission',
    }
  }

  try {
    const stats = statSync(dir)
    if (stats.isDirectory()) {
      return {
        name: '.delta9 directory',
        category: 'config',
        status: 'pass',
        message: 'Directory exists',
      }
    }
  } catch {
    // Fall through
  }

  return {
    name: '.delta9 directory',
    category: 'config',
    status: 'fail',
    message: '.delta9 exists but is not a directory',
  }
}

function checkMissionState(cwd: string): HealthCheck {
  const missionFile = join(cwd, '.delta9', 'mission.json')

  if (!existsSync(missionFile)) {
    return {
      name: 'Mission state',
      category: 'mission',
      status: 'pass',
      message: 'No active mission',
      details: 'Use mission_create to start',
    }
  }

  try {
    const content = readFileSync(missionFile, 'utf-8')
    const mission = JSON.parse(content)

    if (mission.status === 'active' || mission.status === 'in_progress') {
      return {
        name: 'Mission state',
        category: 'mission',
        status: 'pass',
        message: `Active mission: ${mission.title || mission.id}`,
      }
    }

    return {
      name: 'Mission state',
      category: 'mission',
      status: 'pass',
      message: `Mission status: ${mission.status}`,
    }
  } catch {
    return {
      name: 'Mission state',
      category: 'mission',
      status: 'fail',
      message: 'Invalid mission.json',
      details: 'File exists but contains invalid JSON',
    }
  }
}

function checkMissionValidity(cwd: string): HealthCheck {
  const missionFile = join(cwd, '.delta9', 'mission.json')

  if (!existsSync(missionFile)) {
    return {
      name: 'Mission schema',
      category: 'mission',
      status: 'pass',
      message: 'No mission to validate',
    }
  }

  try {
    const content = readFileSync(missionFile, 'utf-8')
    const mission = JSON.parse(content)

    // Basic schema checks
    if (!mission.id) {
      return {
        name: 'Mission schema',
        category: 'mission',
        status: 'fail',
        message: 'Mission missing required field: id',
      }
    }

    if (!mission.objectives || !Array.isArray(mission.objectives)) {
      return {
        name: 'Mission schema',
        category: 'mission',
        status: 'warn',
        message: 'Mission has no objectives array',
      }
    }

    return {
      name: 'Mission schema',
      category: 'mission',
      status: 'pass',
      message: 'Mission schema is valid',
    }
  } catch {
    return {
      name: 'Mission schema',
      category: 'mission',
      status: 'fail',
      message: 'Cannot parse mission.json',
    }
  }
}

function checkBackgroundCapacity(cwd: string): HealthCheck {
  const bgFile = join(cwd, '.delta9', 'background.json')

  if (!existsSync(bgFile)) {
    return {
      name: 'Background tasks',
      category: 'background',
      status: 'pass',
      message: 'No background task state',
    }
  }

  try {
    const content = readFileSync(bgFile, 'utf-8')
    const bg = JSON.parse(content)

    const active = bg.active || 0
    const capacity = bg.capacity || 3

    if (active >= capacity) {
      return {
        name: 'Background tasks',
        category: 'background',
        status: 'warn',
        message: `At capacity: ${active}/${capacity}`,
        details: 'New tasks will be queued',
      }
    }

    return {
      name: 'Background tasks',
      category: 'background',
      status: 'pass',
      message: `${active}/${capacity} slots used`,
    }
  } catch {
    return {
      name: 'Background tasks',
      category: 'background',
      status: 'pass',
      message: 'Background state unavailable',
    }
  }
}

function checkEventsFile(cwd: string): HealthCheck {
  const eventsFile = join(cwd, '.delta9', 'events.jsonl')

  if (!existsSync(eventsFile)) {
    return {
      name: 'Events log',
      category: 'files',
      status: 'pass',
      message: 'No events logged yet',
    }
  }

  try {
    const stats = statSync(eventsFile)
    const sizeMB = stats.size / (1024 * 1024)

    if (sizeMB > 10) {
      return {
        name: 'Events log',
        category: 'files',
        status: 'warn',
        message: `Events file is large: ${sizeMB.toFixed(1)}MB`,
        details: 'Consider running compact to clean old events',
      }
    }

    return {
      name: 'Events log',
      category: 'files',
      status: 'pass',
      message: `${sizeMB.toFixed(2)}MB`,
    }
  } catch {
    return {
      name: 'Events log',
      category: 'files',
      status: 'fail',
      message: 'Cannot read events file',
    }
  }
}

function checkKnowledgeFile(cwd: string): HealthCheck {
  const knowledgeFile = join(cwd, '.delta9', 'knowledge.md')

  if (!existsSync(knowledgeFile)) {
    return {
      name: 'Knowledge base',
      category: 'files',
      status: 'pass',
      message: 'No knowledge stored',
    }
  }

  try {
    const stats = statSync(knowledgeFile)
    const sizeKB = stats.size / 1024

    return {
      name: 'Knowledge base',
      category: 'files',
      status: 'pass',
      message: `${sizeKB.toFixed(1)}KB`,
    }
  } catch {
    return {
      name: 'Knowledge base',
      category: 'files',
      status: 'warn',
      message: 'Cannot read knowledge file',
    }
  }
}

function checkNodeVersion(): HealthCheck {
  const version = process.version
  const major = parseInt(version.slice(1).split('.')[0], 10)

  if (major < 18) {
    return {
      name: 'Node.js version',
      category: 'sdk',
      status: 'fail',
      message: `${version} - requires >= 18`,
    }
  }

  if (major < 20) {
    return {
      name: 'Node.js version',
      category: 'sdk',
      status: 'warn',
      message: `${version} - recommend >= 20`,
    }
  }

  return {
    name: 'Node.js version',
    category: 'sdk',
    status: 'pass',
    message: version,
  }
}

function checkDiskSpace(_cwd: string): HealthCheck {
  // This is a simplified check - in production, would use os.freemem() or df
  return {
    name: 'Disk space',
    category: 'files',
    status: 'pass',
    message: 'Check skipped (requires system call)',
  }
}

function checkRecentActivity(cwd: string): HealthCheck {
  const eventsFile = join(cwd, '.delta9', 'events.jsonl')

  if (!existsSync(eventsFile)) {
    return {
      name: 'Recent activity',
      category: 'files',
      status: 'pass',
      message: 'No events to check',
    }
  }

  try {
    const content = readFileSync(eventsFile, 'utf-8')
    const lines = content.trim().split('\n').filter(Boolean)

    if (lines.length === 0) {
      return {
        name: 'Recent activity',
        category: 'files',
        status: 'pass',
        message: 'No events recorded',
      }
    }

    const lastLine = lines[lines.length - 1]
    const lastEvent = JSON.parse(lastLine)
    const lastTime = new Date(lastEvent.timestamp)
    const ageMs = Date.now() - lastTime.getTime()
    const ageHours = ageMs / (1000 * 60 * 60)

    if (ageHours > 24) {
      return {
        name: 'Recent activity',
        category: 'files',
        status: 'warn',
        message: `No activity in ${Math.round(ageHours)} hours`,
      }
    }

    return {
      name: 'Recent activity',
      category: 'files',
      status: 'pass',
      message: `Last event: ${formatTimeAgo(ageMs)}`,
    }
  } catch {
    return {
      name: 'Recent activity',
      category: 'files',
      status: 'warn',
      message: 'Cannot parse last event',
    }
  }
}

// =============================================================================
// Report Building
// =============================================================================

function buildHealthReport(checks: HealthCheck[]): HealthReport {
  const warned = checks.filter((c) => c.status === 'warn').length
  const failed = checks.filter((c) => c.status === 'fail').length

  let status: 'healthy' | 'degraded' | 'unhealthy' = 'healthy'
  if (failed > 0) status = 'unhealthy'
  else if (warned > 0) status = 'degraded'

  return {
    status,
    checks: {
      config: summarizeCategory(checks, 'config'),
      mission: summarizeCategory(checks, 'mission'),
      background: summarizeCategory(checks, 'background'),
      sdk: summarizeCategory(checks, 'sdk'),
    },
    uptime: 'N/A',
    timestamp: new Date().toISOString(),
  }
}

function summarizeCategory(
  checks: HealthCheck[],
  category: string
): { ok: boolean; message: string } {
  const categoryChecks = checks.filter((c) => c.category === category)
  const failed = categoryChecks.filter((c) => c.status === 'fail')
  const warned = categoryChecks.filter((c) => c.status === 'warn')

  if (failed.length > 0) {
    return { ok: false, message: failed[0].message }
  }

  if (warned.length > 0) {
    return { ok: true, message: warned[0].message }
  }

  if (categoryChecks.length > 0) {
    return { ok: true, message: categoryChecks[0].message }
  }

  return { ok: true, message: 'No checks' }
}

// =============================================================================
// Output Formatting
// =============================================================================

function printSummaryFormat(checks: HealthCheck[], report: HealthReport): void {
  const width = 60

  console.log()
  console.log(colorize('┌' + '─'.repeat(width - 2) + '┐', 'cyan'))
  console.log(colorize('│', 'cyan') + '  Delta9 Health Check'.padEnd(width - 3) + colorize('│', 'cyan'))
  console.log(colorize('└' + '─'.repeat(width - 2) + '┘', 'cyan'))
  console.log()

  // Group checks by category
  const categories = ['config', 'mission', 'background', 'files', 'sdk'] as const
  const categoryNames: Record<string, string> = {
    config: 'Configuration',
    mission: 'Mission State',
    background: 'Background Tasks',
    files: 'File System',
    sdk: 'Runtime',
  }

  for (const category of categories) {
    const categoryChecks = checks.filter((c) => c.category === category)
    if (categoryChecks.length === 0) continue

    console.log(colorize(categoryNames[category] || category, 'bold'))

    for (const check of categoryChecks) {
      const icon = check.status === 'pass' ? colorize(symbols.check, 'green')
        : check.status === 'warn' ? colorize(symbols.warning, 'yellow')
        : colorize(symbols.cross, 'red')

      console.log(`  ${icon} ${check.name}: ${check.message}`)

      if (check.details) {
        console.log(colorize(`      ${check.details}`, 'dim'))
      }
    }

    console.log()
  }

  // Summary
  const passed = checks.filter((c) => c.status === 'pass').length
  const warned = checks.filter((c) => c.status === 'warn').length
  const failed = checks.filter((c) => c.status === 'fail').length

  const statusIcon = report.status === 'healthy' ? colorize(symbols.check, 'green')
    : report.status === 'degraded' ? colorize(symbols.warning, 'yellow')
    : colorize(symbols.cross, 'red')

  console.log(colorize('─'.repeat(width), 'gray'))
  console.log(`${statusIcon} Summary: ${colorize(String(passed), 'green')} passed, ${colorize(String(warned), 'yellow')} warnings, ${colorize(String(failed), 'red')} failed`)
  console.log()
}

// =============================================================================
// Helpers
// =============================================================================

function formatTimeAgo(ms: number): string {
  const seconds = Math.floor(ms / 1000)
  const minutes = Math.floor(seconds / 60)
  const hours = Math.floor(minutes / 60)
  const days = Math.floor(hours / 24)

  if (days > 0) return `${days}d ago`
  if (hours > 0) return `${hours}h ago`
  if (minutes > 0) return `${minutes}m ago`
  return `${seconds}s ago`
}
