/**
 * Delta9 Strategic Advisors - The Council
 *
 * The Council consists of 6 Strategic Advisors, each with unique traits,
 * perspectives, and specialties. Users configure which AI model powers each
 * personality in their delta9.json config.
 *
 * The Strategic Advisors:
 * - CIPHER (The Strategist) - Decisive, architectural, low temperature
 * - VECTOR (The Analyst) - Methodical, logical, catches edge cases
 * - APEX (The Optimizer) - Performance-obsessed, efficiency-focused
 * - AEGIS (The Guardian) - Security-focused, risk-aware, thorough
 * - RAZOR (The Simplifier) - KISS-focused, pragmatic, simplification
 * - ORACLE (The Visionary) - Innovation-focused, future-proofing, creative
 */

import type { AgentConfig } from '@opencode-ai/sdk'
import { loadConfig } from '../../lib/config.js'

// =============================================================================
// Agent Factory Exports (Config-Driven)
// =============================================================================

export { createCipherAgent, CIPHER_PROFILE, CIPHER_PROMPT } from './oracle-cipher.js'
export { createVectorAgent, VECTOR_PROFILE, VECTOR_PROMPT } from './oracle-vector.js'
export { createApexAgent, APEX_PROFILE, APEX_PROMPT } from './oracle-apex.js'
export { createAegisAgent, AEGIS_PROFILE, AEGIS_PROMPT } from './oracle-aegis.js'
export { createRazorAgent, RAZOR_PROFILE, RAZOR_PROMPT } from './oracle-razor.js'
export { createOracleAdvisorAgent, ORACLE_PROFILE, ORACLE_PROMPT } from './oracle-oracle.js'

// =============================================================================
// Internal Imports for Registry
// =============================================================================

import { createCipherAgent, CIPHER_PROFILE } from './oracle-cipher.js'
import { createVectorAgent, VECTOR_PROFILE } from './oracle-vector.js'
import { createApexAgent, APEX_PROFILE } from './oracle-apex.js'
import { createAegisAgent, AEGIS_PROFILE } from './oracle-aegis.js'
import { createRazorAgent, RAZOR_PROFILE } from './oracle-razor.js'
import { createOracleAdvisorAgent, ORACLE_PROFILE } from './oracle-oracle.js'

// =============================================================================
// Types
// =============================================================================

export type OracleCodename = 'Cipher' | 'Vector' | 'Apex' | 'Aegis' | 'Razor' | 'Oracle'
export type OracleSpecialty =
  | 'architecture'
  | 'logic'
  | 'performance'
  | 'security'
  | 'simplification'
  | 'innovation'

export interface OracleProfile {
  codename: OracleCodename
  role: string
  temperature: number
  specialty: OracleSpecialty
  traits: string[]
}

// =============================================================================
// Static Registry (Profiles only - models come from config)
// =============================================================================

/**
 * All Strategic Advisor profiles by codename (static personality data)
 */
export const oracleProfiles: Record<OracleCodename, OracleProfile> = {
  Cipher: CIPHER_PROFILE as OracleProfile,
  Vector: VECTOR_PROFILE as OracleProfile,
  Apex: APEX_PROFILE as OracleProfile,
  Aegis: AEGIS_PROFILE as OracleProfile,
  Razor: RAZOR_PROFILE as OracleProfile,
  Oracle: ORACLE_PROFILE as OracleProfile,
}

// =============================================================================
// Config-Driven Agent Creation
// =============================================================================

/**
 * Create all Strategic Advisor agents with models from config
 */
export function createCouncilAgents(cwd: string): Record<OracleCodename, AgentConfig> {
  return {
    Cipher: createCipherAgent(cwd),
    Vector: createVectorAgent(cwd),
    Apex: createApexAgent(cwd),
    Aegis: createAegisAgent(cwd),
    Razor: createRazorAgent(cwd),
    Oracle: createOracleAdvisorAgent(cwd),
  }
}

/**
 * Get agent config for a Strategic Advisor by codename (config-driven)
 */
export function getOracleAgent(cwd: string, codename: OracleCodename): AgentConfig {
  const factories: Record<OracleCodename, (cwd: string) => AgentConfig> = {
    Cipher: createCipherAgent,
    Vector: createVectorAgent,
    Apex: createApexAgent,
    Aegis: createAegisAgent,
    Razor: createRazorAgent,
    Oracle: createOracleAdvisorAgent,
  }
  return factories[codename](cwd)
}

/**
 * Get oracle config by codename from delta9.json config
 */
export function getOracleConfig(cwd: string, codename: OracleCodename) {
  const config = loadConfig(cwd)
  return config.council.members.find((m) => m.name === codename)
}

/**
 * Get oracle profile by codename (static personality data)
 */
export function getOracleProfile(codename: OracleCodename): OracleProfile {
  return oracleProfiles[codename]
}

/**
 * List all available Strategic Advisor codenames
 */
export function listOracleCodenames(): OracleCodename[] {
  return ['Cipher', 'Vector', 'Apex', 'Aegis', 'Razor', 'Oracle']
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
 * Get Strategic Advisor description for display
 */
export function getOracleDescription(codename: OracleCodename): string {
  const profile = oracleProfiles[codename]
  return `${profile.codename} - ${profile.role} (${profile.specialty})`
}
