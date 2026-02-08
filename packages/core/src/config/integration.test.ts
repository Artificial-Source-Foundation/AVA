/**
 * Settings Integration Tests
 *
 * Tests for functions that bridge settings to agent, context, session,
 * permission, and validator modules.
 */

import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest'
import {
  applySettingsToAgentConfig,
  createAgentConfigFromSettings,
  createContextOptionsFromSettings,
  createSessionConfigFromSettings,
  getEnabledValidators,
  getLLMClientOptions,
  getRequestTimeout,
  initializeSettingsIntegration,
  isPathDenied,
  isValidatorEnabled,
  requiresConfirmation,
  watchAgentSettings,
  watchContextSettings,
} from './integration.js'
import { SettingsManager, setSettingsManager } from './manager.js'
import {
  DEFAULT_AGENT_SETTINGS,
  DEFAULT_CONTEXT_SETTINGS,
  DEFAULT_PROVIDER_SETTINGS,
  DEFAULT_SETTINGS,
} from './types.js'

// Mock storage so SettingsManager doesn't hit filesystem
vi.mock('./storage.js', () => ({
  loadSettingsFromFile: vi.fn().mockResolvedValue(null),
  saveSettingsToFile: vi.fn().mockResolvedValue(undefined),
}))

// ============================================================================
// Tests
// ============================================================================

