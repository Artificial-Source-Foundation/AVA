import { afterEach, beforeEach, describe, expect, it } from 'vitest'
import { installMockPlatform, type MockPlatform } from '../__test-utils__/mock-platform.js'
import { resetLogger } from '../logger/logger.js'
import { grepTool } from './grep.js'
import type { ToolContext } from './types.js'

function makeCtx(cwd = '/project'): ToolContext {
  return {
    sessionId: 'test',
    workingDirectory: cwd,
    signal: new AbortController().signal,
  }
}

describe('grep tool', () => {
  let platform: MockPlatform

  beforeEach(() => {
    platform = installMockPlatform()
    platform.fs.addDir('/project')
    platform.fs.addDir('/project/src')
  })

  afterEach(() => {
    resetLogger()
  })

  it('finds matches in files', async () => {
    platform.fs.addFile('/project/src/a.ts', 'const foo = 1\nconst bar = 2')
    platform.fs.addFile('/project/src/b.ts', 'const baz = 3')

    const result = await grepTool.execute({ pattern: 'const' }, makeCtx())
    expect(result.success).toBe(true)
    expect(result.output).toContain('3 match(es)')
  })

  it('supports regex patterns', async () => {
    platform.fs.addFile('/project/src/a.ts', 'function foo() {}\nconst bar = 1')

    const result = await grepTool.execute({ pattern: 'function \\w+' }, makeCtx())
    expect(result.success).toBe(true)
    expect(result.output).toContain('foo')
  })

  it('returns no matches message', async () => {
    platform.fs.addFile('/project/src/a.ts', 'hello world')

    const result = await grepTool.execute({ pattern: 'nonexistent' }, makeCtx())
    expect(result.success).toBe(true)
    expect(result.output).toContain('No matches found')
  })

  it('filters by include pattern', async () => {
    platform.fs.addFile('/project/src/a.ts', 'target')
    platform.fs.addFile('/project/src/b.js', 'target')

    const result = await grepTool.execute({ pattern: 'target', include: '*.ts' }, makeCtx())
    expect(result.success).toBe(true)
    expect(result.output).toContain('a.ts')
    expect(result.output).not.toContain('b.js')
  })

  it('searches from specified path', async () => {
    platform.fs.addFile('/project/src/a.ts', 'target')
    platform.fs.addFile('/project/lib/b.ts', 'target')
    platform.fs.addDir('/project/lib')

    const result = await grepTool.execute({ pattern: 'target', path: 'src' }, makeCtx())
    expect(result.success).toBe(true)
    expect(result.output).toContain('a.ts')
  })

  it('groups results by file', async () => {
    platform.fs.addFile('/project/src/a.ts', 'line1 match\nline2 match')

    const result = await grepTool.execute({ pattern: 'match' }, makeCtx())
    expect(result.output).toContain('a.ts:')
    expect(result.output).toContain('Line 1')
    expect(result.output).toContain('Line 2')
  })

  it('includes line numbers', async () => {
    platform.fs.addFile('/project/src/a.ts', 'no\nyes match\nno')

    const result = await grepTool.execute({ pattern: 'match' }, makeCtx())
    expect(result.output).toContain('Line 2')
  })

  it('returns metadata', async () => {
    platform.fs.addFile('/project/src/a.ts', 'match')

    const result = await grepTool.execute({ pattern: 'match' }, makeCtx())
    expect(result.metadata).toBeDefined()
    expect(result.metadata!.count).toBe(1)
    expect(result.metadata!.fileCount).toBe(1)
  })

  it('returns locations', async () => {
    platform.fs.addFile('/project/src/a.ts', 'match')

    const result = await grepTool.execute({ pattern: 'match' }, makeCtx())
    expect(result.locations).toBeDefined()
    expect(result.locations!.length).toBeGreaterThan(0)
  })

  it('validates regex pattern', () => {
    expect(() => grepTool.validate!({ pattern: '[invalid' })).toThrow('Invalid regex')
  })

  it('skips binary files', async () => {
    platform.fs.addBinary('/project/src/image.png', new Uint8Array([137, 80, 78, 71]))

    const result = await grepTool.execute({ pattern: '.*' }, makeCtx())
    expect(result.output).toContain('No matches')
  })

  it('skips node_modules', async () => {
    platform.fs.addFile('/project/node_modules/pkg/index.js', 'match')
    platform.fs.addDir('/project/node_modules')
    platform.fs.addDir('/project/node_modules/pkg')

    const result = await grepTool.execute({ pattern: 'match' }, makeCtx())
    expect(result.output).not.toContain('node_modules')
  })
})
