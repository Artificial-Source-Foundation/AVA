/**
 * Delta9 Diagnostics Tools
 *
 * Tools for system health checks and troubleshooting:
 * - delta9_health: Comprehensive system diagnostics
 */

import { tool, type ToolDefinition } from '@opencode-ai/plugin'
import type { MissionState } from '../mission/state.js'
import type { Task } from '../types/mission.js'
import { getBackgroundManager, type OpenCodeClient } from '../lib/background-manager.js'
import { getConfig, getEnabledOracles, isCouncilEnabled } from '../lib/config.js'
import { getEventStore } from '../lib/event-store.js'
import { readHistory } from '../mission/history.js'
import { getReasoningTracer } from '../lib/reasoning-traces.js'

// Use the tool's built-in schema (Zod 4 compatible)
const s = tool.schema

// =============================================================================
// Constants
// =============================================================================

const CONCURRENCY_LIMIT = 3
let pluginStartTime = Date.now()

/**
 * Set the plugin start time (call during plugin initialization)
 */
export function setPluginStartTime(time: number): void {
  pluginStartTime = time
}

// =============================================================================
// Helpers
// =============================================================================

/**
 * Format duration in human-readable format
 */
function formatDuration(ms: number): string {
  if (ms < 1000) return `${ms}ms`
  if (ms < 60000) return `${(ms / 1000).toFixed(1)}s`
  if (ms < 3600000) return `${(ms / 60000).toFixed(1)}m`
  return `${(ms / 3600000).toFixed(1)}h`
}

/**
 * Get all tasks from mission state (across all objectives)
 */
function getAllMissionTasks(state: MissionState): Task[] {
  const mission = state.getMission()
  if (!mission) return []

  const tasks: Task[] = []
  for (const objective of mission.objectives) {
    tasks.push(...objective.tasks)
  }
  return tasks
}

/**
 * Determine overall health status
 */
function determineHealthStatus(checks: {
  sdkAvailable: boolean
  configValid: boolean
  hasMission: boolean
  failedTasks: number
  runningTasks: number
}): 'healthy' | 'degraded' | 'unhealthy' {
  // Unhealthy: critical issues
  if (!checks.configValid) {
    return 'unhealthy'
  }

  // Degraded: non-critical issues
  if (!checks.sdkAvailable || checks.failedTasks > 0) {
    return 'degraded'
  }

  return 'healthy'
}

// =============================================================================
// Tool Definitions
// =============================================================================

/**
 * Create diagnostics tools
 *
 * @param state - MissionState instance
 * @param cwd - Project root directory
 * @param client - Optional OpenCode SDK client
 */
