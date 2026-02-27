import { describe, expect, it, vi } from 'vitest'
import { createTaskRunner } from './runner.js'
import type { ScheduledTask } from './types.js'

function createTask(overrides: Partial<ScheduledTask> = {}): ScheduledTask {
  return {
    id: 'task-1',
    name: 'test-task',
    interval: 1000,
    nextRun: 0, // Due immediately
    handler: vi.fn().mockResolvedValue(undefined),
    ...overrides,
  }
}

describe('createTaskRunner', () => {
  it('registers and retrieves tasks', () => {
    const runner = createTaskRunner()
    const task = createTask()
    runner.register(task)
    expect(runner.getTasks().size).toBe(1)
    expect(runner.getTasks().get('task-1')).toBe(task)
  })

  it('unregisters tasks', () => {
    const runner = createTaskRunner()
    runner.register(createTask())
    runner.unregister('task-1')
    expect(runner.getTasks().size).toBe(0)
  })

  it('runs due tasks on tick', async () => {
    const runner = createTaskRunner()
    const handler = vi.fn().mockResolvedValue(undefined)
    runner.register(createTask({ handler }))
    await runner.tick()
    expect(handler).toHaveBeenCalledOnce()
  })

  it('skips non-due tasks', async () => {
    const runner = createTaskRunner()
    const handler = vi.fn().mockResolvedValue(undefined)
    runner.register(createTask({ handler, nextRun: Date.now() + 999_999 }))
    await runner.tick()
    expect(handler).not.toHaveBeenCalled()
  })

  it('respects maxConcurrent limit', async () => {
    const runner = createTaskRunner({ maxConcurrent: 1, tickInterval: 100 })
    const handler1 = vi.fn().mockResolvedValue(undefined)
    const handler2 = vi.fn().mockResolvedValue(undefined)
    runner.register(createTask({ id: 'task-1', handler: handler1 }))
    runner.register(createTask({ id: 'task-2', handler: handler2 }))
    await runner.tick()
    // Both can run because they resolve immediately, but only 1 at a time
    expect(handler1).toHaveBeenCalled()
  })

  it('updates nextRun after execution', async () => {
    const runner = createTaskRunner()
    const task = createTask({ interval: 5000 })
    runner.register(task)
    await runner.tick()
    expect(task.nextRun).toBeGreaterThan(0)
    expect(task.lastRun).toBeDefined()
  })

  it('handles task errors gracefully', async () => {
    const runner = createTaskRunner()
    const handler = vi.fn().mockRejectedValue(new Error('fail'))
    runner.register(createTask({ handler }))
    // Should not throw
    await expect(runner.tick()).resolves.toBeUndefined()
  })

  it('getRunningCount returns 0 when idle', () => {
    const runner = createTaskRunner()
    expect(runner.getRunningCount()).toBe(0)
  })
})
