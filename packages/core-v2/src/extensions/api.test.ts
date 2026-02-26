import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest'
import * as z from 'zod'
import { installMockPlatform } from '../__test-utils__/mock-platform.js'
import { MessageBus, resetMessageBus } from '../bus/message-bus.js'
import { getSettingsManager, resetSettingsManager } from '../config/manager.js'
import { hasProvider, resetProviders } from '../llm/client.js'
import type { LLMClient } from '../llm/types.js'
import { resetLogger } from '../logger/logger.js'
import { createSessionManager } from '../session/manager.js'
import { defineTool } from '../tools/define.js'
import { getTool, resetTools } from '../tools/registry.js'
import {
  createExtensionAPI,
  emitEvent,
  getAgentModes,
  getCommands,
  getContextStrategies,
  getToolMiddlewares,
  getValidators,
  resetRegistries,
} from './api.js'
import type { ExtensionAPI } from './types.js'

function makeAPI(name = 'test-ext'): ExtensionAPI {
  const bus = new MessageBus()
  const sm = createSessionManager()
  return createExtensionAPI(name, bus, sm)
}

describe('ExtensionAPI', () => {
  let api: ExtensionAPI

  beforeEach(() => {
    resetRegistries()
    resetTools()
    resetProviders()
    resetSettingsManager()
    installMockPlatform()
    api = makeAPI()
  })

  afterEach(() => {
    resetRegistries()
    resetTools()
    resetProviders()
    resetSettingsManager()
    resetMessageBus()
    resetLogger()
  })

  // ─── registerTool ─────────────────────────────────────────────────────

  describe('registerTool', () => {
    it('registers a tool in the global registry', () => {
      const tool = defineTool({
        name: 'ext_tool',
        description: 'Extension tool',
        schema: z.object({}),
        async execute() {
          return { success: true, output: 'done' }
        },
      })

      api.registerTool(tool)
      expect(getTool('ext_tool')).toBe(tool)
    })

    it('returns disposable that unregisters', () => {
      const tool = defineTool({
        name: 'ext_tool',
        description: 'Extension tool',
        schema: z.object({}),
        async execute() {
          return { success: true, output: '' }
        },
      })

      const disposable = api.registerTool(tool)
      expect(getTool('ext_tool')).toBeDefined()
      disposable.dispose()
      expect(getTool('ext_tool')).toBeUndefined()
    })
  })

  // ─── registerCommand ──────────────────────────────────────────────────

  describe('registerCommand', () => {
    it('registers a slash command', () => {
      api.registerCommand({
        name: 'test',
        description: 'Test command',
        async execute() {
          return 'result'
        },
      })
      expect(getCommands().has('test')).toBe(true)
    })

    it('returns disposable that unregisters', () => {
      const d = api.registerCommand({
        name: 'test',
        description: 'Test command',
        async execute() {
          return 'result'
        },
      })
      d.dispose()
      expect(getCommands().has('test')).toBe(false)
    })
  })

  // ─── registerAgentMode ────────────────────────────────────────────────

  describe('registerAgentMode', () => {
    it('registers an agent mode', () => {
      api.registerAgentMode({ name: 'plan', description: 'Plan mode' })
      expect(getAgentModes().has('plan')).toBe(true)
    })

    it('returns disposable that unregisters', () => {
      const d = api.registerAgentMode({ name: 'plan', description: 'Plan mode' })
      d.dispose()
      expect(getAgentModes().has('plan')).toBe(false)
    })
  })

  // ─── registerValidator ────────────────────────────────────────────────

  describe('registerValidator', () => {
    it('registers a validator', () => {
      api.registerValidator({
        name: 'test-validator',
        description: 'Test',
        async validate() {
          return { passed: true, errors: [], warnings: [] }
        },
      })
      expect(getValidators().has('test-validator')).toBe(true)
    })

    it('returns disposable', () => {
      const d = api.registerValidator({
        name: 'test-validator',
        description: 'Test',
        async validate() {
          return { passed: true, errors: [], warnings: [] }
        },
      })
      d.dispose()
      expect(getValidators().has('test-validator')).toBe(false)
    })
  })

  // ─── registerContextStrategy ──────────────────────────────────────────

  describe('registerContextStrategy', () => {
    it('registers a context strategy', () => {
      api.registerContextStrategy({
        name: 'test-strategy',
        description: 'Test',
        compact(msgs) {
          return msgs
        },
      })
      expect(getContextStrategies().has('test-strategy')).toBe(true)
    })

    it('returns disposable', () => {
      const d = api.registerContextStrategy({
        name: 'test-strategy',
        description: 'Test',
        compact(msgs) {
          return msgs
        },
      })
      d.dispose()
      expect(getContextStrategies().has('test-strategy')).toBe(false)
    })
  })

  // ─── registerProvider ─────────────────────────────────────────────────

  describe('registerProvider', () => {
    it('registers an LLM provider', () => {
      api.registerProvider('test-llm', () => ({}) as unknown as LLMClient)
      expect(hasProvider('test-llm')).toBe(true)
    })

    it('returns disposable that unregisters', () => {
      const d = api.registerProvider('test-llm', () => ({}) as unknown as LLMClient)
      expect(hasProvider('test-llm')).toBe(true)
      d.dispose()
      expect(hasProvider('test-llm')).toBe(false)
    })
  })

  // ─── addToolMiddleware ────────────────────────────────────────────────

  describe('addToolMiddleware', () => {
    it('adds middleware', () => {
      api.addToolMiddleware({ name: 'test', priority: 0 })
      expect(getToolMiddlewares()).toHaveLength(1)
    })

    it('sorts by priority', () => {
      api.addToolMiddleware({ name: 'second', priority: 10 })
      api.addToolMiddleware({ name: 'first', priority: 0 })
      const mws = getToolMiddlewares()
      expect(mws[0].name).toBe('first')
      expect(mws[1].name).toBe('second')
    })

    it('returns disposable', () => {
      const d = api.addToolMiddleware({ name: 'test', priority: 0 })
      d.dispose()
      expect(getToolMiddlewares()).toHaveLength(0)
    })
  })

  // ─── Events ───────────────────────────────────────────────────────────

  describe('events', () => {
    it('subscribes to events', () => {
      const handler = vi.fn()
      api.on('custom:event', handler)
      emitEvent('custom:event', { data: 'test' })
      expect(handler).toHaveBeenCalledWith({ data: 'test' })
    })

    it('emits events', () => {
      const handler = vi.fn()
      api.on('custom:event', handler)
      api.emit('custom:event', { data: 'test' })
      expect(handler).toHaveBeenCalledOnce()
    })

    it('unsubscribes on dispose', () => {
      const handler = vi.fn()
      const d = api.on('custom:event', handler)
      d.dispose()
      emitEvent('custom:event', { data: 'test' })
      expect(handler).not.toHaveBeenCalled()
    })
  })

  // ─── Settings ─────────────────────────────────────────────────────────

  describe('settings', () => {
    it('reads settings', () => {
      const provider = api.getSettings<{ defaultProvider: string }>('provider')
      expect(provider.defaultProvider).toBe('anthropic')
    })

    it('observes settings changes', () => {
      const handler = vi.fn()
      api.onSettingsChanged('provider', handler)
      // Trigger via the singleton manager
      getSettingsManager().set('provider', { defaultModel: 'gpt-4' })
      expect(handler).toHaveBeenCalled()
    })
  })

  // ─── Storage ──────────────────────────────────────────────────────────

  describe('storage', () => {
    it('stores and retrieves values', async () => {
      await api.storage.set('key', { value: 42 })
      const result = await api.storage.get<{ value: number }>('key')
      expect(result).toEqual({ value: 42 })
    })

    it('returns null for missing keys', async () => {
      expect(await api.storage.get('missing')).toBeNull()
    })

    it('deletes keys', async () => {
      await api.storage.set('key', 'value')
      await api.storage.delete('key')
      expect(await api.storage.get('key')).toBeNull()
    })

    it('lists keys', async () => {
      await api.storage.set('a', 1)
      await api.storage.set('b', 2)
      const keys = await api.storage.keys()
      expect(keys).toContain('a')
      expect(keys).toContain('b')
    })
  })

  // ─── Infrastructure access ────────────────────────────────────────────

  describe('infrastructure', () => {
    it('exposes bus', () => {
      expect(api.bus).toBeInstanceOf(MessageBus)
    })

    it('exposes log', () => {
      expect(api.log).toBeDefined()
      expect(typeof api.log.info).toBe('function')
    })

    it('exposes platform', () => {
      expect(api.platform).toBeDefined()
      expect(api.platform.fs).toBeDefined()
    })

    it('exposes session manager', () => {
      const sm = api.getSessionManager()
      expect(sm).toBeDefined()
      expect(typeof sm.create).toBe('function')
    })
  })
})

// ─── Global Registries ──────────────────────────────────────────────────────

describe('resetRegistries', () => {
  beforeEach(() => {
    installMockPlatform()
  })

  afterEach(() => {
    resetRegistries()
    resetLogger()
  })

  it('clears all registries', () => {
    const api = makeAPI()
    api.registerCommand({
      name: 'cmd',
      description: 'Test',
      async execute() {
        return ''
      },
    })
    api.registerAgentMode({ name: 'mode', description: 'Test' })
    api.addToolMiddleware({ name: 'mw', priority: 0 })

    resetRegistries()
    expect(getCommands().size).toBe(0)
    expect(getAgentModes().size).toBe(0)
    expect(getToolMiddlewares()).toHaveLength(0)
  })
})
