/**
 * Tests for Model Fallback Chains
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
    it('should contain all major models', () => {
      expect(MODEL_REGISTRY['anthropic/claude-opus-4-5']).toBeDefined()
      expect(MODEL_REGISTRY['anthropic/claude-sonnet-4']).toBeDefined()
      expect(MODEL_REGISTRY['anthropic/claude-haiku-4']).toBeDefined()
      expect(MODEL_REGISTRY['openai/gpt-4o']).toBeDefined()
      expect(MODEL_REGISTRY['openai/gpt-4o-mini']).toBeDefined()
      expect(MODEL_REGISTRY['google/gemini-2.0-pro']).toBeDefined()
      expect(MODEL_REGISTRY['google/gemini-2.0-flash']).toBeDefined()
      expect(MODEL_REGISTRY['deepseek/deepseek-chat']).toBeDefined()
    })

    it('should have valid tier assignments', () => {
      expect(MODEL_REGISTRY['anthropic/claude-opus-4-5'].tier).toBe('premium')
      expect(MODEL_REGISTRY['anthropic/claude-sonnet-4'].tier).toBe('standard')
      expect(MODEL_REGISTRY['anthropic/claude-haiku-4'].tier).toBe('economy')
    })

    it('should have valid cost values', () => {
      for (const model of Object.values(MODEL_REGISTRY)) {
        expect(model.costPer1M).toBeGreaterThan(0)
      }
    })

    it('should have valid context windows', () => {
      for (const model of Object.values(MODEL_REGISTRY)) {
        expect(model.contextWindow).toBeGreaterThan(0)
      }
    })

    it('should have capabilities arrays', () => {
      for (const model of Object.values(MODEL_REGISTRY)) {
        expect(Array.isArray(model.capabilities)).toBe(true)
        expect(model.capabilities.length).toBeGreaterThan(0)
      }
    })
  })

  describe('FALLBACK_CHAINS', () => {
    it('should have chains for all major models', () => {
      expect(FALLBACK_CHAINS['anthropic/claude-opus-4-5']).toBeDefined()
      expect(FALLBACK_CHAINS['anthropic/claude-sonnet-4']).toBeDefined()
      expect(FALLBACK_CHAINS['anthropic/claude-haiku-4']).toBeDefined()
    })

    it('should have valid fallback models', () => {
      for (const [_primary, chain] of Object.entries(FALLBACK_CHAINS)) {
        for (const fallback of chain) {
          expect(MODEL_REGISTRY[fallback]).toBeDefined()
        }
      }
    })

    it('should not include self in fallback chain', () => {
      for (const [primary, chain] of Object.entries(FALLBACK_CHAINS)) {
        expect(chain).not.toContain(primary)
      }
    })
  })

  describe('FallbackChainManager', () => {
    describe('Provider Health', () => {
      it('should start with healthy status', () => {
        const manager = new FallbackChainManager()
        const status = manager.getProviderStatus('anthropic')

        expect(status).toBe('healthy')
      })

      it('should record success', () => {
        const manager = new FallbackChainManager()
        manager.recordSuccess('anthropic')

        expect(manager.isProviderAvailable('anthropic')).toBe(true)
        expect(manager.getSuccessRate('anthropic')).toBe(1.0)
      })

      it('should record failure', () => {
        const manager = new FallbackChainManager({ circuitBreakerThreshold: 5 })
        manager.recordFailure('anthropic')

        // With threshold 5, floor(5/2) = 2, so 1 failure stays healthy
        expect(manager.getProviderStatus('anthropic')).toBe('healthy')
        expect(manager.getSuccessRate('anthropic')).toBe(0)
      })

      it('should degrade status after multiple failures', () => {
        const manager = new FallbackChainManager({ circuitBreakerThreshold: 5 })

        manager.recordFailure('anthropic')
        expect(manager.getProviderStatus('anthropic')).toBe('healthy') // 1 failure

        manager.recordFailure('anthropic')
        expect(manager.getProviderStatus('anthropic')).toBe('degraded') // 2 failures = floor(5/2)
      })

      it('should open circuit breaker after threshold failures', () => {
        const manager = new FallbackChainManager({ circuitBreakerThreshold: 3 })

        manager.recordFailure('anthropic')
        manager.recordFailure('anthropic')
        manager.recordFailure('anthropic')

        expect(manager.getProviderStatus('anthropic')).toBe('unavailable')
        expect(manager.isProviderAvailable('anthropic')).toBe(false)
      })

      it('should reset circuit breaker after timeout', async () => {
        const manager = new FallbackChainManager({
          circuitBreakerThreshold: 2,
          circuitBreakerResetMs: 50,
        })

        manager.recordFailure('anthropic')
        manager.recordFailure('anthropic')
        expect(manager.isProviderAvailable('anthropic')).toBe(false)

        // Wait for reset
        await new Promise((resolve) => setTimeout(resolve, 100))

        expect(manager.isProviderAvailable('anthropic')).toBe(true)
      })

      it('should clear failures on success', () => {
        const manager = new FallbackChainManager({ circuitBreakerThreshold: 3 })

        manager.recordFailure('anthropic')
        manager.recordFailure('anthropic')
        expect(manager.getProviderStatus('anthropic')).toBe('degraded')

        manager.recordSuccess('anthropic')
        expect(manager.getProviderStatus('anthropic')).toBe('healthy')
      })

      it('should get all provider health', () => {
        const manager = new FallbackChainManager()

        manager.recordSuccess('anthropic')
        manager.recordFailure('openai')

        const health = manager.getAllProviderHealth()
        expect(health.length).toBe(2)
      })

      it('should reset provider health', () => {
        const manager = new FallbackChainManager()

        manager.recordSuccess('anthropic')
        manager.resetProviderHealth('anthropic')

        expect(manager.getSuccessRate('anthropic')).toBe(1.0) // Fresh start
      })
    })

    describe('Fallback Selection', () => {
      it('should get model definition', () => {
        const manager = new FallbackChainManager()
        const model = manager.getModelDefinition('anthropic/claude-sonnet-4')

        expect(model).toBeDefined()
        expect(model?.tier).toBe('standard')
      })

      it('should return undefined for unknown model', () => {
        const manager = new FallbackChainManager()
        const model = manager.getModelDefinition('unknown/model')

        expect(model).toBeUndefined()
      })

      it('should get fallback chain', () => {
        const manager = new FallbackChainManager()
        const chain = manager.getFallbackChain('anthropic/claude-sonnet-4')

        expect(chain.length).toBeGreaterThan(0)
        expect(chain).not.toContain('anthropic/claude-sonnet-4')
      })

      it('should return empty chain for unknown model', () => {
        const manager = new FallbackChainManager()
        const chain = manager.getFallbackChain('unknown/model')

        expect(chain).toEqual([])
      })

      it('should select first available fallback', () => {
        const manager = new FallbackChainManager()
        const selection = manager.selectFallback('anthropic/claude-sonnet-4')

        expect(selection).not.toBeNull()
        expect(selection?.model).toBeDefined()
        expect(selection?.position).toBeGreaterThan(0)
      })

      it('should skip unavailable providers', () => {
        const manager = new FallbackChainManager({ circuitBreakerThreshold: 1 })

        // Make first fallback provider unavailable
        const chain = manager.getFallbackChain('anthropic/claude-sonnet-4')
        const firstFallback = MODEL_REGISTRY[chain[0]]
        manager.recordFailure(firstFallback.provider)

        const selection = manager.selectFallback('anthropic/claude-sonnet-4')

        expect(selection?.model.id).not.toBe(chain[0])
        expect(selection?.skipped.some((s) => s.reason === 'Provider unavailable')).toBe(true)
      })

      it('should skip already attempted models', () => {
        const manager = new FallbackChainManager()
        const attempted = new Set(['openai/gpt-4o'])

        const selection = manager.selectFallback('anthropic/claude-sonnet-4', attempted)

        expect(selection?.model.id).not.toBe('openai/gpt-4o')
        expect(selection?.skipped.some((s) => s.reason === 'Already attempted')).toBe(true)
      })

      it('should preserve quality tier when configured', () => {
        const manager = new FallbackChainManager({ preserveQualityTier: true })
        const selection = manager.selectFallback('anthropic/claude-opus-4-5')

        // Should only suggest premium tier models
        expect(selection?.model.tier).toBe('premium')
      })

      it('should filter by required capabilities', () => {
        const manager = new FallbackChainManager({
          requiredCapabilities: ['vision'],
        })

        const selection = manager.selectFallback('anthropic/claude-sonnet-4')

        expect(selection?.model.capabilities).toContain('vision')
      })

      it('should return null when no fallbacks available', () => {
        const manager = new FallbackChainManager({ circuitBreakerThreshold: 1 })

        // Make all providers unavailable
        const chain = manager.getFallbackChain('anthropic/claude-sonnet-4')
        for (const modelId of chain) {
          const model = MODEL_REGISTRY[modelId]
          manager.recordFailure(model.provider)
        }

        const selection = manager.selectFallback('anthropic/claude-sonnet-4')
        expect(selection).toBeNull()
      })
    })

    describe('Execution with Fallback', () => {
      it('should succeed on first attempt', async () => {
        const manager = new FallbackChainManager()

        const result = await manager.executeWithFallback(
          async (model) => `result from ${model}`,
          'anthropic/claude-sonnet-4'
        )

        expect(result.success).toBe(true)
        expect(result.result).toBe('result from anthropic/claude-sonnet-4')
        expect(result.usedModel).toBe('anthropic/claude-sonnet-4')
        expect(result.totalAttempts).toBe(1)
      })

      it('should fallback on failure', async () => {
        const manager = new FallbackChainManager()
        let attempts = 0

        const result = await manager.executeWithFallback(
          async (model) => {
            attempts++
            if (model === 'anthropic/claude-sonnet-4') {
              throw new Error('Primary failed')
            }
            return `result from ${model}`
          },
          'anthropic/claude-sonnet-4'
        )

        expect(result.success).toBe(true)
        expect(result.usedModel).not.toBe('anthropic/claude-sonnet-4')
        expect(result.attemptedModels.length).toBeGreaterThan(1)
      })

      it('should call onAttempt callback', async () => {
        const manager = new FallbackChainManager()
        const attempts: string[] = []

        await manager.executeWithFallback(
          async (model) => `result from ${model}`,
          'anthropic/claude-sonnet-4',
          {
            onAttempt: (modelId) => attempts.push(modelId),
          }
        )

        expect(attempts).toContain('anthropic/claude-sonnet-4')
      })

      it('should call onFallback callback', async () => {
        const manager = new FallbackChainManager()
        const fallbacks: { from: string; to: string }[] = []

        await manager.executeWithFallback(
          async (model) => {
            if (model === 'anthropic/claude-sonnet-4') {
              throw new Error('Failed')
            }
            return 'success'
          },
          'anthropic/claude-sonnet-4',
          {
            onFallback: (from, to) => fallbacks.push({ from, to }),
          }
        )

        expect(fallbacks.length).toBeGreaterThan(0)
        expect(fallbacks[0].from).toBe('anthropic/claude-sonnet-4')
      })

      it('should apply retry delay', async () => {
        const manager = new FallbackChainManager()
        const start = Date.now()

        await manager.executeWithFallback(
          async (model) => {
            if (model === 'anthropic/claude-sonnet-4') {
              throw new Error('Failed')
            }
            return 'success'
          },
          'anthropic/claude-sonnet-4',
          {
            retryDelay: () => 50,
          }
        )

        const elapsed = Date.now() - start
        expect(elapsed).toBeGreaterThanOrEqual(40) // Allow some variance
      })

      it('should fail when chain exhausted', async () => {
        const manager = new FallbackChainManager({ maxRetries: 10 })

        const result = await manager.executeWithFallback(
          async () => {
            throw new Error('Always fails')
          },
          'anthropic/claude-sonnet-4'
        )

        expect(result.success).toBe(false)
        expect(result.error).toBeDefined()
        expect(result.attemptedModels.length).toBeGreaterThan(1)
      })

      it('should respect max retries', async () => {
        const manager = new FallbackChainManager({ maxRetries: 2 })

        const result = await manager.executeWithFallback(
          async () => {
            throw new Error('Always fails')
          },
          'anthropic/claude-sonnet-4'
        )

        expect(result.success).toBe(false)
        expect(result.totalAttempts).toBe(2)
      })

      it('should track total delay', async () => {
        const manager = new FallbackChainManager()

        const result = await manager.executeWithFallback(
          async (model) => {
            if (model === 'anthropic/claude-sonnet-4') {
              throw new Error('Failed')
            }
            return 'success'
          },
          'anthropic/claude-sonnet-4',
          {
            retryDelay: () => 10,
          }
        )

        expect(result.totalDelayMs).toBeGreaterThan(0)
      })
    })
  })

  describe('Singleton', () => {
    it('should return same instance', () => {
      const manager1 = getFallbackManager()
      const manager2 = getFallbackManager()

      expect(manager1).toBe(manager2)
    })

    it('should reset instance', () => {
      const manager1 = getFallbackManager()
      resetFallbackManager()
      const manager2 = getFallbackManager()

      expect(manager1).not.toBe(manager2)
    })
  })

  describe('Utility Functions', () => {
    describe('getModelTier', () => {
      it('should return tier for known models', () => {
        expect(getModelTier('anthropic/claude-opus-4-5')).toBe('premium')
        expect(getModelTier('anthropic/claude-sonnet-4')).toBe('standard')
        expect(getModelTier('anthropic/claude-haiku-4')).toBe('economy')
      })

      it('should return undefined for unknown models', () => {
        expect(getModelTier('unknown/model')).toBeUndefined()
      })
    })

    describe('getModelsByTier', () => {
      it('should return models for each tier', () => {
        const premium = getModelsByTier('premium')
        const standard = getModelsByTier('standard')
        const economy = getModelsByTier('economy')

        expect(premium.length).toBeGreaterThan(0)
        expect(standard.length).toBeGreaterThan(0)
        expect(economy.length).toBeGreaterThan(0)

        expect(premium.every((m) => m.tier === 'premium')).toBe(true)
        expect(standard.every((m) => m.tier === 'standard')).toBe(true)
        expect(economy.every((m) => m.tier === 'economy')).toBe(true)
      })
    })

    describe('getModelsByProvider', () => {
      it('should return models for each provider', () => {
        const anthropic = getModelsByProvider('anthropic')
        const openai = getModelsByProvider('openai')
        const google = getModelsByProvider('google')

        expect(anthropic.length).toBeGreaterThan(0)
        expect(openai.length).toBeGreaterThan(0)
        expect(google.length).toBeGreaterThan(0)

        expect(anthropic.every((m) => m.provider === 'anthropic')).toBe(true)
        expect(openai.every((m) => m.provider === 'openai')).toBe(true)
        expect(google.every((m) => m.provider === 'google')).toBe(true)
      })
    })

    describe('describeFallbackResult', () => {
      it('should describe success', () => {
        const result = {
          success: true,
          result: 'test',
          usedModel: 'anthropic/claude-sonnet-4',
          attemptedModels: ['anthropic/claude-sonnet-4'],
          totalAttempts: 1,
          totalDelayMs: 0,
          chainExhausted: false,
        }

        const description = describeFallbackResult(result)

        expect(description).toContain('Success')
        expect(description).toContain('anthropic/claude-sonnet-4')
        expect(description).toContain('Attempts: 1')
      })

      it('should describe failure', () => {
        const result = {
          success: false,
          error: new Error('Test error'),
          attemptedModels: ['anthropic/claude-sonnet-4', 'openai/gpt-4o'],
          totalAttempts: 2,
          totalDelayMs: 1000,
          chainExhausted: true,
        }

        const description = describeFallbackResult(result)

        expect(description).toContain('Failed')
        expect(description).toContain('Test error')
        expect(description).toContain('->') // Shows chain
        expect(description).toContain('exhausted')
      })
    })
  })
})
