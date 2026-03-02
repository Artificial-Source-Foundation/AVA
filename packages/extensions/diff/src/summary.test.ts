import { describe, expect, it } from 'vitest'
import { summarizeDiffSession } from './summary.js'
import type { DiffSession } from './types.js'

function makeSession(overrides: Partial<DiffSession> = {}): DiffSession {
  return { sessionId: 'test', diffs: [], startedAt: Date.now(), ...overrides }
}

describe('summarizeDiffSession', () => {
  it('returns zeros for empty session', () => {
    const summary = summarizeDiffSession(makeSession())
    expect(summary.filesChanged).toBe(0)
    expect(summary.additions).toBe(0)
    expect(summary.deletions).toBe(0)
    expect(summary.files).toHaveLength(0)
  })

  it('counts additions from added file', () => {
    const session = makeSession({
      diffs: [
        {
          path: '/src/new.ts',
          type: 'added',
          modified: 'line1\nline2',
          hunks: [
            {
              oldStart: 0,
              oldLines: 0,
              newStart: 1,
              newLines: 2,
              content: '+line1\n+line2',
            },
          ],
        },
      ],
    })

    const summary = summarizeDiffSession(session)
    expect(summary.filesChanged).toBe(1)
    expect(summary.additions).toBe(2)
    expect(summary.deletions).toBe(0)
    expect(summary.files[0]!.path).toBe('/src/new.ts')
    expect(summary.files[0]!.type).toBe('added')
  })

  it('counts both additions and deletions from modified file', () => {
    const session = makeSession({
      diffs: [
        {
          path: '/src/app.ts',
          type: 'modified',
          original: 'old line',
          modified: 'new line',
          hunks: [
            {
              oldStart: 1,
              oldLines: 1,
              newStart: 1,
              newLines: 1,
              content: '-old line\n+new line',
            },
          ],
        },
      ],
    })

    const summary = summarizeDiffSession(session)
    expect(summary.filesChanged).toBe(1)
    expect(summary.additions).toBe(1)
    expect(summary.deletions).toBe(1)
  })

  it('aggregates across multiple files', () => {
    const session = makeSession({
      diffs: [
        {
          path: '/a.ts',
          type: 'added',
          modified: 'a',
          hunks: [{ oldStart: 0, oldLines: 0, newStart: 1, newLines: 1, content: '+a' }],
        },
        {
          path: '/b.ts',
          type: 'modified',
          original: 'x',
          modified: 'y',
          hunks: [{ oldStart: 1, oldLines: 1, newStart: 1, newLines: 1, content: '-x\n+y' }],
        },
        {
          path: '/c.ts',
          type: 'deleted',
          original: 'gone',
          hunks: [],
        },
      ],
    })

    const summary = summarizeDiffSession(session)
    expect(summary.filesChanged).toBe(3)
    expect(summary.additions).toBe(2)
    expect(summary.deletions).toBe(1)
    expect(summary.files).toHaveLength(3)
    expect(summary.files[2]!.type).toBe('deleted')
  })

  it('handles deleted file with no hunks', () => {
    const session = makeSession({
      diffs: [
        {
          path: '/deleted.ts',
          type: 'deleted',
          original: 'content',
          hunks: [],
        },
      ],
    })

    const summary = summarizeDiffSession(session)
    expect(summary.filesChanged).toBe(1)
    expect(summary.additions).toBe(0)
    expect(summary.deletions).toBe(0)
    expect(summary.files[0]!.type).toBe('deleted')
  })

  it('handles multiple hunks in single file', () => {
    const session = makeSession({
      diffs: [
        {
          path: '/multi-hunk.ts',
          type: 'modified',
          original: 'a\nb\nc\nd',
          modified: 'a\nB\nc\nD',
          hunks: [
            { oldStart: 2, oldLines: 1, newStart: 2, newLines: 1, content: '-b\n+B' },
            { oldStart: 4, oldLines: 1, newStart: 4, newLines: 1, content: '-d\n+D' },
          ],
        },
      ],
    })

    const summary = summarizeDiffSession(session)
    expect(summary.additions).toBe(2)
    expect(summary.deletions).toBe(2)
  })
})
