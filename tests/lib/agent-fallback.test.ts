/**
 * Agent Fallback Manager Tests
 *
 * Tests for circuit breaker pattern and auto-fallback (BUG-16 fix)
 */

import { describe, it, expect, beforeEach } from 'vitest'
import {
  AgentFallbackManager,
  resetAgentFallbackManager,
} from '../../src/lib/agent-fallback.js'

describe('AgentFallbackManager', () => {
  let manager: AgentFallbackManager

  beforeEach(() => {
    resetAgentFallbackManager()
    manager = new AgentFallbackManager({
      failureThreshold: 3,
      cooldownMs: 1000, // Short cooldown for testing
      fallbackChains: {
        uiOps: ['operator'],
        scout: ['operator'],
        qa: ['validator', 'operator'],
        validator: ['operator'],
        operator: [],
      },
    })
  })

  describe('circuit breaker states', () => {
    it('starts in closed state (agent available)', () => {
      expect(manager.isAgentAvailable('uiOps')).toBe(true)
      const health = manager.getAgentHealth('uiOps')
      expect(health?.state).toBe('closed')
    })

    it('opens circuit after threshold failures', () => {
      manager.recordFailure('uiOps')
      manager.recordFailure('uiOps')
      expect(manager.isAgentAvailable('uiOps')).toBe(true) // Still below threshold

      manager.recordFailure('uiOps') // 3rd failure
      expect(manager.isAgentAvailable('uiOps')).toBe(false)

      const health = manager.getAgentHealth('uiOps')
      expect(health?.state).toBe('open')
      expect(health?.failureCount).toBe(3)
    })

    it('transitions to half-open after cooldown', async () => {
      // Open circuit
      manager.recordFailure('uiOps')
      manager.recordFailure('uiOps')
      manager.recordFailure('uiOps')
      expect(manager.isAgentAvailable('uiOps')).toBe(false)

      // Wait for cooldown (1 second in test config)
      await new Promise((resolve) => setTimeout(resolve, 1100))

      // Should now be half-open (available for testing)
      expect(manager.isAgentAvailable('uiOps')).toBe(true)

      const health = manager.getAgentHealth('uiOps')
      expect(health?.state).toBe('half_open')
    })

    it('closes circuit after success in half-open state', async () => {
      // Open circuit
      manager.recordFailure('uiOps')
      manager.recordFailure('uiOps')
      manager.recordFailure('uiOps')

      // Wait for cooldown to reach half-open
      await new Promise((resolve) => setTimeout(resolve, 1100))
      manager.isAgentAvailable('uiOps') // Triggers half-open

      // Record success
      manager.recordSuccess('uiOps')

      const health = manager.getAgentHealth('uiOps')
      expect(health?.state).toBe('closed')
      expect(health?.failureCount).toBe(0)
    })

    it('reopens circuit if failure in half-open state', async () => {
      // Open circuit
      manager.recordFailure('uiOps')
      manager.recordFailure('uiOps')
      manager.recordFailure('uiOps')

      // Wait for cooldown to reach half-open
      await new Promise((resolve) => setTimeout(resolve, 1100))
      manager.isAgentAvailable('uiOps') // Triggers half-open

      // Fail again
      manager.recordFailure('uiOps')

      const health = manager.getAgentHealth('uiOps')
      expect(health?.state).toBe('open')
    })
  })

  describe('auto-fallback', () => {
    it('returns requested agent when available', () => {
      const result = manager.getBestAgent('uiOps')
      expect(result.agent).toBe('uiOps')
      expect(result.isFallback).toBe(false)
    })

    it('falls back when circuit is open', () => {
      // Open circuit for uiOps
      manager.recordFailure('uiOps')
      manager.recordFailure('uiOps')
      manager.recordFailure('uiOps')

      const result = manager.getBestAgent('uiOps')
      expect(result.agent).toBe('operator')
      expect(result.isFallback).toBe(true)
      expect(result.originalAgent).toBe('uiOps')
    })

    it('follows fallback chain', () => {
      // Open circuit for qa
      manager.recordFailure('qa')
      manager.recordFailure('qa')
      manager.recordFailure('qa')

      const result = manager.getBestAgent('qa')
      // Should fallback to validator (first in chain)
      expect(result.agent).toBe('validator')
      expect(result.isFallback).toBe(true)
    })

    it('continues fallback chain if first fallback also unavailable', () => {
      // Open circuits for qa and validator
      manager.recordFailure('qa')
      manager.recordFailure('qa')
      manager.recordFailure('qa')
      manager.recordFailure('validator')
      manager.recordFailure('validator')
      manager.recordFailure('validator')

      const result = manager.getBestAgent('qa')
      // Should fallback all the way to operator
      expect(result.agent).toBe('operator')
      expect(result.isFallback).toBe(true)
    })

    it('returns requested agent if no fallbacks available', () => {
      // Open circuit for operator (no fallbacks defined)
      manager.recordFailure('operator')
      manager.recordFailure('operator')
      manager.recordFailure('operator')

      const result = manager.getBestAgent('operator')
      expect(result.agent).toBe('operator')
      expect(result.isFallback).toBe(false)
      expect(result.reason).toContain('No fallbacks available')
    })
  })

  describe('failure tracking', () => {
    it('increments failure count', () => {
      manager.recordFailure('uiOps')
      expect(manager.getAgentHealth('uiOps')?.failureCount).toBe(1)

      manager.recordFailure('uiOps')
      expect(manager.getAgentHealth('uiOps')?.failureCount).toBe(2)
    })

    it('decays failure count on success in closed state', () => {
      manager.recordFailure('uiOps')
      manager.recordFailure('uiOps')
      expect(manager.getAgentHealth('uiOps')?.failureCount).toBe(2)

      manager.recordSuccess('uiOps')
      expect(manager.getAgentHealth('uiOps')?.failureCount).toBe(1)

      manager.recordSuccess('uiOps')
      expect(manager.getAgentHealth('uiOps')?.failureCount).toBe(0)
    })

    it('resets success count on failure', () => {
      manager.recordSuccess('uiOps')
      manager.recordSuccess('uiOps')
      expect(manager.getAgentHealth('uiOps')?.successCount).toBe(2)

      manager.recordFailure('uiOps')
      expect(manager.getAgentHealth('uiOps')?.successCount).toBe(0)
    })
  })

  describe('manual controls', () => {
    it('resets agent health manually', () => {
      manager.recordFailure('uiOps')
      manager.recordFailure('uiOps')
      manager.recordFailure('uiOps')
      expect(manager.isAgentAvailable('uiOps')).toBe(false)

      manager.resetAgent('uiOps')
      expect(manager.isAgentAvailable('uiOps')).toBe(true)
      expect(manager.getAgentHealth('uiOps')?.failureCount).toBe(0)
    })

    it('resets all agent health', () => {
      manager.recordFailure('uiOps')
      manager.recordFailure('qa')

      manager.resetAll()

      expect(manager.getHealthStatus()).toHaveLength(0)
    })

    it('returns health status for all tracked agents', () => {
      manager.recordFailure('uiOps')
      manager.recordSuccess('operator')

      const status = manager.getHealthStatus()
      expect(status).toHaveLength(2)
      expect(status.map((h) => h.agent)).toContain('uiOps')
      expect(status.map((h) => h.agent)).toContain('operator')
    })
  })
})
