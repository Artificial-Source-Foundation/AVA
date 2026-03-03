import type { ToolMiddlewareContext } from '@ava/core-v2/extensions'
import { describe, expect, it } from 'vitest'
import { createMockExtensionAPI } from '../../../core-v2/src/__test-utils__/mock-extension-api.js'
import { activate } from './index.js'

function activateDiff(api: unknown) {
  return activate(api as never)
}

function makeCtx(
  toolName: string,
  args: Record<string, unknown>,
  sessionId = 'test'
): ToolMiddlewareContext {
  return {
    toolName,
    args,
    ctx: { sessionId, workingDirectory: '/tmp', signal: new AbortController().signal },
    definition: {
      name: toolName,
      description: '',
      input_schema: { type: 'object', properties: {} },
    },
  }
}

describe('diff extension', () => {
  it('activates and registers middleware', () => {
    const { api, registeredMiddleware } = createMockExtensionAPI()
    activateDiff(api)
    expect(registeredMiddleware).toHaveLength(1)
    expect(registeredMiddleware[0].name).toBe('ava-diff-tracker')
    expect(registeredMiddleware[0].priority).toBe(20)
  })

  it('middleware has before and after hooks', () => {
    const { api, registeredMiddleware } = createMockExtensionAPI()
    activateDiff(api)
    expect(registeredMiddleware[0].before).toBeTypeOf('function')
    expect(registeredMiddleware[0].after).toBeTypeOf('function')
  })

  it('registers diff_review tool', () => {
    const { api, registeredTools } = createMockExtensionAPI()
    activateDiff(api)
    expect(registeredTools.some((t) => t.definition?.name === 'diff_review')).toBe(true)
  })

  it('before hook snapshots file content for write tools', async () => {
    const { api, registeredMiddleware } = createMockExtensionAPI()
    api.platform.fs.addFile('/test.ts', 'original content')
    activateDiff(api)

    const mw = registeredMiddleware[0]
    const result = await mw.before!(makeCtx('write_file', { path: '/test.ts' }))

    // Should not block
    expect(result).toBeUndefined()
  })

  it('before hook skips non-write tools', async () => {
    const { api, registeredMiddleware } = createMockExtensionAPI()
    activateDiff(api)

    const mw = registeredMiddleware[0]
    const result = await mw.before!(makeCtx('read_file', { path: '/test.ts' }))

    expect(result).toBeUndefined()
  })

  it('tracks delete_file tool', async () => {
    const { api, registeredMiddleware } = createMockExtensionAPI()
    api.platform.fs.addFile('/to-delete.ts', 'content to delete')
    activateDiff(api)

    const mw = registeredMiddleware[0]
    // Before should snapshot the file
    const beforeResult = await mw.before!(makeCtx('delete_file', { path: '/to-delete.ts' }))
    expect(beforeResult).toBeUndefined()
  })

  it('cleans up on dispose', () => {
    const { api, registeredMiddleware } = createMockExtensionAPI()
    const disposable = activateDiff(api)
    expect(registeredMiddleware).toHaveLength(1)
    disposable.dispose()
    expect(registeredMiddleware).toHaveLength(0)
  })
})

// ─── Undo / Redo ────────────────────────────────────────────────────────────

