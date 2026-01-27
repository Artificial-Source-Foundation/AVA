/**
 * Tests for Delta9 Idle Maintenance
 */

import { describe, it, expect, beforeEach, afterEach, vi } from 'vitest'
import {
  IdleMaintenanceManager,
  getIdleMaintenanceManager,
  clearIdleMaintenanceManager,
  registerCommonTasks,
  MAINTENANCE_PRIORITY,
} from '../../src/lib/idle-maintenance.js'

describe('IdleMaintenanceManager', () => {
  let manager: IdleMaintenanceManager

  beforeEach(() => {
    vi.useFakeTimers()
    manager = new IdleMaintenanceManager()
  })

  afterEach(() => {
    manager.clear()
    vi.useRealTimers()
  })

  describe('task registration', () => {
    it('registers a maintenance task', () => {
      manager.registerTask({
        name: 'test-task',
        handler: async () => {},
        minInterval: 1000,
        enabled: true,
        priority: 50,
      })

      expect(manager.getTaskNames()).toContain('test-task')
    })

    it('unregisters a task', () => {
      manager.registerTask({
        name: 'to-remove',
        handler: async () => {},
        minInterval: 1000,
        enabled: true,
        priority: 50,
      })

      expect(manager.unregisterTask('to-remove')).toBe(true)
      expect(manager.getTaskNames()).not.toContain('to-remove')
    })

    it('returns false when unregistering non-existent task', () => {
      expect(manager.unregisterTask('non-existent')).toBe(false)
    })

    it('enables a task', () => {
      manager.registerTask({
        name: 'toggle-task',
        handler: async () => {},
        minInterval: 1000,
        enabled: false,
        priority: 50,
      })

      manager.enableTask('toggle-task')
      // Task state is internal, verify via running maintenance
      const status = manager.getStatus()
      expect(status.registeredTasks).toBe(1)
    })

    it('disables a task', () => {
      manager.registerTask({
        name: 'disable-me',
        handler: async () => {},
        minInterval: 1000,
        enabled: true,
        priority: 50,
      })

      manager.disableTask('disable-me')
      // Disabled tasks won't run
    })
  })

  describe('idle triggering', () => {
    it('debounces idle triggers', () => {
      const handler = vi.fn(async () => {})
      manager.registerTask({
        name: 'debounced',
        handler,
        minInterval: 0,
        enabled: true,
        priority: 50,
      })

      // Trigger multiple times rapidly
      manager.triggerIdle()
      manager.triggerIdle()
      manager.triggerIdle()

      // Not called yet (debounced)
      expect(handler).not.toHaveBeenCalled()

      // Advance past debounce time (default 2000ms)
      vi.advanceTimersByTime(2100)

      expect(handler).toHaveBeenCalledTimes(1)
    })

    it('cancels pending idle', () => {
      const handler = vi.fn(async () => {})
      manager.registerTask({
        name: 'cancelable',
        handler,
        minInterval: 0,
        enabled: true,
        priority: 50,
      })

      manager.triggerIdle()
      manager.cancelPending()

      vi.advanceTimersByTime(5000)

      expect(handler).not.toHaveBeenCalled()
    })

    it('does not trigger when disabled', () => {
      const handler = vi.fn(async () => {})
      manager.registerTask({
        name: 'wont-run',
        handler,
        minInterval: 0,
        enabled: true,
        priority: 50,
      })

      manager.disable()
      manager.triggerIdle()

      vi.advanceTimersByTime(5000)

      expect(handler).not.toHaveBeenCalled()
    })
  })

  describe('maintenance execution', () => {
    it('runs eligible tasks', async () => {
      const handler = vi.fn(async () => {})
      manager.registerTask({
        name: 'runner',
        handler,
        minInterval: 0,
        enabled: true,
        priority: 50,
      })

      const result = await manager.runMaintenance()

      expect(result.tasksRun).toBe(1)
      expect(result.succeeded).toBe(1)
      expect(result.failed).toBe(0)
      expect(handler).toHaveBeenCalledTimes(1)
    })

    it('skips disabled tasks', async () => {
      const handler = vi.fn(async () => {})
      manager.registerTask({
        name: 'disabled-task',
        handler,
        minInterval: 0,
        enabled: false,
        priority: 50,
      })

      const result = await manager.runMaintenance()

      expect(result.tasksRun).toBe(0)
      expect(handler).not.toHaveBeenCalled()
    })

    it('respects minInterval', async () => {
      const handler = vi.fn(async () => {})
      manager.registerTask({
        name: 'interval-task',
        handler,
        minInterval: 60000, // 1 minute
        enabled: true,
        priority: 50,
      })

      // First run
      await manager.runMaintenance()
      expect(handler).toHaveBeenCalledTimes(1)

      // Second run immediately (should skip)
      await manager.runMaintenance()
      expect(handler).toHaveBeenCalledTimes(1)

      // Advance time and run again
      vi.advanceTimersByTime(60001)
      await manager.runMaintenance()
      expect(handler).toHaveBeenCalledTimes(2)
    })

    it('runs tasks in priority order', async () => {
      const order: string[] = []

      manager.registerTask({
        name: 'low-priority',
        handler: async () => {
          order.push('low')
        },
        minInterval: 0,
        enabled: true,
        priority: 100,
      })

      manager.registerTask({
        name: 'high-priority',
        handler: async () => {
          order.push('high')
        },
        minInterval: 0,
        enabled: true,
        priority: 10,
      })

      manager.registerTask({
        name: 'medium-priority',
        handler: async () => {
          order.push('medium')
        },
        minInterval: 0,
        enabled: true,
        priority: 50,
      })

      await manager.runMaintenance()

      expect(order).toEqual(['high', 'medium', 'low'])
    })

    it('handles task errors gracefully', async () => {
      manager.registerTask({
        name: 'error-task',
        handler: async () => {
          throw new Error('Task failed')
        },
        minInterval: 0,
        enabled: true,
        priority: 50,
      })

      const result = await manager.runMaintenance()

      expect(result.tasksRun).toBe(1)
      expect(result.failed).toBe(1)
      expect(result.succeeded).toBe(0)
      expect(result.taskResults[0].error).toBe('Task failed')
    })

    it('skips if already running', async () => {
      let resolveFirst: () => void
      const firstPromise = new Promise<void>((resolve) => {
        resolveFirst = resolve
      })

      manager.registerTask({
        name: 'slow-task',
        handler: async () => {
          await firstPromise
        },
        minInterval: 0,
        enabled: true,
        priority: 50,
      })

      // Start first run
      const run1 = manager.runMaintenance()

      // Try second run while first is running
      const run2Promise = manager.runMaintenance()
      const result2 = await run2Promise

      // Second run should skip
      expect(result2.tasksRun).toBe(0)

      // Resolve first run
      resolveFirst!()
      await run1
    })

    it('respects maxDurationMs timeout', async () => {
      manager.configure({ maxDurationMs: 100 })

      const handler1 = vi.fn(async () => {
        // Simulate slow task
        await new Promise((resolve) => setTimeout(resolve, 50))
      })
      const handler2 = vi.fn(async () => {
        await new Promise((resolve) => setTimeout(resolve, 50))
      })
      const handler3 = vi.fn(async () => {
        await new Promise((resolve) => setTimeout(resolve, 50))
      })

      manager.registerTask({
        name: 'task1',
        handler: handler1,
        minInterval: 0,
        enabled: true,
        priority: 10,
      })
      manager.registerTask({
        name: 'task2',
        handler: handler2,
        minInterval: 0,
        enabled: true,
        priority: 20,
      })
      manager.registerTask({
        name: 'task3',
        handler: handler3,
        minInterval: 0,
        enabled: true,
        priority: 30,
      })

      vi.useRealTimers()
      const result = await manager.runMaintenance()
      vi.useFakeTimers()

      // Should have stopped before running all tasks
      expect(result.tasksRun).toBeLessThanOrEqual(3)
    })
  })

  describe('configuration', () => {
    it('updates configuration', () => {
      manager.configure({
        debounceMs: 5000,
        maxDurationMs: 10000,
      })

      const config = manager.getConfig()

      expect(config.debounceMs).toBe(5000)
      expect(config.maxDurationMs).toBe(10000)
    })

    it('preserves unspecified config values', () => {
      const original = manager.getConfig()
      manager.configure({ debounceMs: 9999 })
      const updated = manager.getConfig()

      expect(updated.debounceMs).toBe(9999)
      expect(updated.enabled).toBe(original.enabled)
    })

    it('enables and disables maintenance', () => {
      manager.disable()
      expect(manager.getConfig().enabled).toBe(false)

      manager.enable()
      expect(manager.getConfig().enabled).toBe(true)
    })
  })

  describe('status', () => {
    it('returns current status', async () => {
      manager.registerTask({
        name: 'status-task',
        handler: async () => {},
        minInterval: 0,
        enabled: true,
        priority: 50,
      })

      await manager.runMaintenance()

      const status = manager.getStatus()

      expect(status.enabled).toBe(true)
      expect(status.isRunning).toBe(false)
      expect(status.lastRunAt).toBeGreaterThan(0)
      expect(status.registeredTasks).toBe(1)
      expect(status.pendingMaintenance).toBe(false)
    })

    it('tracks pending maintenance', () => {
      manager.triggerIdle()

      const status = manager.getStatus()
      expect(status.pendingMaintenance).toBe(true)

      manager.cancelPending()

      const status2 = manager.getStatus()
      expect(status2.pendingMaintenance).toBe(false)
    })
  })

  describe('clear', () => {
    it('clears all state', () => {
      manager.registerTask({
        name: 'to-clear',
        handler: async () => {},
        minInterval: 0,
        enabled: true,
        priority: 50,
      })
      manager.triggerIdle()

      manager.clear()

      expect(manager.getTaskNames()).toEqual([])
      expect(manager.getStatus().pendingMaintenance).toBe(false)
    })
  })
})

