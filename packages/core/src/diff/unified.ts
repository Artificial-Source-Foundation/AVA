/**
 * Unified Diff Utilities
 * Create and parse unified diff format
 */

import { createTwoFilesPatch, parsePatch } from 'diff'
import type { DiffHunk, DiffStats } from './types.js'

// ============================================================================
// Create Diffs
// ============================================================================

/**
 * Create a unified diff between two strings
 *
 * @param path - File path for the diff header
 * @param original - Original content
 * @param modified - Modified content
 * @param context - Number of context lines (default: 3)
 * @returns Unified diff string
 */
export function createDiff(path: string, original: string, modified: string, context = 3): string {
  return createTwoFilesPatch(`a/${path}`, `b/${path}`, original, modified, undefined, undefined, {
    context,
  })
}

/**
 * Create a diff with custom headers
 *
 * @param oldPath - Path in "from" header
 * @param newPath - Path in "to" header
 * @param original - Original content
 * @param modified - Modified content
 * @param context - Number of context lines
 * @returns Unified diff string
 */
export function createDiffWithHeaders(
  oldPath: string,
  newPath: string,
  original: string,
  modified: string,
  context = 3
): string {
  return createTwoFilesPatch(oldPath, newPath, original, modified, undefined, undefined, {
    context,
  })
}

// ============================================================================
// Parse Diffs
// ============================================================================

/**
 * Parse a unified diff string into hunks
 *
 * @param diff - Unified diff string
 * @returns Array of diff hunks
 */
export function parseDiffHunks(diff: string): DiffHunk[] {
  const patches = parsePatch(diff)

  return patches.flatMap((patch) =>
    patch.hunks.map((hunk) => ({
      oldStart: hunk.oldStart,
      oldLines: hunk.oldLines,
      newStart: hunk.newStart,
      newLines: hunk.newLines,
      lines: hunk.lines,
    }))
  )
}

/**
 * Get statistics from a diff
 *
 * @param diff - Unified diff string
 * @returns Diff statistics
 */
export function getDiffStats(diff: string): DiffStats {
  const hunks = parseDiffHunks(diff)

  let additions = 0
  let deletions = 0

  for (const hunk of hunks) {
    for (const line of hunk.lines) {
      if (line.startsWith('+') && !line.startsWith('+++')) {
        additions++
      } else if (line.startsWith('-') && !line.startsWith('---')) {
        deletions++
      }
    }
  }

  return {
    additions,
    deletions,
    hunks: hunks.length,
  }
}

// ============================================================================
// Diff Utilities
// ============================================================================

/**
 * Check if a diff represents any changes
 *
 * @param diff - Unified diff string
 * @returns True if there are actual changes
 */
export function hasChanges(diff: string): boolean {
  const stats = getDiffStats(diff)
  return stats.additions > 0 || stats.deletions > 0
}

/**
 * Extract file paths from a diff header
 *
 * @param diff - Unified diff string
 * @returns Object with oldPath and newPath, or null if not found
 */
export function extractPaths(diff: string): { oldPath: string; newPath: string } | null {
  const lines = diff.split('\n')

  let oldPath: string | null = null
  let newPath: string | null = null

  for (const line of lines) {
    if (line.startsWith('--- ')) {
      oldPath = line.slice(4).trim()
      // Remove 'a/' prefix if present
      if (oldPath.startsWith('a/')) {
        oldPath = oldPath.slice(2)
      }
    } else if (line.startsWith('+++ ')) {
      newPath = line.slice(4).trim()
      // Remove 'b/' prefix if present
      if (newPath.startsWith('b/')) {
        newPath = newPath.slice(2)
      }
    }

    if (oldPath && newPath) {
      return { oldPath, newPath }
    }
  }

  return null
}

/**
 * Format a diff for display with color hints
 *
 * @param diff - Unified diff string
 * @returns Array of lines with type hints
 */
export function formatDiffLines(
  diff: string
): Array<{ text: string; type: 'header' | 'add' | 'remove' | 'context' | 'hunk' }> {
  const lines = diff.split('\n')
  const result: Array<{
    text: string
    type: 'header' | 'add' | 'remove' | 'context' | 'hunk'
  }> = []

  for (const line of lines) {
    if (line.startsWith('+++') || line.startsWith('---')) {
      result.push({ text: line, type: 'header' })
    } else if (line.startsWith('@@')) {
      result.push({ text: line, type: 'hunk' })
    } else if (line.startsWith('+')) {
      result.push({ text: line, type: 'add' })
    } else if (line.startsWith('-')) {
      result.push({ text: line, type: 'remove' })
    } else {
      result.push({ text: line, type: 'context' })
    }
  }

  return result
}
