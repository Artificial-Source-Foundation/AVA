import type { DeclarativePolicyRule } from '../types.js'

const SOURCE_ORDER: Record<string, number> = {
  runtime: 4,
  project: 3,
  user: 2,
  builtin: 1,
}

export function mergePolicyRules(rules: DeclarativePolicyRule[]): DeclarativePolicyRule[] {
  return [...rules].sort((a, b) => {
    if (a.priority !== b.priority) return b.priority - a.priority
    return (SOURCE_ORDER[b.source] ?? 0) - (SOURCE_ORDER[a.source] ?? 0)
  })
}
