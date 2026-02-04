/**
 * Skill System
 * Reusable knowledge modules for LLM context
 *
 * Based on OpenCode's skill system pattern
 */

// Export discovery functions
export {
  discoverSkills,
  findSkillByName,
  findSkillsForFile,
  getDefaultDiscoveryConfig,
} from './discovery.js'
// Export loader functions
export { loadSkill, parseFrontmatter, SKILL_FILE_NAME, validateSkill } from './loader.js'
// Export types
export type {
  Skill,
  SkillDiscoveryConfig,
  SkillDiscoveryResult,
  SkillFrontmatter,
} from './types.js'
export { SkillFrontmatterSchema } from './types.js'

// ============================================================================
// Skill Cache
// ============================================================================

import { discoverSkills, getDefaultDiscoveryConfig } from './discovery.js'
import type { Skill, SkillDiscoveryConfig } from './types.js'

/** Cached skills by working directory */
const skillCache = new Map<string, { skills: Skill[]; timestamp: number }>()

/** Cache TTL in milliseconds (5 minutes) */
const CACHE_TTL = 5 * 60 * 1000

/**
 * Get all available skills for a working directory
 * Uses caching to avoid repeated filesystem scans
 *
 * @param workingDirectory - Current working directory
 * @param config - Optional custom discovery config
 * @returns Array of discovered skills
 */
export async function getSkills(
  workingDirectory: string,
  config?: Partial<SkillDiscoveryConfig>
): Promise<Skill[]> {
  const cacheKey = workingDirectory
  const cached = skillCache.get(cacheKey)
  const now = Date.now()

  // Return cached if valid
  if (cached && now - cached.timestamp < CACHE_TTL) {
    return cached.skills
  }

  // Discover skills
  const discoveryConfig = {
    ...getDefaultDiscoveryConfig(workingDirectory),
    ...config,
  }

  const result = await discoverSkills(discoveryConfig)

  // Update cache
  skillCache.set(cacheKey, {
    skills: result.skills,
    timestamp: now,
  })

  return result.skills
}

/**
 * Get a skill by name
 *
 * @param name - Skill name
 * @param workingDirectory - Current working directory
 * @returns Skill if found, undefined otherwise
 */
export async function getSkillByName(
  name: string,
  workingDirectory: string
): Promise<Skill | undefined> {
  const skills = await getSkills(workingDirectory)
  return skills.find((s) => s.name === name)
}

/**
 * Clear the skill cache
 * Call this when skills may have changed
 *
 * @param workingDirectory - Optional specific directory to clear
 */
export function clearSkillCache(workingDirectory?: string): void {
  if (workingDirectory) {
    skillCache.delete(workingDirectory)
  } else {
    skillCache.clear()
  }
}

/**
 * List all available skill names
 *
 * @param workingDirectory - Current working directory
 * @returns Array of skill names
 */
export async function listSkillNames(workingDirectory: string): Promise<string[]> {
  const skills = await getSkills(workingDirectory)
  return skills.map((s) => s.name)
}
