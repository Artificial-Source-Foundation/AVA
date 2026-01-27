/**
 * Tests for Delta9 Rollback Triggers
 */

import { describe, it, expect, beforeEach, vi } from 'vitest'
import {
  RollbackTriggerManager,
  createRollbackTriggerManager,
  getRollbackTriggerManager,
  resetRollbackTriggerManager,
  describeTrigger,
  describeRollbackResult,
  DEFAULT_TRIGGERS,
} from '../../src/lib/rollback-triggers.js'

describe('RollbackTriggerManager', () => {
  let manager: RollbackTriggerManager

  beforeEach(() => {
    manager = createRollbackTriggerManager()
  })

  describe('constructor', () => {
    it('loads default triggers', () => {
      const triggers = manager.getTriggers()
      expect(triggers.length).toBeGreaterThan(0)
      expect(triggers.map((t) => t.id)).toContain('consecutive_failures_3')
    })

    it('allows custom triggers', () => {
      const customManager = createRollbackTriggerManager({
        triggers: [
          {
            id: 'custom_trigger',
            condition: 'consecutive_failures',
            threshold: 5,
            action: 'notify',
            enabled: true,
          },
        ],
      })
      expect(customManager.getTrigger('custom_trigger')).toBeDefined()
    })

    it('can disable defaults', () => {
      const customManager = createRollbackTriggerManager({ useDefaults: false })
      expect(customManager.getTriggers()).toHaveLength(0)
    })
  })

  describe('trigger management', () => {
    it('adds a trigger', () => {
      manager.addTrigger({
        id: 'new_trigger',
        condition: 'timeout_cascade',
        threshold: 10,
        action: 'pause',
        enabled: true,
      })
      expect(manager.getTrigger('new_trigger')).toBeDefined()
    })

    it('removes a trigger', () => {
      expect(manager.removeTrigger('consecutive_failures_3')).toBe(true)
      expect(manager.getTrigger('consecutive_failures_3')).toBeUndefined()
    })

    it('enables and disables triggers', () => {
      manager.setTriggerEnabled('consecutive_failures_3', false)
      expect(manager.getTrigger('consecutive_failures_3')?.enabled).toBe(false)

      manager.setTriggerEnabled('consecutive_failures_3', true)
      expect(manager.getTrigger('consecutive_failures_3')?.enabled).toBe(true)
    })
  })

  describe('event recording', () => {
    it('records task failures', () => {
      const result = manager.recordTaskFailure('task_1', 'Error occurred')
      expect(result).toBeDefined()
      expect(result.triggered).toBe(false) // First failure shouldn't trigger
    })

    it('triggers after consecutive failures', () => {
      manager.recordTaskFailure('task_1')
      manager.recordTaskFailure('task_2')
      const result = manager.recordTaskFailure('task_3')

      expect(result.triggered).toBe(true)
      expect(result.recommendedAction).toBe('checkpoint_restore')
    })

    it('resets consecutive failures on success', () => {
      manager.recordTaskFailure('task_1')
      manager.recordTaskFailure('task_2')
      manager.recordTaskSuccess('task_3')
      const result = manager.recordTaskFailure('task_4')

      expect(result.triggered).toBe(false)
    })

    it('records budget updates', () => {
      const result = manager.recordBudgetUpdate(10, 10) // 100% budget used
      expect(result.triggered).toBe(true)
      expect(result.recommendedAction).toBe('abort')
    })

    it('records timeouts', () => {
      const result = manager.recordTimeout('task_1', 60000)
      expect(result).toBeDefined()
    })
  })

  describe('trigger checking', () => {
    it('respects cooldown', () => {
      // Set up a trigger with cooldown
      manager.addTrigger({
        id: 'test_cooldown',
        condition: 'consecutive_failures',
        threshold: 1,
        action: 'notify',
        enabled: true,
        cooldownMs: 60000, // 1 minute
      })

      // First failure triggers
      const result1 = manager.recordTaskFailure('task_1')
      expect(result1.firedTriggers.some((t) => t.trigger.id === 'test_cooldown')).toBe(true)

      // Second failure within cooldown should not trigger same rule
      const result2 = manager.recordTaskFailure('task_2')
      expect(result2.firedTriggers.some((t) => t.trigger.id === 'test_cooldown')).toBe(false)
    })

    it('checks multiple triggers', () => {
      // Create failures for multiple triggers
      manager.recordTaskFailure('task_1')
      manager.recordTaskFailure('task_2')
      manager.recordTaskFailure('task_3')
      manager.recordBudgetUpdate(11, 10) // Over budget

      const state = manager.getState()
      expect(state.consecutiveFailures).toBe(3)
    })
  })

  describe('rollback execution', () => {
    it('executes checkpoint restore', async () => {
      const restoreCheckpoint = vi.fn().mockResolvedValue(true)

      // Trigger consecutive failures
      manager.recordTaskFailure('task_1')
      manager.recordTaskFailure('task_2')
      const checkResult = manager.recordTaskFailure('task_3')

      const result = await manager.executeRollback(checkResult, { restoreCheckpoint })

      expect(restoreCheckpoint).toHaveBeenCalled()
      expect(result?.success).toBe(true)
      expect(result?.action).toBe('checkpoint_restore')
    })

    it('executes abort', async () => {
      const abortMission = vi.fn().mockResolvedValue(undefined)

      const checkResult = manager.recordBudgetUpdate(11, 10)

      const result = await manager.executeRollback(checkResult, { abortMission })

      expect(abortMission).toHaveBeenCalled()
      expect(result?.success).toBe(true)
      expect(result?.action).toBe('abort')
    })

    it('handles missing handlers', async () => {
      manager.recordTaskFailure('task_1')
      manager.recordTaskFailure('task_2')
      const checkResult = manager.recordTaskFailure('task_3')

      const result = await manager.executeRollback(checkResult, {})

      expect(result?.success).toBe(false)
      expect(result?.error).toContain('No handler available')
    })

    it('calls onRollback callback', async () => {
      const onRollback = vi.fn()
      const managerWithCallback = createRollbackTriggerManager({ onRollback })

      managerWithCallback.recordTaskFailure('task_1')
      managerWithCallback.recordTaskFailure('task_2')
      const checkResult = managerWithCallback.recordTaskFailure('task_3')

      await managerWithCallback.executeRollback(checkResult, {
        restoreCheckpoint: vi.fn().mockResolvedValue(true),
      })

      expect(onRollback).toHaveBeenCalled()
    })
  })

  describe('state management', () => {
    it('returns current state', () => {
      manager.recordTaskFailure('task_1')
      manager.recordTaskFailure('task_2')

      const state = manager.getState()

      expect(state.consecutiveFailures).toBe(2)
      expect(state.eventCount).toBe(2)
      expect(state.enabledTriggers).toBeGreaterThan(0)
    })

    it('resets state', () => {
      manager.recordTaskFailure('task_1')
      manager.reset()

      const state = manager.getState()
      expect(state.consecutiveFailures).toBe(0)
      expect(state.eventCount).toBe(0)
    })

    it('clears history only', () => {
      manager.recordTaskFailure('task_1')
      manager.clearHistory()

      const state = manager.getState()
      expect(state.eventCount).toBe(0)
      expect(state.totalTriggers).toBeGreaterThan(0)
    })
  })
})

