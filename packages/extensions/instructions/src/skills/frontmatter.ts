/**
 * YAML frontmatter parser — shared utility for skills and rules.
 *
 * Parses simple YAML frontmatter blocks delimited by `---`.
 * Supports key-value pairs, arrays, comments, and quoted strings.
 */

export interface Frontmatter {
  [key: string]: string | string[] | undefined
}

/**
 * Parse a file with YAML frontmatter into frontmatter fields + body content.
 */
export function parseFrontmatter(raw: string): { frontmatter: Frontmatter; content: string } {
  const match = raw.match(/^---\s*\n([\s\S]*?)\n---\s*\n([\s\S]*)$/)
  if (!match) return { frontmatter: {}, content: raw }

  const yamlBlock = match[1]!
  const content = match[2]!
  const frontmatter: Frontmatter = {}

  let currentKey = ''
  for (const line of yamlBlock.split('\n')) {
    const trimmed = line.trim()
    if (!trimmed || trimmed.startsWith('#')) continue

    // Array item: "  - value"
    if (trimmed.startsWith('- ') && currentKey) {
      const value = trimmed.slice(2).replace(/^["']|["']$/g, '')
      const existing = frontmatter[currentKey]
      if (Array.isArray(existing)) {
        existing.push(value)
      } else {
        frontmatter[currentKey] = [value]
      }
      continue
    }

    // Key-value: "key: value"
    const kvMatch = trimmed.match(/^(\w+)\s*:\s*(.*)$/)
    if (kvMatch) {
      currentKey = kvMatch[1]!
      const value = kvMatch[2]!.replace(/^["']|["']$/g, '')
      if (value) {
        frontmatter[currentKey] = value
      } else {
        // Empty value — likely an array follows
        frontmatter[currentKey] = []
      }
    }
  }

  return { frontmatter, content }
}

/**
 * Parse a frontmatter value into an array of glob patterns.
 */
export function parseGlobs(value: string | string[] | undefined): string[] {
  if (!value) return []
  if (typeof value === 'string') return [value]
  return value.filter(Boolean)
}

/**
 * Parse a frontmatter value into a string array (or undefined if empty).
 */
export function parseStringArray(value: string | string[] | undefined): string[] | undefined {
  if (!value) return undefined
  if (typeof value === 'string') return [value]
  return value.length > 0 ? value : undefined
}
