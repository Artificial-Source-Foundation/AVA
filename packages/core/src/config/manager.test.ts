/**
 * Settings Manager Tests
 *
 * Tests for SettingsManager class: load/save lifecycle, get/set,
 * validation, events, and reset.
 */

import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest'
import {
  createSettingsManager,
  getSettingsManager,
  SettingsManager,
  SettingsValidationError,
  setSettingsManager,
} from './manager.js'
import {
  DEFAULT_AGENT_SETTINGS,
  DEFAULT_CONTEXT_SETTINGS,
  DEFAULT_PERMISSION_SETTINGS,
  DEFAULT_PROVIDER_SETTINGS,
  DEFAULT_SETTINGS,
  DEFAULT_UI_SETTINGS,
} from './types.js'

// ============================================================================
// Mock Storage
// ============================================================================

// Mock the storage module to avoid filesystem dependencies
vi.mock('./storage.js', () => ({
  loadSettingsFromFile: vi.fn().mockResolvedValue(null),
  saveSettingsToFile: vi.fn().mockResolvedValue(undefined),
}))

// Import mocked functions for assertions
import { loadSettingsFromFile, saveSettingsToFile } from './storage.js'

const mockLoad = vi.mocked(loadSettingsFromFile)
const mockSave = vi.mocked(saveSettingsToFile)

// ============================================================================
// Tests
// ============================================================================

