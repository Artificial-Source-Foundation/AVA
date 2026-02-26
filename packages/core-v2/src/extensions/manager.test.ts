import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest'
import { installMockPlatform } from '../__test-utils__/mock-platform.js'
import { MessageBus, resetMessageBus } from '../bus/message-bus.js'
import { resetSettingsManager } from '../config/manager.js'
import { resetLogger } from '../logger/logger.js'
import { createSessionManager, type SessionManager } from '../session/manager.js'
import { resetTools } from '../tools/registry.js'
import { resetRegistries } from './api.js'
import { ExtensionManager } from './manager.js'
import type { ExtensionEvent, ExtensionManifest, ExtensionModule } from './types.js'

function makeManager(): { manager: ExtensionManager; bus: MessageBus; sm: SessionManager } {
  const bus = new MessageBus()
  const sm = createSessionManager()
  return { manager: new ExtensionManager(bus, sm), bus, sm }
}

function makeManifest(overrides?: Partial<ExtensionManifest>): ExtensionManifest {
  return {
    name: 'test-ext',
    version: '1.0.0',
    main: 'src/index.ts',
    ...overrides,
  }
}

function makeModule(activate?: ExtensionModule['activate']): ExtensionModule {
  return {
    activate: activate ?? (() => undefined),
  }
}

