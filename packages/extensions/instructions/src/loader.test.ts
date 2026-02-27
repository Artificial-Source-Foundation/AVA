import { MockFileSystem } from '@ava/core-v2/__test-utils__/mock-platform'
import { describe, expect, it } from 'vitest'
import { loadInstructions, mergeInstructions } from './loader.js'

describe('loadInstructions', () => {
  it('returns empty array when no instruction files exist', async () => {
    const fs = new MockFileSystem()
    const result = await loadInstructions('/project', fs)
    expect(result).toEqual([])
  })

  it('finds instruction files in the working directory', async () => {
    const fs = new MockFileSystem()
    fs.addFile('/project/CLAUDE.md', '# Instructions')
    const result = await loadInstructions('/project', fs)
    expect(result).toHaveLength(1)
    expect(result[0].path).toBe('/project/CLAUDE.md')
    expect(result[0].scope).toBe('project')
  })

  it('finds instruction files in parent directories', async () => {
    const fs = new MockFileSystem()
    fs.addFile('/project/CLAUDE.md', '# Project')
    fs.addFile('/CLAUDE.md', '# Root')
    const result = await loadInstructions('/project', fs)
    expect(result).toHaveLength(2)
    // Project-level should have higher priority
    expect(result[0].path).toBe('/project/CLAUDE.md')
  })

  it('respects maxSize config', async () => {
    const fs = new MockFileSystem()
    fs.addFile('/project/CLAUDE.md', 'x'.repeat(100))
    const result = await loadInstructions('/project', fs, {
      fileNames: ['CLAUDE.md'],
      maxDepth: 5,
      maxSize: 50,
    })
    expect(result).toEqual([])
  })

  it('respects maxDepth config', async () => {
    const fs = new MockFileSystem()
    fs.addFile('/a/b/c/CLAUDE.md', 'deep')
    const result = await loadInstructions('/a/b/c', fs, {
      fileNames: ['CLAUDE.md'],
      maxDepth: 1,
      maxSize: 10_000,
    })
    expect(result).toHaveLength(1) // Only /a/b/c, not parents beyond depth 1
  })

  it('sorts by priority (higher first)', async () => {
    const fs = new MockFileSystem()
    fs.addFile('/project/CLAUDE.md', '# Project')
    fs.addFile('/CLAUDE.md', '# Root')
    const result = await loadInstructions('/project', fs)
    expect(result[0].priority).toBeGreaterThan(result[1].priority)
  })
})

describe('mergeInstructions', () => {
  it('returns empty string for no files', () => {
    expect(mergeInstructions([])).toBe('')
  })

  it('merges files with headers', () => {
    const result = mergeInstructions([
      { path: '/a', content: 'content A', scope: 'project', priority: 2 },
      { path: '/b', content: 'content B', scope: 'directory', priority: 1 },
    ])
    expect(result).toContain('# Instructions from /a')
    expect(result).toContain('content A')
    expect(result).toContain('# Instructions from /b')
  })
})
