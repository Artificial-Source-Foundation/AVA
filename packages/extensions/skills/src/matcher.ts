/**
 * Skill matcher — matches skills to current files using glob patterns.
 */

import type { Skill, SkillConfig, SkillMatch } from './types.js'
import { DEFAULT_SKILL_CONFIG } from './types.js'

/**
 * Simple glob matching (supports * and ** patterns).
 */
function matchGlob(pattern: string, filePath: string): boolean {
  // Escape regex special chars except * and **
  const regexStr = pattern
    .replace(/[.+^${}()|[\]\\]/g, '\\$&')
    .replace(/\*\*\//g, '{{GLOBSTAR_SLASH}}')
    .replace(/\*\*/g, '{{GLOBSTAR}}')
    .replace(/\*/g, '[^/]*')
    .replace(/\{\{GLOBSTAR_SLASH\}\}/g, '(.*/)?')
    .replace(/\{\{GLOBSTAR\}\}/g, '.*')

  return new RegExp(`^${regexStr}$`).test(filePath)
}

/**
 * Match skills against current files.
 * Returns matched skills capped at maxActive.
 */
export function matchSkills(
  skills: Skill[],
  currentFiles: string[],
  config: SkillConfig = DEFAULT_SKILL_CONFIG
): SkillMatch[] {
  const matches: SkillMatch[] = []

  for (const skill of skills) {
    for (const glob of skill.globs) {
      const matchedFile = currentFiles.find((f) => matchGlob(glob, f))
      if (matchedFile) {
        matches.push({ skill, matchedGlob: glob, matchedFile })
        break // One match per skill is enough
      }
    }
  }

  return matches.slice(0, config.maxActive)
}
