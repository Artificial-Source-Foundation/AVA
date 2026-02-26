import { afterEach, beforeEach, describe, expect, it } from 'vitest'
import { installMockPlatform, type MockPlatform } from '../__test-utils__/mock-platform.js'
import { resetLogger } from '../logger/logger.js'
import { readFileTool } from './read.js'
import type { ToolContext } from './types.js'

function makeCtx(cwd = '/project'): ToolContext {
  return {
    sessionId: 'test',
    workingDirectory: cwd,
    signal: new AbortController().signal,
  }
}

describe('read_file tool', () => {
  let platform: MockPlatform

  beforeEach(() => {
    platform = installMockPlatform()
  })

  afterEach(() => {
    resetLogger()
  })

  it('reads file with line numbers', async () => {
    platform.fs.addFile('/project/test.ts', 'line1\nline2\nline3')
    const result = await readFileTool.execute({ path: 'test.ts' }, makeCtx())
    expect(result.success).toBe(true)
    expect(result.output).toContain('line1')
    expect(result.output).toContain('line2')
    expect(result.output).toContain('line3')
  })

  it('reads with offset', async () => {
    platform.fs.addFile('/project/test.ts', 'line1\nline2\nline3\nline4')
    const result = await readFileTool.execute({ path: 'test.ts', offset: 2 }, makeCtx())
    expect(result.success).toBe(true)
    expect(result.output).toContain('line3')
    expect(result.output).not.toContain('| line1')
  })

  it('reads with limit', async () => {
    platform.fs.addFile('/project/test.ts', 'line1\nline2\nline3\nline4')
    const result = await readFileTool.execute({ path: 'test.ts', limit: 2 }, makeCtx())
    expect(result.success).toBe(true)
    expect(result.output).toContain('line1')
    expect(result.output).toContain('line2')
    expect(result.output).toContain('Use offset=2 to continue')
  })

  it('throws for non-existent file', async () => {
    await expect(readFileTool.execute({ path: 'nonexistent.ts' }, makeCtx())).rejects.toThrow(
      'File not found'
    )
  })

  it('throws for directory', async () => {
    platform.fs.addDir('/project/src')
    await expect(readFileTool.execute({ path: 'src' }, makeCtx())).rejects.toThrow(
      'Path is a directory'
    )
  })

  it('throws for binary file', async () => {
    platform.fs.addBinary('/project/image.png', new Uint8Array([137, 80, 78, 71]))
    await expect(readFileTool.execute({ path: 'image.png' }, makeCtx())).rejects.toThrow(
      'Binary file'
    )
  })

  it('shows end of file note', async () => {
    platform.fs.addFile('/project/small.ts', 'one line')
    const result = await readFileTool.execute({ path: 'small.ts' }, makeCtx())
    expect(result.output).toContain('End of file')
  })

  it('includes file path in output', async () => {
    platform.fs.addFile('/project/test.ts', 'content')
    const result = await readFileTool.execute({ path: 'test.ts' }, makeCtx())
    expect(result.output).toContain('/project/test.ts')
  })

  it('returns metadata', async () => {
    platform.fs.addFile('/project/test.ts', 'a\nb\nc')
    const result = await readFileTool.execute({ path: 'test.ts' }, makeCtx())
    expect(result.metadata).toBeDefined()
    expect(result.metadata!.totalLines).toBe(3)
    expect(result.metadata!.linesRead).toBe(3)
  })

  it('returns locations', async () => {
    platform.fs.addFile('/project/test.ts', 'content')
    const result = await readFileTool.execute({ path: 'test.ts' }, makeCtx())
    expect(result.locations).toBeDefined()
    expect(result.locations![0].type).toBe('read')
  })

  it('resolves absolute paths', async () => {
    platform.fs.addFile('/other/file.ts', 'content')
    const result = await readFileTool.execute({ path: '/other/file.ts' }, makeCtx())
    expect(result.success).toBe(true)
  })

  it('throws on abort', async () => {
    platform.fs.addFile('/project/test.ts', 'content')
    const controller = new AbortController()
    controller.abort()
    await expect(
      readFileTool.execute(
        { path: 'test.ts' },
        {
          sessionId: 'test',
          workingDirectory: '/project',
          signal: controller.signal,
        }
      )
    ).rejects.toThrow('Aborted')
  })
})
