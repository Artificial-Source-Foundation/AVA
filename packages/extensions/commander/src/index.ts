/**
 * Commander extension — Praxis v2 hierarchy.
 *
 * Hierarchy: Director -> Tech Leads -> Engineers -> Reviewer
 */

import type { AgentMode, Disposable, ExtensionAPI } from '@ava/core-v2/extensions'
import type { ToolDefinition } from '@ava/core-v2/llm'
import { getModelPack, resolveModelForTier } from '../../models/src/packs.js'
import type { AgentDefinition } from './agent-definition.js'
import { configureDelegation } from './delegate.js'
import { createInvokeSubagentTool } from './invoke-subagent.js'
import { createInvokeTeamTool } from './invoke-team.js'
import { getAgentsByTier, registerAgents } from './registry.js'
import { BUILTIN_AGENTS } from './workers.js'

export function activate(api: ExtensionAPI): Disposable {
  const disposables: Disposable[] = []

  // Check if commander is disabled via settings (default: enabled)
  try {
    const config = api.getSettings<{ enabled?: boolean }>('commander')
    if (config?.enabled === false) {
      api.log.debug('Commander extension disabled via settings')
      return { dispose() {} }
    }
  } catch {
    // Settings category not registered — default to enabled
  }

  // Apply delegation settings (isolation, etc.)
  try {
    const delegateConfig = api.getSettings<{ isolation?: boolean; maxRetries?: number }>(
      'commander'
    )
    if (delegateConfig?.isolation !== undefined || delegateConfig?.maxRetries !== undefined) {
      configureDelegation({
        ...(delegateConfig.isolation !== undefined ? { isolation: delegateConfig.isolation } : {}),
        ...(delegateConfig.maxRetries !== undefined
          ? { maxRetries: delegateConfig.maxRetries }
          : {}),
      })
    }
  } catch {
    // Settings category not registered — use defaults
  }

  // Apply model pack to agents if configured
  const agentsToRegister = applyModelPack(api, BUILTIN_AGENTS)

  // Register all built-in agents in the registry
  disposables.push(registerAgents(agentsToRegister))

  disposables.push(api.registerTool(createInvokeTeamTool('director')))
  disposables.push(api.registerTool(createInvokeSubagentTool()))

  // Register the praxis agent mode
  const praxisMode: AgentMode = {
    name: 'praxis',
    description: 'Praxis mode: 4-tier hierarchy (Director -> Tech Lead -> Engineer -> Reviewer)',

    filterTools(tools: ToolDefinition[]): ToolDefinition[] {
      return tools
    },

    systemPrompt(base: string): string {
      return `${base}\n\n${buildDirectorPrompt()}`
    },
  }

  disposables.push(api.registerAgentMode(praxisMode))

  api.log.debug(
    `Commander: registered invoke_team + invoke_subagent + praxis mode (${agentsToRegister.length} agents total)`
  )

  return {
    dispose() {
      for (const d of disposables) d.dispose()
    },
  }
}

function buildDirectorPrompt(): string {
  const engineers = [...getAgentsByTier('engineer'), ...getAgentsByTier('worker')]

  const engineerList = engineers
    .map((w) => {
      return `- **${w.displayName}**: ${w.description}`
    })
    .join('\n')

  const workerSummary = formatAgentTable(engineers)

  return `## Praxis v2 — Multi-Agent Hierarchy

You are the **Director**. You orchestrate work and never write code directly.

### Task Complexity Assessment

Use invoke_team for persistent team members and invoke_subagent for ephemeral analysis.

### Engineers

${engineerList}

### Engineer Reference

${workerSummary}

### Delegation Rules

- Director -> Tech Lead -> Engineer
- Engineers must pass reviewer validation before final handoff
- Summarize outcomes and propose next roadmap steps`
}

function formatAgentTable(agents: AgentDefinition[]): string {
  return agents
    .map((a) => `| ${a.displayName} | ${a.domain ?? '—'} | ${a.tools.join(', ')} |`)
    .join('\n')
}

/** Apply model pack settings to agent definitions (returns new array, does not mutate). */
function applyModelPack(api: ExtensionAPI, agents: AgentDefinition[]): AgentDefinition[] {
  let packName: string | undefined
  try {
    const config = api.getSettings<{ modelPack?: string }>('commander')
    packName = config?.modelPack
  } catch {
    // Settings not registered
  }

  if (!packName) return agents

  const pack = getModelPack(packName)
  if (!pack) {
    api.log.warn(`Model pack '${packName}' not found, using default models`)
    return agents
  }

  api.log.debug(`Applying model pack '${packName}' to agents`)
  return agents.map((agent) => {
    // Only override if the agent doesn't already have a model set
    if (agent.model) return agent

    const resolved = resolveModelForTier(pack, agent.tier)
    if (!resolved) return agent

    return { ...agent, model: resolved.model, provider: resolved.provider }
  })
}
