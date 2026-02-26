import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest'
import { resetLogger } from '../logger/logger.js'
import {
  getSettingsManager,
  resetSettingsManager,
  SettingsManager,
  setSettingsManager,
} from './manager.js'
import type { SettingsEvent } from './types.js'

describe('SettingsManager', () => {
  let manager: SettingsManager

  beforeEach(() => {
    resetSettingsManager()
    manager = new SettingsManager()
  })

  afterEach(() => {
    resetSettingsManager()
    resetLogger()
  })

  // ─── Built-in Categories ──────────────────────────────────────────────

  describe('built-in categories', () => {
    it('registers provider category on construction', () => {
      expect(manager.getRegisteredCategories()).toContain('provider')
    })

    it('registers agent category on construction', () => {
      expect(manager.getRegisteredCategories()).toContain('agent')
    })

    it('returns provider defaults', () => {
      const provider = manager.get<{ defaultProvider: string }>('provider')
      expect(provider.defaultProvider).toBe('anthropic')
    })

    it('returns agent defaults', () => {
      const agent = manager.get<{ maxTurns: number }>('agent')
      expect(agent.maxTurns).toBe(50)
    })
  })

  // ─── registerCategory ─────────────────────────────────────────────────

  describe('registerCategory', () => {
    it('registers a custom category', () => {
      manager.registerCategory('custom', { foo: 'bar' })
      expect(manager.getRegisteredCategories()).toContain('custom')
    })

    it('returns custom category values', () => {
      manager.registerCategory('custom', { foo: 'bar' })
      expect(manager.get('custom')).toEqual({ foo: 'bar' })
    })

    it('does not overwrite existing category', () => {
      manager.registerCategory('custom', { a: 1 })
      manager.registerCategory('custom', { a: 2 })
      expect(manager.get<{ a: number }>('custom').a).toBe(1)
    })

    it('emits category_registered event', () => {
      const events: SettingsEvent[] = []
      manager.on((e) => events.push(e))
      manager.registerCategory('ext', { x: 1 })
      expect(events).toContainEqual({ type: 'category_registered', category: 'ext' })
    })
  })

  // ─── get / set ────────────────────────────────────────────────────────

  describe('get/set', () => {
    it('throws for unknown category on get', () => {
      expect(() => manager.get('nonexistent')).toThrow('Unknown settings category')
    })

    it('throws for unknown category on set', () => {
      expect(() => manager.set('nonexistent', {})).toThrow('Unknown settings category')
    })

    it('merges values into category', () => {
      manager.set('provider', { defaultModel: 'gpt-4' })
      const result = manager.get<{ defaultProvider: string; defaultModel: string }>('provider')
      expect(result.defaultModel).toBe('gpt-4')
      expect(result.defaultProvider).toBe('anthropic') // unchanged
    })

    it('marks as dirty after set', () => {
      expect(manager.isDirty()).toBe(false)
      manager.set('provider', { defaultModel: 'gpt-4' })
      expect(manager.isDirty()).toBe(true)
    })

    it('emits category_changed event', () => {
      const events: SettingsEvent[] = []
      manager.on((e) => events.push(e))
      manager.set('provider', { defaultModel: 'gpt-4' })
      expect(events).toContainEqual({ type: 'category_changed', category: 'provider' })
    })
  })

  // ─── getAll ───────────────────────────────────────────────────────────

  describe('getAll', () => {
    it('returns all categories', () => {
      const all = manager.getAll()
      expect(all).toHaveProperty('provider')
      expect(all).toHaveProperty('agent')
    })
  })

  // ─── reset ────────────────────────────────────────────────────────────

  describe('reset', () => {
    it('resets single category to defaults', () => {
      manager.set('provider', { defaultModel: 'gpt-4' })
      manager.reset('provider')
      const provider = manager.get<{ defaultModel: string }>('provider')
      expect(provider.defaultModel).toBe('claude-sonnet-4-20250514')
    })

    it('throws for unknown category', () => {
      expect(() => manager.reset('nonexistent')).toThrow('Unknown settings category')
    })

    it('emits settings_reset event with category', () => {
      const events: SettingsEvent[] = []
      manager.on((e) => events.push(e))
      manager.reset('provider')
      expect(events).toContainEqual({ type: 'settings_reset', category: 'provider' })
    })
  })

  // ─── resetAll ─────────────────────────────────────────────────────────

  describe('resetAll', () => {
    it('resets all categories to defaults', () => {
      manager.set('provider', { defaultModel: 'gpt-4' })
      manager.set('agent', { maxTurns: 100 })
      manager.resetAll()
      expect(manager.get<{ defaultModel: string }>('provider').defaultModel).toBe(
        'claude-sonnet-4-20250514'
      )
      expect(manager.get<{ maxTurns: number }>('agent').maxTurns).toBe(50)
    })

    it('emits settings_reset without category', () => {
      const events: SettingsEvent[] = []
      manager.on((e) => events.push(e))
      manager.resetAll()
      expect(events).toContainEqual({ type: 'settings_reset' })
    })
  })

  // ─── markClean ────────────────────────────────────────────────────────

  describe('markClean', () => {
    it('clears dirty flag', () => {
      manager.set('provider', { defaultModel: 'gpt-4' })
      expect(manager.isDirty()).toBe(true)
      manager.markClean()
      expect(manager.isDirty()).toBe(false)
    })
  })

  // ─── Events ───────────────────────────────────────────────────────────

  describe('events', () => {
    it('unsubscribes listener', () => {
      const handler = vi.fn()
      const unsub = manager.on(handler)
      manager.set('provider', { defaultModel: 'gpt-4' })
      expect(handler).toHaveBeenCalledOnce()
      unsub()
      manager.set('provider', { defaultModel: 'gpt-3' })
      expect(handler).toHaveBeenCalledOnce()
    })
  })
})

// ─── Singleton ────────────────────────────────────────────────────────────

describe('SettingsManager singleton', () => {
  afterEach(() => {
    resetSettingsManager()
    resetLogger()
  })

  it('returns same instance', () => {
    const a = getSettingsManager()
    const b = getSettingsManager()
    expect(a).toBe(b)
  })

  it('allows replacement', () => {
    const custom = new SettingsManager()
    setSettingsManager(custom)
    expect(getSettingsManager()).toBe(custom)
  })

  it('resets to new instance', () => {
    const first = getSettingsManager()
    resetSettingsManager()
    const second = getSettingsManager()
    expect(second).not.toBe(first)
  })
})
