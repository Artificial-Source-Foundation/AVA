import { afterEach, beforeEach, describe, expect, it } from 'vitest'
import { installMockPlatform, type MockPlatform } from '../__test-utils__/mock-platform.js'
import { resetLogger } from '../logger/logger.js'
import { editTool } from './edit.js'
import type { ToolContext } from './types.js'

function makeCtx(cwd = '/project'): ToolContext {
  return {
    sessionId: 'test',
    workingDirectory: cwd,
    signal: new AbortController().signal,
  }
}

describe('edit tool', () => {
  let platform: MockPlatform

  beforeEach(() => {
    platform = installMockPlatform()
    platform.fs.addDir('/project')
  })

  afterEach(() => {
    resetLogger()
  })

  // ─── Basic replacement ────────────────────────────────────────────────

  it('replaces text in file', async () => {
    platform.fs.addFile('/project/test.ts', 'const x = 1\nconst y = 2')
    const result = await editTool.execute(
      { filePath: '/project/test.ts', oldString: 'const x = 1', newString: 'const x = 10' },
      makeCtx()
    )
    expect(result.success).toBe(true)
    const content = await platform.fs.readFile('/project/test.ts')
    expect(content).toContain('const x = 10')
    expect(content).toContain('const y = 2')
  })

  it('shows line delta in output', async () => {
    platform.fs.addFile('/project/test.ts', 'line1\nline2')
    const result = await editTool.execute(
      { filePath: '/project/test.ts', oldString: 'line2', newString: 'line2a\nline2b' },
      makeCtx()
    )
    expect(result.output).toContain('+1')
  })

  it('includes file path in output', async () => {
    platform.fs.addFile('/project/test.ts', 'content')
    const result = await editTool.execute(
      { filePath: '/project/test.ts', oldString: 'content', newString: 'new' },
      makeCtx()
    )
    expect(result.output).toContain('/project/test.ts')
  })

  // ─── Empty oldString (create/overwrite) ───────────────────────────────

  it('creates file when oldString is empty', async () => {
    const result = await editTool.execute(
      { filePath: '/project/new.ts', oldString: '', newString: 'new content' },
      makeCtx()
    )
    expect(result.success).toBe(true)
    const content = await platform.fs.readFile('/project/new.ts')
    expect(content).toContain('new content')
  })

  // ─── Replace all ──────────────────────────────────────────────────────

  it('replaces all occurrences with replaceAll', async () => {
    platform.fs.addFile('/project/test.ts', 'foo bar foo baz foo')
    const result = await editTool.execute(
      {
        filePath: '/project/test.ts',
        oldString: 'foo',
        newString: 'replaced',
        replaceAll: true,
      },
      makeCtx()
    )
    expect(result.success).toBe(true)
    const content = await platform.fs.readFile('/project/test.ts')
    expect(content).not.toContain('foo')
    expect(content.match(/replaced/g)).toHaveLength(3)
  })

  // ─── Error cases ──────────────────────────────────────────────────────

  it('throws for non-existent file', async () => {
    await expect(
      editTool.execute(
        { filePath: '/project/missing.ts', oldString: 'x', newString: 'y' },
        makeCtx()
      )
    ).rejects.toThrow('File not found')
  })

  it('throws for directory', async () => {
    platform.fs.addDir('/project/src')
    await expect(
      editTool.execute({ filePath: '/project/src', oldString: 'x', newString: 'y' }, makeCtx())
    ).rejects.toThrow('Path is a directory')
  })

  it('validates oldString != newString', () => {
    expect(() =>
      editTool.validate!({
        filePath: '/test',
        oldString: 'same',
        newString: 'same',
      })
    ).toThrow('identical')
  })

  it('resolves relative paths', async () => {
    platform.fs.addFile('/project/src/test.ts', 'content')
    const result = await editTool.execute(
      { filePath: 'src/test.ts', oldString: 'content', newString: 'new' },
      makeCtx()
    )
    expect(result.success).toBe(true)
  })

  it('returns metadata', async () => {
    platform.fs.addFile('/project/test.ts', 'old')
    const result = await editTool.execute(
      { filePath: '/project/test.ts', oldString: 'old', newString: 'new' },
      makeCtx()
    )
    expect(result.metadata).toBeDefined()
    expect(result.metadata!.filePath).toBe('/project/test.ts')
    expect(result.metadata!.mode).toBe('replace')
  })

  it('returns locations', async () => {
    platform.fs.addFile('/project/test.ts', 'old')
    const result = await editTool.execute(
      { filePath: '/project/test.ts', oldString: 'old', newString: 'new' },
      makeCtx()
    )
    expect(result.locations).toBeDefined()
    expect(result.locations![0].type).toBe('write')
  })

  it('throws on abort', async () => {
    platform.fs.addFile('/project/test.ts', 'content')
    const controller = new AbortController()
    controller.abort()
    await expect(
      editTool.execute(
        { filePath: '/project/test.ts', oldString: 'content', newString: 'new' },
        { sessionId: 'test', workingDirectory: '/project', signal: controller.signal }
      )
    ).rejects.toThrow('Aborted')
  })
})