describe('ExtensionManager', () => {
  let manager: ExtensionManager

  beforeEach(() => {
    resetRegistries()
    resetTools()
    resetSettingsManager()
    installMockPlatform()
    const ctx = makeManager()
    manager = ctx.manager
  })

  afterEach(() => {
    manager.reset()
    resetRegistries()
    resetTools()
    resetSettingsManager()
    resetMessageBus()
    resetLogger()
  })

  // ─── Registration ─────────────────────────────────────────────────────

  describe('register', () => {
    it('registers an extension', () => {
      manager.register(makeManifest(), '/path/to/ext')
      expect(manager.getExtension('test-ext')).toBeDefined()
    })

    it('creates inactive extension', () => {
      manager.register(makeManifest(), '/path')
      expect(manager.isActive('test-ext')).toBe(false)
    })

    it('increments size', () => {
      expect(manager.size).toBe(0)
      manager.register(makeManifest(), '/path')
      expect(manager.size).toBe(1)
    })

    it('returns extension object', () => {
      const ext = manager.register(makeManifest({ name: 'my-ext' }), '/path')
      expect(ext.manifest.name).toBe('my-ext')
      expect(ext.isActive).toBe(false)
    })
  })

  // ─── Activation ───────────────────────────────────────────────────────

  describe('activate', () => {
    it('activates registered extension', async () => {
      manager.register(makeManifest(), '/path')
      await manager.activate('test-ext', makeModule())
      expect(manager.isActive('test-ext')).toBe(true)
    })

    it('throws for unregistered extension', async () => {
      await expect(manager.activate('nonexistent', makeModule())).rejects.toThrow(
        'Extension not found'
      )
    })

    it('calls activate function with API', async () => {
      const activate = vi.fn()
      manager.register(makeManifest(), '/path')
      await manager.activate('test-ext', makeModule(activate))
      expect(activate).toHaveBeenCalledOnce()
      expect(activate.mock.calls[0][0]).toHaveProperty('registerTool')
    })

    it('stores disposable from activate', async () => {
      const dispose = vi.fn()
      manager.register(makeManifest(), '/path')
      await manager.activate(
        'test-ext',
        makeModule(() => ({ dispose }))
      )
      await manager.deactivate('test-ext')
      expect(dispose).toHaveBeenCalledOnce()
    })

    it('skips already active extension', async () => {
      const activate = vi.fn()
      manager.register(makeManifest(), '/path')
      await manager.activate('test-ext', makeModule(activate))
      await manager.activate('test-ext', makeModule(activate))
      expect(activate).toHaveBeenCalledOnce()
    })

    it('emits activated event', async () => {
      const events: ExtensionEvent[] = []
      manager.on((e) => events.push(e))
      manager.register(makeManifest(), '/path')
      await manager.activate('test-ext', makeModule())
      expect(events).toContainEqual({ type: 'activated', name: 'test-ext' })
    })

    it('emits error event on failure', async () => {
      const events: ExtensionEvent[] = []
      manager.on((e) => events.push(e))
      manager.register(makeManifest(), '/path')
      await expect(
        manager.activate(
          'test-ext',
          makeModule(() => {
            throw new Error('activate failed')
          })
        )
      ).rejects.toThrow('activate failed')
      expect(events.some((e) => e.type === 'error')).toBe(true)
    })
  })

  // ─── Deactivation ────────────────────────────────────────────────────

  describe('deactivate', () => {
    it('deactivates active extension', async () => {
      manager.register(makeManifest(), '/path')
      await manager.activate('test-ext', makeModule())
      await manager.deactivate('test-ext')
      expect(manager.isActive('test-ext')).toBe(false)
    })

    it('is safe for inactive extension', async () => {
      manager.register(makeManifest(), '/path')
      await expect(manager.deactivate('test-ext')).resolves.not.toThrow()
    })

    it('is safe for nonexistent extension', async () => {
      await expect(manager.deactivate('nonexistent')).resolves.not.toThrow()
    })

    it('emits deactivated event', async () => {
      const events: ExtensionEvent[] = []
      manager.on((e) => events.push(e))
      manager.register(makeManifest(), '/path')
      await manager.activate('test-ext', makeModule())
      await manager.deactivate('test-ext')
      expect(events).toContainEqual({ type: 'deactivated', name: 'test-ext' })
    })
  })

  // ─── activateAll ──────────────────────────────────────────────────────

  describe('activateAll', () => {
    it('activates all default-enabled extensions', async () => {
      manager.register(makeManifest({ name: 'a', enabledByDefault: true }), '/a')
      manager.register(makeManifest({ name: 'b', enabledByDefault: true }), '/b')

      const modules = new Map<string, ExtensionModule>()
      modules.set('a', makeModule())
      modules.set('b', makeModule())

      await manager.activateAll(modules)
      expect(manager.isActive('a')).toBe(true)
      expect(manager.isActive('b')).toBe(true)
    })

    it('skips extensions with enabledByDefault: false', async () => {
      manager.register(makeManifest({ name: 'enabled' }), '/e')
      manager.register(makeManifest({ name: 'disabled', enabledByDefault: false }), '/d')

      const modules = new Map<string, ExtensionModule>()
      modules.set('enabled', makeModule())
      modules.set('disabled', makeModule())

      await manager.activateAll(modules)
      expect(manager.isActive('enabled')).toBe(true)
      expect(manager.isActive('disabled')).toBe(false)
    })

    it('activates in priority order', async () => {
      const order: string[] = []
      manager.register(makeManifest({ name: 'second', priority: 10 }), '/s')
      manager.register(makeManifest({ name: 'first', priority: 0 }), '/f')

      const modules = new Map<string, ExtensionModule>()
      modules.set(
        'first',
        makeModule(() => {
          order.push('first')
          return undefined
        })
      )
      modules.set(
        'second',
        makeModule(() => {
          order.push('second')
          return undefined
        })
      )

      await manager.activateAll(modules)
      expect(order).toEqual(['first', 'second'])
    })

    it('emits loaded event with count', async () => {
      const events: ExtensionEvent[] = []
      manager.on((e) => events.push(e))
      manager.register(makeManifest({ name: 'a' }), '/a')
      manager.register(makeManifest({ name: 'b' }), '/b')

      const modules = new Map<string, ExtensionModule>()
      modules.set('a', makeModule())
      modules.set('b', makeModule())

      await manager.activateAll(modules)
      expect(events).toContainEqual({ type: 'loaded', count: 2 })
    })
  })

  // ─── Queries ──────────────────────────────────────────────────────────

  describe('queries', () => {
    it('getExtensions returns all', () => {
      manager.register(makeManifest({ name: 'a' }), '/a')
      manager.register(makeManifest({ name: 'b' }), '/b')
      expect(manager.getExtensions()).toHaveLength(2)
    })

    it('getActiveExtensions returns only active', async () => {
      manager.register(makeManifest({ name: 'a' }), '/a')
      manager.register(makeManifest({ name: 'b' }), '/b')
      await manager.activate('a', makeModule())
      expect(manager.getActiveExtensions()).toHaveLength(1)
      expect(manager.getActiveExtensions()[0].manifest.name).toBe('a')
    })
  })

  // ─── dispose ──────────────────────────────────────────────────────────

  describe('dispose', () => {
    it('deactivates all and clears', async () => {
      manager.register(makeManifest({ name: 'a' }), '/a')
      await manager.activate('a', makeModule())
      await manager.dispose()
      expect(manager.size).toBe(0)
    })
  })

  // ─── reset ────────────────────────────────────────────────────────────

  describe('reset', () => {
    it('clears everything', async () => {
      manager.register(makeManifest({ name: 'a' }), '/a')
      await manager.activate('a', makeModule())
      manager.reset()
      expect(manager.size).toBe(0)
    })
  })

  // ─── Events ───────────────────────────────────────────────────────────

  describe('events', () => {
    it('unsubscribes listener', () => {
      const handler = vi.fn()
      const unsub = manager.on(handler)
      manager.register(makeManifest(), '/path')
      unsub()
      // No more events after unsub
    })
  })
})