describe('integration', () => {
  let manager: SettingsManager

  beforeEach(() => {
    vi.clearAllMocks()
    manager = new SettingsManager()
    setSettingsManager(manager)
  })

  afterEach(() => {
    setSettingsManager(null)
  })

  // ==========================================================================
  // Agent Integration
  // ==========================================================================

  describe('createAgentConfigFromSettings', () => {
    it('creates config from explicit settings', () => {
      const config = createAgentConfigFromSettings(DEFAULT_SETTINGS)

      expect(config.maxTurns).toBe(DEFAULT_AGENT_SETTINGS.maxTurns)
      expect(config.maxTimeMinutes).toBe(DEFAULT_AGENT_SETTINGS.maxTimeMinutes)
      expect(config.maxRetries).toBe(DEFAULT_AGENT_SETTINGS.maxRetries)
      expect(config.provider).toBe(DEFAULT_PROVIDER_SETTINGS.defaultProvider)
      expect(config.model).toBe(DEFAULT_PROVIDER_SETTINGS.defaultModel)
    })

    it('creates config from global singleton', () => {
      const config = createAgentConfigFromSettings()

      expect(config.maxTurns).toBe(DEFAULT_AGENT_SETTINGS.maxTurns)
      expect(config.model).toBe(DEFAULT_PROVIDER_SETTINGS.defaultModel)
    })

    it('applies overrides', () => {
      const config = createAgentConfigFromSettings(DEFAULT_SETTINGS, { maxTurns: 999 })

      expect(config.maxTurns).toBe(999)
      expect(config.model).toBe(DEFAULT_PROVIDER_SETTINGS.defaultModel)
    })
  })

  describe('applySettingsToAgentConfig', () => {
    it('overrides agent config fields with settings values', () => {
      const original = {
        maxTurns: 10,
        maxTimeMinutes: 5,
        maxRetries: 1,
        gracePeriodMs: 1000,
        provider: 'openai' as const,
        model: 'gpt-4',
      }

      const result = applySettingsToAgentConfig(original, DEFAULT_SETTINGS)

      expect(result.maxTurns).toBe(DEFAULT_AGENT_SETTINGS.maxTurns)
      expect(result.provider).toBe(DEFAULT_PROVIDER_SETTINGS.defaultProvider)
    })
  })

  describe('watchAgentSettings', () => {
    it('fires callback on agent settings change', () => {
      const onUpdate = vi.fn()
      const cleanup = watchAgentSettings(manager, onUpdate)

      manager.set('agent', { maxTurns: 200 })

      expect(onUpdate).toHaveBeenCalledWith(expect.objectContaining({ maxTurns: 200 }))
      cleanup()
    })

    it('fires callback on provider settings change', () => {
      const onUpdate = vi.fn()
      const cleanup = watchAgentSettings(manager, onUpdate)

      manager.set('provider', { defaultModel: 'claude-opus-4-20250514' })

      expect(onUpdate).toHaveBeenCalledWith(
        expect.objectContaining({ model: 'claude-opus-4-20250514' })
      )
      cleanup()
    })

    it('cleanup stops further notifications', () => {
      const onUpdate = vi.fn()
      const cleanup = watchAgentSettings(manager, onUpdate)
      cleanup()

      manager.set('agent', { maxTurns: 200 })
      expect(onUpdate).not.toHaveBeenCalled()
    })
  })

  // ==========================================================================
  // Context Integration
  // ==========================================================================

  describe('createContextOptionsFromSettings', () => {
    it('converts percent threshold to decimal', () => {
      const result = createContextOptionsFromSettings(DEFAULT_CONTEXT_SETTINGS)

      expect(result.maxTokens).toBe(200000)
      expect(result.compactionThreshold).toBe(0.8) // 80 / 100
    })
  })

  describe('watchContextSettings', () => {
    it('fires on context settings change', () => {
      const context = {} as Record<string, never>
      const cleanup = watchContextSettings(manager, context)

      // Should not throw
      manager.set('context', { maxTokens: 100000 })

      cleanup()
    })
  })

  // ==========================================================================
  // Session Integration
  // ==========================================================================

  describe('createSessionConfigFromSettings', () => {
    it('maps context settings to session config', () => {
      const config = createSessionConfigFromSettings(DEFAULT_CONTEXT_SETTINGS)

      expect(config.maxSessions).toBe(DEFAULT_CONTEXT_SETTINGS.maxSessions)
      expect(config.autoSaveInterval).toBe(DEFAULT_CONTEXT_SETTINGS.autoSaveInterval)
    })

    it('disables autoSave when flag is false', () => {
      const config = createSessionConfigFromSettings({
        ...DEFAULT_CONTEXT_SETTINGS,
        autoSave: false,
      })

      expect(config.autoSaveInterval).toBe(0)
    })
  })

  // ==========================================================================
  // Provider Integration
  // ==========================================================================

  describe('getLLMClientOptions', () => {
    it('returns provider, model, and timeout', () => {
      const options = getLLMClientOptions(DEFAULT_PROVIDER_SETTINGS)

      expect(options.provider).toBe('anthropic')
      expect(options.model).toBe(DEFAULT_PROVIDER_SETTINGS.defaultModel)
      expect(options.timeout).toBe(120000)
    })
  })

  describe('getRequestTimeout', () => {
    it('returns timeout from explicit settings', () => {
      expect(getRequestTimeout(DEFAULT_PROVIDER_SETTINGS)).toBe(120000)
    })

    it('returns timeout from global singleton', () => {
      expect(getRequestTimeout()).toBe(120000)
    })
  })

  // ==========================================================================
  // Validator Integration
  // ==========================================================================

  describe('getEnabledValidators', () => {
    it('returns enabled validators', () => {
      const validators = getEnabledValidators(DEFAULT_AGENT_SETTINGS)
      expect(validators).toEqual(['syntax', 'typescript', 'lint'])
    })

    it('returns empty when validators disabled', () => {
      const validators = getEnabledValidators({
        ...DEFAULT_AGENT_SETTINGS,
        validatorsEnabled: false,
      })
      expect(validators).toEqual([])
    })
  })

  describe('isValidatorEnabled', () => {
    it('returns true for enabled validator', () => {
      expect(isValidatorEnabled('syntax', DEFAULT_AGENT_SETTINGS)).toBe(true)
    })

    it('returns false for disabled validator', () => {
      expect(isValidatorEnabled('selfReview', DEFAULT_AGENT_SETTINGS)).toBe(false)
    })

    it('returns false when validators globally disabled', () => {
      expect(
        isValidatorEnabled('syntax', {
          ...DEFAULT_AGENT_SETTINGS,
          validatorsEnabled: false,
        })
      ).toBe(false)
    })

    it('uses global singleton when no settings provided', () => {
      expect(isValidatorEnabled('syntax')).toBe(true)
    })
  })

  // ==========================================================================
  // Permission Integration
  // ==========================================================================

  describe('isPathDenied', () => {
    it('denies paths matching denied list', () => {
      expect(isPathDenied('~/.ssh/id_rsa', DEFAULT_SETTINGS)).toBe(true)
    })

    it('allows paths not in denied list', () => {
      expect(isPathDenied('/home/user/project/src/main.ts', DEFAULT_SETTINGS)).toBe(false)
    })

    it('normalizes backslashes', () => {
      expect(isPathDenied('~\\.ssh\\id_rsa', DEFAULT_SETTINGS)).toBe(true)
    })

    it('uses global singleton when no settings provided', () => {
      expect(isPathDenied('~/.ssh/id_rsa')).toBe(true)
    })
  })

  describe('requiresConfirmation', () => {
    it('returns true for actions requiring confirmation', () => {
      expect(requiresConfirmation('delete', DEFAULT_SETTINGS)).toBe(true)
      expect(requiresConfirmation('execute', DEFAULT_SETTINGS)).toBe(true)
    })

    it('returns false for actions not requiring confirmation', () => {
      expect(requiresConfirmation('write', DEFAULT_SETTINGS)).toBe(false)
      expect(requiresConfirmation('network', DEFAULT_SETTINGS)).toBe(false)
    })

    it('uses global singleton when no settings provided', () => {
      expect(requiresConfirmation('delete')).toBe(true)
    })
  })

  // ==========================================================================
  // Combined Setup
  // ==========================================================================

  describe('initializeSettingsIntegration', () => {
    it('returns cleanup function', () => {
      const cleanup = initializeSettingsIntegration({ manager })
      expect(typeof cleanup).toBe('function')
      cleanup()
    })

    it('wires agent config watcher', () => {
      const onAgentConfigChange = vi.fn()
      const cleanup = initializeSettingsIntegration({
        manager,
        onAgentConfigChange,
      })

      manager.set('agent', { maxTurns: 200 })
      expect(onAgentConfigChange).toHaveBeenCalled()

      cleanup()
    })

    it('cleanup stops all watchers', () => {
      const onAgentConfigChange = vi.fn()
      const cleanup = initializeSettingsIntegration({
        manager,
        onAgentConfigChange,
      })
      cleanup()

      manager.set('agent', { maxTurns: 200 })
      expect(onAgentConfigChange).not.toHaveBeenCalled()
    })
  })
})
