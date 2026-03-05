/**
 * Skill matcher — matches skills to current files using glob patterns.
 */

import { matchGlob } from './glob.js'
import type { Skill, SkillConfig, SkillMatch } from './types.js'
import { DEFAULT_SKILL_CONFIG } from './types.js'

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
    // Only match auto skills (default) — always/agent/manual handled separately
    const activation = skill.activation ?? 'auto'
    if (activation !== 'auto') continue

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