describe('SettingsManager', () => {
  let manager: SettingsManager

  beforeEach(() => {
    vi.clearAllMocks()
    manager = new SettingsManager()
  })

  afterEach(() => {
    setSettingsManager(null)
  })

  // ==========================================================================
  // Constructor
  // ==========================================================================

  describe('constructor', () => {
    it('starts with default settings', () => {
      const all = manager.getAll()
      expect(all.provider.defaultProvider).toBe(DEFAULT_PROVIDER_SETTINGS.defaultProvider)
      expect(all.agent.maxTurns).toBe(DEFAULT_AGENT_SETTINGS.maxTurns)
      expect(all.context.maxTokens).toBe(DEFAULT_CONTEXT_SETTINGS.maxTokens)
    })

    it('starts not loaded and not dirty', () => {
      expect(manager.isLoaded()).toBe(false)
      expect(manager.isDirty()).toBe(false)
    })
  })

  // ==========================================================================
  // Load / Save
  // ==========================================================================

  describe('load', () => {
    it('loads successfully with no stored settings', async () => {
      mockLoad.mockResolvedValueOnce(null)
      await manager.load()

      expect(manager.isLoaded()).toBe(true)
      expect(manager.isDirty()).toBe(false)
    })

    it('merges stored settings with defaults', async () => {
      mockLoad.mockResolvedValueOnce({
        agent: { ...DEFAULT_AGENT_SETTINGS, maxTurns: 100 },
      })
      await manager.load()

      expect(manager.get('agent').maxTurns).toBe(100)
      // Other categories should still have defaults
      expect(manager.get('provider').defaultProvider).toBe('anthropic')
    })

    it('falls back to defaults on invalid stored settings', async () => {
      mockLoad.mockResolvedValueOnce({
        agent: { maxTurns: -999 },
      })
      await manager.load()

      // Should use defaults since validation failed
      expect(manager.get('agent').maxTurns).toBe(DEFAULT_AGENT_SETTINGS.maxTurns)
    })

    it('emits settings_loaded event', async () => {
      const listener = vi.fn()
      manager.on(listener)

      await manager.load()

      expect(listener).toHaveBeenCalledWith({ type: 'settings_loaded' })
    })
  })

  describe('save', () => {
    it('calls storage save with current settings', async () => {
      await manager.save()

      expect(mockSave).toHaveBeenCalledOnce()
      expect(mockSave).toHaveBeenCalledWith(manager.getAll())
    })

    it('clears dirty flag after save', async () => {
      manager.set('agent', { maxTurns: 100 })
      expect(manager.isDirty()).toBe(true)

      await manager.save()
      expect(manager.isDirty()).toBe(false)
    })

    it('emits settings_saved event', async () => {
      const listener = vi.fn()
      manager.on(listener)

      await manager.save()

      expect(listener).toHaveBeenCalledWith({ type: 'settings_saved' })
    })
  })

  // ==========================================================================
  // Get / Set
  // ==========================================================================

  describe('get', () => {
    it('returns correct defaults for each category', () => {
      expect(manager.get('provider')).toEqual(DEFAULT_PROVIDER_SETTINGS)
      expect(manager.get('agent')).toEqual(DEFAULT_AGENT_SETTINGS)
      expect(manager.get('permissions')).toEqual(DEFAULT_PERMISSION_SETTINGS)
      expect(manager.get('context')).toEqual(DEFAULT_CONTEXT_SETTINGS)
      expect(manager.get('ui')).toEqual(DEFAULT_UI_SETTINGS)
    })
  })

  describe('set', () => {
    it('performs partial update', () => {
      manager.set('agent', { maxTurns: 100 })

      expect(manager.get('agent').maxTurns).toBe(100)
      // Other fields unchanged
      expect(manager.get('agent').maxTimeMinutes).toBe(DEFAULT_AGENT_SETTINGS.maxTimeMinutes)
    })

    it('sets dirty flag', () => {
      manager.set('ui', { theme: 'dark' })
      expect(manager.isDirty()).toBe(true)
    })

    it('emits category_changed event', () => {
      const listener = vi.fn()
      manager.on(listener)

      manager.set('context', { maxTokens: 100000 })

      expect(listener).toHaveBeenCalledWith({
        type: 'category_changed',
        category: 'context',
      })
    })

    it('throws SettingsValidationError for invalid partial value', () => {
      expect(() => {
        manager.set('agent', { maxTurns: -1 })
      }).toThrow(SettingsValidationError)
    })

    it('throws SettingsValidationError for invalid full category after merge', () => {
      // Set maxTurns to boundary max, which should be fine
      manager.set('agent', { maxTurns: 1000 })
      expect(manager.get('agent').maxTurns).toBe(1000)

      // Set to above boundary: should fail
      expect(() => {
        manager.set('agent', { maxTurns: 1001 })
      }).toThrow(SettingsValidationError)
    })

    it('validation error includes category and issues', () => {
      try {
        manager.set('agent', { maxTurns: -1 })
        expect.fail('Should have thrown')
      } catch (err) {
        expect(err).toBeInstanceOf(SettingsValidationError)
        const validationErr = err as SettingsValidationError
        expect(validationErr.category).toBe('agent')
        expect(validationErr.zodError.issues.length).toBeGreaterThan(0)
      }
    })
  })

  // ==========================================================================
  // Type-Safe Getters
  // ==========================================================================

  describe('typed getters', () => {
    it('provides type-safe accessors', () => {
      expect(manager.provider).toEqual(DEFAULT_PROVIDER_SETTINGS)
      expect(manager.agent).toEqual(DEFAULT_AGENT_SETTINGS)
      expect(manager.permissions).toEqual(DEFAULT_PERMISSION_SETTINGS)
      expect(manager.context).toEqual(DEFAULT_CONTEXT_SETTINGS)
      expect(manager.ui).toEqual(DEFAULT_UI_SETTINGS)
    })
  })

  // ==========================================================================
  // Reset
  // ==========================================================================

  describe('reset', () => {
    it('resets a single category to defaults', () => {
      manager.set('agent', { maxTurns: 100 })
      manager.reset('agent')

      expect(manager.get('agent')).toEqual(DEFAULT_AGENT_SETTINGS)
    })

    it('sets dirty flag', () => {
      manager.reset('agent')
      expect(manager.isDirty()).toBe(true)
    })

    it('emits settings_reset event with category', () => {
      const listener = vi.fn()
      manager.on(listener)

      manager.reset('agent')

      expect(listener).toHaveBeenCalledWith({
        type: 'settings_reset',
        category: 'agent',
      })
    })
  })

  describe('resetAll', () => {
    it('resets all categories to defaults', () => {
      manager.set('agent', { maxTurns: 100 })
      manager.set('ui', { theme: 'dark' })
      manager.resetAll()

      expect(manager.getAll()).toEqual(DEFAULT_SETTINGS)
    })

    it('emits settings_reset event without category', () => {
      const listener = vi.fn()
      manager.on(listener)

      manager.resetAll()

      expect(listener).toHaveBeenCalledWith({ type: 'settings_reset' })
    })
  })

  // ==========================================================================
  // Events
  // ==========================================================================

  describe('events', () => {
    it('on() returns unsubscribe function', () => {
      const listener = vi.fn()
      const unsub = manager.on(listener)

      manager.set('ui', { theme: 'dark' })
      expect(listener).toHaveBeenCalledOnce()

      unsub()
      manager.set('ui', { theme: 'light' })
      expect(listener).toHaveBeenCalledOnce() // not called again
    })

    it('multiple listeners receive events', () => {
      const listener1 = vi.fn()
      const listener2 = vi.fn()
      manager.on(listener1)
      manager.on(listener2)

      manager.set('agent', { maxTurns: 99 })

      expect(listener1).toHaveBeenCalledOnce()
      expect(listener2).toHaveBeenCalledOnce()
    })

    it('listener error does not prevent other listeners', () => {
      const errorListener = vi.fn(() => {
        throw new Error('boom')
      })
      const goodListener = vi.fn()
      manager.on(errorListener)
      manager.on(goodListener)

      // Should not throw
      manager.set('agent', { maxTurns: 99 })

      expect(errorListener).toHaveBeenCalledOnce()
      expect(goodListener).toHaveBeenCalledOnce()
    })
  })

  // ==========================================================================
  // Singleton
  // ==========================================================================

  describe('singleton', () => {
    it('getSettingsManager returns same instance', () => {
      const a = getSettingsManager()
      const b = getSettingsManager()
      expect(a).toBe(b)
    })

    it('setSettingsManager replaces instance', () => {
      const custom = new SettingsManager()
      setSettingsManager(custom)
      expect(getSettingsManager()).toBe(custom)
    })

    it('setSettingsManager(null) causes new instance on next get', () => {
      const first = getSettingsManager()
      setSettingsManager(null)
      const second = getSettingsManager()
      expect(first).not.toBe(second)
    })
  })

  // ==========================================================================
  // Factory
  // ==========================================================================

  describe('createSettingsManager', () => {
    it('creates independent instance', () => {
      const a = createSettingsManager()
      const b = createSettingsManager()
      expect(a).not.toBe(b)
    })
  })
})
