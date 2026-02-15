/**
 * Scheduler Tests
 */

import { afterEach, describe, expect, it, vi } from 'vitest'
import {
  createScheduler,
  disposeScheduler,
  getScheduler,
  Scheduler,
  setScheduler,
} from './scheduler.js'
import type { TaskResult } from './types.js'

afterEach(() => {
  disposeScheduler()
  vi.useRealTimers()
})

function makeTask(
  id: string,
  opts: { intervalMs?: number; run?: () => Promise<void>; enabled?: boolean } = {}
) {
  return {
    id,
    name: `Task ${id}`,
    intervalMs: opts.intervalMs ?? 1000,
    run: opts.run ?? (async () => {}),
    scope: 'session' as const,
    enabled: opts.enabled,
  }
}

// ============================================================================
// Task Registration
// ============================================================================

describe('Scheduler registration', () => {
  it('registers a task', () => {
    const scheduler = new Scheduler()
    scheduler.register(makeTask('t1'))
    expect(scheduler.getTasks()).toHaveLength(1)
    expect(scheduler.getTask('t1')).toBeDefined()
  })

  it('throws on duplicate registration', () => {
    const scheduler = new Scheduler()
    scheduler.register(makeTask('t1'))
    expect(() => scheduler.register(makeTask('t1'))).toThrow('already registered')
  })

  it('unregisters a task', () => {
    const scheduler = new Scheduler()
    scheduler.register(makeTask('t1'))
    scheduler.unregister('t1')
    expect(scheduler.getTasks()).toHaveLength(0)
    expect(scheduler.getTask('t1')).toBeUndefined()
  })

  it('enables a disabled task', () => {
    vi.useFakeTimers()
    const scheduler = new Scheduler()
    scheduler.register(makeTask('t1', { enabled: false }))
    expect(scheduler.getTask('t1')!.enabled).toBe(false)
    scheduler.enable('t1')
    expect(scheduler.getTask('t1')!.enabled).toBe(true)
  })

  it('disables a task', () => {
    const scheduler = new Scheduler()
    scheduler.register(makeTask('t1'))
    scheduler.disable('t1')
    expect(scheduler.getTask('t1')!.enabled).toBe(false)
  })
})

// ============================================================================
// Scheduler Control
// ============================================================================

describe('Scheduler start/stop', () => {
  it('starts and stops', () => {
    const scheduler = new Scheduler()
    expect(scheduler.isStarted()).toBe(false)
    scheduler.start()
    expect(scheduler.isStarted()).toBe(true)
    scheduler.stop()
    expect(scheduler.isStarted()).toBe(false)
  })

  it('is idempotent on start', () => {
    const scheduler = new Scheduler()
    scheduler.start()
    scheduler.start()
    expect(scheduler.isStarted()).toBe(true)
  })

  it('is idempotent on stop', () => {
    const scheduler = new Scheduler()
    scheduler.stop()
    expect(scheduler.isStarted()).toBe(false)
  })

  it('autoStart starts scheduler', () => {
    const scheduler = new Scheduler({ autoStart: true })
    expect(scheduler.isStarted()).toBe(true)
    scheduler.dispose()
  })

  it('runs tasks immediately on start', async () => {
    vi.useFakeTimers()
    let ran = false
    const scheduler = new Scheduler()
    scheduler.register(
      makeTask('t1', {
        run: async () => {
          ran = true
        },
      })
    )
    scheduler.start()
    await vi.advanceTimersByTimeAsync(0)
    expect(ran).toBe(true)
    scheduler.dispose()
  })
})

// ============================================================================
// runNow
// ============================================================================

describe('Scheduler runNow', () => {
  it('runs a task immediately and returns result', async () => {
    const scheduler = new Scheduler()
    scheduler.register(makeTask('t1'))
    const result = await scheduler.runNow('t1')
    expect(result.taskId).toBe('t1')
    expect(result.success).toBe(true)
    expect(result.durationMs).toBeGreaterThanOrEqual(0)
  })

  it('throws for unknown task', async () => {
    const scheduler = new Scheduler()
    await expect(scheduler.runNow('nonexistent')).rejects.toThrow('not found')
  })

  it('captures errors in result', async () => {
    const scheduler = new Scheduler()
    scheduler.register(
      makeTask('t1', {
        run: async () => {
          throw new Error('boom')
        },
      })
    )
    const result = await scheduler.runNow('t1')
    expect(result.success).toBe(false)
    expect(result.error).toBe('boom')
  })
})

