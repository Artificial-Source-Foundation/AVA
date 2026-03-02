import { createMockPlatform, type MockPlatform } from '@ava/core-v2/__test-utils__/mock-platform'
import type { ToolMiddlewareContext } from '@ava/core-v2/extensions'
import type { SimpleLogger } from '@ava/core-v2/logger'
import type { ToolContext, ToolResult } from '@ava/core-v2/tools'
import { afterEach, describe, expect, it, vi } from 'vitest'
import { clearFormatterCache, createFormatterMiddleware } from './formatter.js'

function createMockLogger(): SimpleLogger {
  return {
    debug: vi.fn(),
    info: vi.fn(),
    warn: vi.fn(),
    error: vi.fn(),
    timing: vi.fn(),
    child: vi.fn().mockReturnThis(),
  }
}

function createMiddlewareContext(
  toolName: string,
  args: Record<string, unknown>,
  cwd = '/project'
): ToolMiddlewareContext {
  return {
    toolName,
    args,
    ctx: {
      sessionId: 'test-session',
      workingDirectory: cwd,
      signal: new AbortController().signal,
    } as ToolContext,
    definition: {
      name: toolName,
      description: '',
      input_schema: { type: 'object', properties: {} },
    },
  }
}

function successResult(output = 'OK'): ToolResult {
  return { success: true, output }
}

function failResult(): ToolResult {
  return { success: false, output: '', error: 'Failed' }
}

describe('createFormatterMiddleware', () => {
  let platform: MockPlatform
  let log: SimpleLogger

  afterEach(() => {
    clearFormatterCache()
  })

  it('skips non-file-editing tools', async () => {
    platform = createMockPlatform()
    log = createMockLogger()
    const mw = createFormatterMiddleware(platform, log)

    const ctx = createMiddlewareContext('read_file', { path: '/tmp/a.ts' })
    const result = await mw.after!(ctx, successResult())
    expect(result).toBeUndefined()
  })

  it('skips when tool result is failure', async () => {
    platform = createMockPlatform()
    log = createMockLogger()
    const mw = createFormatterMiddleware(platform, log)

    const ctx = createMiddlewareContext('write_file', { path: '/tmp/a.ts' })
    const result = await mw.after!(ctx, failResult())
    expect(result).toBeUndefined()
  })

  it('skips when no formatter config found', async () => {
    platform = createMockPlatform()
    log = createMockLogger()
    const mw = createFormatterMiddleware(platform, log)

    const ctx = createMiddlewareContext('write_file', { path: '/tmp/a.ts' })
    const result = await mw.after!(ctx, successResult())
    expect(result).toBeUndefined()
  })

  it('runs biome format when biome.json exists', async () => {
    platform = createMockPlatform()
    log = createMockLogger()
    platform.fs.addFile('/project/biome.json', '{}')
    const mw = createFormatterMiddleware(platform, log)

    const ctx = createMiddlewareContext('write_file', { path: '/project/src/index.ts' })
    const result = await mw.after!(ctx, successResult())
    expect(result).toBeUndefined()
    expect(log.debug).toHaveBeenCalledWith(expect.stringContaining('biome'))
  })

  it('runs prettier when .prettierrc exists', async () => {
    platform = createMockPlatform()
    log = createMockLogger()
    platform.fs.addFile('/project/.prettierrc', '{}')
    const mw = createFormatterMiddleware(platform, log)

    const ctx = createMiddlewareContext('edit', { filePath: '/project/src/app.tsx' })
    const result = await mw.after!(ctx, successResult())
    expect(result).toBeUndefined()
    expect(log.debug).toHaveBeenCalledWith(expect.stringContaining('prettier'))
  })

  it('runs deno fmt when deno.json exists', async () => {
    platform = createMockPlatform()
    log = createMockLogger()
    platform.fs.addFile('/project/deno.json', '{}')
    const mw = createFormatterMiddleware(platform, log)

    const ctx = createMiddlewareContext('create_file', { path: '/project/mod.ts' })
    const result = await mw.after!(ctx, successResult())
    expect(result).toBeUndefined()
    expect(log.debug).toHaveBeenCalledWith(expect.stringContaining('deno'))
  })

  it('handles formatter errors gracefully', async () => {
    platform = createMockPlatform()
    log = createMockLogger()
    platform.fs.addFile('/project/biome.json', '{}')
    platform.shell.setResult('npx biome format --write "/project/fail.ts"', {
      stdout: '',
      stderr: 'Biome error',
      exitCode: 1,
    })
    // Override exec to throw
    const origExec = platform.shell.exec.bind(platform.shell)
    platform.shell.exec = async (cmd: string) => {
      if (cmd.includes('fail.ts')) {
        throw new Error('Formatter crashed')
      }
      return origExec(cmd)
    }
    const mw = createFormatterMiddleware(platform, log)

    const ctx = createMiddlewareContext('write_file', { path: '/project/fail.ts' })
    // Should not throw
    const result = await mw.after!(ctx, successResult())
    expect(result).toBeUndefined()
    expect(log.warn).toHaveBeenCalledWith(expect.stringContaining('Formatter'))
  })

  it('handles apply_patch tool', async () => {
    platform = createMockPlatform()
    log = createMockLogger()
    platform.fs.addFile('/project/.prettierrc.json', '{}')
    const mw = createFormatterMiddleware(platform, log)

    const ctx = createMiddlewareContext('apply_patch', { path: '/project/src/utils.ts' })
    const result = await mw.after!(ctx, successResult())
    expect(result).toBeUndefined()
    expect(log.debug).toHaveBeenCalled()
  })

  it('skips when no file path in args', async () => {
    platform = createMockPlatform()
    log = createMockLogger()
    platform.fs.addFile('/project/biome.json', '{}')
    const mw = createFormatterMiddleware(platform, log)

    const ctx = createMiddlewareContext('write_file', { content: 'hello' })
    const result = await mw.after!(ctx, successResult())
    expect(result).toBeUndefined()
  })

  it('caches formatter detection per directory', async () => {
    platform = createMockPlatform()
    log = createMockLogger()
    platform.fs.addFile('/project/biome.json', '{}')
    const mw = createFormatterMiddleware(platform, log)

    const ctx1 = createMiddlewareContext('write_file', { path: '/project/a.ts' })
    await mw.after!(ctx1, successResult())

    const ctx2 = createMiddlewareContext('write_file', { path: '/project/b.ts' })
    await mw.after!(ctx2, successResult())

    // Both should have run the formatter
    expect(log.debug).toHaveBeenCalledTimes(2)
  })

  it('has correct priority', () => {
    platform = createMockPlatform()
    log = createMockLogger()
    const mw = createFormatterMiddleware(platform, log)
    expect(mw.priority).toBe(50)
  })
})