describe('undo/redo', () => {
  async function simulateWrite(
    api: {
      platform: { fs: { writeFile: (path: string, content: string) => Promise<void> } }
      emit: (event: string, data: unknown) => void
    },
    mw: {
      before?: (ctx: ToolMiddlewareContext) => Promise<unknown>
      after?: (ctx: ToolMiddlewareContext, result: unknown) => Promise<unknown>
    },
    path: string,
    newContent: string,
    sessionId = 'test'
  ) {
    const ctx = makeCtx('write_file', { path }, sessionId)
    await mw.before!(ctx)
    // Simulate the tool writing the file
    await api.platform.fs.writeFile(path, newContent)
    await mw.after!(ctx, { success: true, output: 'ok' })
  }

  async function simulateCreate(
    api: {
      platform: { fs: { writeFile: (path: string, content: string) => Promise<void> } }
      emit: (event: string, data: unknown) => void
    },
    mw: {
      before?: (ctx: ToolMiddlewareContext) => Promise<unknown>
      after?: (ctx: ToolMiddlewareContext, result: unknown) => Promise<unknown>
    },
    path: string,
    content: string,
    sessionId = 'test'
  ) {
    const ctx = makeCtx('create_file', { path }, sessionId)
    await mw.before!(ctx)
    await api.platform.fs.writeFile(path, content)
    await mw.after!(ctx, { success: true, output: 'ok' })
  }

  async function simulateDelete(
    api: {
      platform: { fs: { remove: (path: string) => Promise<void> } }
      emit: (event: string, data: unknown) => void
    },
    mw: {
      before?: (ctx: ToolMiddlewareContext) => Promise<unknown>
      after?: (ctx: ToolMiddlewareContext, result: unknown) => Promise<unknown>
    },
    path: string,
    sessionId = 'test'
  ) {
    const ctx = makeCtx('delete_file', { path }, sessionId)
    await mw.before!(ctx)
    await api.platform.fs.remove(path)
    await mw.after!(ctx, { success: true, output: 'ok' })
  }

  it('undo modified file restores original content', async () => {
    const { api, registeredMiddleware, emittedEvents } = createMockExtensionAPI()
    ;(api.platform.fs as unknown as { addFile: (path: string, content: string) => void }).addFile(
      '/file.ts',
      'original'
    )
    activateDiff(api)
    const mw = registeredMiddleware[0]

    await simulateWrite(api, mw, '/file.ts', 'modified')
    expect(await api.platform.fs.readFile('/file.ts')).toBe('modified')

    // Trigger undo
    api.emit('diff:undo', { sessionId: 'test' })
    // Allow async handler to complete
    await new Promise((r) => setTimeout(r, 10))

    expect(await api.platform.fs.readFile('/file.ts')).toBe('original')
    expect(emittedEvents.some((e) => e.event === 'diff:undone')).toBe(true)
  })

  it('undo created file deletes it', async () => {
    const { api, registeredMiddleware } = createMockExtensionAPI()
    activateDiff(api)
    const mw = registeredMiddleware[0]

    await simulateCreate(api, mw, '/new.ts', 'new content')
    expect(await api.platform.fs.exists('/new.ts')).toBe(true)

    api.emit('diff:undo', { sessionId: 'test' })
    await new Promise((r) => setTimeout(r, 10))

    expect(await api.platform.fs.exists('/new.ts')).toBe(false)
  })

  it('undo deleted file recreates it with original content', async () => {
    const { api, registeredMiddleware } = createMockExtensionAPI()
    ;(api.platform.fs as unknown as { addFile: (path: string, content: string) => void }).addFile(
      '/doomed.ts',
      'precious content'
    )
    activateDiff(api)
    const mw = registeredMiddleware[0]

    await simulateDelete(api, mw, '/doomed.ts')
    expect(await api.platform.fs.exists('/doomed.ts')).toBe(false)

    api.emit('diff:undo', { sessionId: 'test' })
    await new Promise((r) => setTimeout(r, 10))

    expect(await api.platform.fs.readFile('/doomed.ts')).toBe('precious content')
  })

  it('redo after undo re-applies the change', async () => {
    const { api, registeredMiddleware } = createMockExtensionAPI()
    ;(api.platform.fs as unknown as { addFile: (path: string, content: string) => void }).addFile(
      '/file.ts',
      'original'
    )
    activateDiff(api)
    const mw = registeredMiddleware[0]

    await simulateWrite(api, mw, '/file.ts', 'modified')
    api.emit('diff:undo', { sessionId: 'test' })
    await new Promise((r) => setTimeout(r, 10))
    expect(await api.platform.fs.readFile('/file.ts')).toBe('original')

    api.emit('diff:redo', { sessionId: 'test' })
    await new Promise((r) => setTimeout(r, 10))
    expect(await api.platform.fs.readFile('/file.ts')).toBe('modified')
  })

  it('new write after undo clears redo stack', async () => {
    const { api, registeredMiddleware, emittedEvents } = createMockExtensionAPI()
    ;(api.platform.fs as unknown as { addFile: (path: string, content: string) => void }).addFile(
      '/file.ts',
      'v1'
    )
    activateDiff(api)
    const mw = registeredMiddleware[0]

    await simulateWrite(api, mw, '/file.ts', 'v2')
    api.emit('diff:undo', { sessionId: 'test' })
    await new Promise((r) => setTimeout(r, 10))

    // New write should clear redo stack
    await simulateWrite(api, mw, '/file.ts', 'v3')

    // Redo should fail now
    api.emit('diff:redo', { sessionId: 'test' })
    await new Promise((r) => setTimeout(r, 10))

    expect(emittedEvents.some((e) => e.event === 'diff:redo-failed')).toBe(true)
    expect(await api.platform.fs.readFile('/file.ts')).toBe('v3')
  })

  it('undo with nothing to undo emits diff:undo-failed', async () => {
    const { api, emittedEvents } = createMockExtensionAPI()
    activateDiff(api)

    api.emit('diff:undo', { sessionId: 'test' })
    await new Promise((r) => setTimeout(r, 10))

    expect(emittedEvents.some((e) => e.event === 'diff:undo-failed')).toBe(true)
  })

  it('redo with nothing to redo emits diff:redo-failed', async () => {
    const { api, emittedEvents } = createMockExtensionAPI()
    activateDiff(api)

    api.emit('diff:redo', { sessionId: 'test' })
    await new Promise((r) => setTimeout(r, 10))

    expect(emittedEvents.some((e) => e.event === 'diff:redo-failed')).toBe(true)
  })
})
