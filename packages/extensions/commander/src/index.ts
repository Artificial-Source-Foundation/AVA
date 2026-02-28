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
import type { AgentDefinition } from './agent-definition.js'
import { createDelegateTool } from './delegate.js'
import { getAgent, getAgentsByTier, registerAgents } from './registry.js'
import { BUILTIN_AGENTS, LEAD_AGENTS, WORKER_AGENTS } from './workers.js'

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

  // Register all built-in agents in the registry
  disposables.push(registerAgents(BUILTIN_AGENTS))

  // Register delegate tools for leads (commander delegates to leads)
  for (const lead of LEAD_AGENTS) {
    const tool = createDelegateTool(lead)
    disposables.push(api.registerTool(tool))
  }

  // Register delegate tools for workers (leads delegate to workers)
  for (const worker of WORKER_AGENTS) {
    const tool = createDelegateTool(worker)
    disposables.push(api.registerTool(tool))
  }

  // Register the praxis agent mode
  const praxisMode: AgentMode = {
    name: 'praxis',
    description: 'Praxis mode: 3-tier hierarchy (Commander → Leads → Workers)',

    filterTools(tools: ToolDefinition[]): ToolDefinition[] {
      // Commander only gets delegate tools + meta tools (question, attempt_completion)
      const delegateToolNames = new Set<string>()
      const commander = getAgent('commander')
      if (commander?.delegates) {
        for (const id of commander.delegates) {
          delegateToolNames.add(`delegate_${id}`)
        }
      }
      const metaTools = new Set(['question', 'attempt_completion', 'todoread', 'todowrite'])

      return tools.filter((t) => delegateToolNames.has(t.name) || metaTools.has(t.name))
    },

    systemPrompt(base: string): string {
      return `${base}\n\n${buildCommanderPrompt()}`
    },
  }

  disposables.push(api.registerAgentMode(praxisMode))

  const leadCount = LEAD_AGENTS.length
  const workerCount = WORKER_AGENTS.length
  api.log.debug(
    `Commander: registered ${leadCount} leads + ${workerCount} workers + praxis mode (${BUILTIN_AGENTS.length} agents total)`
  )

  return {
    dispose() {
      for (const d of disposables) d.dispose()
    },
  }
}

function buildCommanderPrompt(): string {
  const leads = getAgentsByTier('lead')
  const workers = getAgentsByTier('worker')

  const leadList = leads
    .map((l) => {
      const workerNames = (l.delegates ?? [])
        .map((id) => {
          const w = getAgent(id)
          return w ? w.displayName : id
        })
        .join(', ')
      return `- **${l.displayName}** (\`delegate_${l.name}\`): ${l.description}. Workers: ${workerNames}`
    })
    .join('\n')

  const workerSummary = formatAgentTable(workers)

  return `## Praxis — 3-Tier Agent Hierarchy

You are the **Commander**. You plan and coordinate — you never write code directly.
Your only tools are delegate tools, question, and attempt_completion.

### Available Leads

${leadList}

### Worker Reference

${workerSummary}

### Planning Protocol

For complex tasks (3+ files, multiple domains):
1. Call \`delegate_planner\` with the full task description
2. Review the returned plan
3. Call \`delegate_architect\` to validate the plan (optional)
4. Delegate subtasks to the appropriate leads

For simple tasks (1-2 files, single domain):
- Delegate directly to the appropriate lead

### Delegation Rules

- **Commander** → delegates to **Leads** (and planner/architect for planning)
- **Leads** → delegate to their **Workers**
- **Workers** → execute tasks directly (no delegation)
- Each agent may use a different model for cost optimization
- Review results from leads before completing

### Tips

- Use \`delegate_planner\` to break complex tasks into subtasks
- Use \`delegate_architect\` to validate architectural decisions
- Prefer \`delegate_fullstack-lead\` for cross-cutting tasks
- Prefer specific leads (\`delegate_frontend-lead\`, \`delegate_backend-lead\`) when the domain is clear`
}

function formatAgentTable(agents: AgentDefinition[]): string {
  return agents
    .map((a) => `| ${a.displayName} | ${a.domain ?? '—'} | ${a.tools.join(', ')} |`)
    .join('\n')
}