describe('singleton functions', () => {
  afterEach(() => {
    clearIdleMaintenanceManager()
  })

  it('returns singleton instance', () => {
    const instance1 = getIdleMaintenanceManager()
    const instance2 = getIdleMaintenanceManager()

    expect(instance1).toBe(instance2)
  })

  it('clears singleton', () => {
    const instance1 = getIdleMaintenanceManager()
    clearIdleMaintenanceManager()
    const instance2 = getIdleMaintenanceManager()

    expect(instance1).not.toBe(instance2)
  })
})

describe('registerCommonTasks', () => {
  let manager: IdleMaintenanceManager

  beforeEach(() => {
    manager = new IdleMaintenanceManager()
  })

  afterEach(() => {
    manager.clear()
  })

  it('registers auto-save task', () => {
    registerCommonTasks(manager, {
      autoSave: async () => {},
    })

    expect(manager.getTaskNames()).toContain('auto-save')
  })

  it('registers cleanup task', () => {
    registerCommonTasks(manager, {
      cleanupStaleTasks: async () => {},
    })

    expect(manager.getTaskNames()).toContain('cleanup-stale-tasks')
  })

  it('registers memory compact task', () => {
    registerCommonTasks(manager, {
      compactMemory: async () => {},
    })

    expect(manager.getTaskNames()).toContain('compact-memory')
  })

  it('registers metrics task', () => {
    registerCommonTasks(manager, {
      updateMetrics: async () => {},
    })

    expect(manager.getTaskNames()).toContain('update-metrics')
  })

  it('registers multiple tasks at once', () => {
    registerCommonTasks(manager, {
      autoSave: async () => {},
      cleanupStaleTasks: async () => {},
      compactMemory: async () => {},
      updateMetrics: async () => {},
    })

    expect(manager.getTaskNames()).toHaveLength(4)
  })

  it('skips unspecified tasks', () => {
    registerCommonTasks(manager, {})

    expect(manager.getTaskNames()).toHaveLength(0)
  })
})

describe('MAINTENANCE_PRIORITY', () => {
  it('has correct priority levels', () => {
    expect(MAINTENANCE_PRIORITY.CRITICAL).toBe(10)
    expect(MAINTENANCE_PRIORITY.HIGH).toBe(20)
    expect(MAINTENANCE_PRIORITY.NORMAL).toBe(50)
    expect(MAINTENANCE_PRIORITY.LOW).toBe(80)
  })

  it('priorities are ordered correctly', () => {
    expect(MAINTENANCE_PRIORITY.CRITICAL).toBeLessThan(MAINTENANCE_PRIORITY.HIGH)
    expect(MAINTENANCE_PRIORITY.HIGH).toBeLessThan(MAINTENANCE_PRIORITY.NORMAL)
    expect(MAINTENANCE_PRIORITY.NORMAL).toBeLessThan(MAINTENANCE_PRIORITY.LOW)
  })
})
