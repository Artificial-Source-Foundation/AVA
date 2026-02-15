/**
 * Task Parallel Execution Tests
 *
 * Tests validation of parallel task params and concurrency constants.
 * Execution tests are in agent/__tests__/agent-pipeline.integration.test.ts
 */

import { describe, expect, it } from 'vitest'
import { taskTool } from './task.js'
import { DEFAULT_CONCURRENCY, MAX_CONCURRENCY, MAX_PARALLEL_TASKS } from './task-parallel.js'

// ============================================================================
// Constants
// ============================================================================

describe('Concurrency constants', () => {
  it('explore allows up to 5 concurrent', () => {
    expect(MAX_CONCURRENCY.explore).toBe(5)
    expect(DEFAULT_CONCURRENCY.explore).toBe(5)
  })

  it('plan allows up to 3 concurrent', () => {
    expect(MAX_CONCURRENCY.plan).toBe(3)
    expect(DEFAULT_CONCURRENCY.plan).toBe(3)
  })

  it('execute limited to 1 concurrent for file safety', () => {
    expect(MAX_CONCURRENCY.execute).toBe(1)
    expect(DEFAULT_CONCURRENCY.execute).toBe(1)
  })

  it('custom allows up to 5 concurrent', () => {
    expect(MAX_CONCURRENCY.custom).toBe(5)
    expect(DEFAULT_CONCURRENCY.custom).toBe(3)
  })

  it('MAX_PARALLEL_TASKS is 10', () => {
    expect(MAX_PARALLEL_TASKS).toBe(10)
  })

  it('defaults never exceed maximums', () => {
    for (const key of Object.keys(DEFAULT_CONCURRENCY) as Array<keyof typeof DEFAULT_CONCURRENCY>) {
      expect(DEFAULT_CONCURRENCY[key]).toBeLessThanOrEqual(MAX_CONCURRENCY[key])
    }
  })
})

// ============================================================================
// Validation: tasks param
// ============================================================================

describe('taskTool.validate — tasks param', () => {
  const base = {
    description: 'test',
    prompt: 'test prompt',
    agentType: 'explore',
  }

  it('accepts valid single task (no tasks array)', () => {
    const result = taskTool.validate!({ ...base })
    expect(result.tasks).toBeUndefined()
  })

  it('accepts valid tasks array', () => {
    const result = taskTool.validate!({
      ...base,
      tasks: [
        { description: 'Task 1', prompt: 'Do thing 1' },
        { description: 'Task 2', prompt: 'Do thing 2' },
      ],
    })
    expect(result.tasks).toHaveLength(2)
    expect(result.tasks![0].description).toBe('Task 1')
  })

  it('rejects non-array tasks', () => {
    expect(() => taskTool.validate!({ ...base, tasks: 'not-array' })).toThrow('must be array')
  })

  it('rejects empty tasks array', () => {
    expect(() => taskTool.validate!({ ...base, tasks: [] })).toThrow('at least one task')
  })

  it('rejects tasks exceeding MAX_PARALLEL_TASKS', () => {
    const tasks = Array.from({ length: MAX_PARALLEL_TASKS + 1 }, (_, i) => ({
      description: `Task ${i}`,
      prompt: `Prompt ${i}`,
    }))
    expect(() => taskTool.validate!({ ...base, tasks })).toThrow(`maximum ${MAX_PARALLEL_TASKS}`)
  })

  it('accepts exactly MAX_PARALLEL_TASKS tasks', () => {
    const tasks = Array.from({ length: MAX_PARALLEL_TASKS }, (_, i) => ({
      description: `Task ${i}`,
      prompt: `Prompt ${i}`,
    }))
    const result = taskTool.validate!({ ...base, tasks })
    expect(result.tasks).toHaveLength(MAX_PARALLEL_TASKS)
  })

  it('rejects task with non-object item', () => {
    expect(() => taskTool.validate!({ ...base, tasks: ['not-an-object'] })).toThrow(
      'each must be an object'
    )
  })

  it('rejects task with null item', () => {
    expect(() => taskTool.validate!({ ...base, tasks: [null] })).toThrow('each must be an object')
  })

  it('rejects task without description', () => {
    expect(() => taskTool.validate!({ ...base, tasks: [{ prompt: 'do it' }] })).toThrow(
      'non-empty description'
    )
  })

  it('rejects task with empty description', () => {
    expect(() =>
      taskTool.validate!({ ...base, tasks: [{ description: '  ', prompt: 'do it' }] })
    ).toThrow('non-empty description')
  })

  it('rejects task without prompt', () => {
    expect(() => taskTool.validate!({ ...base, tasks: [{ description: 'test' }] })).toThrow(
      'non-empty prompt'
    )
  })

  it('rejects task with empty prompt', () => {
    expect(() =>
      taskTool.validate!({ ...base, tasks: [{ description: 'test', prompt: '' }] })
    ).toThrow('non-empty prompt')
  })
})

// ============================================================================
// Validation: maxConcurrent param
// ============================================================================

