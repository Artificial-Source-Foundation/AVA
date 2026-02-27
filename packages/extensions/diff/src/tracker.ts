/**
 * Diff tracker — computes and stores file diffs.
 */

import type { DiffHunk, DiffSession, FileDiff } from './types.js'

export function createDiffSession(sessionId: string): DiffSession {
  return { sessionId, diffs: [], startedAt: Date.now() }
}

/**
 * Compute a simple line-by-line diff between two strings.
 * Returns hunks of changes. Not a full Myers diff — optimized for clarity.
 */
export function computeSimpleDiff(original: string, modified: string): DiffHunk[] {
  const oldLines = original.split('\n')
  const newLines = modified.split('\n')
  const hunks: DiffHunk[] = []

  let i = 0
  let j = 0

  while (i < oldLines.length || j < newLines.length) {
    // Skip matching lines
    if (i < oldLines.length && j < newLines.length && oldLines[i] === newLines[j]) {
      i++
      j++
      continue
    }

    // Found a difference — collect the hunk
    const hunkOldStart = i + 1
    const hunkNewStart = j + 1
    const contentLines: string[] = []

    // Collect differing lines
    while (i < oldLines.length && (j >= newLines.length || oldLines[i] !== newLines[j])) {
      contentLines.push(`-${oldLines[i]}`)
      i++
    }
    while (j < newLines.length && (i >= oldLines.length || oldLines[i] !== newLines[j])) {
      contentLines.push(`+${newLines[j]}`)
      j++
    }

    if (contentLines.length > 0) {
      const removed = contentLines.filter((l) => l.startsWith('-')).length
      const added = contentLines.filter((l) => l.startsWith('+')).length
      hunks.push({
        oldStart: hunkOldStart,
        oldLines: removed,
        newStart: hunkNewStart,
        newLines: added,
        content: contentLines.join('\n'),
      })
    }
  }

  return hunks
}

export function createFileDiff(
  path: string,
  original: string | undefined,
  modified: string
): FileDiff {
  if (original === undefined) {
    return {
      path,
      type: 'added',
      modified,
      hunks: [
        {
          oldStart: 0,
          oldLines: 0,
          newStart: 1,
          newLines: modified.split('\n').length,
          content: modified,
        },
      ],
    }
  }

  return {
    path,
    type: 'modified',
    original,
    modified,
    hunks: computeSimpleDiff(original, modified),
  }
}

export function addDiff(session: DiffSession, diff: FileDiff): void {
  // Replace existing diff for same path, or append
  const idx = session.diffs.findIndex((d) => d.path === diff.path)
  if (idx !== -1) {
    session.diffs[idx] = diff
  } else {
    session.diffs.push(diff)
  }
}