export function createDiagnosticsTools(
  state: MissionState,
  cwd: string,
  client?: OpenCodeClient
): Record<string, ToolDefinition> {
  const manager = getBackgroundManager(state, cwd, client)

  /**
   * Check Delta9 system health
   */
  const delta9_health = tool({
    description: `Check Delta9 system health and configuration.

**Purpose:** Diagnose issues and verify system status.

**Reports on:**
- SDK connection status (live vs simulation mode)
- Mission state (active, tasks, status)
- Background task pool (active, pending, capacity)
- Configuration validity
- System uptime

**Use when:**
- Troubleshooting why tasks aren't running
- Verifying configuration is loaded correctly
- Checking system status before starting work
- Debugging unexpected behavior

**Related:** background_list, mission_status, council_status`,

    args: {
      verbose: s
        .boolean()
        .optional()
        .describe('Include detailed diagnostics (task history, mission history)'),
    },

    async execute(args, _ctx) {
      // Gather SDK status
      const sdkStatus = {
        available: !!client,
        mode: client ? 'live' : 'simulation',
      }

      // Gather mission status
      const mission = state.getMission()
      const missionTasks = getAllMissionTasks(state)
      const missionStatus = {
        active: !!mission,
        id: mission?.id,
        status: mission?.status,
        taskCount: missionTasks.length,
        completedTasks: missionTasks.filter((t) => t.status === 'completed').length,
        pendingTasks: missionTasks.filter((t) => t.status === 'pending').length,
        inProgressTasks: missionTasks.filter((t) => t.status === 'in_progress').length,
      }

      // Gather background task status
      const allBgTasks = manager.listTasks()
      const backgroundStatus = {
        active: manager.getActiveCount(),
        pending: manager.getPendingCount(),
        maxConcurrency: CONCURRENCY_LIMIT,
        utilization: `${Math.round((manager.getActiveCount() / CONCURRENCY_LIMIT) * 100)}%`,
        totalTasks: allBgTasks.length,
        completedTasks: allBgTasks.filter((t) => t.status === 'completed').length,
        failedTasks: allBgTasks.filter((t) => t.status === 'failed').length,
        cancelledTasks: allBgTasks.filter((t) => t.status === 'cancelled').length,
      }

      // Gather config status
      let configStatus: { loaded: boolean; valid: boolean; errors?: string[] }
      try {
        getConfig() // Just verify config loads
        configStatus = {
          loaded: true,
          valid: true,
        }
        // Check council config
        if (isCouncilEnabled(cwd)) {
          const oracles = getEnabledOracles(cwd)
          if (oracles.length === 0) {
            configStatus.errors = configStatus.errors || []
            configStatus.errors.push('Council enabled but no oracles configured')
          }
        }
      } catch (error) {
        configStatus = {
          loaded: false,
          valid: false,
          errors: [error instanceof Error ? error.message : String(error)],
        }
      }

      // Determine overall health
      const healthStatus = determineHealthStatus({
        sdkAvailable: sdkStatus.available,
        configValid: configStatus.valid,
        hasMission: missionStatus.active,
        failedTasks: backgroundStatus.failedTasks,
        runningTasks: backgroundStatus.active,
      })

      // Build health report
      const health: Record<string, unknown> = {
        status: healthStatus,
        statusEmoji:
          healthStatus === 'healthy'
            ? '\\u2705'
            : healthStatus === 'degraded'
              ? '\\u26A0\\uFE0F'
              : '\\u274C',
        timestamp: new Date().toISOString(),
        uptime: formatDuration(Date.now() - pluginStartTime),

        sdk: sdkStatus,
        mission: missionStatus,
        backgroundTasks: backgroundStatus,
        config: configStatus,

        summary: buildSummary(healthStatus, sdkStatus, missionStatus, backgroundStatus),
      }

      // Add verbose details if requested
      if (args.verbose) {
        health.details = {
          recentTasks: allBgTasks.slice(-10).map((t) => {
            const startTime = t.startedAt ? new Date(t.startedAt).getTime() : undefined
            const endTime = t.completedAt
              ? new Date(t.completedAt).getTime()
              : t.status === 'running'
                ? Date.now()
                : undefined
            return {
              id: t.id,
              agent: t.agent,
              status: t.status,
              duration: startTime && endTime ? formatDuration(endTime - startTime) : '-',
            }
          }),
          missionTasks: missionTasks.slice(-5).map((t) => ({
            id: t.id,
            description: t.description.slice(0, 50) + (t.description.length > 50 ? '...' : ''),
            status: t.status,
          })),
        }
      }

      return JSON.stringify(health, null, 2)
    },
  })

  /**
   * Unified event log - aggregate events from all sources
   */
  const delta9_events = tool({
    description: `View unified event log from all Delta9 subsystems.

**Purpose:** Single view of all system events for debugging and audit.

**Event sources:**
- Mission history (objectives, tasks, status changes)
- Background tasks (spawn, complete, fail)
- Event store (persistence events)
- Council events (convene, deliberate, conclude)

**Example:**
delta9_events()                    # Last 20 events
delta9_events({ limit: 50 })       # Last 50 events
delta9_events({ type: "task" })    # Only task events
delta9_events({ since: "1h" })     # Events from last hour

**Returns:** Chronologically sorted events with source, type, and details.

**Use when:**
- Debugging what happened during a mission
- Auditing decision trails
- Troubleshooting failures
- Understanding system behavior`,

    args: {
      limit: s.number().optional().describe('Maximum events to return (default: 20)'),
      type: s
        .enum(['all', 'mission', 'task', 'council', 'background', 'error'])
        .optional()
        .describe('Filter by event type'),
      since: s
        .string()
        .optional()
        .describe('Time filter: "1h", "30m", "1d" (default: all)'),
    },

    async execute(args) {
      const limit = args.limit ?? 20
      const typeFilter = args.type ?? 'all'

      // Calculate time boundary if specified
      let sinceTimestamp: number | undefined
      if (args.since) {
        const match = args.since.match(/^(\d+)(m|h|d)$/)
        if (match) {
          const value = parseInt(match[1], 10)
          const unit = match[2]
          const multipliers: Record<string, number> = {
            m: 60 * 1000,
            h: 60 * 60 * 1000,
            d: 24 * 60 * 60 * 1000,
          }
          sinceTimestamp = Date.now() - value * multipliers[unit]
        }
      }

      // Collect events from all sources
      const allEvents: Array<{
        timestamp: string
        source: string
        type: string
        summary: string
        details?: Record<string, unknown>
      }> = []

      // 1. Mission history events
      try {
        const history = readHistory(cwd)
        for (const entry of history) {
          const entryTime = new Date(entry.timestamp).getTime()
          if (sinceTimestamp && entryTime < sinceTimestamp) continue

          // Map history types to our categories
          const eventCategory = categorizeHistoryEvent(entry.type)
          if (typeFilter !== 'all' && eventCategory !== typeFilter) continue

          allEvents.push({
            timestamp: entry.timestamp,
            source: 'mission',
            type: entry.type,
            summary: summarizeHistoryEvent(entry),
            details: entry.data as Record<string, unknown> | undefined,
          })
        }
      } catch {
        // History may not exist yet
      }

      // 2. Background task events
      const bgTasks = manager.listTasks()
      for (const task of bgTasks) {
        // Task creation event (using queuedAt)
        if (task.queuedAt) {
          const entryTime = new Date(task.queuedAt).getTime()
          if (!sinceTimestamp || entryTime >= sinceTimestamp) {
            if (typeFilter === 'all' || typeFilter === 'background' || typeFilter === 'task') {
              allEvents.push({
                timestamp: task.queuedAt,
                source: 'background',
                type: 'task_created',
                summary: `Background task created: ${task.agent} - ${task.prompt.slice(0, 50)}...`,
                details: { taskId: task.id, agent: task.agent },
              })
            }
          }
        }

        // Task completion/failure event
        if (task.completedAt) {
          const entryTime = new Date(task.completedAt).getTime()
          if (!sinceTimestamp || entryTime >= sinceTimestamp) {
            const isError = task.status === 'failed'
            if (typeFilter === 'all' || typeFilter === 'background' || (isError && typeFilter === 'error')) {
              allEvents.push({
                timestamp: task.completedAt,
                source: 'background',
                type: task.status === 'completed' ? 'task_completed' : 'task_failed',
                summary: `Background task ${task.status}: ${task.agent}${task.error ? ` - ${task.error}` : ''}`,
                details: { taskId: task.id, agent: task.agent, status: task.status, error: task.error },
              })
            }
          }
        }
      }

      // 3. Event store events
      try {
        const eventStore = getEventStore(cwd)
        const storeEvents = eventStore.read()
        // Take last N events (read returns all, so we slice)
        const recentStoreEvents = storeEvents.slice(-limit * 2)
        for (const event of recentStoreEvents) {
          const entryTime = new Date(event.timestamp).getTime()
          if (sinceTimestamp && entryTime < sinceTimestamp) continue

          const eventCategory = event.type.includes('error') ? 'error' : 'task'
          if (typeFilter !== 'all' && eventCategory !== typeFilter) continue

          allEvents.push({
            timestamp: event.timestamp,
            source: 'event_store',
            type: event.type,
            summary: `Event: ${event.type}`,
            details: event.payload as Record<string, unknown>,
          })
        }
      } catch {
        // Event store may not be initialized
      }

      // Sort all events by timestamp (newest first)
      allEvents.sort((a, b) => new Date(b.timestamp).getTime() - new Date(a.timestamp).getTime())

      // Apply limit
      const limitedEvents = allEvents.slice(0, limit)

      // Build summary
      const eventCounts: Record<string, number> = {}
      for (const event of allEvents) {
        eventCounts[event.source] = (eventCounts[event.source] || 0) + 1
      }

      return JSON.stringify({
        success: true,
        events: limitedEvents,
        summary: {
          total: allEvents.length,
          returned: limitedEvents.length,
          bySource: eventCounts,
          timeRange: limitedEvents.length > 0 ? {
            oldest: limitedEvents[limitedEvents.length - 1].timestamp,
            newest: limitedEvents[0].timestamp,
          } : null,
        },
        filters: {
          type: typeFilter,
          since: args.since || 'all',
          limit,
        },
      }, null, 2)
    },
  })

  /**
   * Performance metrics dashboard
   */
  const delta9_metrics = tool({
    description: `View performance metrics dashboard for Delta9 agents and tasks.

**Purpose:** Monitor system performance, identify bottlenecks, and track trends.

**Metrics tracked:**
- Agent performance (success rate, avg duration, task count)
- Task duration statistics (min, max, avg, p50, p95)
- Model usage distribution
- Reasoning trace statistics
- Throughput trends

**Example:**
delta9_metrics()                     # Full dashboard
delta9_metrics({ period: "1h" })     # Last hour only
delta9_metrics({ agent: "operator" }) # Filter by agent
delta9_metrics({ focus: "latency" }) # Focus on latency metrics

**Returns:** Comprehensive performance statistics with trend analysis.

**Use when:**
- Identifying slow agents or bottlenecks
- Monitoring system throughput
- Analyzing model performance
- Debugging latency issues
- Capacity planning`,

    args: {
      period: s
        .enum(['1h', '24h', '7d', 'all'])
        .optional()
        .describe('Time period to analyze (default: all)'),
      agent: s
        .string()
        .optional()
        .describe('Filter metrics by specific agent name'),
      focus: s
        .enum(['overview', 'latency', 'agents', 'models', 'errors'])
        .optional()
        .describe('Focus area for detailed metrics (default: overview)'),
    },

    async execute(args) {
      const period = args.period ?? 'all'
      const focus = args.focus ?? 'overview'

      // Calculate time boundary
      let sinceTimestamp: number | undefined
      if (period !== 'all') {
        const periodMs: Record<string, number> = {
          '1h': 60 * 60 * 1000,
          '24h': 24 * 60 * 60 * 1000,
          '7d': 7 * 24 * 60 * 60 * 1000,
        }
        sinceTimestamp = Date.now() - periodMs[period]
      }

      // Collect background task data
      const allTasks = manager.listTasks()
      const filteredTasks = allTasks.filter((task) => {
        if (sinceTimestamp && task.queuedAt) {
          const taskTime = new Date(task.queuedAt).getTime()
          if (taskTime < sinceTimestamp) return false
        }
        if (args.agent && task.agent !== args.agent) return false
        return true
      })

      // Agent performance metrics
      const agentMetrics: Record<string, {
        taskCount: number
        completed: number
        failed: number
        durations: number[]
        errors: string[]
      }> = {}

      for (const task of filteredTasks) {
        const agent = task.agent || 'unknown'
        if (!agentMetrics[agent]) {
          agentMetrics[agent] = {
            taskCount: 0,
            completed: 0,
            failed: 0,
            durations: [],
            errors: [],
          }
        }

        agentMetrics[agent].taskCount++

        if (task.status === 'completed') {
          agentMetrics[agent].completed++
          if (task.startedAt && task.completedAt) {
            const duration = new Date(task.completedAt).getTime() - new Date(task.startedAt).getTime()
            agentMetrics[agent].durations.push(duration)
          }
        } else if (task.status === 'failed') {
          agentMetrics[agent].failed++
          if (task.error) {
            agentMetrics[agent].errors.push(task.error)
          }
        }
      }

      // Calculate agent statistics
      const agentStats = Object.entries(agentMetrics).map(([agent, metrics]) => {
        const successRate = metrics.taskCount > 0
          ? (metrics.completed / metrics.taskCount) * 100
          : 0
        const avgDuration = metrics.durations.length > 0
          ? metrics.durations.reduce((a, b) => a + b, 0) / metrics.durations.length
          : 0
        const sortedDurations = [...metrics.durations].sort((a, b) => a - b)
        const p50 = sortedDurations[Math.floor(sortedDurations.length * 0.5)] || 0
        const p95 = sortedDurations[Math.floor(sortedDurations.length * 0.95)] || 0

        return {
          agent,
          taskCount: metrics.taskCount,
          completed: metrics.completed,
          failed: metrics.failed,
          successRate: `${successRate.toFixed(1)}%`,
          avgDuration: formatDuration(avgDuration),
          p50Duration: formatDuration(p50),
          p95Duration: formatDuration(p95),
          minDuration: metrics.durations.length > 0 ? formatDuration(Math.min(...metrics.durations)) : '-',
          maxDuration: metrics.durations.length > 0 ? formatDuration(Math.max(...metrics.durations)) : '-',
          recentErrors: metrics.errors.slice(-3),
        }
      }).sort((a, b) => b.taskCount - a.taskCount)

      // Overall latency statistics
      const allDurations: number[] = []
      for (const metrics of Object.values(agentMetrics)) {
        allDurations.push(...metrics.durations)
      }
      const sortedAllDurations = [...allDurations].sort((a, b) => a - b)

      const latencyStats = {
        sampleCount: allDurations.length,
        min: allDurations.length > 0 ? formatDuration(Math.min(...allDurations)) : '-',
        max: allDurations.length > 0 ? formatDuration(Math.max(...allDurations)) : '-',
        avg: allDurations.length > 0
          ? formatDuration(allDurations.reduce((a, b) => a + b, 0) / allDurations.length)
          : '-',
        p50: sortedAllDurations[Math.floor(sortedAllDurations.length * 0.5)]
          ? formatDuration(sortedAllDurations[Math.floor(sortedAllDurations.length * 0.5)])
          : '-',
        p95: sortedAllDurations[Math.floor(sortedAllDurations.length * 0.95)]
          ? formatDuration(sortedAllDurations[Math.floor(sortedAllDurations.length * 0.95)])
          : '-',
        p99: sortedAllDurations[Math.floor(sortedAllDurations.length * 0.99)]
          ? formatDuration(sortedAllDurations[Math.floor(sortedAllDurations.length * 0.99)])
          : '-',
      }

      // Throughput calculation
      const completedTasks = filteredTasks.filter((t) => t.status === 'completed')
      const failedTasks = filteredTasks.filter((t) => t.status === 'failed')

      let throughput = {
        total: filteredTasks.length,
        completed: completedTasks.length,
        failed: failedTasks.length,
        inProgress: filteredTasks.filter((t) => t.status === 'running').length,
        successRate: filteredTasks.length > 0
          ? `${((completedTasks.length / filteredTasks.length) * 100).toFixed(1)}%`
          : '-',
        tasksPerHour: '-' as string | number,
      }

      // Calculate tasks per hour if we have timestamp data
      if (filteredTasks.length > 0 && period !== 'all') {
        const periodMs: Record<string, number> = {
          '1h': 60 * 60 * 1000,
          '24h': 24 * 60 * 60 * 1000,
          '7d': 7 * 24 * 60 * 60 * 1000,
        }
        const hours = periodMs[period] / (60 * 60 * 1000)
        throughput.tasksPerHour = (filteredTasks.length / hours).toFixed(2)
      }

      // Error analysis
      const errorCategories: Record<string, number> = {}
      for (const task of failedTasks) {
        if (task.error) {
          // Categorize errors
          let category = 'unknown'
          if (task.error.includes('timeout')) category = 'timeout'
          else if (task.error.includes('rate limit') || task.error.includes('429')) category = 'rate_limit'
          else if (task.error.includes('auth') || task.error.includes('401')) category = 'auth'
          else if (task.error.includes('parse') || task.error.includes('JSON')) category = 'parse_error'
          else if (task.error.includes('network') || task.error.includes('ECONNREFUSED')) category = 'network'
          else category = 'other'

          errorCategories[category] = (errorCategories[category] || 0) + 1
        }
      }

      // Reasoning trace statistics
      let reasoningStats: {
        totalTraces: number
        activeTraces: number
        completedTraces: number
        totalSteps: number
        avgStepsPerTrace: number
      } | undefined

      try {
        const tracer = getReasoningTracer()
        const stats = tracer.getStats()
        reasoningStats = {
          totalTraces: stats.totalTraces,
          activeTraces: stats.activeTraces,
          completedTraces: stats.completedTraces,
          totalSteps: stats.totalSteps,
          avgStepsPerTrace: stats.avgStepsPerTrace,
        }
      } catch {
        // Tracer may not be available
      }

      // Build response based on focus
      const response: Record<string, unknown> = {
        success: true,
        period,
        focus,
        generatedAt: new Date().toISOString(),
      }

      if (focus === 'overview' || focus === 'agents') {
        response.agentPerformance = agentStats.slice(0, 10)
      }

      if (focus === 'overview' || focus === 'latency') {
        response.latency = latencyStats
      }

      if (focus === 'overview') {
        response.throughput = throughput
      }

      if (focus === 'overview' || focus === 'errors') {
        response.errors = {
          total: failedTasks.length,
          byCategory: errorCategories,
          recentErrors: failedTasks.slice(-5).map((t) => ({
            agent: t.agent,
            error: t.error?.slice(0, 100),
            timestamp: t.completedAt,
          })),
        }
      }

      if (focus === 'overview' && reasoningStats) {
        response.reasoning = reasoningStats
      }

      // Add summary recommendations
      response.recommendations = generateRecommendations(agentStats, latencyStats, errorCategories, throughput)

      return JSON.stringify(response, null, 2)
    },
  })

  return {
    delta9_health,
    delta9_events,
    delta9_metrics,
  }
}

