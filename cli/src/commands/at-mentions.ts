/**
 * File @mentions in CLI input.
 *
 * Expands `@path/to/file` patterns in the goal string by reading
 * the referenced files and inlining their content.
 */

import * as fs from 'node:fs/promises'
import * as path from 'node:path'

/** Regex to match @file mentions. Supports relative and absolute paths. */
const AT_MENTION_RE = /@((?:\/|\.\/|\.\.\/|[a-zA-Z0-9_-])[^\s,;'")`\]}>]*)/g

/**
 * Expand @path/to/file mentions in a goal string.
 *
 * For each `@path` found, reads the file and replaces with a
 * `<file path="...">content</file>` block. If the file doesn't
 * exist, the @mention is left as-is.
 */
export async function expandAtMentions(goal: string, cwd: string): Promise<string> {
  const matches = [...goal.matchAll(AT_MENTION_RE)]
  if (matches.length === 0) return goal

  // Deduplicate and resolve paths
  const seen = new Map<string, string | null>()

  for (const match of matches) {
    const rawPath = match[1]!
    if (seen.has(rawPath)) continue

    const resolved = path.isAbsolute(rawPath) ? rawPath : path.resolve(cwd, rawPath)

    try {
      const content = await fs.readFile(resolved, 'utf-8')
      seen.set(rawPath, content)
    } catch {
      // File doesn't exist or can't be read — leave as-is
      seen.set(rawPath, null)
    }
  }

  // Replace mentions with file blocks
  return goal.replace(AT_MENTION_RE, (_match, rawPath: string) => {
    const content = seen.get(rawPath)
    if (content === null || content === undefined) {
      return `@${rawPath}`
    }
    return `<file path="${rawPath}">\n${content}\n</file>`
  })
}
