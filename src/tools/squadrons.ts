/**
 * Delta9 Squadron Tools
 *
 * Tools for wave-based batch agent execution.
 *
 * Tools:
 * - spawn_squadron: Launch a squadron with multiple waves of agents
 * - squadron_status: Check status of a squadron
 * - wait_for_squadron: Block until squadron completes
 * - list_squadrons: List all squadrons
 */

import { tool, type ToolDefinition } from '@opencode-ai/plugin'
import type { MissionState } from '../mission/state.js'
import type { OpenCodeClient } from '../lib/background-manager.js'
import { getSquadronManager } from '../squadrons/index.js'
import { formatErrorResponse } from '../lib/errors.js'
import { quickEstimate, formatTimeout } from '../lib/timeout-estimator.js'

// Use the tool's built-in schema (Zod 4 compatible)
const s = tool.schema

// =============================================================================
// Tool Definitions
// =============================================================================

/**
 * Create squadron tools
 */
export function createSquadronTools(
  state: MissionState,
  cwd: string,
  client?: OpenCodeClient
): Record<string, ToolDefinition> {
  const getManager = () => getSquadronManager(state, cwd, client)

  /**
   * Spawn a new squadron with waves of agents
   */
  const spawn_squadron = tool({
    description: `Launch a squadron of agents executing in waves.

**Purpose:** Coordinate multiple agents in sequential waves for complex tasks.

**Wave Pattern:**
- Wave 1: Reconnaissance agents run in parallel (scout, intel)
- Wave 2: Implementation agents run in parallel (operators)
- Wave 3: Validation agents run (validator)

Each wave waits for all agents to complete before advancing to the next wave.

**Example:**
spawn_squadron({
  description: "Implement Gallery feature",
  waves: [
    { agents: [
      { type: "scout", prompt: "Map component structure..." },
      { type: "intel", prompt: "Research gallery patterns..." }
    ]},
    { agents: [
      { type: "operator", prompt: "Create GalleryGrid component..." },
      { type: "operator", prompt: "Create GalleryImage component..." }
    ]},
    { agents: [
      { type: "validator", prompt: "Verify implementation..." }
    ]}
  ]
})

**Returns:** Squadron ID and alias for tracking.

**Related:** squadron_status, wait_for_squadron, list_squadrons`,

    args: {
      description: s.string().describe('Description of what this squadron is doing'),
      waves: s
        .array(
          s.object({
            agents: s.array(
              s.object({
                type: s.string().describe('Agent type (scout, intel, operator, validator, etc.)'),
                prompt: s.string().describe('Task prompt for this agent'),
                context: s.string().optional().describe('Additional context'),
                skills: s.array(s.string()).optional().describe('Skills to load'),
              })
            ),
          })
        )
        .describe('Waves of agents to execute in order'),
      alias: s.string().optional().describe('Optional alias (auto-generated if not provided)'),
      autoAdvance: s
        .boolean()
        .optional()
        .describe('Automatically advance to next wave when current completes (default: true)'),
    },

    async execute(args, ctx) {
      try {
        const manager = getManager()

        // Extract parent session ID for Ctrl+X navigation
        const parentSessionId = (ctx as { sessionID?: string })?.sessionID

        const squadron = await manager.spawnSquadron({
          description: args.description,
          waves: args.waves,
          alias: args.alias,
          parentSessionId,
          config: {
            autoAdvance: args.autoAdvance,
          },
        })

        const totalAgents = args.waves.reduce((sum, w) => sum + w.agents.length, 0)

        return JSON.stringify({
          success: true,
          squadronId: squadron.id,
          alias: squadron.alias,
          status: squadron.status,
          totalWaves: squadron.waves.length,
          totalAgents,
          currentWave: squadron.currentWave,
          message: `Squadron "${squadron.alias}" launched with ${squadron.waves.length} waves (${totalAgents} agents)`,
          hint: 'Use squadron_status or wait_for_squadron to track progress. Use Ctrl+X Left/Right to navigate between agents.',
        })
      } catch (error) {
        return formatErrorResponse(error)
      }
    },
  })

  /**
   * Check squadron status
   */
  const squadron_status = tool({
    description: `Get status of a squadron including wave progress and agent states.

**Purpose:** Check progress of a running squadron.

**Example:**
squadron_status({ squadronId: "sqd_abc123" })
// or
squadron_status({ alias: "brave-yoda" })

**Returns:** Squadron status, current wave, and agent states.`,

    args: {
      squadronId: s.string().optional().describe('Squadron ID'),
      alias: s.string().optional().describe('Squadron alias'),
    },

    async execute(args) {
      try {
        const manager = getManager()

        let squadron
        if (args.squadronId) {
          squadron = manager.getSquadron(args.squadronId)
        } else if (args.alias) {
          squadron = manager.getByAlias(args.alias)
        } else {
          return JSON.stringify({
            success: false,
            error: 'Must provide either squadronId or alias',
          })
        }

        if (!squadron) {
          return JSON.stringify({
            success: false,
            error: `Squadron not found: ${args.squadronId || args.alias}`,
          })
        }

        // Build status summary with timeout tracking
        const waveSummaries = squadron.waves.map((wave) => {
          const completed = wave.agents.filter((a) => a.state === 'completed').length
          const failed = wave.agents.filter((a) => a.state === 'failed').length
          const running = wave.agents.filter(
            (a) => a.state === 'active' || a.state === 'spawning'
          ).length
          const pending = wave.agents.filter((a) => a.state === 'pending').length

          return {
            number: wave.number,
            status: wave.status,
            agents: {
              total: wave.agents.length,
              completed,
              failed,
              running,
              pending,
            },
            agentDetails: wave.agents.map((a) => {
              // Calculate timeout info for active agents
              const estimatedTimeoutMs = quickEstimate(a.agentType, a.prompt)
              const spawnedAt = a.spawnedAt ? new Date(a.spawnedAt).getTime() : null
              const elapsedMs = spawnedAt ? Date.now() - spawnedAt : 0
              const isActive = a.state === 'active' || a.state === 'spawning'

              return {
                type: a.agentType,
                alias: a.alias,
                state: a.state,
                hasOutput: !!a.output,
                error: a.error,
                // Timeout info (only for active/pending agents)
                timeout: isActive || a.state === 'pending' ? {
                  estimated: formatTimeout(estimatedTimeoutMs),
                  estimatedMs: estimatedTimeoutMs,
                  ...(isActive && spawnedAt ? {
                    elapsed: formatTimeout(elapsedMs),
                    elapsedMs,
                    remaining: formatTimeout(Math.max(0, estimatedTimeoutMs - elapsedMs)),
                    remainingMs: Math.max(0, estimatedTimeoutMs - elapsedMs),
                    percentComplete: Math.min(100, Math.round((elapsedMs / estimatedTimeoutMs) * 100)),
                  } : {}),
                } : undefined,
              }
            }),
          }
        })

        return JSON.stringify({
          success: true,
          squadron: {
            id: squadron.id,
            alias: squadron.alias,
            description: squadron.description,
            status: squadron.status,
            currentWave: squadron.currentWave,
            totalWaves: squadron.waves.length,
            createdAt: squadron.createdAt,
            completedAt: squadron.completedAt,
          },
          waves: waveSummaries,
        })
      } catch (error) {
        return formatErrorResponse(error)
      }
    },
  })

  /**
   * Wait for squadron to complete
   */
  const wait_for_squadron = tool({
    description: `Block until a squadron completes all waves.

**Purpose:** Wait for all agents across all waves to finish.

**Example:**
wait_for_squadron({ squadronId: "sqd_abc123" })

**Returns:** Full squadron results including all agent outputs.

**Note:** Use squadron_status for non-blocking checks.`,

    args: {
      squadronId: s.string().optional().describe('Squadron ID'),
      alias: s.string().optional().describe('Squadron alias'),
      timeoutMs: s.number().optional().describe('Timeout in milliseconds'),
    },

    async execute(args) {
      try {
        const manager = getManager()

        let squadronId = args.squadronId
        if (!squadronId && args.alias) {
          const squadron = manager.getByAlias(args.alias)
          if (!squadron) {
            return JSON.stringify({
              success: false,
              error: `Squadron not found: ${args.alias}`,
            })
          }
          squadronId = squadron.id
        }

        if (!squadronId) {
          return JSON.stringify({
            success: false,
            error: 'Must provide either squadronId or alias',
          })
        }

        const result = await manager.waitForSquadron(squadronId, args.timeoutMs)

        return JSON.stringify({
          success: true,
          result: {
            id: result.id,
            alias: result.alias,
            status: result.status,
            duration: result.duration,
            waves: result.waves.map((wave) => ({
              number: wave.number,
              status: wave.status,
              duration: wave.duration,
              agents: wave.agents.map((a) => ({
                type: a.agentType,
                alias: a.alias,
                state: a.state,
                output: a.output
                  ? `${a.output.slice(0, 500)}${a.output.length > 500 ? '...' : ''}`
                  : undefined,
                error: a.error,
                duration: a.duration,
              })),
            })),
          },
          message: `Squadron "${result.alias}" ${result.status}`,
        })
      } catch (error) {
        return formatErrorResponse(error)
      }
    },
  })

  /**
   * List squadrons
   */
  const list_squadrons = tool({
    description: `List all squadrons with optional status filter.

**Purpose:** See all squadrons and their current states.

**Example:**
list_squadrons()
list_squadrons({ status: "running" })

**Returns:** List of squadrons with summary info.`,

    args: {
      status: s
        .enum(['pending', 'running', 'completed', 'failed', 'cancelled'])
        .optional()
        .describe('Filter by status'),
    },

    async execute(args) {
      try {
        const manager = getManager()
        const squadrons = manager.listSquadrons({ status: args.status })

        if (squadrons.length === 0) {
          return JSON.stringify({
            success: true,
            squadrons: [],
            message: args.status
              ? `No squadrons with status: ${args.status}`
              : 'No squadrons found',
          })
        }

        const summaries = squadrons.map((s) => {
          const totalAgents = s.waves.reduce((sum, w) => sum + w.agents.length, 0)
          const completedAgents = s.waves.reduce(
            (sum, w) => sum + w.agents.filter((a) => a.state === 'completed').length,
            0
          )

          return {
            id: s.id,
            alias: s.alias,
            description: s.description,
            status: s.status,
            currentWave: s.currentWave,
            totalWaves: s.waves.length,
            progress: `${completedAgents}/${totalAgents} agents`,
            createdAt: s.createdAt,
          }
        })

        return JSON.stringify({
          success: true,
          squadrons: summaries,
          total: summaries.length,
        })
      } catch (error) {
        return formatErrorResponse(error)
      }
    },
  })

  /**
   * Cancel a squadron
   */
  const cancel_squadron = tool({
    description: `Cancel a running squadron.

**Purpose:** Stop a squadron that is no longer needed.

**Example:**
cancel_squadron({ squadronId: "sqd_abc123" })

**Note:** Agents that have already started may continue running.`,

    args: {
      squadronId: s.string().optional().describe('Squadron ID'),
      alias: s.string().optional().describe('Squadron alias'),
    },

    async execute(args) {
      try {
        const manager = getManager()

        let squadronId = args.squadronId
        if (!squadronId && args.alias) {
          const squadron = manager.getByAlias(args.alias)
          if (!squadron) {
            return JSON.stringify({
              success: false,
              error: `Squadron not found: ${args.alias}`,
            })
          }
          squadronId = squadron.id
        }

        if (!squadronId) {
          return JSON.stringify({
            success: false,
            error: 'Must provide either squadronId or alias',
          })
        }

        const cancelled = manager.cancelSquadron(squadronId)

        if (cancelled) {
          return JSON.stringify({
            success: true,
            message: 'Squadron cancelled',
          })
        } else {
          return JSON.stringify({
            success: false,
            error: 'Squadron not found or not running',
          })
        }
      } catch (error) {
        return formatErrorResponse(error)
      }
    },
  })

  return {
    spawn_squadron,
    squadron_status,
    wait_for_squadron,
    list_squadrons,
    cancel_squadron,
  }
}

// =============================================================================
// Type Export
// =============================================================================

export type SquadronTools = ReturnType<typeof createSquadronTools>
