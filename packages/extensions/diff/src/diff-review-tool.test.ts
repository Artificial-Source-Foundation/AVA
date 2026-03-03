import { mkdtemp, readFile, rm, writeFile } from 'node:fs/promises'
import { tmpdir } from 'node:os'
import { join } from 'node:path'
import { describe, expect, it } from 'vitest'
import { createDiffReviewTool } from './diff-review-tool.js'
import { HunkReviewState } from './hunk-review/state.js'
import { createFileDiff } from './tracker.js'

function ctx() {
  return {
    sessionId: 's1',
    workingDirectory: '/tmp',
    signal: AbortSignal.timeout(5000),
  }
}

describe('diff_review tool', () => {
  it('lists hunks for a session', async () => {
    const state = new HunkReviewState()
    state.ingest('s1', createFileDiff('/a.ts', 'a\n', 'b\n'))
    const tool = createDiffReviewTool(state)

    const result = await tool.execute({ action: 'list' }, ctx())
    expect(result.success).toBe(true)
    expect(result.output).toContain('/a.ts')
  })

  it('returns summary status', async () => {
    const state = new HunkReviewState()
    state.ingest('s1', createFileDiff('/a.ts', 'a\n', 'b\n'))
    const tool = createDiffReviewTool(state)

    const result = await tool.execute({ action: 'status' }, ctx())
    expect(result.success).toBe(true)
    expect(result.output).toContain('"total"')
  })

  it('accepts a hunk by id', async () => {
    const state = new HunkReviewState()
    state.ingest('s1', createFileDiff('/a.ts', 'a\n', 'b\n'))
    const id = state.list('s1')[0]?.id
    const tool = createDiffReviewTool(state)

    const result = await tool.execute({ action: 'accept', hunkId: id }, ctx())
    expect(result.success).toBe(true)
    expect(state.list('s1')[0]?.status).toBe('accepted')
  })

  it('rejects a hunk by id', async () => {
    const state = new HunkReviewState()
    state.ingest('s1', createFileDiff('/a.ts', 'a\n', 'b\n'))
    const id = state.list('s1')[0]?.id
    const tool = createDiffReviewTool(state)

    const result = await tool.execute({ action: 'reject', hunkId: id }, ctx())
    expect(result.success).toBe(true)
    expect(state.list('s1')[0]?.status).toBe('rejected')
  })

  it('errors when accept/reject has no hunkId', async () => {
    const state = new HunkReviewState()
    const tool = createDiffReviewTool(state)

    const result = await tool.execute({ action: 'accept' }, ctx())
    expect(result.success).toBe(false)
    expect(result.error).toContain('hunkId is required')
  })

  it('applies accepted hunks to disk', async () => {
    const temp = await mkdtemp(join(tmpdir(), 'diff-review-'))
    const filePath = join(temp, 'example.ts')
    await writeFile(filePath, 'a\nb\n', 'utf-8')

    try {
      const state = new HunkReviewState()
      state.ingest('s1', createFileDiff(filePath, 'a\nb\n', 'x\nb\n'))
      const id = state.list('s1')[0]?.id
      const tool = createDiffReviewTool(state)

      await tool.execute({ action: 'accept', hunkId: id }, ctx())
      const result = await tool.execute({ action: 'apply' } as never, ctx())

      expect(result.success).toBe(true)
      const next = await readFile(filePath, 'utf-8')
      expect(next).toBe('x\nb\n')
    } finally {
      await rm(temp, { recursive: true, force: true })
    }
  })

  it('does not apply pending or rejected hunks', async () => {
    const temp = await mkdtemp(join(tmpdir(), 'diff-review-'))
    const filePath = join(temp, 'example.ts')
    await writeFile(filePath, 'a\nb\n', 'utf-8')

    try {
      const state = new HunkReviewState()
      state.ingest('s1', createFileDiff(filePath, 'a\nb\n', 'x\nb\n'))
      const id = state.list('s1')[0]?.id
      const tool = createDiffReviewTool(state)

      await tool.execute({ action: 'reject', hunkId: id }, ctx())
      const result = await tool.execute({ action: 'apply' } as never, ctx())

      expect(result.success).toBe(true)
      const next = await readFile(filePath, 'utf-8')
      expect(next).toBe('a\nb\n')
    } finally {
      await rm(temp, { recursive: true, force: true })
    }
  })
})
