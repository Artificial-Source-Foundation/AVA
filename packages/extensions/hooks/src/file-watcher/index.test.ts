/**
 * Activation test for file-watcher extension.
 */

import { createMockExtensionAPI } from '@ava/core-v2/__test-utils__/mock-extension-api'
import { afterEach, describe, expect, it, vi } from 'vitest'
import { activate } from './index.js'

function activateWatcher(api: unknown) {
  return activate(api as never)
}

function wait(ms = 20): Promise<void> {
  return new Promise((resolve) => setTimeout(resolve, ms))
}

afterEach(() => {
  vi.useRealTimers()
})

describe('file-watcher extension', () => {
  it('activates and listens for session:opened', () => {
    const { api, eventHandlers } = createMockExtensionAPI()
    const disposable = activateWatcher(api)

    expect(eventHandlers.has('session:opened')).toBe(true)
    disposable.dispose()
  })

  it('cleans up on dispose', () => {
    const { api, eventHandlers } = createMockExtensionAPI()
    const disposable = activateWatcher(api)

    expect(eventHandlers.has('session:opened')).toBe(true)
    disposable.dispose()
    expect(eventHandlers.has('session:opened')).toBe(false)
  })

  it('creates watcher on session:opened event', () => {
    const { api, eventHandlers } = createMockExtensionAPI()
    const disposable = activateWatcher(api)

    // Simulate session:opened event
    const handlers = eventHandlers.get('session:opened')
    expect(handlers).toBeDefined()
    expect(handlers!.size).toBe(1)

    // Trigger the handler — it should not throw
    const handler = [...handlers!][0]!
    expect(() =>
      handler({ sessionId: 'test', workingDirectory: '/tmp/test-project' })
    ).not.toThrow()

    disposable.dispose()
  })

  it('logs activation message', () => {
    const { api } = createMockExtensionAPI()
    const disposable = activateWatcher(api)

    expect(api.log.debug).toHaveBeenCalledWith('File watcher extension activated')
    disposable.dispose()
  })

  it('emits comment:trigger for // ava directives', async () => {
    const { api, emittedEvents } = createMockExtensionAPI()
    const fs = api.platform.fs as unknown as {
      addFile: (path: string, content: string) => void
      addDir: (path: string) => void
    }
    fs.addDir('/project/.git')
    fs.addFile('/project/.git/HEAD', 'ref: refs/heads/main\n')
    fs.addFile('/project/src/a.ts', 'const a = 1\n// ava: refactor a\n')

    const disposable = activateWatcher(api)
    api.emit('session:opened', { sessionId: 's1', workingDirectory: '/project' })
    await wait()

    const evt = emittedEvents.find((e) => e.event === 'comment:trigger')
    expect(evt).toBeDefined()
    expect((evt!.data as { comment: string }).comment).toBe('ava: refactor a')
    disposable.dispose()
  })

  it('emits comment:trigger for # ava directives', async () => {
    const { api, emittedEvents } = createMockExtensionAPI()
    const fs = api.platform.fs as unknown as {
      addFile: (path: string, content: string) => void
      addDir: (path: string) => void
    }
    fs.addDir('/project/.git')
    fs.addFile('/project/.git/HEAD', 'ref: refs/heads/main\n')
    fs.addFile('/project/scripts/tool.py', '# ava: improve parser\nprint(1)\n')

    const disposable = activateWatcher(api)
    api.emit('session:opened', { sessionId: 's1', workingDirectory: '/project' })
    await wait()

    const evt = emittedEvents.find((e) => e.event === 'comment:trigger')
    expect(evt).toBeDefined()
    expect((evt!.data as { comment: string }).comment).toBe('ava: improve parser')
    disposable.dispose()
  })

  it('does not emit for inline pseudo-directives', async () => {
    const { api, emittedEvents } = createMockExtensionAPI()
    const fs = api.platform.fs as unknown as {
      addFile: (path: string, content: string) => void
      addDir: (path: string) => void
    }
    fs.addDir('/project/.git')
    fs.addFile('/project/.git/HEAD', 'ref: refs/heads/main\n')
    fs.addFile('/project/src/noise.ts', 'const s = "// ava: not a directive"\n')

    const disposable = activateWatcher(api)
    api.emit('session:opened', { sessionId: 's1', workingDirectory: '/project' })
    await wait()

    const directiveEvents = emittedEvents.filter((e) => e.event === 'comment:trigger')
    expect(directiveEvents).toHaveLength(0)
    disposable.dispose()
  })

  it('deduplicates unchanged directives across polling cycles', async () => {
    vi.useFakeTimers()
    const { api, emittedEvents } = createMockExtensionAPI()
    const fs = api.platform.fs as unknown as {
      addFile: (path: string, content: string) => void
      addDir: (path: string) => void
    }
    fs.addDir('/project/.git')
    fs.addFile('/project/.git/HEAD', 'ref: refs/heads/main\n')
    fs.addFile('/project/src/a.ts', '// ava: stable\n')

    const disposable = activateWatcher(api)
    api.emit('session:opened', { sessionId: 's1', workingDirectory: '/project' })
    await vi.advanceTimersByTimeAsync(7000)

    const directiveEvents = emittedEvents.filter((e) => e.event === 'comment:trigger')
    expect(directiveEvents).toHaveLength(1)
    disposable.dispose()
  })
})
