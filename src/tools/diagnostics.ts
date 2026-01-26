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

  return {
    delta9_health,
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
// Type Export
// =============================================================================

export type DiagnosticsTools = ReturnType<typeof createDiagnosticsTools>
