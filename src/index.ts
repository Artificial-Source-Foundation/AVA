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
import type { AgentConfig } from '@opencode-ai/sdk'
import { MissionState } from './mission/state.js'
import { createDelta9Tools, type OpenCodeClient } from './tools/index.js'
import { createDelta9Hooks } from './hooks/index.js'
import { initLogger, getNamedLogger } from './lib/logger.js'
import { loadConfig } from './lib/config.js'
import { commanderAgent } from './agents/commander.js'
import { operatorAgent } from './agents/operator.js'
import { validatorAgent } from './agents/validator.js'
// Support agents - Delta Team (registry for dynamic creation)
import { supportAgentFactories, codenameToConfigKey } from './agents/support/index.js'
// Council agents - Oracles
import { cipherAgent, vectorAgent, prismAgent, apexAgent } from './agents/council/index.js'

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

  // Load Delta9 configuration (from ~/.config/opencode/delta9.json or .delta9/config.json)
  const delta9Config = loadConfig(cwd)

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
    commanderModel: delta9Config.commander.model,
  })

  return {
    // All Delta9 tools
    tool: tools,

    // Session lifecycle events (created, compacted, deleted, idle, error)
    event: hooks.event,

    // Tool hooks for context injection and recovery
    'tool.execute.before': hooks['tool.execute.before'],
    'tool.execute.after': hooks['tool.execute.after'],

    // Config handler to register Delta9 agents and set commander as default
    async config(config) {
      const existingAgents = (config.agent as Record<string, Record<string, unknown>>) ?? {}
      const existingBuild = existingAgents.build ?? {}
      const existingPlan = existingAgents.plan ?? {}

      // Apply models from Delta9 config to agents
      const configuredCommander = {
        ...commanderAgent,
        model: delta9Config.commander.model,
        temperature: delta9Config.commander.temperature,
      }

      const configuredOperator = {
        ...operatorAgent,
        model: delta9Config.operators.defaultModel,
      }

      const configuredValidator = {
        ...validatorAgent,
        model: delta9Config.validator.model,
      }

      // Create support agents with cwd context
      // Use lowercase names for delegate_task compatibility
      const supportAgents: Record<string, AgentConfig> = {}
      for (const [codename, factory] of Object.entries(supportAgentFactories)) {
        const configKey = codenameToConfigKey[codename as keyof typeof codenameToConfigKey]
        supportAgents[configKey] = {
          ...factory(cwd),
          mode: 'subagent' as const,
        }
      }

      // Configure council agents with models from config
      const councilAgents: Record<string, AgentConfig> = {
        cipher: { ...cipherAgent, mode: 'subagent' as const },
        vector: { ...vectorAgent, mode: 'subagent' as const },
        prism: { ...prismAgent, mode: 'subagent' as const },
        apex: { ...apexAgent, mode: 'subagent' as const },
      }

      // Apply council member models from config if specified
      const councilMembers = delta9Config.council?.members ?? []
      for (const member of councilMembers) {
        const agentKey = member.name.toLowerCase()
        if (councilAgents[agentKey]) {
          councilAgents[agentKey] = {
            ...councilAgents[agentKey],
            model: member.model,
            temperature: member.temperature,
          }
        }
      }

      // Merge Delta9 agents into config, hiding built-in agents
      // Delta9 agents MUST be added here for prompts to be applied
      config.agent = {
        // Existing agents from config
        ...existingAgents,
        // Delta9 agents with models from config
        commander: configuredCommander,
        operator: configuredOperator,
        validator: configuredValidator,
        // Support agents (Delta Team)
        ...supportAgents,
        // Council agents (Oracles)
        ...councilAgents,
        // Hide built-in agents so Commander is the primary interface
        build: { ...existingBuild, mode: 'subagent', hidden: true },
        plan: { ...existingPlan, mode: 'subagent', hidden: true },
      }

      // Set commander as the default agent
      const configWithDefault = config as { default_agent?: string }
      if (!configWithDefault.default_agent) {
        configWithDefault.default_agent = 'commander'
      }
    },
  } satisfies Hooks
}

export default Delta9
