/**
 * Delta9 Council Agents - The Delta Team
 *
 * The Council consists of personality-based Oracles, each with unique traits,
 * perspectives, and specialties. Users configure which AI model powers each
 * personality in their delta9.json config.
 *
 * The Delta Team:
 * - CIPHER (The Strategist) - Decisive, architectural, low temperature
 * - VECTOR (The Analyst) - Methodical, logical, catches edge cases
 * - PRISM (The Creative) - Innovative, user-focused, higher temperature
 * - APEX (The Optimizer) - Performance-obsessed, efficiency-focused
 */

// =============================================================================
// Agent Definitions
// =============================================================================

export { cipherAgent, cipherConfig, CIPHER_PROFILE } from './oracle-cipher.js'

export { vectorAgent, vectorConfig, VECTOR_PROFILE } from './oracle-vector.js'

export { prismAgent, prismConfig, PRISM_PROFILE } from './oracle-prism.js'

export { apexAgent, apexConfig, APEX_PROFILE } from './oracle-apex.js'

// =============================================================================
// Agent Registry
// =============================================================================

import { cipherAgent, cipherConfig, CIPHER_PROFILE } from './oracle-cipher.js'
import { vectorAgent, vectorConfig, VECTOR_PROFILE } from './oracle-vector.js'
import { prismAgent, prismConfig, PRISM_PROFILE } from './oracle-prism.js'
import { apexAgent, apexConfig, APEX_PROFILE } from './oracle-apex.js'
import type { AgentConfig } from '@opencode-ai/sdk'
import type { OracleConfig } from '../../types/config.js'

// =============================================================================
// Types
// =============================================================================

export type OracleCodename = 'Cipher' | 'Vector' | 'Prism' | 'Apex'
export type OracleSpecialty = 'architecture' | 'logic' | 'ui' | 'performance'

export interface OracleProfile {
  codename: OracleCodename
  role: string
  temperature: number
  specialty: OracleSpecialty
  traits: string[]
}

// =============================================================================
// Registry
// =============================================================================

/**
 * All Oracle profiles by codename
 * Cast to proper types since the source exports use `as const`
 */
export const oracleProfiles: Record<OracleCodename, OracleProfile> = {
  Cipher: CIPHER_PROFILE as OracleProfile,
  Vector: VECTOR_PROFILE as OracleProfile,
  Prism: PRISM_PROFILE as OracleProfile,
  Apex: APEX_PROFILE as OracleProfile,
}

/**
 * All council agent definitions keyed by Oracle codename
 */
export const councilAgents: Record<OracleCodename, AgentConfig> = {
  Cipher: cipherAgent,
  Vector: vectorAgent,
  Prism: prismAgent,
  Apex: apexAgent,
}

/**
 * All oracle configs keyed by Oracle codename
 * These are the default configs - users override models in delta9.json
 */
export const oracleConfigs: Record<OracleCodename, OracleConfig> = {
  Cipher: {
    name: cipherConfig.name,
    model: cipherConfig.defaultModel,
    specialty: cipherConfig.specialty,
    enabled: cipherConfig.enabled,
    temperature: cipherConfig.temperature,
  },
  Vector: {
    name: vectorConfig.name,
    model: vectorConfig.defaultModel,
    specialty: vectorConfig.specialty,
    enabled: vectorConfig.enabled,
    temperature: vectorConfig.temperature,
  },
  Prism: {
    name: prismConfig.name,
    model: prismConfig.defaultModel,
    specialty: prismConfig.specialty,
    enabled: prismConfig.enabled,
    temperature: prismConfig.temperature,
  },
  Apex: {
    name: apexConfig.name,
    model: apexConfig.defaultModel,
    specialty: apexConfig.specialty,
    enabled: apexConfig.enabled,
    temperature: apexConfig.temperature,
  },
}

// =============================================================================
// Helper Functions
// =============================================================================

/**
 * Get agent config for an Oracle by codename
 */
export function getOracleAgent(codename: OracleCodename): AgentConfig {
  return councilAgents[codename]
}

/**
 * Get oracle config by codename
 */
export function getOracleConfig(codename: OracleCodename): OracleConfig {
  return oracleConfigs[codename]
}

/**
 * Get oracle profile by codename
 */
export function getOracleProfile(codename: OracleCodename): OracleProfile {
  return oracleProfiles[codename]
}

/**
 * List all available Oracle codenames
 */
export function listOracleCodenames(): OracleCodename[] {
  return ['Cipher', 'Vector', 'Prism', 'Apex']
}

/**
 * Get Oracles by specialty
 */
export function getOraclesBySpecialty(specialty: OracleSpecialty): OracleCodename[] {
  return listOracleCodenames().filter(
    (codename) => oracleProfiles[codename].specialty === specialty
  )
}

/**
 * Default oracle configs (used when no custom config provided)
 * Returns all Oracles in strategic order:
 * 1. Cipher (architecture) - sees big picture first
 * 2. Vector (logic) - validates correctness
 * 3. Prism (ui) - considers user impact
 * 4. Apex (performance) - optimizes
 */
export const defaultOracleConfigs: OracleConfig[] = [
  oracleConfigs.Cipher,
  oracleConfigs.Vector,
  oracleConfigs.Prism,
  oracleConfigs.Apex,
]

/**
 * Get recommended Oracles for QUICK mode (1 Oracle)
 * Returns Cipher by default as the strategic lead
 */
export function getQuickModeOracles(): OracleConfig[] {
  return [oracleConfigs.Cipher]
}

/**
 * Get recommended Oracles for STANDARD mode (all Oracles)
 */
export function getStandardModeOracles(): OracleConfig[] {
  return defaultOracleConfigs
}

/**
 * Get Oracle description for display
 */
export function getOracleDescription(codename: OracleCodename): string {
  const profile = oracleProfiles[codename]
  return `${profile.codename} - ${profile.role} (${profile.specialty})`
}
