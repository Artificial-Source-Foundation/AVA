import type { ToolMiddlewareContext } from '@ava/core-v2/extensions'
import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest'
import {
  createModifiableToolMiddleware,
  type ModifyContext,
  makeModifiable,
  resetModifiableToolRegistry,
} from './modifiable-tool'

interface MockFs {
  readFile: ReturnType<typeof vi.fn>
  writeFile: ReturnType<typeof vi.fn>
  exists: ReturnType<typeof vi.fn>
  remove: ReturnType<typeof vi.fn>
}

const memory = new Map<string, string>()
const shellExec = vi.fn()
const fsMock: MockFs = {
  readFile: vi.fn(async (filePath: string) => {
    if (!memory.has(filePath)) {
      throw new Error(`ENOENT: ${filePath}`)
    }
    return memory.get(filePath) ?? ''
  }),
  writeFile: vi.fn(async (filePath: string, content: string) => {
    memory.set(filePath, content)
  }),
  exists: vi.fn(async (filePath: string) => memory.has(filePath)),
  remove: vi.fn(async (filePath: string) => {
    memory.delete(filePath)
  }),
}

vi.mock('@ava/core-v2/platform', () => ({
  getPlatform: () => ({
    fs: fsMock,
    shell: { exec: shellExec },
  }),
}))

const baseCtx: ToolMiddlewareContext = {
  toolName: 'write_file',
  args: { path: 'notes.txt', content: 'next' },
  ctx: {
    sessionId: 's-1',
    workingDirectory: '/workspace',
    signal: new AbortController().signal,
  },
  definition: {
    name: 'write_file',
    description: 'write file',
    input_schema: {
      type: 'object',
      properties: {
        path: { type: 'string' },
        content: { type: 'string' },
      },
      required: ['path', 'content'],
    },
  },
}

describe('modifiable tool middleware', () => {
  const originalEditor = process.env.EDITOR
  const originalVisual = process.env.VISUAL

  beforeEach(() => {
    resetModifiableToolRegistry()
    memory.clear()
    shellExec.mockReset()
    fsMock.readFile.mockClear()
    fsMock.writeFile.mockClear()
    fsMock.exists.mockClear()
    fsMock.remove.mockClear()
    process.env.EDITOR = 'nano'
    delete process.env.VISUAL
    memory.set('/workspace/notes.txt', 'current')
  })

  afterEach(() => {
    process.env.EDITOR = originalEditor
    process.env.VISUAL = originalVisual
  })

  it('passes through unchanged content when not modified', async () => {
    shellExec.mockResolvedValue({ stdout: '', stderr: '', exitCode: 0 })
    const middleware = createModifiableToolMiddleware()

    const result = await middleware.before?.(baseCtx)

    expect(result?.args).toEqual(baseCtx.args)
  })

  it('updates params when proposed content is modified', async () => {
    shellExec.mockImplementation(async (command: string) => {
      const matches = [...command.matchAll(/"([^"]+)"/g)].map((item) => item[1] ?? '')
      const proposedPath = matches[1]
      if (proposedPath) {
        memory.set(proposedPath, 'changed by editor')
      }
      return { stdout: '', stderr: '', exitCode: 0 }
    })

    const middleware = createModifiableToolMiddleware()
    const result = await middleware.before?.(baseCtx)

    expect(result?.args).toEqual({ path: 'notes.txt', content: 'changed by editor' })
  })

  it('falls back to vi when no editor variables are set', async () => {
    delete process.env.EDITOR
    delete process.env.VISUAL
    shellExec.mockResolvedValue({ stdout: '', stderr: '', exitCode: 0 })

    const middleware = createModifiableToolMiddleware()
    await middleware.before?.(baseCtx)

    expect(shellExec).toHaveBeenCalledTimes(1)
    const command = shellExec.mock.calls[0]?.[0] as string
    expect(command.startsWith('vi ')).toBe(true)
  })

  it('cleans up temp files on success and on error', async () => {
    const middleware = createModifiableToolMiddleware()

    shellExec.mockResolvedValue({ stdout: '', stderr: '', exitCode: 0 })
    await middleware.before?.(baseCtx)
    const successRemovals = fsMock.remove.mock.calls.length
    expect(successRemovals).toBeGreaterThanOrEqual(2)

    fsMock.remove.mockClear()
    shellExec.mockRejectedValueOnce(new Error('editor failed'))

    await expect(middleware.before?.(baseCtx)).rejects.toThrow('editor failed')
    expect(fsMock.remove.mock.calls.length).toBeGreaterThanOrEqual(2)
  })
})

describe('makeModifiable', () => {
  it('returns a tool with modifiable marker', () => {
    const tool = {
      definition: {
        name: 'dummy',
        description: 'dummy',
        input_schema: { type: 'object', properties: {} },
      },
      execute: vi.fn(),
    }

    const context: ModifyContext<{ content: string }> = {
      getFilePath: () => 'dummy.txt',
      async getCurrentContent() {
        return ''
      },
      getProposedContent(params) {
        return params.content
      },
      createUpdatedParams(_current, modified) {
        return { content: modified }
      },
    }

    const wrapped = makeModifiable(tool as never, context)
    const withMarker = wrapped.definition as unknown as { modifiable?: boolean }
    expect(withMarker.modifiable).toBe(true)
  })
})
