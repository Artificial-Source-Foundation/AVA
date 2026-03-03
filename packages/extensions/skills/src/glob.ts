/**
 * Simple glob matching — shared utility for skills and rules.
 * Supports `*` (single segment) and `**` (any depth) patterns.
 */

export function matchGlob(pattern: string, filePath: string): boolean {
  const regexStr = pattern
    .replace(/[.+^${}()|[\]\\]/g, '\\$&')
    .replace(/\*\*\//g, '{{GLOBSTAR_SLASH}}')
    .replace(/\*\*/g, '{{GLOBSTAR}}')
    .replace(/\*/g, '[^/]*')
    .replace(/\{\{GLOBSTAR_SLASH\}\}/g, '(.*/)?')
    .replace(/\{\{GLOBSTAR\}\}/g, '.*')

  return new RegExp(`^${regexStr}$`).test(filePath)
}