// ============================================================================
// Concurrency Limits
// ============================================================================

describe('Scheduler concurrency', () => {
  it('respects maxConcurrent limit', async () => {
    vi.useFakeTimers()
    let concurrent = 0
    let maxConcurrent = 0

    const slowTask = async () => {
      concurrent++
      maxConcurrent = Math.max(maxConcurrent, concurrent)
      await new Promise((r) => setTimeout(r, 100))
      concurrent--
    }

    const scheduler = new Scheduler({ maxConcurrent: 2 })
    scheduler.register(makeTask('t1', { run: slowTask, intervalMs: 50 }))
    scheduler.register(makeTask('t2', { run: slowTask, intervalMs: 50 }))
    scheduler.register(makeTask('t3', { run: slowTask, intervalMs: 50 }))
    scheduler.start()

    await vi.advanceTimersByTimeAsync(200)
    scheduler.dispose()

    expect(maxConcurrent).toBeLessThanOrEqual(2)
  })

  it('does not run same task concurrently', async () => {
    vi.useFakeTimers()
    let concurrentRuns = 0
    let maxConcurrentRuns = 0

    const task = makeTask('t1', {
      intervalMs: 10,
      run: async () => {
        concurrentRuns++
        maxConcurrentRuns = Math.max(maxConcurrentRuns, concurrentRuns)
        await new Promise((r) => setTimeout(r, 50))
        concurrentRuns--
      },
    })

    const scheduler = new Scheduler({ maxConcurrent: 5 })
    scheduler.register(task)
    scheduler.start()

    await vi.advanceTimersByTimeAsync(100)
    scheduler.dispose()

    expect(maxConcurrentRuns).toBe(1)
  })
})

// ============================================================================
// Callbacks
// ============================================================================

describe('Scheduler callbacks', () => {
  it('calls onTaskComplete after task runs', async () => {
    const results: TaskResult[] = []
    const scheduler = new Scheduler({ onTaskComplete: (r) => results.push(r) })
    scheduler.register(makeTask('t1'))
    await scheduler.runNow('t1')
    expect(results).toHaveLength(1)
    expect(results[0].taskId).toBe('t1')
    expect(results[0].success).toBe(true)
  })

  it('calls onTaskComplete even on failure', async () => {
    const results: TaskResult[] = []
    const scheduler = new Scheduler({ onTaskComplete: (r) => results.push(r) })
    scheduler.register(
      makeTask('t1', {
        run: async () => {
          throw new Error('fail')
        },
      })
    )
    await scheduler.runNow('t1')
    expect(results[0].success).toBe(false)
    expect(results[0].error).toBe('fail')
  })
})

// ============================================================================
// Dispose
// ============================================================================

describe('Scheduler dispose', () => {
  it('stops and clears all tasks', () => {
    const scheduler = new Scheduler({ autoStart: true })
    scheduler.register(makeTask('t1'))
    scheduler.dispose()
    expect(scheduler.isStarted()).toBe(false)
    expect(scheduler.getTasks()).toHaveLength(0)
  })
})

// ============================================================================
// Factory / Singleton
// ============================================================================

describe('factory and singleton', () => {
  it('createScheduler creates new instance', () => {
    const s1 = createScheduler()
    const s2 = createScheduler()
    expect(s1).not.toBe(s2)
  })

  it('getScheduler returns singleton that auto-starts', () => {
    const s = getScheduler()
    expect(s.isStarted()).toBe(true)
    expect(getScheduler()).toBe(s)
  })

  it('setScheduler replaces singleton', () => {
    const custom = new Scheduler()
    setScheduler(custom)
    expect(getScheduler()).toBe(custom)
  })

  it('disposeScheduler clears singleton', () => {
    const old = getScheduler()
    disposeScheduler()
    const fresh = getScheduler()
    expect(fresh).not.toBe(old)
  })
})
