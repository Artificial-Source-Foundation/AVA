/**
 * Delta9 Agent Definitions
 *
 * Agents for the Delta9 multi-agent system:
 * - Commander: Strategic planning and orchestration (primary)
 * - Operator: Task execution (subagent)
 * - Validator: Quality verification (subagent)
 */

import type { AgentConfig } from '@opencode-ai/sdk'

// Import agent configs for getAgentSystemPrompt (BUG-17)
import { commanderAgent } from './commander.js'
import { operatorAgent } from './operator.js'
import { validatorAgent } from './validator.js'

// Import support agent utilities for getAgentSystemPrompt (BUG-17)
import {
  supportAgentFactories,
  configKeyToCodename,
  type SupportAgentConfigKey,
} from './support/index.js'

// Import council prompts for getAgentSystemPrompt (BUG-17)
import {
  CIPHER_PROMPT,
  VECTOR_PROMPT,
  APEX_PROMPT,
  AEGIS_PROMPT,
  RAZOR_PROMPT,
  ORACLE_PROMPT,
} from './council/index.js'

// =============================================================================
// Legacy Agent Exports (for backward compatibility)
// =============================================================================

export { commanderAgent }

export { operatorAgent } from './operator.js'

export { validatorAgent } from './validator.js'

// Agent router
export {
  routeTask,
  getAgentModel,
  isSupportAgent,
  getSupportAgents,
  suggestSupportAgents,
  type AgentType,
  type RoutingDecision,
} from './router.js'

// Support agents - Delta Team
export {
  // RECON (reconnaissance)
  createReconAgent,
  reconConfig,
  RECON_PROFILE,
  // SIGINT (intelligence research)
  createSigintAgent,
  sigintConfig,
  SIGINT_PROFILE,
  // TACCOM (tactical command)
  createTaccomAgent,
  taccomConfig,
  TACCOM_PROFILE,
  // SURGEON (surgical fixes)
  createSurgeonAgent,
  surgeonConfig,
  SURGEON_PROFILE,
  // SENTINEL (quality assurance)
  createSentinelAgent,
  sentinelConfig,
  SENTINEL_PROFILE,
  // SCRIBE (documentation)
  createScribeAgent,
  scribeConfig,
  SCRIBE_PROFILE,
  // FACADE (frontend operations + visual tasks - SPECTRE merged)
  createFacadeAgent,
  facadeConfig,
  FACADE_PROFILE,
  // Registry
  supportAgentFactories,
  supportProfiles,
  supportConfigs,
  codenameToConfigKey,
  configKeyToCodename,
  createSupportAgent,
  createSupportAgentByConfigKey,
  listSupportAgents,
  isSupportAgentAvailable,
  getSupportAgentProfile,
  type SupportAgentName,
  type SupportAgentConfigKey,
} from './support/index.js'

// Strategic Advisors - The Council (config-driven factory functions)
export {
  // Factory functions (config-driven)
  createCipherAgent,
  createVectorAgent,
  createApexAgent,
  createAegisAgent,
  createRazorAgent,
  createOracleAdvisorAgent,
  createCouncilAgents,
  // Profiles (static personality data)
  CIPHER_PROFILE,
  VECTOR_PROFILE,
  APEX_PROFILE,
  AEGIS_PROFILE,
  RAZOR_PROFILE,
  ORACLE_PROFILE,
  oracleProfiles,
  // Prompts (for external use)
  CIPHER_PROMPT,
  VECTOR_PROMPT,
  APEX_PROMPT,
  AEGIS_PROMPT,
  RAZOR_PROMPT,
  ORACLE_PROMPT,
  // Config-driven helpers
  getOracleAgent,
  getOracleConfig,
  getOracleProfile,
  listOracleCodenames,
  getOraclesBySpecialty,
  getOracleDescription,
  // Types
  type OracleCodename,
  type OracleSpecialty,
  type OracleProfile,
} from './council/index.js'

// Re-export the AgentConfig type
export type { AgentConfig }

// =============================================================================
// Agent Configurations (OpenCode SDK Format)
// =============================================================================

/**
 * Get agent configurations for OpenCode registration.
 * These agents will appear in the agent selector (Tab menu).
 * Note: Models are applied from Delta9 config at runtime in index.ts
 */
export function getAgentConfigs(): Record<string, AgentConfig> {
  return {
    commander: commanderAgent,
    operator: operatorAgent,
    validator: validatorAgent,
  }
}

// =============================================================================
// Agent System Prompt Registry (BUG-17 fix)
// =============================================================================

/**
 * Map of main agent prompts for quick lookup.
 * Uses imported agents to avoid prompt duplication.
 */
const MAIN_AGENT_PROMPTS: Record<string, string> = {
  commander: commanderAgent.prompt!,
  operator: operatorAgent.prompt!,
  validator: validatorAgent.prompt!,
}

/**
 * Get system prompt for any agent type.
 *
 * Used by background-manager to pass prompts directly to new sessions,
 * avoiding the issue where agents aren't registered in spawned sessions.
 *
 * Resolves prompts for:
 * - Main agents (commander, operator, validator)
 * - Support agents (scout, intel, uiOps, etc.)
 * - Strategic Advisors (Cipher, Vector, Apex, Aegis, Razor, Oracle)
 *
 * @param agentType - Agent type name (case-insensitive for council)
 * @returns System prompt string or undefined if agent not found
 */
export function getAgentSystemPrompt(agentType: string): string | undefined {
  // Check main agents first
  if (MAIN_AGENT_PROMPTS[agentType]) {
    return MAIN_AGENT_PROMPTS[agentType]
  }

  // Check support agents by config key (scout, intel, uiOps, etc.)
  const supportConfigKey = agentType as SupportAgentConfigKey
  const supportCodename = configKeyToCodename[supportConfigKey]
  if (supportCodename) {
    // Support agent prompts are in the agent configs returned by factory functions
    const factory = supportAgentFactories[supportCodename]
    if (factory) {
      // Factory requires cwd, use empty string to just get the prompt
      const agentConfig = factory('')
      return agentConfig.prompt
    }
  }

  // Check Strategic Advisors (case-insensitive match)
  // Use static prompts directly to avoid needing cwd
  const councilPrompts: Record<string, string> = {
    Cipher: CIPHER_PROMPT,
    Vector: VECTOR_PROMPT,
    Apex: APEX_PROMPT,
    Aegis: AEGIS_PROMPT,
    Razor: RAZOR_PROMPT,
    Oracle: ORACLE_PROMPT,
  }

  const normalizedCodename = agentType.charAt(0).toUpperCase() + agentType.slice(1).toLowerCase()
  if (councilPrompts[normalizedCodename]) {
    return councilPrompts[normalizedCodename]
  }

  // Try lowercase match for council (e.g., 'cipher' -> 'Cipher')
  const lowercaseMap: Record<string, string> = {
    cipher: 'Cipher',
    vector: 'Vector',
    apex: 'Apex',
    aegis: 'Aegis',
    razor: 'Razor',
    oracle: 'Oracle',
  }
  const mappedCodename = lowercaseMap[agentType.toLowerCase()]
  if (mappedCodename && councilPrompts[mappedCodename]) {
    return councilPrompts[mappedCodename]
  }

  return undefined
}
