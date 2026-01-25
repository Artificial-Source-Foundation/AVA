/**
 * Tests for Three-Strike System
 */

import { describe, it, expect, beforeEach, vi } from 'vitest'
import {
  StrikeManager,
  getStrikeManager,
  resetStrikeManager,
  addStrike,
  getAgentStatus,
  clearAgentStrikes,
  getAgentRetryGuidance,
} from '../../src/guardrails/three-strike.js'

describe('Three-Strike System', () => {
  beforeEach(() => {
    resetStrikeManager()
  })

  describe('StrikeManager', () => {
    it('should start with no strikes', () => {
      const manager = new StrikeManager()
      const status = manager.getStatus('agent-1')

      expect(status.strikes).toHaveLength(0)
      expect(status.level).toBe('none')
      expect(status.isEscalated).toBe(false)
      expect(status.canRetry).toBe(true)
    })

    it('should add strikes', () => {
      const manager = new StrikeManager()

      manager.addStrike({
        agentId: 'agent-1',
        reason: 'validation_failed',
        message: 'Test failed',
      })

      const status = manager.getStatus('agent-1')
      expect(status.strikes).toHaveLength(1)
      expect(status.level).toBe('warning')
    })

    it('should escalate after 3 strikes', () => {
      const manager = new StrikeManager({ maxStrikes: 3 })

      manager.addStrike({ agentId: 'agent-1', reason: 'task_failed', message: 'Error 1' })
      manager.addStrike({ agentId: 'agent-1', reason: 'task_failed', message: 'Error 2' })
      const status = manager.addStrike({ agentId: 'agent-1', reason: 'task_failed', message: 'Error 3' })

      expect(status.strikes).toHaveLength(3)
      expect(status.level).toBe('escalate_to_human')
      expect(status.isEscalated).toBe(true)
      expect(status.canRetry).toBe(false)
    })

    it('should track escalation levels correctly', () => {
      const manager = new StrikeManager({ maxStrikes: 3 })

      // 0 strikes
      let status = manager.getStatus('agent-1')
      expect(status.level).toBe('none')

      // 1 strike
      manager.addStrike({ agentId: 'agent-1', reason: 'task_failed', message: 'Error' })
      status = manager.getStatus('agent-1')
      expect(status.level).toBe('warning')

      // 2 strikes
      manager.addStrike({ agentId: 'agent-1', reason: 'task_failed', message: 'Error' })
      status = manager.getStatus('agent-1')
      expect(status.level).toBe('retry_with_guidance')

      // 3 strikes
      manager.addStrike({ agentId: 'agent-1', reason: 'task_failed', message: 'Error' })
      status = manager.getStatus('agent-1')
      expect(status.level).toBe('escalate_to_human')
    })

    it('should clear strikes for an agent', () => {
      const manager = new StrikeManager()

      manager.addStrike({ agentId: 'agent-1', reason: 'task_failed', message: 'Error' })
      manager.addStrike({ agentId: 'agent-1', reason: 'task_failed', message: 'Error' })

      manager.clearStrikes('agent-1')

      const status = manager.getStatus('agent-1')
      expect(status.strikes).toHaveLength(0)
      expect(status.level).toBe('none')
    })

    it('should track strikes per agent separately', () => {
      const manager = new StrikeManager()

      manager.addStrike({ agentId: 'agent-1', reason: 'task_failed', message: 'Error' })
      manager.addStrike({ agentId: 'agent-1', reason: 'task_failed', message: 'Error' })
      manager.addStrike({ agentId: 'agent-2', reason: 'task_failed', message: 'Error' })

      expect(manager.getStatus('agent-1').strikes).toHaveLength(2)
      expect(manager.getStatus('agent-2').strikes).toHaveLength(1)
    })

    it('should decay strikes after timeout', async () => {
      const manager = new StrikeManager({
        strikeDecayMs: 50, // 50ms for testing
      })

      manager.addStrike({ agentId: 'agent-1', reason: 'task_failed', message: 'Error' })

      // Wait for decay
      await new Promise((resolve) => setTimeout(resolve, 100))

      const status = manager.getStatus('agent-1')
      expect(status.strikes).toHaveLength(0)
    })

    it('should provide retry guidance', () => {
      const manager = new StrikeManager()

      manager.addStrike({ agentId: 'agent-1', reason: 'validation_failed', message: 'Test failed' })

      const guidance = manager.getRetryGuidance('agent-1')

      expect(guidance.canRetry).toBe(true)
      expect(guidance.message).toContain('1/3')
      expect(guidance.suggestions).toContain('Check acceptance criteria and test requirements')
    })

    it('should provide escalated guidance after max strikes', () => {
      const manager = new StrikeManager({ maxStrikes: 3 })

      manager.addStrike({ agentId: 'agent-1', reason: 'task_failed', message: 'Error' })
      manager.addStrike({ agentId: 'agent-1', reason: 'task_failed', message: 'Error' })
      manager.addStrike({ agentId: 'agent-1', reason: 'task_failed', message: 'Error' })

      const guidance = manager.getRetryGuidance('agent-1')

      expect(guidance.canRetry).toBe(false)
      expect(guidance.message).toContain('escalated')
      expect(guidance.suggestions).toContain('Manual intervention may be required')
    })

    it('should include task ID in strikes', () => {
      const manager = new StrikeManager()

      manager.addStrike({
        agentId: 'agent-1',
        taskId: 'task-123',
        reason: 'task_failed',
        message: 'Error',
      })

      const status = manager.getStatus('agent-1')
      expect(status.lastStrike?.taskId).toBe('task-123')
    })

    it('should include context in strikes', () => {
      const manager = new StrikeManager()

      manager.addStrike({
        agentId: 'agent-1',
        reason: 'task_failed',
        message: 'Error',
        context: { file: 'test.ts', line: 42 },
      })

      const status = manager.getStatus('agent-1')
      expect(status.lastStrike?.context).toEqual({ file: 'test.ts', line: 42 })
    })

    it('should get total strikes across all agents', () => {
      const manager = new StrikeManager()

      manager.addStrike({ agentId: 'agent-1', reason: 'task_failed', message: 'Error' })
      manager.addStrike({ agentId: 'agent-1', reason: 'task_failed', message: 'Error' })
      manager.addStrike({ agentId: 'agent-2', reason: 'task_failed', message: 'Error' })

      expect(manager.getTotalStrikes()).toBe(3)
    })

    it('should get escalated agents', () => {
      const manager = new StrikeManager({ maxStrikes: 2 })

      manager.addStrike({ agentId: 'agent-1', reason: 'task_failed', message: 'Error' })
      manager.addStrike({ agentId: 'agent-1', reason: 'task_failed', message: 'Error' })
      manager.addStrike({ agentId: 'agent-2', reason: 'task_failed', message: 'Error' })

      const escalated = manager.getEscalatedAgents()
      expect(escalated).toContain('agent-1')
      expect(escalated).not.toContain('agent-2')
    })

    it('should get all statuses', () => {
      const manager = new StrikeManager()

      manager.addStrike({ agentId: 'agent-1', reason: 'task_failed', message: 'Error' })
      manager.addStrike({ agentId: 'agent-2', reason: 'task_failed', message: 'Error' })

      const statuses = manager.getAllStatuses()
      expect(statuses).toHaveLength(2)
    })

    it('should clear all strikes', () => {
      const manager = new StrikeManager()

      manager.addStrike({ agentId: 'agent-1', reason: 'task_failed', message: 'Error' })
      manager.addStrike({ agentId: 'agent-2', reason: 'task_failed', message: 'Error' })

      manager.clearAll()

      expect(manager.getTotalStrikes()).toBe(0)
    })
  })

  describe('Singleton Functions', () => {
    it('should use default manager for addStrike', () => {
      addStrike({ agentId: 'agent-1', reason: 'task_failed', message: 'Error' })

      const status = getAgentStatus('agent-1')
      expect(status.strikes).toHaveLength(1)
    })

    it('should use default manager for clearAgentStrikes', () => {
      addStrike({ agentId: 'agent-1', reason: 'task_failed', message: 'Error' })
      clearAgentStrikes('agent-1')

      const status = getAgentStatus('agent-1')
      expect(status.strikes).toHaveLength(0)
    })

    it('should use default manager for getAgentRetryGuidance', () => {
      const guidance = getAgentRetryGuidance('agent-1')
      expect(guidance.canRetry).toBe(true)
    })

    it('should reset default manager', () => {
      addStrike({ agentId: 'agent-1', reason: 'task_failed', message: 'Error' })
      resetStrikeManager()

      const status = getAgentStatus('agent-1')
      expect(status.strikes).toHaveLength(0)
    })

    it('should return same instance', () => {
      const manager1 = getStrikeManager()
      const manager2 = getStrikeManager()

      expect(manager1).toBe(manager2)
    })
  })

  describe('Retry Guidance', () => {
    it('should suggest council consultation after 2 strikes', () => {
      const manager = new StrikeManager()

      manager.addStrike({ agentId: 'agent-1', reason: 'task_failed', message: 'Error' })
      manager.addStrike({ agentId: 'agent-1', reason: 'task_failed', message: 'Error' })

      const guidance = manager.getRetryGuidance('agent-1')
      expect(guidance.suggestions).toContain('Consider consulting the Council for guidance')
    })

    it('should suggest timeout-specific fixes', () => {
      const manager = new StrikeManager()

      manager.addStrike({ agentId: 'agent-1', reason: 'timeout', message: 'Timeout' })

      const guidance = manager.getRetryGuidance('agent-1')
      expect(guidance.suggestions).toContain('Task may be too complex - consider decomposition')
    })

    it('should suggest budget-specific fixes', () => {
      const manager = new StrikeManager()

      manager.addStrike({ agentId: 'agent-1', reason: 'budget_exceeded', message: 'Over budget' })

      const guidance = manager.getRetryGuidance('agent-1')
      expect(guidance.suggestions).toContain('Optimize token usage - be more concise')
    })

    it('should include retry delay based on strikes', () => {
      const manager = new StrikeManager()

      manager.addStrike({ agentId: 'agent-1', reason: 'task_failed', message: 'Error' })
      let guidance = manager.getRetryGuidance('agent-1')
      expect(guidance.retryDelay).toBeGreaterThan(0)

      manager.addStrike({ agentId: 'agent-1', reason: 'task_failed', message: 'Error' })
      guidance = manager.getRetryGuidance('agent-1')
      expect(guidance.retryDelay).toBeGreaterThan(5000) // Higher delay for more strikes
    })

    it('should dedupe suggestions', () => {
      const manager = new StrikeManager()

      manager.addStrike({ agentId: 'agent-1', reason: 'validation_failed', message: 'Error' })
      manager.addStrike({ agentId: 'agent-1', reason: 'validation_failed', message: 'Error' })

      const guidance = manager.getRetryGuidance('agent-1')
      const uniqueSuggestions = new Set(guidance.suggestions)
      expect(uniqueSuggestions.size).toBe(guidance.suggestions.length)
    })
  })

  describe('Logging', () => {
    it('should log strikes when logger is provided', () => {
      const logs: string[] = []
      const manager = new StrikeManager({
        log: (level, message) => logs.push(`${level}: ${message}`),
      })

      manager.addStrike({ agentId: 'agent-1', reason: 'task_failed', message: 'Error' })

      expect(logs.length).toBeGreaterThan(0)
      expect(logs[0]).toContain('Strike')
    })

    it('should log error level on escalation', () => {
      const logs: { level: string; message: string }[] = []
      const manager = new StrikeManager({
        maxStrikes: 2,
        log: (level, message) => logs.push({ level, message }),
      })

      manager.addStrike({ agentId: 'agent-1', reason: 'task_failed', message: 'Error' })
      manager.addStrike({ agentId: 'agent-1', reason: 'task_failed', message: 'Error' })

      const errorLog = logs.find((l) => l.level === 'error')
      expect(errorLog).toBeDefined()
    })

    it('should log when strikes are cleared', () => {
      const logs: string[] = []
      const manager = new StrikeManager({
        log: (level, message) => logs.push(`${level}: ${message}`),
      })

      manager.addStrike({ agentId: 'agent-1', reason: 'task_failed', message: 'Error' })
      manager.clearStrikes('agent-1')

      expect(logs.some((l) => l.includes('cleared'))).toBe(true)
    })
  })
})
