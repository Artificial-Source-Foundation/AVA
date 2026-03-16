import { createRoot } from 'solid-js'
import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest'
import { useRustAgent } from '../../src/hooks/use-rust-agent'
import { useRustMemory } from '../../src/hooks/use-rust-memory'
import { useRustTools } from '../../src/hooks/use-rust-tools'
import { MockIpc } from './helpers/mock-ipc'

function createHook<T>(factory: () => T): { hook: T; dispose: () => void } {
  let dispose = () => {}
  let hook!: T
  createRoot((disposeRoot) => {
    dispose = disposeRoot
    hook = factory()
  })
  return { hook, dispose }
}

async function flush(): Promise<void> {
  await Promise.resolve()
  await Promise.resolve()
}

describe('rust hooks', () => {
  const ipc = new MockIpc()

  beforeEach(() => {
    ipc.reset()
    ipc.install()
  })

  afterEach(() => {
    vi.restoreAllMocks()
  })

  it('useRustTools loads and executes tools', async () => {
    ipc.setResponse('list_tools', [{ name: 'read_file', description: 'Read file' }])
    ipc.setResponse('execute_tool', { content: 'ok', is_error: false })

    const { hook, dispose } = createHook(() => useRustTools())
    await flush()

    expect(hook.loading()).toBe(false)
    expect(hook.tools()).toHaveLength(1)

    const result = await hook.execute('read_file', { path: '/tmp/a.ts' })
    expect(result.is_error).toBe(false)

    dispose()
  })

  it('useRustAgent streams events from tauri listener', async () => {
    ipc.setHandler('submit_goal', async () => {
      ipc.emit('agent-event', { type: 'token', content: 'Hello ' })
      ipc.emit('agent-event', { type: 'token', content: 'World' })
      ipc.emit('agent-event', {
        type: 'complete',
        session: {
          id: 's1',
          goal: 'test goal',
          messages: [],
          completed: true,
        },
      })
      return { id: 's1', completed: true, messages: [] }
    })

    const { hook, dispose } = createHook(() => useRustAgent())
    await hook.run('test goal')

    expect(hook.currentTokens()).toBe('Hello World')
    expect(hook.events().length).toBeGreaterThanOrEqual(2)
    expect(hook.isRunning()).toBe(false)

    dispose()
  })

  it('useRustMemory runs remember and recent loading', async () => {
    ipc.setResponse('memory_recent', [{ id: 1, key: 'k', value: 'v', createdAt: 'now' }])
    ipc.setResponse('memory_remember', { id: 2, key: 'k2', value: 'v2', createdAt: 'now' })

    const { hook, dispose } = createHook(() => useRustMemory())
    await flush()
    expect(hook.memories()).toHaveLength(1)

    const entry = await hook.remember('k2', 'v2')
    expect(entry.key).toBe('k2')
    expect(hook.error()).toBeNull()

    dispose()
  })
})
