import { describe, expect, it } from 'vitest'
import { createMockExtensionAPI } from '../../../core-v2/src/__test-utils__/mock-extension-api.js'
import { activate } from './index.js'
import { DiffSandbox } from './sandbox.js'

type ToolMiddlewareContext = {
  toolName: string
  args: Record<string, unknown>
  ctx: {
    sessionId: string
    workingDirectory: string
    signal: AbortSignal
  }
  definition: {
    name: string
    description: string
    input_schema: {
      type: 'object'
      properties: Record<string, unknown>
    }
  }
}

function activateDiff(api: unknown) {
  return activate(api as never)
}

function makeContext(
  toolName: string,
  args: Record<string, unknown>,
  sessionId = 'sandbox-session'
): ToolMiddlewareContext {
  return {
    toolName,
    args,
    ctx: {
      sessionId,
      workingDirectory: '/tmp',
      signal: new AbortController().signal,
    },
    definition: {
      name: toolName,
      description: `Mock ${toolName}`,
      input_schema: {
        type: 'object',
        properties: {},
      },
    },
  }
}

describe('DiffSandbox', () => {
  it('stages changes and returns them from getPending', async () => {
    const { api } = createMockExtensionAPI()
    const sandbox = new DiffSandbox(api.platform.fs)

    const staged = sandbox.stage({
      file: '/tmp/sandbox-a.txt',
      type: 'create',
      originalContent: null,
      newContent: 'hello',
    })

    const pending = sandbox.getPending()
    expect(pending).toHaveLength(1)
    expect(pending[0]?.id).toBe(staged.id)
    expect(pending[0]?.diff).toContain('+++ b//tmp/sandbox-a.txt')
  })

  it('applies a specific staged change to filesystem', async () => {
    const { api } = createMockExtensionAPI()
    const file = '/tmp/sandbox-b.txt'
    await api.platform.fs.writeFile(file, 'old')
    const sandbox = new DiffSandbox(api.platform.fs)

    const staged = sandbox.stage({
      file,
      type: 'modify',
      originalContent: 'old',
      newContent: 'new',
    })

    await sandbox.apply(staged.id)

    await expect(api.platform.fs.readFile(file)).resolves.toBe('new')
    expect(sandbox.getPending()).toHaveLength(0)
  })

  it('reject removes a pending change', async () => {
    const { api } = createMockExtensionAPI()
    const sandbox = new DiffSandbox(api.platform.fs)
    const staged = sandbox.stage({
      file: '/tmp/sandbox-c.txt',
      type: 'create',
      originalContent: null,
      newContent: 'new',
    })

    sandbox.reject(staged.id)

    expect(sandbox.getPending()).toHaveLength(0)
  })

  it('applyAll writes all staged changes and clears pending', async () => {
    const { api } = createMockExtensionAPI()
    const sandbox = new DiffSandbox(api.platform.fs)

    sandbox.stage({
      file: '/tmp/sandbox-d.txt',
      type: 'create',
      originalContent: null,
      newContent: 'd',
    })
    sandbox.stage({
      file: '/tmp/sandbox-e.txt',
      type: 'create',
      originalContent: null,
      newContent: 'e',
    })

    await sandbox.applyAll()

    await expect(api.platform.fs.readFile('/tmp/sandbox-d.txt')).resolves.toBe('d')
    await expect(api.platform.fs.readFile('/tmp/sandbox-e.txt')).resolves.toBe('e')
    expect(sandbox.getPending()).toHaveLength(0)
  })

  it('bypasses sandbox middleware when diff.sandbox.enabled is false', async () => {
    const { api, registeredMiddleware, emittedEvents } = createMockExtensionAPI()
    api.getSettings = (<T>(namespace: string): T => {
      if (namespace === 'diff') {
        return { sandbox: { enabled: false } } as T
      }
      return {} as T
    }) as typeof api.getSettings

    const ext = activateDiff(api)
    const sandboxMiddleware = registeredMiddleware.find((mw) => mw.name === 'ava-diff-sandbox')
    expect(sandboxMiddleware?.before).toBeDefined()

    const ctx = makeContext('write_file', { path: '/tmp/sandbox-f.txt', content: 'f' })
    const result = await sandboxMiddleware?.before?.(ctx)

    expect(result).toBeUndefined()
    expect(emittedEvents.some((event) => event.event === 'diff:staged')).toBe(false)

    ext.dispose()
  })
})
