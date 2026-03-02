/**
 * ExtensionAPI implementation.
 *
 * Each extension gets its own API instance scoped to its namespace.
 * All registration methods return Disposables for clean teardown.
 */

import type { MessageBus } from '../bus/message-bus.js'
import { getSettingsManager } from '../config/manager.js'
import { registerProvider, unregisterProvider } from '../llm/client.js'
import { createLogger } from '../logger/logger.js'
import { getPlatform } from '../platform.js'
import type { SessionManager } from '../session/manager.js'
import { registerTool, unregisterTool } from '../tools/registry.js'
import type { Tool } from '../tools/types.js'
import type {
  AgentMode,
  ContextStrategy,
  Disposable,
  EventHandler,
  ExtensionAPI,
  ExtensionStorage,
  LLMClientFactory,
  SlashCommand,
  ToolMiddleware,
  Validator,
} from './types.js'

// ─── Global Registries ───────────────────────────────────────────────────────

const commands = new Map<string, SlashCommand>()
const agentModes = new Map<string, AgentMode>()
const validators = new Map<string, Validator>()
const contextStrategies = new Map<string, ContextStrategy>()
const toolMiddlewares: ToolMiddleware[] = []
const eventHandlers = new Map<string, Set<EventHandler>>()

export function getCommands(): ReadonlyMap<string, SlashCommand> {
  return commands
}

export function getAgentModes(): ReadonlyMap<string, AgentMode> {
  return agentModes
}

export function getValidators(): ReadonlyMap<string, Validator> {
  return validators
}

export function getContextStrategies(): ReadonlyMap<string, ContextStrategy> {
  return contextStrategies
}

export function getToolMiddlewares(): readonly ToolMiddleware[] {
  return [...toolMiddlewares].sort((a, b) => a.priority - b.priority)
}

export function emitEvent(event: string, data: unknown): void {
  const handlers = eventHandlers.get(event)
  if (handlers) {
    for (const handler of handlers) {
      handler(data)
    }
  }
}

/** Subscribe to events from outside the extension API (for CLI/app integration). */
export function onEvent(event: string, handler: EventHandler): Disposable {
  let handlers = eventHandlers.get(event)
  if (!handlers) {
    handlers = new Set()
    eventHandlers.set(event, handlers)
  }
  handlers.add(handler)
  return {
    dispose() {
      handlers!.delete(handler)
      if (handlers!.size === 0) eventHandlers.delete(event)
    },
  }
}

/** Add middleware directly to the global registry (for testing / core use). */
export function addToolMiddleware(middleware: ToolMiddleware): Disposable {
  toolMiddlewares.push(middleware)
  return {
    dispose() {
      const idx = toolMiddlewares.indexOf(middleware)
      if (idx !== -1) toolMiddlewares.splice(idx, 1)
    },
  }
}

export function resetRegistries(): void {
  commands.clear()
  agentModes.clear()
  validators.clear()
  contextStrategies.clear()
  toolMiddlewares.length = 0
  eventHandlers.clear()
}

// ─── API Factory ─────────────────────────────────────────────────────────────

export function createExtensionAPI(
  extensionName: string,
  bus: MessageBus,
  sessionManager: SessionManager
): ExtensionAPI {
  const log = createLogger(`ext:${extensionName}`)
  const disposables: Disposable[] = []

  const track = (d: Disposable): Disposable => {
    disposables.push(d)
    return d
  }

  const storage: ExtensionStorage = createInMemoryStorage()

  const api: ExtensionAPI = {
    registerTool(tool: Tool): Disposable {
      registerTool(tool)
      log.debug(`Tool registered: ${tool.definition.name}`)
      return track({
        dispose() {
          unregisterTool(tool.definition.name)
        },
      })
    },

    registerCommand(command: SlashCommand): Disposable {
      commands.set(command.name, command)
      log.debug(`Command registered: /${command.name}`)
      return track({
        dispose() {
          commands.delete(command.name)
        },
      })
    },

    registerAgentMode(mode: AgentMode): Disposable {
      agentModes.set(mode.name, mode)
      log.debug(`Agent mode registered: ${mode.name}`)
      return track({
        dispose() {
          agentModes.delete(mode.name)
        },
      })
    },

    registerValidator(validator: Validator): Disposable {
      validators.set(validator.name, validator)
      log.debug(`Validator registered: ${validator.name}`)
      return track({
        dispose() {
          validators.delete(validator.name)
        },
      })
    },

    registerContextStrategy(strategy: ContextStrategy): Disposable {
      contextStrategies.set(strategy.name, strategy)
      log.debug(`Context strategy registered: ${strategy.name}`)
      return track({
        dispose() {
          contextStrategies.delete(strategy.name)
        },
      })
    },

    registerProvider(name: string, factory: LLMClientFactory): Disposable {
      registerProvider(name, factory)
      log.debug(`LLM provider registered: ${name}`)
      return track({
        dispose() {
          unregisterProvider(name)
        },
      })
    },

    addToolMiddleware(middleware: ToolMiddleware): Disposable {
      toolMiddlewares.push(middleware)
      log.debug(`Tool middleware added: ${middleware.name} (priority ${middleware.priority})`)
      return track({
        dispose() {
          const idx = toolMiddlewares.indexOf(middleware)
          if (idx !== -1) toolMiddlewares.splice(idx, 1)
        },
      })
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
      emitEvent(event, data)
    },

    getSettings<T>(namespace: string): T {
      return getSettingsManager().get<T>(namespace)
    },

    onSettingsChanged(namespace: string, cb: (settings: unknown) => void): Disposable {
      const unsub = getSettingsManager().on((event) => {
        if (event.type === 'category_changed' && event.category === namespace) {
          cb(getSettingsManager().get(namespace))
        }
      })
      return track({ dispose: unsub })
    },

    bus,
    log,
    platform: getPlatform(),
    storage,

    getSessionManager(): SessionManager {
      return sessionManager
    },
  }

  return api
}

// ─── In-Memory Storage ───────────────────────────────────────────────────────

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
