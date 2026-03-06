import { describe, expect, it, vi } from 'vitest'
import { activate } from './index.js'

describe('commander extension activate', () => {
  it('registers praxis mode and invoke tools', () => {
    const registerTool = vi.fn(() => ({ dispose: vi.fn() }))
    const registerAgentMode = vi.fn(() => ({ dispose: vi.fn() }))
    const registerSettings = vi.fn(() => ({ dispose: vi.fn() }))
    const registerPromptTemplate = vi.fn(() => ({ dispose: vi.fn() }))
    const registerChatCommand = vi.fn(() => ({ dispose: vi.fn() }))

    const disposable = activate({
      registerTool,
      registerAgentMode,
      registerSettings,
      registerPromptTemplate,
      registerChatCommand,
      getSettings: vi.fn(() => ({})),
      log: {
        debug: vi.fn(),
        warn: vi.fn(),
      },
      context: {
        storage: {
          get: vi.fn().mockResolvedValue(undefined),
          set: vi.fn().mockResolvedValue(undefined),
        },
      },
    } as never)

    expect(registerAgentMode).toHaveBeenCalled()
    expect(registerTool).toHaveBeenCalledTimes(2)
    disposable.dispose()
  })
})
