/**
 * Agent Registry — central store for all AgentDefinitions.
 *
 * Extensions and user settings feed into it. Used by delegation mechanics
 * to resolve agents and their capabilities.
 */

import type { AgentDefinition, AgentTier } from './agent-definition.js'

const registry = new Map<string, AgentDefinition>()

export interface Disposable {
  dispose(): void
}

/** Register an agent definition. Returns a disposable to unregister. */
export function registerAgent(agent: AgentDefinition): Disposable {
  registry.set(agent.id, agent)
  return {
    dispose() {
      registry.delete(agent.id)
    },
  }
}

/** Get an agent definition by ID. */
export function getAgent(id: string): AgentDefinition | undefined {
  return registry.get(id)
}

/** Get all agents of a given tier. */
export function getAgentsByTier(tier: AgentTier): AgentDefinition[] {
  return [...registry.values()].filter((a) => a.tier === tier)
}

/** Get all registered agents. */
export function getAllAgents(): AgentDefinition[] {
  return [...registry.values()]
}

/** Check if an agent is registered. */
export function hasAgent(id: string): boolean {
  return registry.has(id)
}

/** Clear all registrations (useful for tests). */
export function clearRegistry(): void {
  registry.clear()
}

/** Register multiple agents at once. Returns a single disposable. */
export function registerAgents(agents: AgentDefinition[]): Disposable {
  const disposables = agents.map((a) => registerAgent(a))
  return {
    dispose() {
      for (const d of disposables) d.dispose()
    },
  }
}