describe('utility functions', () => {
  it('describeTrigger formats correctly', () => {
    const description = describeTrigger(DEFAULT_TRIGGERS[0])
    expect(description).toContain('[✓]')
    expect(description).toContain('consecutive failures')
  })

  it('describeRollbackResult formats success', () => {
    const description = describeRollbackResult({
      success: true,
      action: 'checkpoint_restore',
      trigger: DEFAULT_TRIGGERS[0],
      durationMs: 100,
      checkpointName: 'test_checkpoint',
    })
    expect(description).toContain('✅')
    expect(description).toContain('checkpoint_restore')
    expect(description).toContain('test_checkpoint')
  })

  it('describeRollbackResult formats failure', () => {
    const description = describeRollbackResult({
      success: false,
      action: 'abort',
      trigger: DEFAULT_TRIGGERS[0],
      error: 'Test error',
      durationMs: 50,
    })
    expect(description).toContain('❌')
    expect(description).toContain('Test error')
  })
})

describe('singleton', () => {
  beforeEach(() => {
    resetRollbackTriggerManager()
  })

  it('returns same instance', () => {
    const instance1 = getRollbackTriggerManager()
    const instance2 = getRollbackTriggerManager()
    expect(instance1).toBe(instance2)
  })

  it('resets singleton', () => {
    const instance1 = getRollbackTriggerManager()
    resetRollbackTriggerManager()
    const instance2 = getRollbackTriggerManager()
    expect(instance1).not.toBe(instance2)
  })
})
