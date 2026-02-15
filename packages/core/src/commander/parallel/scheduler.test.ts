/**
 * Task Scheduler (DAG) Tests
 */

import { describe, expect, it } from 'vitest'
import type { DependentTask, WorkerDefinition } from '../types.js'
import { createFanIn, createFanOut, createLinearChain, TaskScheduler } from './scheduler.js'

// ============================================================================
// Helpers
// ============================================================================

const dummyWorker: WorkerDefinition = {
  name: 'test-worker',
  description: 'Test worker',
  tools: [],
}

function makeTask(id: string, dependsOn: string[] = []): DependentTask {
  return {
    id,
    worker: dummyWorker,
    inputs: { task: `Task ${id}` },
    dependsOn,
  }
}

// ============================================================================
// TaskScheduler
// ============================================================================

describe('TaskScheduler', () => {
  describe('add', () => {
    it('adds a task', () => {
      const scheduler = new TaskScheduler()
      scheduler.add(makeTask('t1'))
      const states = scheduler.getTaskStates()
      expect(states.has('t1')).toBe(true)
      expect(states.get('t1')!.status).toBe('pending')
    })

    it('throws on duplicate ID', () => {
      const scheduler = new TaskScheduler()
      scheduler.add(makeTask('t1'))
      expect(() => scheduler.add(makeTask('t1'))).toThrow('already exists')
    })
  })

  describe('addAll', () => {
    it('adds multiple tasks', () => {
      const scheduler = new TaskScheduler()
      scheduler.addAll([makeTask('t1'), makeTask('t2')])
      expect(scheduler.getTaskStates().size).toBe(2)
    })
  })

  describe('validate', () => {
    it('validates acyclic graph', () => {
      const scheduler = new TaskScheduler()
      scheduler.addAll([makeTask('t1'), makeTask('t2', ['t1'])])
      expect(scheduler.validate().valid).toBe(true)
    })

    it('detects missing dependency', () => {
      const scheduler = new TaskScheduler()
      scheduler.add(makeTask('t1', ['missing']))
      const result = scheduler.validate()
      expect(result.valid).toBe(false)
      expect(result.error).toContain('unknown task')
    })

    it('detects cycle', () => {
      const scheduler = new TaskScheduler()
      scheduler.addAll([makeTask('t1', ['t2']), makeTask('t2', ['t1'])])
      const result = scheduler.validate()
      expect(result.valid).toBe(false)
      expect(result.error).toContain('Cycle')
    })
  })

  describe('getReady', () => {
    it('returns tasks with no dependencies', () => {
      const scheduler = new TaskScheduler()
      scheduler.addAll([makeTask('t1'), makeTask('t2', ['t1'])])
      const ready = scheduler.getReady()
      expect(ready).toHaveLength(1)
      expect(ready[0].id).toBe('t1')
    })

    it('returns multiple ready tasks', () => {
      const scheduler = new TaskScheduler()
      scheduler.addAll([makeTask('t1'), makeTask('t2'), makeTask('t3', ['t1'])])
      const ready = scheduler.getReady()
      expect(ready).toHaveLength(2)
    })
  })

  describe('clear', () => {
    it('removes all tasks', () => {
      const scheduler = new TaskScheduler()
      scheduler.addAll([makeTask('t1'), makeTask('t2')])
      scheduler.clear()
      expect(scheduler.getTaskStates().size).toBe(0)
    })
  })
})

// ============================================================================
// Convenience Functions
// ============================================================================

describe('createLinearChain', () => {
  it('chains tasks sequentially', () => {
    const tasks = createLinearChain([
      { id: 't1', worker: dummyWorker, inputs: {} },
      { id: 't2', worker: dummyWorker, inputs: {} },
      { id: 't3', worker: dummyWorker, inputs: {} },
    ])
    expect(tasks[0].dependsOn).toEqual([])
    expect(tasks[1].dependsOn).toEqual(['t1'])
    expect(tasks[2].dependsOn).toEqual(['t2'])
  })

  it('handles single task', () => {
    const tasks = createLinearChain([{ id: 't1', worker: dummyWorker, inputs: {} }])
    expect(tasks[0].dependsOn).toEqual([])
  })
})

describe('createFanOut', () => {
  it('creates one task then parallel', () => {
    const tasks = createFanOut({ id: 'init', worker: dummyWorker, inputs: {} }, [
      { id: 'p1', worker: dummyWorker, inputs: {} },
      { id: 'p2', worker: dummyWorker, inputs: {} },
    ])
    expect(tasks).toHaveLength(3)
    expect(tasks[0].dependsOn).toEqual([])
    expect(tasks[1].dependsOn).toEqual(['init'])
    expect(tasks[2].dependsOn).toEqual(['init'])
  })
})

describe('createFanIn', () => {
  it('creates parallel tasks then one', () => {
    const tasks = createFanIn(
      [
        { id: 'p1', worker: dummyWorker, inputs: {} },
        { id: 'p2', worker: dummyWorker, inputs: {} },
      ],
      { id: 'final', worker: dummyWorker, inputs: {} }
    )
    expect(tasks).toHaveLength(3)
    expect(tasks[0].dependsOn).toEqual([])
    expect(tasks[1].dependsOn).toEqual([])
    expect(tasks[2].dependsOn).toEqual(['p1', 'p2'])
  })
})
