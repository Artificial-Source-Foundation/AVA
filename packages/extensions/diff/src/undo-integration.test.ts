/**
 * Full undo/redo integration test — verifies the complete chain:
 * write_file → diff middleware captures → emit diff:undo → file restored.
 */

import { createMockExtensionAPI } from '@ava/core-v2/__test-utils__/mock-extension-api'
import type { MockPlatform } from '@ava/core-v2/__test-utils__/mock-platform'
import type { ToolMiddlewareContext } from '@ava/core-v2/extensions'
import type { ChatMessage } from '@ava/core-v2/llm'
import { describe, expect, it } from 'vitest'
import { activate } from './index.js'

function makeCtx(
  toolName: string,
  args: Record<string, unknown>,
  sessionId = 'int-test'
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

async function simulateWrite(
  api: { platform: MockPlatform; emit: (e: string, d: unknown) => void },
  mw: {
    before?: (ctx: ToolMiddlewareContext) => Promise<unknown>
    after?: (ctx: ToolMiddlewareContext, result: unknown) => Promise<unknown>
  },
  path: string,
  newContent: string,
  sessionId = 'int-test'
): Promise<void> {
  const ctx = makeCtx('write_file', { path }, sessionId)
  await mw.before!(ctx)
  await api.platform.fs.writeFile(path, newContent)
  await mw.after!(ctx, { success: true, output: 'ok' })
}

/**
 * Create a mock API with a functional session manager that supports
 * get() and setMessages() for message tracking in undo/redo tests.
 */
function createMockAPIWithSessions(sessionId = 'int-test') {
  const result = createMockExtensionAPI()

  // Lightweight session state for message tracking
  const sessionMessages: ChatMessage[] = []
  const mockSessionMgr = {
    get(id: string) {
      if (id === sessionId) {
        return { id: sessionId, messages: sessionMessages }
      }
      return null
    },
    setMessages(id: string, msgs: ChatMessage[]) {
      if (id === sessionId) {
        sessionMessages.length = 0
        sessionMessages.push(...msgs)
      }
    },
  }

  // Patch the API's getSessionManager to return the mock
  ;(result.api as Record<string, unknown>).getSessionManager = () => mockSessionMgr

  return { ...result, sessionMessages }
}

const tick = () => new Promise((r) => setTimeout(r, 10))

describe('undo integration — full chain', () => {
  it('write → capture → undo → file restored', async () => {
    const { api, registeredMiddleware, emittedEvents } = createMockExtensionAPI()
    api.platform.fs.addFile('/src/app.ts', 'const x = 1')
    activate(api)
    const mw = registeredMiddleware[0]

    // 1. Simulate write_file tool modifying the file
    await simulateWrite(api, mw, '/src/app.ts', 'const x = 2')
    expect(await api.platform.fs.readFile('/src/app.ts')).toBe('const x = 2')

    // 2. Verify diff:changed was emitted
    const changedEvent = emittedEvents.find((e) => e.event === 'diff:changed')
    expect(changedEvent).toBeDefined()
    const diffData = changedEvent!.data as { sessionId: string; diff: { path: string } }
    expect(diffData.sessionId).toBe('int-test')
    expect(diffData.diff.path).toBe('/src/app.ts')

    // 3. Emit diff:undo → file should be restored
    api.emit('diff:undo', { sessionId: 'int-test' })
    await tick()

    expect(await api.platform.fs.readFile('/src/app.ts')).toBe('const x = 1')

    // 4. Verify diff:undone was emitted
    const undoneEvent = emittedEvents.find((e) => e.event === 'diff:undone')
    expect(undoneEvent).toBeDefined()
    const undoneData = undoneEvent!.data as { sessionId: string; diff: { path: string } }
    expect(undoneData.diff.path).toBe('/src/app.ts')
  })

  it('undo then redo restores modified content', async () => {
    const { api, registeredMiddleware, emittedEvents } = createMockExtensionAPI()
    api.platform.fs.addFile('/src/util.ts', 'v1')
    activate(api)
    const mw = registeredMiddleware[0]

    await simulateWrite(api, mw, '/src/util.ts', 'v2')
    expect(await api.platform.fs.readFile('/src/util.ts')).toBe('v2')

    // Undo
    api.emit('diff:undo', { sessionId: 'int-test' })
    await tick()
    expect(await api.platform.fs.readFile('/src/util.ts')).toBe('v1')

    // Redo
    api.emit('diff:redo', { sessionId: 'int-test' })
    await tick()
    expect(await api.platform.fs.readFile('/src/util.ts')).toBe('v2')

    // Verify diff:redone emitted
    const redoneEvent = emittedEvents.find((e) => e.event === 'diff:redone')
    expect(redoneEvent).toBeDefined()
  })

  it('multiple writes → sequential undos restore each version', async () => {
    const { api, registeredMiddleware } = createMockExtensionAPI()
    api.platform.fs.addFile('/src/multi.ts', 'v0')
    activate(api)
    const mw = registeredMiddleware[0]

    await simulateWrite(api, mw, '/src/multi.ts', 'v1')
    await simulateWrite(api, mw, '/src/multi.ts', 'v2')
    await simulateWrite(api, mw, '/src/multi.ts', 'v3')
    expect(await api.platform.fs.readFile('/src/multi.ts')).toBe('v3')

    // Undo v3 → v2
    api.emit('diff:undo', { sessionId: 'int-test' })
    await tick()
    expect(await api.platform.fs.readFile('/src/multi.ts')).toBe('v2')

    // Undo v2 → v1
    api.emit('diff:undo', { sessionId: 'int-test' })
    await tick()
    expect(await api.platform.fs.readFile('/src/multi.ts')).toBe('v1')

    // Undo v1 → v0
    api.emit('diff:undo', { sessionId: 'int-test' })
    await tick()
    expect(await api.platform.fs.readFile('/src/multi.ts')).toBe('v0')
  })

  it('undo of created file deletes it, redo recreates it', async () => {
    const { api, registeredMiddleware } = createMockExtensionAPI()
    activate(api)
    const mw = registeredMiddleware[0]

    // Simulate create_file
    const ctx = makeCtx('create_file', { path: '/src/new.ts' })
    await mw.before!(ctx)
    await api.platform.fs.writeFile('/src/new.ts', 'new content')
    await mw.after!(ctx, { success: true, output: 'ok' })

    expect(await api.platform.fs.exists('/src/new.ts')).toBe(true)

    // Undo → file removed
    api.emit('diff:undo', { sessionId: 'int-test' })
    await tick()
    expect(await api.platform.fs.exists('/src/new.ts')).toBe(false)

    // Redo → file recreated
    api.emit('diff:redo', { sessionId: 'int-test' })
    await tick()
    expect(await api.platform.fs.exists('/src/new.ts')).toBe(true)
    expect(await api.platform.fs.readFile('/src/new.ts')).toBe('new content')
  })

  it('undo of deleted file recreates it, redo deletes it again', async () => {
    const { api, registeredMiddleware } = createMockExtensionAPI()
    api.platform.fs.addFile('/src/doomed.ts', 'precious')
    activate(api)
    const mw = registeredMiddleware[0]

    // Simulate delete_file
    const ctx = makeCtx('delete_file', { path: '/src/doomed.ts' })
    await mw.before!(ctx)
    await api.platform.fs.remove('/src/doomed.ts')
    await mw.after!(ctx, { success: true, output: 'ok' })

    expect(await api.platform.fs.exists('/src/doomed.ts')).toBe(false)

    // Undo → file restored
    api.emit('diff:undo', { sessionId: 'int-test' })
    await tick()
    expect(await api.platform.fs.readFile('/src/doomed.ts')).toBe('precious')

    // Redo → file deleted again
    api.emit('diff:redo', { sessionId: 'int-test' })
    await tick()
    expect(await api.platform.fs.exists('/src/doomed.ts')).toBe(false)
  })

  it('undo across different files works independently', async () => {
    const { api, registeredMiddleware } = createMockExtensionAPI()
    api.platform.fs.addFile('/src/a.ts', 'a-original')
    api.platform.fs.addFile('/src/b.ts', 'b-original')
    activate(api)
    const mw = registeredMiddleware[0]

    await simulateWrite(api, mw, '/src/a.ts', 'a-modified')
    await simulateWrite(api, mw, '/src/b.ts', 'b-modified')

    // Undo b → b restored, a unchanged
    api.emit('diff:undo', { sessionId: 'int-test' })
    await tick()
    expect(await api.platform.fs.readFile('/src/b.ts')).toBe('b-original')
    expect(await api.platform.fs.readFile('/src/a.ts')).toBe('a-modified')

    // Undo a → both restored
    api.emit('diff:undo', { sessionId: 'int-test' })
    await tick()
    expect(await api.platform.fs.readFile('/src/a.ts')).toBe('a-original')
  })

  it('new write after undo clears redo stack', async () => {
    const { api, registeredMiddleware, emittedEvents } = createMockExtensionAPI()
    api.platform.fs.addFile('/src/fork.ts', 'v1')
    activate(api)
    const mw = registeredMiddleware[0]

    await simulateWrite(api, mw, '/src/fork.ts', 'v2')

    // Undo
    api.emit('diff:undo', { sessionId: 'int-test' })
    await tick()
    expect(await api.platform.fs.readFile('/src/fork.ts')).toBe('v1')

    // New write (clears redo stack)
    await simulateWrite(api, mw, '/src/fork.ts', 'v3')

    // Redo should fail
    api.emit('diff:redo', { sessionId: 'int-test' })
    await tick()

    expect(emittedEvents.some((e) => e.event === 'diff:redo-failed')).toBe(true)
    expect(await api.platform.fs.readFile('/src/fork.ts')).toBe('v3')
  })

  it('dispose clears all state', async () => {
    const { api, registeredMiddleware } = createMockExtensionAPI()
    api.platform.fs.addFile('/src/x.ts', 'original')
    const disposable = activate(api)
    const mw = registeredMiddleware[0]

    await simulateWrite(api, mw, '/src/x.ts', 'changed')
    disposable.dispose()

    // After dispose, undo event has no handler
    api.emit('diff:undo', { sessionId: 'int-test' })
    await tick()

    // File should still be 'changed' since the handler was disposed
    expect(await api.platform.fs.readFile('/src/x.ts')).toBe('changed')
  })
})

// ─── Message Removal on Undo / Restoration on Redo ──────────────────────────

describe('undo integration — message removal', () => {
  it('undo removes the assistant message from the session', async () => {
    const { api, registeredMiddleware, sessionMessages } = createMockAPIWithSessions()
    api.platform.fs.addFile('/src/msg.ts', 'original')
    activate(api)
    const mw = registeredMiddleware[0]

    // Simulate assistant message being present before the write
    const assistantMsg: ChatMessage = {
      role: 'assistant',
      content: [{ type: 'text', text: 'I will edit msg.ts' }],
    }
    sessionMessages.push(assistantMsg)

    // Simulate write_file — the after() middleware records messageIndex
    await simulateWrite(api, mw, '/src/msg.ts', 'modified')

    // Session should still have the assistant message
    expect(sessionMessages.length).toBe(1)
    expect(sessionMessages[0].role).toBe('assistant')

    // Undo should restore file AND remove the assistant message
    api.emit('diff:undo', { sessionId: 'int-test' })
    await tick()

    expect(await api.platform.fs.readFile('/src/msg.ts')).toBe('original')
    expect(sessionMessages.length).toBe(0)
  })

  it('redo restores the previously removed assistant message', async () => {
    const { api, registeredMiddleware, sessionMessages } = createMockAPIWithSessions()
    api.platform.fs.addFile('/src/msg2.ts', 'v1')
    activate(api)
    const mw = registeredMiddleware[0]

    // Simulate assistant message
    const assistantMsg: ChatMessage = {
      role: 'assistant',
      content: [{ type: 'text', text: 'Editing msg2.ts' }],
    }
    sessionMessages.push(assistantMsg)

    await simulateWrite(api, mw, '/src/msg2.ts', 'v2')
    expect(sessionMessages.length).toBe(1)

    // Undo — message removed
    api.emit('diff:undo', { sessionId: 'int-test' })
    await tick()
    expect(sessionMessages.length).toBe(0)
    expect(await api.platform.fs.readFile('/src/msg2.ts')).toBe('v1')

    // Redo — message restored
    api.emit('diff:redo', { sessionId: 'int-test' })
    await tick()
    expect(sessionMessages.length).toBe(1)
    expect(sessionMessages[0].role).toBe('assistant')
    expect(await api.platform.fs.readFile('/src/msg2.ts')).toBe('v2')
  })

  it('multiple undos remove correct messages in order', async () => {
    const { api, registeredMiddleware, sessionMessages } = createMockAPIWithSessions()
    api.platform.fs.addFile('/src/multi-msg.ts', 'v0')
    activate(api)
    const mw = registeredMiddleware[0]

    // Simulate two assistant messages, each followed by a write
    sessionMessages.push({
      role: 'assistant',
      content: [{ type: 'text', text: 'Write v1' }],
    })
    await simulateWrite(api, mw, '/src/multi-msg.ts', 'v1')

    sessionMessages.push({
      role: 'assistant',
      content: [{ type: 'text', text: 'Write v2' }],
    })
    await simulateWrite(api, mw, '/src/multi-msg.ts', 'v2')

    expect(sessionMessages.length).toBe(2)

    // Undo v2 — removes second assistant message
    api.emit('diff:undo', { sessionId: 'int-test' })
    await tick()
    expect(sessionMessages.length).toBe(1)
    expect((sessionMessages[0].content as Array<{ text: string }>)[0].text).toBe('Write v1')
    expect(await api.platform.fs.readFile('/src/multi-msg.ts')).toBe('v1')

    // Undo v1 — removes first assistant message
    api.emit('diff:undo', { sessionId: 'int-test' })
    await tick()
    expect(sessionMessages.length).toBe(0)
    expect(await api.platform.fs.readFile('/src/multi-msg.ts')).toBe('v0')
  })

  it('undo works gracefully when session manager is unavailable', async () => {
    // Use standard mock (no session manager wired) — should still undo files
    const { api, registeredMiddleware } = createMockExtensionAPI()
    api.platform.fs.addFile('/src/no-mgr.ts', 'before')
    activate(api)
    const mw = registeredMiddleware[0]

    await simulateWrite(api, mw, '/src/no-mgr.ts', 'after')
    expect(await api.platform.fs.readFile('/src/no-mgr.ts')).toBe('after')

    // Undo should still restore file even if session manager throws
    api.emit('diff:undo', { sessionId: 'int-test' })
    await tick()
    expect(await api.platform.fs.readFile('/src/no-mgr.ts')).toBe('before')
  })

  it('diff records messageIndex from session manager', async () => {
    const { api, registeredMiddleware, emittedEvents, sessionMessages } =
      createMockAPIWithSessions()
    api.platform.fs.addFile('/src/idx.ts', 'a')
    activate(api)
    const mw = registeredMiddleware[0]

    // Add messages to simulate a conversation
    sessionMessages.push({ role: 'user', content: 'do something' })
    sessionMessages.push({
      role: 'assistant',
      content: [{ type: 'text', text: 'editing' }],
    })

    await simulateWrite(api, mw, '/src/idx.ts', 'b')

    const changedEvent = emittedEvents.find((e) => e.event === 'diff:changed')
    expect(changedEvent).toBeDefined()
    const diff = (changedEvent!.data as { diff: { messageIndex: number } }).diff
    // messageIndex should be messages.length - 1 = 1 (the last message)
    expect(diff.messageIndex).toBe(1)
  })
})
