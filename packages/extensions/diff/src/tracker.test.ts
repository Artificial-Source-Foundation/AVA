import { describe, expect, it } from 'vitest'
import { addDiff, computeSimpleDiff, createDiffSession, createFileDiff } from './tracker.js'

describe('computeSimpleDiff', () => {
  it('returns empty hunks for identical strings', () => {
    const hunks = computeSimpleDiff('hello\nworld', 'hello\nworld')
    expect(hunks).toHaveLength(0)
  })

  it('detects added lines', () => {
    const hunks = computeSimpleDiff('a\nb', 'a\nb\nc')
    expect(hunks).toHaveLength(1)
    expect(hunks[0].content).toContain('+c')
  })

  it('detects removed lines', () => {
    const hunks = computeSimpleDiff('a\nb\nc', 'a\nb')
    expect(hunks).toHaveLength(1)
    expect(hunks[0].content).toContain('-c')
  })

  it('detects modified lines', () => {
    const hunks = computeSimpleDiff('hello\nworld', 'hello\nearth')
    expect(hunks).toHaveLength(1)
    expect(hunks[0].content).toContain('-world')
    expect(hunks[0].content).toContain('+earth')
  })

  it('handles complete replacement', () => {
    const hunks = computeSimpleDiff('a\nb', 'c\nd')
    expect(hunks.length).toBeGreaterThan(0)
  })
})

describe('createFileDiff', () => {
  it('creates added diff when original is undefined', () => {
    const diff = createFileDiff('/test.ts', undefined, 'new content')
    expect(diff.type).toBe('added')
    expect(diff.original).toBeUndefined()
    expect(diff.modified).toBe('new content')
  })

  it('creates modified diff with hunks', () => {
    const diff = createFileDiff('/test.ts', 'old', 'new')
    expect(diff.type).toBe('modified')
    expect(diff.original).toBe('old')
    expect(diff.hunks.length).toBeGreaterThan(0)
  })
})

describe('createDiffSession + addDiff', () => {
  it('creates a session and adds diffs', () => {
    const session = createDiffSession('s1')
    expect(session.diffs).toHaveLength(0)

    const diff = createFileDiff('/test.ts', 'old', 'new')
    addDiff(session, diff)
    expect(session.diffs).toHaveLength(1)
  })

  it('replaces diff for same path', () => {
    const session = createDiffSession('s1')
    addDiff(session, createFileDiff('/test.ts', 'v1', 'v2'))
    addDiff(session, createFileDiff('/test.ts', 'v2', 'v3'))
    expect(session.diffs).toHaveLength(1)
    expect(session.diffs[0].original).toBe('v2')
  })

  it('appends diff for different paths', () => {
    const session = createDiffSession('s1')
    addDiff(session, createFileDiff('/a.ts', 'a', 'a2'))
    addDiff(session, createFileDiff('/b.ts', 'b', 'b2'))
    expect(session.diffs).toHaveLength(2)
  })
})
