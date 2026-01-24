/**
 * Delta9 Support Agents
 *
 * Support agents are invokable by any agent (Commander, Operators, Oracles)
 * for specialized tasks like search, research, documentation, etc.
 *
 * Support Agents:
 * - SCOUT: Fast codebase search (Haiku)
 * - INTEL: Documentation & research (coming)
 * - STRATEGIST: Mid-execution guidance (coming)
 * - UI-OPS: Frontend specialist (coming)
 * - SCRIBE: Documentation writer (coming)
 * - OPTICS: Vision/multimodal (coming)
 * - QA: Test writer (coming)
 */

// =============================================================================
// Agent Exports
// =============================================================================

export {
  scoutAgent,
  scoutConfig,
  SCOUT_PROFILE,
} from './scout.js'

// =============================================================================
// Types
// =============================================================================

export type SupportAgentName = 'Scout' | 'Intel' | 'Strategist' | 'UiOps' | 'Scribe' | 'Optics' | 'QA'

// =============================================================================
// Registry
// =============================================================================

import { scoutAgent, scoutConfig, SCOUT_PROFILE } from './scout.js'
import type { AgentConfig } from '@opencode-ai/sdk'

/**
 * All support agent definitions keyed by name
 */
export const supportAgents: Partial<Record<SupportAgentName, AgentConfig>> = {
  Scout: scoutAgent,
}

/**
 * Support agent profiles
 */
export const supportProfiles = {
  Scout: SCOUT_PROFILE,
}

/**
 * Support agent configs (for config system)
 */
export const supportConfigs = {
  Scout: scoutConfig,
}

/**
 * Get support agent by name
 */
export function getSupportAgent(name: SupportAgentName): AgentConfig | undefined {
  return supportAgents[name]
}

/**
 * List available support agents
 */
export function listSupportAgents(): SupportAgentName[] {
  return Object.keys(supportAgents) as SupportAgentName[]
}

/**
 * Check if a support agent is available
 */
export function isSupportAgentAvailable(name: SupportAgentName): boolean {
  return name in supportAgents
}
