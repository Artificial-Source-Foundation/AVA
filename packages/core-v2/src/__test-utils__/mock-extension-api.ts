/**
 * Mock ExtensionAPI for testing extensions.
 *
 * Creates a fully functional API instance that tracks all registrations
 * without touching global registries, making tests isolated and fast.
 */

import { vi } from 'vitest'
import { MessageBus } from '../bus/message-bus.js'
import type {
  AgentMode,
  ContextStrategy,
  Disposable,
  EventHandler,
  ExtensionAPI,
  ExtensionStorage,
  HookName,
  LLMClientFactory,
  SlashCommand,
  ToolMiddleware,
  Validator,
} from '../extensions/types.js'
import type { Tool } from '../tools/types.js'
import { createMockPlatform } from './mock-platform.js'

export interface MockExtensionAPIResult {
  api: ExtensionAPI
  registeredTools: Tool[]
  registeredCommands: SlashCommand[]
  registeredModes: AgentMode[]
  registeredValidators: Validator[]
  registeredContextStrategies: ContextStrategy[]
  registeredProviders: Array<{ name: string; factory: LLMClientFactory }>
  registeredMiddleware: ToolMiddleware[]
  eventHandlers: Map<string, Set<EventHandler>>
  emittedEvents: Array<{ event: string; data: unknown }>
  dispose(): void
}

function createInMemoryStorage(): ExtensionStorage {
  const store = new Map<string, unknown>()
  return {
    async get<T>(key: string): Promise<T | null> {
      return (store.get(key) as T) ?? null
    },
    async set<T>(key: string, value: T): Promise<void> {
      store.set(key, value)
    },
    async delete(key: string): Promise<void> {
      store.delete(key)
    },
    async keys(): Promise<string[]> {
      return [...store.keys()]
    },
  }
}

export function createMockExtensionAPI(_name = 'test-extension'): MockExtensionAPIResult {
  const bus = new MessageBus()
  const platform = createMockPlatform()
  const storage = createInMemoryStorage()
  const disposables: Disposable[] = []

  const registeredTools: Tool[] = []
  const registeredCommands: SlashCommand[] = []
  const registeredModes: AgentMode[] = []
  const registeredValidators: Validator[] = []
  const registeredContextStrategies: ContextStrategy[] = []
  const registeredProviders: Array<{ name: string; factory: LLMClientFactory }> = []
  const registeredMiddleware: ToolMiddleware[] = []
  const eventHandlers = new Map<string, Set<EventHandler>>()
  const emittedEvents: Array<{ event: string; data: unknown }> = []
  const hookHandlers = new Map<
    HookName,
    Set<(input: unknown, output: unknown) => Promise<unknown> | unknown>
  >()

  const track = (d: Disposable): Disposable => {
    disposables.push(d)
    return d
  }

  const log = {
    debug: vi.fn(),
    info: vi.fn(),
    warn: vi.fn(),
    error: vi.fn(),
    timing: vi.fn(),
    child: vi.fn((subsource: string) => {
      void subsource
      return log
    }),
  }

  const api: ExtensionAPI = {
    registerTool(tool: Tool): Disposable {
      registeredTools.push(tool)
      return track({
        dispose() {
          const idx = registeredTools.indexOf(tool)
          if (idx !== -1) registeredTools.splice(idx, 1)
        },
      })
    },

    registerCommand(command: SlashCommand): Disposable {
      registeredCommands.push(command)
      return track({
        dispose() {
          const idx = registeredCommands.indexOf(command)
          if (idx !== -1) registeredCommands.splice(idx, 1)
        },
      })
    },

    registerAgentMode(mode: AgentMode): Disposable {
      registeredModes.push(mode)
      return track({
        dispose() {
          const idx = registeredModes.indexOf(mode)
          if (idx !== -1) registeredModes.splice(idx, 1)
        },
      })
    },

    registerValidator(validator: Validator): Disposable {
      registeredValidators.push(validator)
      return track({
        dispose() {
          const idx = registeredValidators.indexOf(validator)
          if (idx !== -1) registeredValidators.splice(idx, 1)
        },
      })
    },

    registerContextStrategy(strategy: ContextStrategy): Disposable {
      registeredContextStrategies.push(strategy)
      return track({
        dispose() {
          const idx = registeredContextStrategies.indexOf(strategy)
          if (idx !== -1) registeredContextStrategies.splice(idx, 1)
        },
      })
    },

    registerProvider(providerName: string, factory: LLMClientFactory): Disposable {
      const entry = { name: providerName, factory }
      registeredProviders.push(entry)
      return track({
        dispose() {
          const idx = registeredProviders.indexOf(entry)
          if (idx !== -1) registeredProviders.splice(idx, 1)
        },
      })
    },

    addToolMiddleware(middleware: ToolMiddleware): Disposable {
      registeredMiddleware.push(middleware)
      return track({
        dispose() {
          const idx = registeredMiddleware.indexOf(middleware)
          if (idx !== -1) registeredMiddleware.splice(idx, 1)
        },
      })
    },

    registerHook(name, handler): Disposable {
      let handlers = hookHandlers.get(name)
      if (!handlers) {
        handlers = new Set()
        hookHandlers.set(name, handlers)
      }
      handlers.add(handler as (input: unknown, output: unknown) => Promise<unknown> | unknown)

      return track({
        dispose() {
          handlers!.delete(
            handler as (input: unknown, output: unknown) => Promise<unknown> | unknown
          )
          if (handlers!.size === 0) hookHandlers.delete(name)
        },
      })
    },

    async callHook(name, input, output) {
      const handlers = hookHandlers.get(name)
      if (!handlers || handlers.size === 0) {
        return { output, handlerCount: 0 }
      }

      let currentOutput: unknown = output
      let handlerCount = 0

      for (const handler of handlers) {
        // eslint-disable-next-line no-await-in-loop
        const nextOutput = await handler(input, currentOutput)
        currentOutput = nextOutput
        handlerCount += 1
      }

      return { output: currentOutput as never, handlerCount }
    },

    on(event: string, handler: EventHandler): Disposable {
      let handlers = eventHandlers.get(event)
      if (!handlers) {
        handlers = new Set()
        eventHandlers.set(event, handlers)
      }
      handlers.add(handler)
      return track({
        dispose() {
          handlers!.delete(handler)
          if (handlers!.size === 0) eventHandlers.delete(event)
        },
      })
    },

    emit(event: string, data: unknown): void {
      emittedEvents.push({ event, data })
      const handlers = eventHandlers.get(event)
      if (handlers) {
        for (const handler of handlers) {
          handler(data)
        }
      }
    },

    getSettings<T>(_namespace: string): T {
      return {} as T
    },

    onSettingsChanged(_namespace: string, _cb: (settings: unknown) => void): Disposable {
      return track({ dispose() {} })
    },

    bus,
    log,
    platform,
    storage,

    getSessionManager() {
      return {
        createSession: vi.fn(),
        getSession: vi.fn(),
        listSessions: vi.fn(),
        deleteSession: vi.fn(),
        updateSession: vi.fn(),
        getCurrentSession: vi.fn(),
        setCurrentSession: vi.fn(),
      } as never
    },
  }

  return {
    api,
    registeredTools,
    registeredCommands,
    registeredModes,
    registeredValidators,
    registeredContextStrategies,
    registeredProviders,
    registeredMiddleware,
    eventHandlers,
    emittedEvents,
    dispose() {
      for (const d of disposables.reverse()) {
        d.dispose()
      }
      disposables.length = 0
      bus.clear()
    },
  }
}
