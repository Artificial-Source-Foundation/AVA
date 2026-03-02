/**
 * Tool name repair — fuzzy match tool names from LLM output.
 *
 * LLMs sometimes hallucinate tool names or use wrong casing/separators.
 * This module tries to recover the intended tool name from what was given.
 */

/**
 * Attempt to repair a tool name that doesn't exist in the registry.
 *
 * Strategies (in order):
 * 1. Exact match (should already be handled, but included for completeness)
 * 2. Case-insensitive match
 * 3. Hyphen/underscore substitution (e.g., "read-file" → "read_file")
 * 4. Prefix match (input is a prefix of a tool name)
 *
 * Returns the corrected tool name, or null if no match found.
 */
export function repairToolName(name: string, availableTools: string[]): string | null {
  // 1. Exact match
  if (availableTools.includes(name)) {
    return name
  }

  const lower = name.toLowerCase()

  // 2. Case-insensitive match
  for (const tool of availableTools) {
    if (tool.toLowerCase() === lower) {
      return tool
    }
  }

  // 3. Hyphen ↔ underscore substitution
  const swapped = name.includes('-') ? name.replace(/-/g, '_') : name.replace(/_/g, '-')

  if (availableTools.includes(swapped)) {
    return swapped
  }

  // Also try case-insensitive swapped
  const swappedLower = swapped.toLowerCase()
  for (const tool of availableTools) {
    if (tool.toLowerCase() === swappedLower) {
      return tool
    }
  }

  // 4. Prefix match — tool name starts with the input
  const prefixMatches = availableTools.filter((t) => t.startsWith(lower))
  if (prefixMatches.length === 1) {
    return prefixMatches[0]
  }

  // Also try prefix with original case
  const prefixMatchesOrig = availableTools.filter((t) => t.toLowerCase().startsWith(lower))
  if (prefixMatchesOrig.length === 1) {
    return prefixMatchesOrig[0]
  }

  return null
}
