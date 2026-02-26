import { afterEach, beforeEach, describe, expect, it } from 'vitest'
import { installMockPlatform, type MockPlatform } from '../__test-utils__/mock-platform.js'
import { resetLogger } from '../logger/logger.js'
import { globTool } from './glob.js'
import type { ToolContext } from './types.js'

function makeCtx(cwd = '/project'): ToolContext {
  return {
    sessionId: 'test',
    workingDirectory: cwd,
    signal: new AbortController().signal,
  }
}

describe('glob tool', () => {
  let platform: MockPlatform

  beforeEach(() => {
    platform = installMockPlatform()
    platform.fs.addDir('/project')
    platform.fs.addDir('/project/src')
  })

  afterEach(() => {
    resetLogger()
  })

  it('finds files matching pattern', async () => {
    platform.fs.addFile('/project/src/index.ts', 'code')
    platform.fs.addFile('/project/src/utils.ts', 'code')
    platform.fs.addFile('/project/src/style.css', 'css')

    const result = await globTool.execute({ pattern: '**/*.ts' }, makeCtx())
    expect(result.success).toBe(true)
    expect(result.output).toContain('index.ts')
    expect(result.output).toContain('utils.ts')
    expect(result.output).not.toContain('style.css')
  })

  it('returns no matches message', async () => {
    const result = await globTool.execute({ pattern: '**/*.xyz' }, makeCtx())
    expect(result.success).toBe(true)
    expect(result.output).toContain('No files found')
  })

  it('searches from specified path', async () => {
    platform.fs.addFile('/project/src/a.ts', 'code')
    platform.fs.addFile('/project/lib/b.ts', 'code')
    platform.fs.addDir('/project/lib')

    const result = await globTool.execute({ pattern: '*.ts', path: 'src' }, makeCtx())
    expect(result.success).toBe(true)
    expect(result.output).toContain('a.ts')
  })

  it('includes file count in output', async () => {
    platform.fs.addFile('/project/src/a.ts', 'code')
    platform.fs.addFile('/project/src/b.ts', 'code')

    const result = await globTool.execute({ pattern: '**/*.ts' }, makeCtx())
    expect(result.output).toContain('2 file(s)')
  })

  it('returns metadata with count', async () => {
    platform.fs.addFile('/project/src/a.ts', 'code')

    const result = await globTool.execute({ pattern: '**/*.ts' }, makeCtx())
    expect(result.metadata).toBeDefined()
    expect(result.metadata!.count).toBe(1)
  })

  it('returns locations', async () => {
    platform.fs.addFile('/project/src/a.ts', 'code')

    const result = await globTool.execute({ pattern: '**/*.ts' }, makeCtx())
    expect(result.locations).toBeDefined()
    expect(result.locations!.length).toBeGreaterThan(0)
  })

  it('skips node_modules', async () => {
    platform.fs.addFile('/project/node_modules/pkg/index.ts', 'code')
    platform.fs.addDir('/project/node_modules')
    platform.fs.addDir('/project/node_modules/pkg')

    const result = await globTool.execute({ pattern: '**/*.ts' }, makeCtx())
    expect(result.output).not.toContain('node_modules')
  })

  it('skips hidden directories', async () => {
    platform.fs.addFile('/project/.hidden/file.ts', 'code')
    platform.fs.addDir('/project/.hidden')

    const result = await globTool.execute({ pattern: '**/*.ts' }, makeCtx())
    expect(result.output).not.toContain('.hidden')
  })
})
