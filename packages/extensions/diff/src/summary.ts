/**
 * Diff session summary — computes aggregate stats for a diff session.
 */

import type { DiffSession } from './types.js'

export interface DiffFileSummary {
  path: string
  type: string
  additions: number
  deletions: number
}

export interface DiffSessionSummary {
  filesChanged: number
  additions: number
  deletions: number
  files: DiffFileSummary[]
}

/**
 * Summarize all diffs in a session.
 * Counts additions and deletions from hunks for each file.
 */
export function summarizeDiffSession(session: DiffSession): DiffSessionSummary {
  const files: DiffFileSummary[] = []
  let totalAdditions = 0
  let totalDeletions = 0

  for (const diff of session.diffs) {
    let additions = 0
    let deletions = 0

    for (const hunk of diff.hunks) {
      const lines = hunk.content.split('\n')
      for (const line of lines) {
        if (line.startsWith('+')) additions++
        else if (line.startsWith('-')) deletions++
      }
    }

    totalAdditions += additions
    totalDeletions += deletions
    files.push({ path: diff.path, type: diff.type, additions, deletions })
  }

  return {
    filesChanged: files.length,
    additions: totalAdditions,
    deletions: totalDeletions,
    files,
  }
}
