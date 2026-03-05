/**
 * Rules extension types.
 *
 * Rules are path-targeted coding instructions that inject into the system prompt.
 * Three activation modes: always (no glob check), auto (glob-matched), manual (explicit only).
 */

export type RuleActivation = 'always' | 'auto' | 'manual'

export interface Rule {
  name: string
  description: string
  globs: string[]
  activation: RuleActivation
  content: string
  source: string
}

export interface RuleMatch {
  rule: Rule
  matchedGlob: string
  matchedFile?: string
}

export interface RuleConfig {
  maxActive: number
  maxContentLength: number
}

export const DEFAULT_RULE_CONFIG: RuleConfig = {
  maxActive: 10,
  maxContentLength: 15_000,
}
