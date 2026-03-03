import type { FileDiff } from '../types.js'
import {
  type HunkReviewItem,
  type HunkReviewSession,
  type HunkReviewStatus,
  type HunkReviewSummary,
  toHunkReviewItems,
} from './types.js'

export class HunkReviewState {
  private readonly sessions = new Map<string, HunkReviewSession>()

  ingest(sessionId: string, diff: FileDiff): void {
    const session = this.getOrCreate(sessionId)
    const nextItems = toHunkReviewItems(diff)

    session.items = session.items.filter((item) => item.path !== diff.path)
    session.items.push(...nextItems)
  }

  list(sessionId: string, path?: string): HunkReviewItem[] {
    const session = this.sessions.get(sessionId)
    if (!session) return []
    if (!path) return [...session.items]
    return session.items.filter((item) => item.path === path)
  }

  updateStatus(sessionId: string, hunkId: string, status: HunkReviewStatus): boolean {
    const session = this.sessions.get(sessionId)
    if (!session) return false
    const item = session.items.find((it) => it.id === hunkId)
    if (!item) return false
    item.status = status
    return true
  }

  summary(sessionId: string): HunkReviewSummary {
    const items = this.list(sessionId)
    return {
      total: items.length,
      pending: items.filter((i) => i.status === 'pending').length,
      accepted: items.filter((i) => i.status === 'accepted').length,
      rejected: items.filter((i) => i.status === 'rejected').length,
    }
  }

  clear(sessionId?: string): void {
    if (sessionId) {
      this.sessions.delete(sessionId)
      return
    }
    this.sessions.clear()
  }

  private getOrCreate(sessionId: string): HunkReviewSession {
    let session = this.sessions.get(sessionId)
    if (!session) {
      session = { sessionId, items: [] }
      this.sessions.set(sessionId, session)
    }
    return session
  }
}
