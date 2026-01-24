/**
 * Delta9 - OpenCode Plugin
 *
 * Strategic AI Coordination for Mission-Critical Development
 *
 * This plugin implements a hierarchical multi-agent system with:
 * - Commander: Strategic planning and orchestration
 * - Operators: Task execution
 * - Validator: Quality verification
 * - Persistent mission state via mission.json
 */

import { loadConfig, getSeamlessConfig } from './lib/config.js'
import { createLogger, setDefaultLogger, type Logger } from './lib/logger.js'
import { MissionState } from './mission/state.js'
import { missionExists } from './lib/paths.js'
import {
  commanderAgent,
  commanderPlanningAgent,
  commanderExecutionAgent,
  operatorAgent,
  operatorComplexAgent,
  validatorAgent,
  validatorStrictAgent,
} from './agents/index.js'
import { createDelta9Tools } from './tools/index.js'

// =============================================================================
// Plugin Types
// =============================================================================

/**
 * OpenCode plugin context
 */
export interface PluginContext {
  /** Project information */
  project: {
    path: string
    name?: string
  }
  /** OpenCode client */
  client: {
    app: {
      log: (message: string) => void
    }
  }
  /** Shell execution */
  $: (command: string) => Promise<{ stdout: string; stderr: string; exitCode: number }>
  /** Current working directory */
  directory: string
}

/**
 * Plugin return type
 */
export interface PluginExport {
  name: string
  agents?: Record<string, unknown>
  tools?: Record<string, unknown>
  hooks?: Record<string, (...args: unknown[]) => Promise<void>>
}

// =============================================================================
// Plugin State
// =============================================================================

let missionState: MissionState | null = null
let logger: Logger | null = null

// =============================================================================
// Plugin Initialization
// =============================================================================

/**
 * Initialize Delta9 plugin
 */
async function initializeDelta9(ctx: PluginContext): Promise<{
  state: MissionState
  logger: Logger
}> {
  const cwd = ctx.project.path || ctx.directory

  // Create logger
  logger = createLogger(ctx.client, { plugin: 'delta9' }, 'info')
  setDefaultLogger(logger)

  // Load configuration (validates and caches)
  loadConfig(cwd)

  // Initialize mission state
  missionState = new MissionState(cwd)

  // Load existing mission if present
  if (missionExists(cwd)) {
    const mission = missionState.load()
    if (mission) {
      logger.info('Loaded existing mission', { missionId: mission.id, status: mission.status })
    }
  }

  logger.info('Delta9 initialized', { cwd })

  return { state: missionState, logger }
}

// =============================================================================
// Plugin Export
// =============================================================================

/**
 * Delta9 OpenCode Plugin
 */
export async function delta9(ctx: PluginContext): Promise<PluginExport> {
  const { state, logger } = await initializeDelta9(ctx)
  const cwd = ctx.project.path || ctx.directory
  const seamless = getSeamlessConfig(cwd)

  // Create tools
  const tools = createDelta9Tools(state)

  // Build agent registry
  const agents: Record<string, unknown> = {
    // Core agents always available
    commander: commanderAgent,
    'commander-planning': commanderPlanningAgent,
    'commander-execution': commanderExecutionAgent,
    operator: operatorAgent,
    'operator-complex': operatorComplexAgent,
    validator: validatorAgent,
    'validator-strict': validatorStrictAgent,
  }

  // Seamless integration: replace default agents if configured
  if (seamless.replaceBuild) {
    agents.build = commanderExecutionAgent
    logger.debug('Replaced default build agent with Commander')
  }

  if (seamless.replacePlan) {
    agents.plan = commanderPlanningAgent
    logger.debug('Replaced default plan agent with Commander (planning mode)')
  }

  return {
    name: 'delta9',

    agents,

    tools: tools as unknown as Record<string, unknown>,

    hooks: {
      /**
       * Session created - load mission state
       */
      'session.created': async () => {
        if (missionExists(cwd)) {
          const mission = state.load()
          if (mission) {
            logger.info('Session started with active mission', {
              missionId: mission.id,
              status: mission.status,
              progress: state.getProgress(),
            })
          }
        }
      },

      /**
       * Session idle - check for pending tasks
       */
      'session.idle': async () => {
        const mission = state.getMission()
        if (!mission) return

        if (mission.status === 'in_progress') {
          const nextTask = state.getNextTask()
          if (nextTask) {
            logger.info('Pending task available', {
              taskId: nextTask.id,
              description: nextTask.description.substring(0, 50),
            })
          }
        }
      },

      /**
       * Tool execution - before hook
       */
      'tool.execute.before': async (_input: unknown) => {
        // Could be used for logging or cost tracking
      },

      /**
       * Tool execution - after hook
       */
      'tool.execute.after': async (_input: unknown, _output: unknown) => {
        // Could be used for logging or cost tracking
      },
    },
  }
}

// =============================================================================
// Exports
// =============================================================================

// Default export for OpenCode plugin loading
export default delta9

// Named exports for programmatic use
export { MissionState } from './mission/index.js'
export { loadConfig, getConfig } from './lib/config.js'
export { createLogger, type Logger } from './lib/logger.js'
export {
  commanderAgent,
  operatorAgent,
  validatorAgent,
} from './agents/index.js'
export { createDelta9Tools } from './tools/index.js'
export * from './types/index.js'
