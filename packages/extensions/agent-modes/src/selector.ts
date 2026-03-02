/**
 * Agent mode selector — auto-detect whether a goal needs the full Praxis hierarchy.
 */

/** Decide whether a goal warrants the full Praxis hierarchy or flat mode. */
export function selectAgentMode(
  goal: string,
  availableModes: ReadonlyMap<string, { name: string }>
): string | undefined {
  if (!availableModes.has('praxis')) return undefined

  const g = goal.toLowerCase()

  // Complex multi-domain indicators → praxis
  const complexPatterns = [
    'refactor',
    'migrate',
    'redesign',
    'architect',
    'full-stack',
    'frontend and backend',
    'multiple files',
    'across the codebase',
    'comprehensive',
    'end-to-end',
    'audit',
    'review the entire',
    'overhaul',
  ]
  if (complexPatterns.some((p) => g.includes(p))) return 'praxis'

  // Long goals (>300 chars) are usually complex
  if (goal.length > 300) return 'praxis'

  // Everything else → flat (undefined = all tools, no delegation)
  return undefined
}