/**
 * Categorize history event type into our filter categories
 */
function categorizeHistoryEvent(type: string): string {
  if (type.includes('council')) return 'council'
  if (type.includes('mission')) return 'mission'
  if (type.includes('task') || type.includes('objective')) return 'task'
  if (type.includes('error') || type.includes('fail')) return 'error'
  return 'mission'
}

/**
 * Generate a summary for a history event
 */
function summarizeHistoryEvent(entry: { type: string; data?: unknown }): string {
  const data = entry.data as Record<string, unknown> | undefined
  switch (entry.type) {
    case 'mission_created':
      return `Mission created: ${data?.description || 'No description'}`
    case 'mission_completed':
      return 'Mission completed'
    case 'objective_added':
      return `Objective added: ${data?.description || 'No description'}`
    case 'task_started':
      return `Task started: ${data?.taskId || 'unknown'}`
    case 'task_completed':
      return `Task completed: ${data?.taskId || 'unknown'}`
    case 'council_convened':
      return `Council convened: ${data?.mode || 'unknown'} mode with ${data?.oracleCount || '?'} oracles`
    case 'council_completed':
      return `Council completed: ${(data?.confidenceAvg as number)?.toFixed?.(2) || '?'} avg confidence`
    default:
      return entry.type.replace(/_/g, ' ')
  }
}

