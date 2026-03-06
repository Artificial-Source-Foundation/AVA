import type { ToolMiddlewareContext } from '@ava/core-v2/extensions'
import type { SimpleLogger } from '@ava/core-v2/logger'
import type { IPlatformProvider } from '@ava/core-v2/platform'
import type { ToolContext, ToolResult } from '@ava/core-v2/tools'
import { afterEach, describe, expect, it, vi } from 'vitest'
import {
  clearFormatterDetectionCache,
  createFormatterDetectionMiddleware,
} from './formatter-detection.js'

function createMockLogger(): SimpleLogger {
  return {
    debug: vi.fn(),
    info: vi.fn(),
    warn: vi.fn(),
    error: vi.fn(),
    time: vi.fn(),
    timing: vi.fn(),
    child: vi.fn().mockReturnThis(),
  }
}

function createPlatform(): {
  platform: IPlatformProvider
  files: Map<string, string>
} {
  const files = new Map<string, string>()
  const platform = {
    fs: {
      async readFile(filePath: string) {
        const value = files.get(filePath)
        if (value === undefined) {
          throw new Error(`ENOENT ${filePath}`)
        }
        return value
      },
      async writeFile(filePath: string, content: string) {
        files.set(filePath, content)
      },
      async exists(filePath: string) {
        return files.has(filePath)
      },
      async remove(filePath: string) {
        files.delete(filePath)
      },
    },
    shell: {
      async exec() {
        return { stdout: '', stderr: '', exitCode: 0 }
      },
    },
  }

  return { platform: platform as unknown as IPlatformProvider, files }
}

function middlewareContext(toolName: string, args: Record<string, unknown>): ToolMiddlewareContext {
  return {
    toolName,
    args,
    ctx: {
      sessionId: 'session-1',
      workingDirectory: '/project',
      signal: new AbortController().signal,
    } as ToolContext,
    definition: {
      name: toolName,
      description: '',
      input_schema: { type: 'object', properties: {} },
    },
  }
}

function successResult(): ToolResult {
  return { success: true, output: 'ok' }
}

describe('createFormatterDetectionMiddleware', () => {
  afterEach(() => {
    clearFormatterDetectionCache()
  })

  it('returns no metadata when formatter is not detected', async () => {
    const { platform, files } = createPlatform()
    files.set('/project/file.ts', 'const x = 1\n')
    const middleware = createFormatterDetectionMiddleware(platform, createMockLogger())

    const ctx = middlewareContext('write_file', {
      path: '/project/file.ts',
      content: 'const x = 2\n',
    })
    await middleware.before?.(ctx)
    files.set('/project/file.ts', 'const x = 2\n')

    const result = await middleware.after?.(ctx, successResult())
    expect(result).toBeUndefined()
  })

  it('reports formatter metadata when formatter changes output', async () => {
    const { platform, files } = createPlatform()
    files.set('/project/biome.json', '{}')
    files.set('/project/file.ts', 'const x=1\n')
    const middleware = createFormatterDetectionMiddleware(platform, createMockLogger())

    const ctx = middlewareContext('write_file', {
      path: '/project/file.ts',
      content: 'const x = 1\n',
    })
    await middleware.before?.(ctx)
    files.set('/project/file.ts', 'const x = 1;\n')

    const result = await middleware.after?.(ctx, successResult())
    const metadata = result?.result?.metadata as {
      formatterApplied?: boolean
      formatterDiff?: string
      formatterChange?: { formatterName: string }
    }

    expect(metadata.formatterApplied).toBe(true)
    expect(typeof metadata.formatterDiff).toBe('string')
    expect(metadata.formatterChange?.formatterName).toBe('biome')
  })

  it('returns no metadata when formatter introduces no extra changes', async () => {
    const { platform, files } = createPlatform()
    files.set('/project/.prettierrc', '{}')
    files.set('/project/file.ts', 'const y = 1\n')
    const middleware = createFormatterDetectionMiddleware(platform, createMockLogger())

    const ctx = middlewareContext('write_file', {
      path: '/project/file.ts',
      content: 'const y = 2\n',
    })
    await middleware.before?.(ctx)
    files.set('/project/file.ts', 'const y = 2\n')

    const result = await middleware.after?.(ctx, successResult())
    expect(result).toBeUndefined()
  })

  it('exposes structured formatter change metadata', async () => {
    const { platform, files } = createPlatform()
    files.set('/project/biome.json', '{}')
    files.set('/project/file.ts', 'const z=1\n')
    const middleware = createFormatterDetectionMiddleware(platform, createMockLogger())

    const ctx = middlewareContext('write_file', {
      path: '/project/file.ts',
      content: 'const z = 1\n',
    })
    await middleware.before?.(ctx)
    files.set('/project/file.ts', 'const z = 1;\n')

    const result = await middleware.after?.(ctx, successResult())
    const change = (result?.result?.metadata as { formatterChange?: Record<string, unknown> })
      .formatterChange

    expect(change).toMatchObject({
      file: '/project/file.ts',
      formatterName: 'biome',
    })
    expect(typeof change?.editChanges).toBe('string')
    expect(typeof change?.formatterChanges).toBe('string')
  })
})
