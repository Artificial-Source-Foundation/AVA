import { afterEach, beforeEach, describe, expect, it } from 'vitest'
import { installMockPlatform, type MockPlatform } from '../__test-utils__/mock-platform.js'
import { resetLogger } from '../logger/logger.js'
import type { ToolContext } from './types.js'
import { writeFileTool } from './write.js'

function makeCtx(cwd = '/project'): ToolContext {
  return {
    sessionId: 'test',
    workingDirectory: cwd,
    signal: new AbortController().signal,
  }
}

describe('write_file tool', () => {
  let platform: MockPlatform

  beforeEach(() => {
    platform = installMockPlatform()
    platform.fs.addDir('/project')
  })

  afterEach(() => {
    resetLogger()
  })

  it('creates a new file', async () => {
    const result = await writeFileTool.execute(
      { path: '/project/new.ts', content: 'const x = 1' },
      makeCtx()
    )
    expect(result.success).toBe(true)
    expect(result.output).toContain('Created')
    expect(await platform.fs.readFile('/project/new.ts')).toContain('const x = 1')
  })

  it('overwrites existing file', async () => {
    platform.fs.addFile('/project/existing.ts', 'old content')
    const result = await writeFileTool.execute(
      { path: '/project/existing.ts', content: 'new content' },
      makeCtx()
    )
    expect(result.success).toBe(true)
    expect(result.output).toContain('Updated')
    expect(await platform.fs.readFile('/project/existing.ts')).toContain('new content')
  })

  it('sanitizes content (strips markdown fences)', async () => {
    const result = await writeFileTool.execute(
      { path: '/project/file.ts', content: '```typescript\nconst x = 1\n```' },
      makeCtx()
    )
    expect(result.success).toBe(true)
    const written = await platform.fs.readFile('/project/file.ts')
    expect(written).not.toContain('```')
    expect(written).toContain('const x = 1')
  })

  it('ensures trailing newline', async () => {
    await writeFileTool.execute({ path: '/project/file.ts', content: 'no newline' }, makeCtx())
    const written = await platform.fs.readFile('/project/file.ts')
    expect(written.endsWith('\n')).toBe(true)
  })

  it('includes line and byte count in output', async () => {
    const result = await writeFileTool.execute(
      { path: '/project/file.ts', content: 'line1\nline2' },
      makeCtx()
    )
    expect(result.output).toMatch(/\d+ lines/)
    expect(result.output).toMatch(/\d+ bytes/)
  })

  it('returns metadata', async () => {
    const result = await writeFileTool.execute(
      { path: '/project/file.ts', content: 'content' },
      makeCtx()
    )
    expect(result.metadata).toBeDefined()
    expect(result.metadata!.filePath).toBe('/project/file.ts')
    expect(result.metadata!.overwritten).toBe(false)
  })

  it('throws for directory path', async () => {
    platform.fs.addDir('/project/src')
    await expect(
      writeFileTool.execute({ path: '/project/src', content: 'test' }, makeCtx())
    ).rejects.toThrow('Path is a directory')
  })

  it('validates content size', () => {
    const bigContent = 'x'.repeat(60 * 1024) // > 50KB
    expect(() => writeFileTool.validate!({ path: '/test', content: bigContent })).toThrow('exceeds')
  })

  it('validates required fields', () => {
    expect(() => writeFileTool.validate!({ path: '/test' })).toThrow()
  })

  it('creates parent directories', async () => {
    const result = await writeFileTool.execute(
      { path: '/project/deep/nested/file.ts', content: 'hi' },
      makeCtx()
    )
    expect(result.success).toBe(true)
  })

  it('throws on abort', async () => {
    const controller = new AbortController()
    controller.abort()
    await expect(
      writeFileTool.execute(
        { path: '/project/file.ts', content: 'test' },
        { sessionId: 'test', workingDirectory: '/project', signal: controller.signal }
      )
    ).rejects.toThrow('Aborted')
  })
})