// =============================================================================
// Helpers
// =============================================================================

function buildSummary(
  status: string,
  sdk: { available: boolean; mode: string },
  mission: { active: boolean; taskCount: number },
  background: { active: number; pending: number; failedTasks: number }
): string[] {
  const summary: string[] = []

  if (status === 'healthy') {
    summary.push('System is healthy')
  } else if (status === 'degraded') {
    summary.push('System is degraded - some features may be limited')
  } else {
    summary.push('System is unhealthy - check configuration')
  }

  if (!sdk.available) {
    summary.push('Running in simulation mode (SDK not available)')
  }

  if (!mission.active) {
    summary.push('No active mission - use mission_create to start')
  } else {
    summary.push(`Mission active with ${mission.taskCount} task(s)`)
  }

  if (background.active > 0) {
    summary.push(`${background.active} background task(s) running`)
  }

  if (background.pending > 0) {
    summary.push(`${background.pending} task(s) queued`)
  }

  if (background.failedTasks > 0) {
    summary.push(`${background.failedTasks} failed task(s) - check background_output`)
  }

  return summary
}

// =============================================================================
// Recommendation Generator
// =============================================================================

interface AgentStat {
  agent: string
  taskCount: number
  successRate: string
  avgDuration: string
  p95Duration: string
  failed: number
}

