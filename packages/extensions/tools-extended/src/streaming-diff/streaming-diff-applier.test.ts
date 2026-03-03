import { beforeEach, describe, expect, it } from 'vitest'

import { createMockPlatform } from '../../../../core-v2/src/__test-utils__/mock-platform.js'
import { setPlatform } from '../../../../core-v2/src/platform.js'
import { applyPatch } from '../apply-patch/applier.js'
import { parsePatch } from '../apply-patch/parser.js'
import { StreamingDiffApplier } from './streaming-diff-applier.js'

interface MutableFs {
  addFile(path: string, content: string): void
  readFile(path: string): Promise<string>
}

describe('StreamingDiffApplier', () => {
  beforeEach(() => {
    setPlatform(createMockPlatform())
  })

  it('applies add operation incrementally once complete', async () => {
    const fs = setPlatformState()
    const applier = new StreamingDiffApplier('/repo')
    await applier.pushChunk('*** Begin Patch\n*** Add File: notes.txt\n+hello')
    const res = await applier.pushChunk('\n*** End Patch\n')
    expect(res.appliedCount).toBe(1)

    const content = await fs.readFile('/repo/notes.txt')
    expect(content).toBe('hello')
  })

  it('applies update operation split across chunks', async () => {
    const fs = setPlatformState()
    fs.addFile('/repo/a.ts', 'const x = 1\n')

    const applier = new StreamingDiffApplier('/repo')
    await applier.pushChunk(
      '*** Begin Patch\n*** Update File: a.ts\n@@ const x = 1 @@\n-const x = 1'
    )
    const res = await applier.pushChunk('\n+const x = 2\n*** End Patch\n')
    expect(res.appliedCount).toBe(1)
    expect(await fs.readFile('/repo/a.ts')).toContain('const x = 2')
  })

  it('keeps pending incomplete operation until finalize', async () => {
    const fs = setPlatformState()
    const applier = new StreamingDiffApplier('/repo')
    const r1 = await applier.pushChunk('*** Begin Patch\n*** Add File: todo.md\n+line1')
    expect(r1.appliedCount).toBe(0)

    const final = await applier.finalize()
    expect(final.appliedCount).toBe(1)
    expect(await fs.readFile('/repo/todo.md')).toBe('line1')
  })

  it('returns errors for invalid operation and continues', async () => {
    const fs = setPlatformState()
    const applier = new StreamingDiffApplier('/repo')
    const chunk = [
      '*** Begin Patch',
      '*** Update File: missing.ts',
      '@@ x @@',
      '-x',
      '+y',
      '*** Add File: ok.txt',
      '+ok',
      '*** End Patch',
      '',
    ].join('\n')

    const result = await applier.pushChunk(chunk)
    expect(result.hadError).toBe(true)
    expect(result.errors.length).toBeGreaterThan(0)
    expect(await fs.readFile('/repo/ok.txt')).toBe('ok')
  })

  it('matches final content with non-streaming patch apply', async () => {
    const fs = setPlatformState()
    fs.addFile('/repo/source.ts', 'export const value = 1\n')

    const fullPatch = [
      '*** Begin Patch',
      '*** Update File: source.ts',
      '@@ export const value = 1 @@',
      '-export const value = 1',
      '+export const value = 9',
      '*** End Patch',
      '',
    ].join('\n')

    const streaming = new StreamingDiffApplier('/repo')
    await streaming.pushChunk(fullPatch.slice(0, Math.floor(fullPatch.length / 2)))
    await streaming.pushChunk(fullPatch.slice(Math.floor(fullPatch.length / 2)))
    await streaming.finalize()
    const streamingOutput = await fs.readFile('/repo/source.ts')

    fs.addFile('/repo/source.ts', 'export const value = 1\n')
    await applyPatch(parsePatch(fullPatch), '/repo', false)
    const nonStreamingOutput = await fs.readFile('/repo/source.ts')

    expect(streamingOutput).toBe(nonStreamingOutput)
  })
})

function setPlatformState(): MutableFs {
  const platform = createMockPlatform()
  setPlatform(platform)
  return platform.fs as unknown as MutableFs
}
