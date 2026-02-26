/**
 * Diff tracking types.
 */

export interface FileDiff {
  path: string
  type: 'added' | 'modified' | 'deleted'
  original?: string
  modified?: string
  hunks: DiffHunk[]
}

export interface DiffHunk {
  oldStart: number
  oldLines: number
  newStart: number
  newLines: number
  content: string
}

export interface DiffSession {
  sessionId: string
  diffs: FileDiff[]
  startedAt: number
}