/**
 * Generate actionable recommendations based on metrics
 */
function generateRecommendations(
  agentStats: AgentStat[],
  latencyStats: { sampleCount: number; p95: string },
  errorCategories: Record<string, number>,
  throughput: { successRate: string; total: number }
): string[] {
  const recommendations: string[] = []

  // Check for low success rates
  for (const agent of agentStats) {
    const rate = parseFloat(agent.successRate)
    if (rate < 80 && agent.taskCount >= 3) {
      recommendations.push(
        `Agent "${agent.agent}" has low success rate (${agent.successRate}). Review task complexity and error patterns.`
      )
    }
  }

  // Check for timeout issues
  if (errorCategories['timeout'] && errorCategories['timeout'] > 2) {
    recommendations.push(
      `High timeout count (${errorCategories['timeout']}). Consider increasing timeout limits or using adaptive timeouts.`
    )
  }

  // Check for rate limit issues
  if (errorCategories['rate_limit'] && errorCategories['rate_limit'] > 1) {
    recommendations.push(
      `Rate limit errors detected (${errorCategories['rate_limit']}). Consider enabling model fallbacks or reducing concurrency.`
    )
  }

  // Check overall success rate
  const overallRate = parseFloat(throughput.successRate)
  if (!isNaN(overallRate) && overallRate < 90 && throughput.total >= 5) {
    recommendations.push(
      `Overall success rate (${throughput.successRate}) is below 90%. Review error distribution and failure patterns.`
    )
  }

  // Check for high latency
  if (latencyStats.sampleCount >= 5) {
    const p95Ms = parseFloat(latencyStats.p95)
    if (!isNaN(p95Ms) && p95Ms > 300000) {  // > 5 minutes
      recommendations.push(
        `P95 latency is high (${latencyStats.p95}). Consider parallel execution or task decomposition.`
      )
    }
  }

  // Add positive feedback if things are good
  if (recommendations.length === 0 && throughput.total >= 5) {
    recommendations.push('System performance is healthy. No immediate actions required.')
  }

  return recommendations
}

// =============================================================================
// Type Export
// =============================================================================

export type DiagnosticsTools = ReturnType<typeof createDiagnosticsTools>
