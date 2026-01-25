/**
 * Delta9 - OpenCode Plugin
 *
 * Strategic AI Coordination for Mission-Critical Development
 *
 * Implements Commander + Council + Operators architecture:
 * - Commander: Lead planner & orchestrator (never writes code)
 * - Council: Multiple oracles for strategic decisions
 * - Operators: Task executors
 * - Validator: QA gate for all completed work
 */

import type { Plugin, Hooks } from '@opencode-ai/plugin'
import { MissionState } from './mission/state.js'
import { createDelta9Tools, type OpenCodeClient } from './tools/index.js'
import { createDelta9Hooks } from './hooks/index.js'
import { initLogger, getNamedLogger } from './lib/logger.js'

const Delta9: Plugin = async (ctx) => {
  const cwd = ctx.worktree || ctx.directory

  // Extract OpenCode client from context (for SDK integration)
  // The client enables real agent execution in background tasks
  // We cast through unknown since the PluginInput type has more specific return types
  const client = (ctx as unknown as { client?: OpenCodeClient }).client

  // Initialize structured logger with OpenCode client if available
  initLogger(client)
  const log = getNamedLogger('core')

  log.info('Plugin loading...', { cwd })

  // Initialize mission state
  const missionState = new MissionState(cwd)

  // Create logger function for hooks (wrapper for backward compatibility)
  const hookLog = (level: string, message: string, data?: Record<string, unknown>) => {
    const hookLogger = getNamedLogger('hooks')
    switch (level) {
      case 'debug':
        hookLogger.debug(message, data)
        break
      case 'warn':
        hookLogger.warn(message, data)
        break
      case 'error':
        hookLogger.error(message, data)
        break
      default:
        hookLogger.info(message, data)
    }
  }

  // Create all tools (mission, dispatch, validation, delegation, background, council, memory, diagnostics)
  // Pass client for SDK integration in delegation and background tools
  const tools = createDelta9Tools(missionState, cwd, client)

  // Create all hooks (session lifecycle, tool output, recovery)
  const hooks = createDelta9Hooks({
    state: missionState,
    cwd,
    log: hookLog,
  })

  log.info('Plugin loaded', {
    tools: Object.keys(tools).length,
    sdk: client ? 'available' : 'simulation',
  })

  return {
    // All Delta9 tools
    tool: tools,

    // Session lifecycle events (created, compacted, deleted, idle, error)
    event: hooks.event,

    // Tool hooks for context injection and recovery
    'tool.execute.before': hooks['tool.execute.before'],
    'tool.execute.after': hooks['tool.execute.after'],

    // Config handler to hide built-in agents
    async config(config) {
      const existingAgents = (config.agent as Record<string, Record<string, unknown>>) ?? {}
      const existingBuild = existingAgents.build ?? {}
      const existingPlan = existingAgents.plan ?? {}

      // Reassign config.agent with build and plan hidden/demoted
      config.agent = {
        ...existingAgents,
        build: { ...existingBuild, mode: 'subagent', hidden: true },
        plan: { ...existingPlan, mode: 'subagent', hidden: true },
      }
    },
  } satisfies Hooks
}

export default Delta9
