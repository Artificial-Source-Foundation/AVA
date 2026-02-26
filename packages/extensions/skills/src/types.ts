/**
 * Skills extension types.
 */

export interface Skill {
  name: string
  description: string
  globs: string[]
  projectTypes?: string[]
  content: string
  source: string
}

export interface SkillMatch {
  skill: Skill
  matchedGlob: string
  matchedFile?: string
}

export interface SkillConfig {
  maxActive: number
  maxContentLength: number
}

export const DEFAULT_SKILL_CONFIG: SkillConfig = {
  maxActive: 5,
  maxContentLength: 10_000,
}
