/**
 * Commander extension — Praxis 3-tier agent hierarchy.
 *
 * Registers all built-in agents, creates delegate tools per tier,
 * and exposes the 'praxis' agent mode. Toggleable via settings.
 *
 * Hierarchy: Commander → Leads → Workers
 */

import type { AgentMode, Disposable, ExtensionAPI } from '@ava/core-v2/extensions'
import type { ToolDefinition } from '@ava/core-v2/llm'
import { getModelPack, resolveModelForTier } from '../../models/src/packs.js'
import type { AgentDefinition } from './agent-definition.js'
import { configureDelegation, createDelegateTool } from './delegate.js'
import { getAgentsByTier, registerAgents } from './registry.js'
import { BUILTIN_AGENTS } from './workers.js'

const DELEGATE_TOOL_AGENT_IDS = new Set(['coder', 'researcher', 'reviewer', 'explorer'])

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

  const delegateAgents = agentsToRegister.filter((a) => DELEGATE_TOOL_AGENT_IDS.has(a.id))

  for (const agent of delegateAgents) {
    const tool = createDelegateTool(agent)
    disposables.push(api.registerTool(tool))
  }

  // Register the praxis agent mode
  const praxisMode: AgentMode = {
    name: 'praxis',
    description: 'Praxis mode: 3-tier hierarchy (Commander → Leads → Workers)',

    filterTools(tools: ToolDefinition[]): ToolDefinition[] {
      // Keep ALL tools — commander handles simple tasks directly.
      // Delegate tools (delegate_*) are already registered and included in `tools`.
      return tools
    },

    systemPrompt(base: string): string {
      return `${base}\n\n${buildCommanderPrompt()}`
    },
  }

  disposables.push(api.registerAgentMode(praxisMode))

  api.log.debug(
    `Commander: registered ${delegateAgents.length} delegate tools + praxis mode (${agentsToRegister.length} agents total)`
  )

  return {
    dispose() {
      for (const d of disposables) d.dispose()
    },
  }
}

function buildCommanderPrompt(): string {
  const workers = getAgentsByTier('worker').filter((w) => DELEGATE_TOOL_AGENT_IDS.has(w.id))

  const workerList = workers
    .map((w) => {
      return `- **${w.displayName}** (\`delegate_${w.name}\`): ${w.description}`
    })
    .join('\n')

  const workerSummary = formatAgentTable(workers)

  return `## Praxis — Tiered Agent Hierarchy

You are the **Commander**. You have full tool access AND can delegate to specialized leads/workers.

### Task Complexity Assessment

**Simple tasks** (read a file, answer a question, small edit, 1-2 files):
- Handle directly with your tools. Do NOT delegate.

**Medium/complex tasks**:
- Delegate to specialist workers when useful (coder, researcher, reviewer, explorer).

**Default to handling it yourself** unless the task clearly needs delegation.

### Available Delegates

${workerList}

### Worker Reference

${workerSummary}

### Delegation Rules

- **Commander** handles simple tasks directly and can delegate to specialized workers.
- Delegated workers execute tasks directly.
- Each agent may use a different model for cost optimization
- Review results before completing`
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
