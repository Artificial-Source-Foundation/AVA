/**
 * Tests for Model Fallback Chains
 *
 * Consolidated tests using representative sampling.
 */

import { describe, it, expect, beforeEach, vi } from 'vitest'
import {
  FallbackChainManager,
  getFallbackManager,
  resetFallbackManager,
  getModelTier,
  getModelsByTier,
  getModelsByProvider,
  describeFallbackResult,
  MODEL_REGISTRY,
  FALLBACK_CHAINS,
} from '../../src/lib/model-fallback.js'

describe('Model Fallback Chains', () => {
  beforeEach(() => {
    resetFallbackManager()
  })

  describe('MODEL_REGISTRY', () => {
    it('contains all major models with valid properties', () => {
      // Check major models exist
      expect(MODEL_REGISTRY['anthropic/claude-opus-4-5']).toBeDefined()
      expect(MODEL_REGISTRY['anthropic/claude-sonnet-4-5']).toBeDefined()
      expect(MODEL_REGISTRY['anthropic/claude-haiku-4']).toBeDefined()
      expect(MODEL_REGISTRY['openai/gpt-4o']).toBeDefined()
      expect(MODEL_REGISTRY['google/gemini-2.0-flash']).toBeDefined()
      expect(MODEL_REGISTRY['deepseek/deepseek-chat']).toBeDefined()

      // Validate all models have required properties
      for (const model of Object.values(MODEL_REGISTRY)) {
        expect(model.costPer1M).toBeGreaterThan(0)
        expect(model.contextWindow).toBeGreaterThan(0)
        expect(model.capabilities.length).toBeGreaterThan(0)
      }
    })

    it('has valid tier assignments', () => {
      expect(MODEL_REGISTRY['anthropic/claude-opus-4-5'].tier).toBe('premium')
      expect(MODEL_REGISTRY['anthropic/claude-sonnet-4-5'].tier).toBe('standard')
      expect(MODEL_REGISTRY['anthropic/claude-haiku-4'].tier).toBe('economy')
    })
  })

  describe('FALLBACK_CHAINS', () => {
    it('has valid chains for major models', () => {
      expect(FALLBACK_CHAINS['anthropic/claude-opus-4-5']).toBeDefined()
      expect(FALLBACK_CHAINS['anthropic/claude-sonnet-4-5']).toBeDefined()

      for (const [primary, chain] of Object.entries(FALLBACK_CHAINS)) {
        // All fallbacks exist in registry and chain doesn't include self
        for (const fallback of chain) {
          expect(MODEL_REGISTRY[fallback]).toBeDefined()
        }
        expect(chain).not.toContain(primary)
      }
    })
  })

  describe('FallbackChainManager', () => {
    describe('Provider Health', () => {
      it('tracks provider health correctly', async () => {
        const manager = new FallbackChainManager({ circuitBreakerThreshold: 3 })

        // Starts healthy
        expect(manager.getProviderStatus('anthropic')).toBe('healthy')
        expect(manager.isProviderAvailable('anthropic')).toBe(true)

        // Record success
        manager.recordSuccess('anthropic')
        expect(manager.getSuccessRate('anthropic')).toBe(1.0)

        // Record failures until circuit breaker opens
        manager.recordFailure('anthropic')
        manager.recordFailure('anthropic')
        manager.recordFailure('anthropic')
        expect(manager.getProviderStatus('anthropic')).toBe('unavailable')
        expect(manager.isProviderAvailable('anthropic')).toBe(false)
      })

      it('resets circuit breaker after timeout', async () => {
        const manager = new FallbackChainManager({
          circuitBreakerThreshold: 2,
          circuitBreakerResetMs: 50,
        })

        manager.recordFailure('anthropic')
        manager.recordFailure('anthropic')
        expect(manager.isProviderAvailable('anthropic')).toBe(false)

        await new Promise((resolve) => setTimeout(resolve, 100))
        expect(manager.isProviderAvailable('anthropic')).toBe(true)
      })

      it('clears failures on success', () => {
        const manager = new FallbackChainManager({ circuitBreakerThreshold: 3 })
        manager.recordFailure('anthropic')
        manager.recordFailure('anthropic')
        manager.recordSuccess('anthropic')
        expect(manager.getProviderStatus('anthropic')).toBe('healthy')
      })
    })

    describe('Fallback Selection', () => {
      it('gets model definition and fallback chain', () => {
        const manager = new FallbackChainManager()

        const model = manager.getModelDefinition('anthropic/claude-sonnet-4-5')
        expect(model?.tier).toBe('standard')

        const chain = manager.getFallbackChain('anthropic/claude-sonnet-4-5')
        expect(chain.length).toBeGreaterThan(0)
        expect(chain).not.toContain('anthropic/claude-sonnet-4-5')

        expect(manager.getModelDefinition('unknown/model')).toBeUndefined()
        expect(manager.getFallbackChain('unknown/model')).toEqual([])
      })

      it('selects fallbacks based on availability', () => {
        const manager = new FallbackChainManager({ circuitBreakerThreshold: 1 })

        const selection = manager.selectFallback('anthropic/claude-sonnet-4-5')
        expect(selection?.model).toBeDefined()
        expect(selection?.position).toBeGreaterThan(0)

        // Make first fallback unavailable
        const chain = manager.getFallbackChain('anthropic/claude-sonnet-4-5')
        const firstFallback = MODEL_REGISTRY[chain[0]]
        manager.recordFailure(firstFallback.provider)

        const selection2 = manager.selectFallback('anthropic/claude-sonnet-4-5')
        expect(selection2?.model.id).not.toBe(chain[0])
        expect(selection2?.skipped.some((s) => s.reason === 'Provider unavailable')).toBe(true)
      })

      it('respects configuration options', () => {
        const manager1 = new FallbackChainManager({ preserveQualityTier: true })
        const sel1 = manager1.selectFallback('anthropic/claude-opus-4-5')
        expect(sel1?.model.tier).toBe('premium')

        const manager2 = new FallbackChainManager({ requiredCapabilities: ['vision'] })
        const sel2 = manager2.selectFallback('anthropic/claude-sonnet-4-5')
        expect(sel2?.model.capabilities).toContain('vision')
      })
    })

    describe('Execution with Fallback', () => {
      it('succeeds on first attempt', async () => {
        const manager = new FallbackChainManager()

        const result = await manager.executeWithFallback(
          async (model) => `result from ${model}`,
          'anthropic/claude-sonnet-4-5'
        )

        expect(result.success).toBe(true)
        expect(result.usedModel).toBe('anthropic/claude-sonnet-4-5')
        expect(result.totalAttempts).toBe(1)
      })

      it('falls back on failure', async () => {
        const manager = new FallbackChainManager()

        const result = await manager.executeWithFallback(
          async (model) => {
            if (model === 'anthropic/claude-sonnet-4-5') throw new Error('Primary failed')
            return `result from ${model}`
          },
          'anthropic/claude-sonnet-4-5'
        )

        expect(result.success).toBe(true)
        expect(result.usedModel).not.toBe('anthropic/claude-sonnet-4-5')
        expect(result.attemptedModels.length).toBeGreaterThan(1)
      })

      it('fails when chain exhausted', async () => {
        const manager = new FallbackChainManager({ maxRetries: 2 })

        const result = await manager.executeWithFallback(
          async () => { throw new Error('Always fails') },
          'anthropic/claude-sonnet-4-5'
        )

        expect(result.success).toBe(false)
        expect(result.error).toBeDefined()
        expect(result.totalAttempts).toBe(2)
      })

      it('calls callbacks correctly', async () => {
        const manager = new FallbackChainManager()
        const attempts: string[] = []
        const fallbacks: { from: string; to: string }[] = []

        await manager.executeWithFallback(
          async (model) => {
            if (model === 'anthropic/claude-sonnet-4-5') throw new Error('Failed')
            return 'success'
          },
          'anthropic/claude-sonnet-4-5',
          {
            onAttempt: (id) => attempts.push(id),
            onFallback: (from, to) => fallbacks.push({ from, to }),
          }
        )

        expect(attempts).toContain('anthropic/claude-sonnet-4-5')
        expect(fallbacks.length).toBeGreaterThan(0)
        expect(fallbacks[0].from).toBe('anthropic/claude-sonnet-4-5')
      })
    })
  })

  describe('Singleton', () => {
    it('manages singleton instance correctly', () => {
      const manager1 = getFallbackManager()
      const manager2 = getFallbackManager()
      expect(manager1).toBe(manager2)

      resetFallbackManager()
      const manager3 = getFallbackManager()
      expect(manager1).not.toBe(manager3)
    })
  })

  describe('Utility Functions', () => {
    it('getModelTier returns correct tiers', () => {
      expect(getModelTier('anthropic/claude-opus-4-5')).toBe('premium')
      expect(getModelTier('anthropic/claude-sonnet-4-5')).toBe('standard')
      expect(getModelTier('anthropic/claude-haiku-4')).toBe('economy')
      expect(getModelTier('unknown/model')).toBeUndefined()
    })

    it('getModelsByTier and getModelsByProvider filter correctly', () => {
      const premium = getModelsByTier('premium')
      const anthropic = getModelsByProvider('anthropic')

      expect(premium.length).toBeGreaterThan(0)
      expect(premium.every((m) => m.tier === 'premium')).toBe(true)

      expect(anthropic.length).toBeGreaterThan(0)
      expect(anthropic.every((m) => m.provider === 'anthropic')).toBe(true)
    })

    it('describeFallbackResult formats results', () => {
      const successDesc = describeFallbackResult({
        success: true,
        result: 'test',
        usedModel: 'anthropic/claude-sonnet-4-5',
        attemptedModels: ['anthropic/claude-sonnet-4-5'],
        totalAttempts: 1,
        totalDelayMs: 0,
        chainExhausted: false,
      })
      expect(successDesc).toContain('Success')

      const failDesc = describeFallbackResult({
        success: false,
        error: new Error('Test error'),
        attemptedModels: ['anthropic/claude-sonnet-4-5', 'openai/gpt-4o'],
        totalAttempts: 2,
        totalDelayMs: 1000,
        chainExhausted: true,
      })
      expect(failDesc).toContain('Failed')
      expect(failDesc).toContain('exhausted')
    })
  })
})
