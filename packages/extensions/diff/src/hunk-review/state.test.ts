import { describe, expect, it } from 'vitest'

import { createFileDiff } from '../tracker.js'
import { HunkReviewState } from './state.js'

describe('hunk review state', () => {
  it('ingests hunks for a session', () => {
    const state = new HunkReviewState()
    const diff = createFileDiff('/a.ts', 'const a = 1\n', 'const a = 2\n')
    state.ingest('s1', diff)
    expect(state.list('s1')).toHaveLength(diff.hunks.length)
  })

  it('replaces hunks for same path when re-ingested', () => {
    const state = new HunkReviewState()
    state.ingest('s1', createFileDiff('/a.ts', 'a\n', 'b\n'))
    state.ingest('s1', createFileDiff('/a.ts', 'b\n', 'c\n'))
    const list = state.list('s1', '/a.ts')
    expect(list).toHaveLength(1)
    expect(list[0]?.path).toBe('/a.ts')
  })

  it('updates status for an existing hunk', () => {
    const state = new HunkReviewState()
    const diff = createFileDiff('/a.ts', 'a\n', 'b\n')
    state.ingest('s1', diff)
    const id = state.list('s1')[0]?.id
    expect(id).toBeDefined()
    expect(state.updateStatus('s1', id!, 'accepted')).toBe(true)
    expect(state.list('s1')[0]?.status).toBe('accepted')
  })

  it('returns false for unknown hunk update', () => {
    const state = new HunkReviewState()
    expect(state.updateStatus('s1', 'missing#0', 'rejected')).toBe(false)
  })

  it('returns summary counts', () => {
    const state = new HunkReviewState()
    const diff = createFileDiff('/a.ts', 'a\n', 'b\n')
    state.ingest('s1', diff)
    const id = state.list('s1')[0]?.id
    state.updateStatus('s1', id!, 'rejected')
    const summary = state.summary('s1')
    expect(summary.total).toBe(1)
    expect(summary.pending).toBe(0)
    expect(summary.rejected).toBe(1)
  })
})
