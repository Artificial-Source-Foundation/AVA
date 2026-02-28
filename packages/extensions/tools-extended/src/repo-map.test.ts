import { setPlatform } from '@ava/core-v2/platform'
import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest'

// Mock platform before importing the tool
const mockFs = {
  glob: vi.fn().mockResolvedValue([]),
  stat: vi.fn().mockResolvedValue({ isFile: true, size: 1024 }),
  readFile: vi.fn(),
  writeFile: vi.fn(),
  remove: vi.fn(),
  exists: vi.fn(),
  mkdir: vi.fn(),
  readDir: vi.fn(),
}

beforeEach(() => {
  setPlatform({
    fs: mockFs,
    shell: { exec: vi.fn() },
    credentials: { get: vi.fn(), set: vi.fn(), delete: vi.fn(), list: vi.fn() },
    database: { get: vi.fn(), set: vi.fn(), delete: vi.fn(), list: vi.fn() },
  } as never)
})

// Import after mock setup
import { repoMapTool } from './repo-map.js'

const mockCtx = {
  sessionId: 'test',
  workingDirectory: '/project',
  signal: new AbortController().signal,
}

describe('repoMapTool', () => {
  afterEach(() => {
    vi.restoreAllMocks()
  })

  it('has correct definition', () => {
    expect(repoMapTool.definition.name).toBe('repo_map')
  })

  it('generates repo map with language breakdown', async () => {
    mockFs.glob.mockImplementation(async (pattern: string) => {
      if (pattern.includes('ts')) {
        return ['/project/src/index.ts', '/project/src/app.tsx', '/project/lib/utils.js']
      }
      return []
    })

    mockFs.stat.mockResolvedValue({ isFile: true, size: 2048 })

    const result = await repoMapTool.execute({}, mockCtx)
    expect(result.success).toBe(true)
    expect(result.output).toContain('Project Structure')
    expect(result.output).toContain('Language Breakdown')
  })

  it('filters by language', async () => {
    mockFs.glob.mockResolvedValue(['/project/src/a.ts', '/project/src/b.py'])
    mockFs.stat.mockResolvedValue({ isFile: true, size: 512 })

    const result = await repoMapTool.execute({ language: 'typescript' }, mockCtx)
    expect(result.success).toBe(true)
    expect(result.output).toContain('typescript')
    // Python files should be filtered out
    expect(result.output).not.toContain('python')
  })

  it('handles empty project', async () => {
    mockFs.glob.mockResolvedValue([])

    const result = await repoMapTool.execute({}, mockCtx)
    expect(result.success).toBe(true)
    expect(result.output).toContain('0 files')
  })

  it('handles cancelled signal', async () => {
    const controller = new AbortController()
    controller.abort()
    const result = await repoMapTool.execute({}, { ...mockCtx, signal: controller.signal })
    expect(result.success).toBe(false)
    expect(result.output).toContain('cancelled')
  })
})
