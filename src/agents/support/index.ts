/**
 * Delta9 Support Agents
 *
 * Support agents are invokable by any agent (Commander, Operators, Oracles)
 * for specialized tasks like search, research, documentation, etc.
 *
 * Delta Team Support Agents:
 * - RECON: Fast codebase reconnaissance (config: scout)
 * - SIGINT: Intelligence research & documentation (config: intel)
 * - TACCOM: Tactical command advisor (config: strategist)
 * - SURGEON: Quick surgical fixes (config: patcher)
 * - SENTINEL: Quality assurance guardian (config: qa)
 * - SCRIBE: Documentation writer (config: scribe)
 * - FACADE: Frontend operations specialist (config: uiOps)
 * - SPECTRE: Visual intelligence analyst (config: optics)
 *
 * All models are user-configurable in delta9.json
 */

// =============================================================================
// Agent Exports
// =============================================================================

export { createReconAgent, reconConfig, RECON_PROFILE } from './recon.js'

export { createSigintAgent, sigintConfig, SIGINT_PROFILE } from './sigint.js'

export { createTaccomAgent, taccomConfig, TACCOM_PROFILE } from './taccom.js'

export { createSurgeonAgent, surgeonConfig, SURGEON_PROFILE } from './surgeon.js'

export { createSentinelAgent, sentinelConfig, SENTINEL_PROFILE } from './sentinel.js'

export { createScribeAgent, scribeConfig, SCRIBE_PROFILE } from './scribe.js'

export { createFacadeAgent, facadeConfig, FACADE_PROFILE } from './facade.js'

export { createSpectreAgent, spectreConfig, SPECTRE_PROFILE } from './spectre.js'

// =============================================================================
// Types
// =============================================================================

export type SupportAgentName =
  | 'RECON'
  | 'SIGINT'
  | 'TACCOM'
  | 'SURGEON'
  | 'SENTINEL'
  | 'SCRIBE'
  | 'FACADE'
  | 'SPECTRE'

export type SupportAgentConfigKey =
  | 'scout'
  | 'intel'
  | 'strategist'
  | 'patcher'
  | 'qa'
  | 'scribe'
  | 'uiOps'
  | 'optics'

// =============================================================================
// Registry
// =============================================================================

import { createReconAgent, reconConfig, RECON_PROFILE } from './recon.js'
import { createSigintAgent, sigintConfig, SIGINT_PROFILE } from './sigint.js'
import { createTaccomAgent, taccomConfig, TACCOM_PROFILE } from './taccom.js'
import { createSurgeonAgent, surgeonConfig, SURGEON_PROFILE } from './surgeon.js'
import { createSentinelAgent, sentinelConfig, SENTINEL_PROFILE } from './sentinel.js'
import { createScribeAgent, scribeConfig, SCRIBE_PROFILE } from './scribe.js'
import { createFacadeAgent, facadeConfig, FACADE_PROFILE } from './facade.js'
import { createSpectreAgent, spectreConfig, SPECTRE_PROFILE } from './spectre.js'
import type { AgentConfig } from '@opencode-ai/sdk'

/**
 * Agent factory functions keyed by Delta codename
 */
export const supportAgentFactories: Record<SupportAgentName, (cwd: string) => AgentConfig> = {
  RECON: createReconAgent,
  SIGINT: createSigintAgent,
  TACCOM: createTaccomAgent,
  SURGEON: createSurgeonAgent,
  SENTINEL: createSentinelAgent,
  SCRIBE: createScribeAgent,
  FACADE: createFacadeAgent,
  SPECTRE: createSpectreAgent,
}

/**
 * Support agent profiles
 */
export const supportProfiles = {
  RECON: RECON_PROFILE,
  SIGINT: SIGINT_PROFILE,
  TACCOM: TACCOM_PROFILE,
  SURGEON: SURGEON_PROFILE,
  SENTINEL: SENTINEL_PROFILE,
  SCRIBE: SCRIBE_PROFILE,
  FACADE: FACADE_PROFILE,
  SPECTRE: SPECTRE_PROFILE,
}

/**
 * Support agent configs (for config system)
 */
export const supportConfigs = {
  RECON: reconConfig,
  SIGINT: sigintConfig,
  TACCOM: taccomConfig,
  SURGEON: surgeonConfig,
  SENTINEL: sentinelConfig,
  SCRIBE: scribeConfig,
  FACADE: facadeConfig,
  SPECTRE: spectreConfig,
}

/**
 * Map Delta codename to config key
 */
export const codenameToConfigKey: Record<SupportAgentName, SupportAgentConfigKey> = {
  RECON: 'scout',
  SIGINT: 'intel',
  TACCOM: 'strategist',
  SURGEON: 'patcher',
  SENTINEL: 'qa',
  SCRIBE: 'scribe',
  FACADE: 'uiOps',
  SPECTRE: 'optics',
}

/**
 * Map config key to Delta codename
 */
export const configKeyToCodename: Record<SupportAgentConfigKey, SupportAgentName> = {
  scout: 'RECON',
  intel: 'SIGINT',
  strategist: 'TACCOM',
  patcher: 'SURGEON',
  qa: 'SENTINEL',
  scribe: 'SCRIBE',
  uiOps: 'FACADE',
  optics: 'SPECTRE',
}

/**
 * Create support agent by name
 */
export function createSupportAgent(name: SupportAgentName, cwd: string): AgentConfig {
  const factory = supportAgentFactories[name]
  if (!factory) {
    throw new Error(`Unknown support agent: ${name}`)
  }
  return factory(cwd)
}

/**
 * Create support agent by config key
 */
export function createSupportAgentByConfigKey(
  configKey: SupportAgentConfigKey,
  cwd: string
): AgentConfig {
  const codename = configKeyToCodename[configKey]
  return createSupportAgent(codename, cwd)
}

/**
 * List available support agents
 */
export function listSupportAgents(): SupportAgentName[] {
  return Object.keys(supportAgentFactories) as SupportAgentName[]
}

/**
 * Check if a support agent is available
 */
export function isSupportAgentAvailable(name: SupportAgentName): boolean {
  return name in supportAgentFactories
}

/**
 * Get support agent profile by name
 */
export function getSupportAgentProfile(name: SupportAgentName) {
  return supportProfiles[name]
}
