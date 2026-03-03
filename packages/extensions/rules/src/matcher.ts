/**
 * Rule matcher — matches rules to current files using glob patterns.
 */

import { matchGlob } from '../../skills/src/glob.js'
import type { Rule, RuleConfig, RuleMatch } from './types.js'
import { DEFAULT_RULE_CONFIG } from './types.js'

/**
 * Match rules against current files.
 *
 * - `always` rules: always included (no glob check)
 * - `auto` rules: included if any glob matches a current file
 * - `manual` rules: excluded from automatic matching
 *
 * Returns matched rules capped at maxActive.
 */
export function matchRules(
  rules: Rule[],
  currentFiles: string[],
  config: RuleConfig = DEFAULT_RULE_CONFIG
): RuleMatch[] {
  const matches: RuleMatch[] = []

  for (const rule of rules) {
    if (rule.activation === 'manual') continue

    if (rule.activation === 'always') {
      matches.push({ rule, matchedGlob: '*' })
      continue
    }

    // activation === 'auto'
    for (const glob of rule.globs) {
      const matchedFile = currentFiles.find((f) => matchGlob(glob, f))
      if (matchedFile) {
        matches.push({ rule, matchedGlob: glob, matchedFile })
        break // One match per rule is enough
      }
    }
  }

  return matches.slice(0, config.maxActive)
}
