/**
 * AgentDefinition — unified type bridging frontend AgentPreset and backend WorkerDefinition.
 *
 * Every agent in the Praxis hierarchy (Commander, Leads, Workers) is an AgentDefinition.
 */

export type AgentTier = 'commander' | 'lead' | 'worker'

export interface AgentDefinition {
  /** Unique identifier, e.g. 'frontend-lead' */
  id: string
  /** Machine name, e.g. 'frontend-lead' */
  name: string
  /** Human-readable label, e.g. 'Frontend Lead' */
  displayName: string
  description: string
  tier: AgentTier
  systemPrompt: string
  /** Concrete tool names this agent can use */
  tools: string[]
  /** Agent IDs this agent can delegate to (leads + commander) */
  delegates?: string[]
  /** Per-agent model override, e.g. 'claude-haiku-4-5' */
  model?: string
  /** Per-agent provider override */
  provider?: string
  maxTurns?: number
  maxTimeMinutes?: number
  /** Icon name for UI */
  icon?: string
  /** Domain specialization */
  domain?: string
  /** UI-facing capability tags */
  capabilities?: string[]
  /** true for built-in agents, false/undefined for user-created */
  isBuiltIn?: boolean
}

/** Convert a legacy WorkerDefinition to an AgentDefinition */
export function workerToDefinition(
  worker: {
    name: string
    displayName: string
    description: string
    systemPrompt: string
    tools: string[]
    maxTurns?: number
    maxTimeMinutes?: number
  },
  overrides?: Partial<AgentDefinition>
): AgentDefinition {
  return {
    id: worker.name,
    name: worker.name,
    displayName: worker.displayName,
    description: worker.description,
    tier: 'worker',
    systemPrompt: worker.systemPrompt,
    tools: worker.tools,
    maxTurns: worker.maxTurns,
    maxTimeMinutes: worker.maxTimeMinutes,
    isBuiltIn: true,
    ...overrides,
  }
}