describe('taskTool.validate — maxConcurrent param', () => {
  const base = {
    description: 'test',
    prompt: 'test prompt',
    agentType: 'explore',
  }

  it('accepts valid maxConcurrent', () => {
    const result = taskTool.validate!({ ...base, maxConcurrent: 3 })
    expect(result.maxConcurrent).toBe(3)
  })

  it('accepts maxConcurrent of 1', () => {
    const result = taskTool.validate!({ ...base, maxConcurrent: 1 })
    expect(result.maxConcurrent).toBe(1)
  })

  it('accepts undefined maxConcurrent (uses default)', () => {
    const result = taskTool.validate!({ ...base })
    expect(result.maxConcurrent).toBeUndefined()
  })

  it('rejects maxConcurrent of 0', () => {
    expect(() => taskTool.validate!({ ...base, maxConcurrent: 0 })).toThrow('must be positive')
  })

  it('rejects negative maxConcurrent', () => {
    expect(() => taskTool.validate!({ ...base, maxConcurrent: -1 })).toThrow('must be positive')
  })

  it('rejects non-number maxConcurrent', () => {
    expect(() => taskTool.validate!({ ...base, maxConcurrent: 'two' })).toThrow('must be positive')
  })

  it('rejects explore maxConcurrent > 5', () => {
    expect(() => taskTool.validate!({ ...base, agentType: 'explore', maxConcurrent: 6 })).toThrow(
      'explore limited to 5'
    )
  })

  it('accepts explore maxConcurrent = 5', () => {
    const result = taskTool.validate!({ ...base, agentType: 'explore', maxConcurrent: 5 })
    expect(result.maxConcurrent).toBe(5)
  })

  it('rejects execute maxConcurrent > 1', () => {
    expect(() => taskTool.validate!({ ...base, agentType: 'execute', maxConcurrent: 2 })).toThrow(
      'execute limited to 1'
    )
  })

  it('accepts execute maxConcurrent = 1', () => {
    const result = taskTool.validate!({ ...base, agentType: 'execute', maxConcurrent: 1 })
    expect(result.maxConcurrent).toBe(1)
  })

  it('rejects plan maxConcurrent > 3', () => {
    expect(() => taskTool.validate!({ ...base, agentType: 'plan', maxConcurrent: 4 })).toThrow(
      'plan limited to 3'
    )
  })
})

// ============================================================================
// Validation: combined tasks + maxConcurrent
// ============================================================================

describe('taskTool.validate — combined parallel params', () => {
  it('accepts tasks with matching maxConcurrent', () => {
    const result = taskTool.validate!({
      description: 'batch',
      prompt: 'batch prompt',
      agentType: 'explore',
      tasks: [
        { description: 'A', prompt: 'do A' },
        { description: 'B', prompt: 'do B' },
      ],
      maxConcurrent: 2,
    })
    expect(result.tasks).toHaveLength(2)
    expect(result.maxConcurrent).toBe(2)
  })

  it('rejects tasks with invalid maxConcurrent for agent type', () => {
    expect(() =>
      taskTool.validate!({
        description: 'batch',
        prompt: 'batch prompt',
        agentType: 'execute',
        tasks: [
          { description: 'A', prompt: 'do A' },
          { description: 'B', prompt: 'do B' },
        ],
        maxConcurrent: 2,
      })
    ).toThrow('execute limited to 1')
  })
})

// ============================================================================
// Tool definition
// ============================================================================

describe('taskTool definition — parallel schema', () => {
  it('includes tasks in input_schema properties', () => {
    const props = taskTool.definition.input_schema.properties as Record<string, unknown>
    expect(props.tasks).toBeDefined()
  })

  it('includes maxConcurrent in input_schema properties', () => {
    const props = taskTool.definition.input_schema.properties as Record<string, unknown>
    expect(props.maxConcurrent).toBeDefined()
  })

  it('tasks schema has array type with object items', () => {
    const props = taskTool.definition.input_schema.properties as Record<
      string,
      { type: string; items?: { type: string } }
    >
    expect(props.tasks.type).toBe('array')
    expect(props.tasks.items?.type).toBe('object')
  })

  it('maxConcurrent schema has number type', () => {
    const props = taskTool.definition.input_schema.properties as Record<string, { type: string }>
    expect(props.maxConcurrent.type).toBe('number')
  })
})

// ============================================================================
// Execute dispatch
// ============================================================================

describe('taskTool.execute — dispatch', () => {
  const baseCtx = {
    sessionId: 'test-session',
    workingDirectory: '/tmp',
    signal: AbortSignal.abort(),
  }

  it('returns cancelled when signal already aborted', async () => {
    const result = await taskTool.execute(
      {
        description: 'test',
        prompt: 'test',
        agentType: 'explore' as const,
      },
      baseCtx
    )
    expect(result.success).toBe(false)
    expect(result.output).toContain('cancelled')
  })

  it('returns cancelled for parallel tasks when signal already aborted', async () => {
    const result = await taskTool.execute(
      {
        description: 'batch',
        prompt: 'batch prompt',
        agentType: 'explore' as const,
        tasks: [
          { description: 'A', prompt: 'do A' },
          { description: 'B', prompt: 'do B' },
        ],
      },
      baseCtx
    )
    expect(result.success).toBe(false)
    expect(result.output).toContain('cancelled')
  })
})
