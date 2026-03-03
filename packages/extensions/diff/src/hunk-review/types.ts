import type { DiffHunk, FileDiff } from '../types.js'

export type HunkReviewStatus = 'pending' | 'accepted' | 'rejected'

export interface HunkReviewItem extends DiffHunk {
  id: string
  path: string
  index: number
  status: HunkReviewStatus
  toolCallIndex?: number
  messageIndex?: number
}

export interface HunkReviewSession {
  sessionId: string
  items: HunkReviewItem[]
}

export interface HunkReviewSummary {
  total: number
  pending: number
  accepted: number
  rejected: number
}

export function createHunkId(path: string, index: number): string {
  return `${path}#${index}`
}

export function toHunkReviewItems(diff: FileDiff): HunkReviewItem[] {
  return diff.hunks.map((hunk, index) => ({
    ...hunk,
    id: createHunkId(diff.path, index),
    path: diff.path,
    index,
    status: 'pending',
    toolCallIndex: diff.toolCallIndex,
    messageIndex: diff.messageIndex,
  }))
}
