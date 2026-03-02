import type { AgentResult } from '@ava/core-v2/agent'
import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest'
import { executeOrchestration, findReadySubtasks } from './orchestrator.js'
import type { TaskPlan } from './planning.js'
import { clearRegistry, registerAgents } from './registry.js'
import { LEAD_AGENTS } from './workers.js'

function makeResult(overrides?: Partial<AgentResult>): AgentResult {
  return {
    success: true,
    terminateMode: 'complete',
    output: 'Done',
    turns: 1,
    tokensUsed: { input: 100, output: 50 },
    durationMs: 100,
    ...overrides,
  }
}

describe('executeOrchestration', () => {
  beforeEach(() => {
    // Register leads so selectBestAgent can resolve them
    registerAgents(LEAD_AGENTS)
  })

  afterEach(() => {
    clearRegistry()
  })

  it('executes a simple plan with no dependencies', async () => {
    const plan: TaskPlan = {
      subtasks: [
        {
          description: 'Add login form',
          domain: 'frontend',
          files: ['src/Login.tsx'],
          assignTo: 'frontend-lead',
        },
        {
          description: 'Add auth API',
          domain: 'backend',
          files: ['src/api/auth.ts'],
          assignTo: 'backend-lead',
        },
      ],
      dependencies: [],
    }

    const delegateFn = vi
      .fn<[string, string], Promise<AgentResult>>()
      .mockResolvedValue(makeResult())

    const result = await executeOrchestration(plan, delegateFn)

    expect(result.success).toBe(true)
    expect(result.results).toHaveLength(2)
    expect(delegateFn).toHaveBeenCalledTimes(2)

    // Verify agent IDs match subtask assignments
    const agentIds = result.results.map((r) => r.agentId)
    expect(agentIds).toContain('frontend-lead')
    expect(agentIds).toContain('backend-lead')
  })

  it('respects dependencies — runs blocked subtasks after blockers', async () => {
    const plan: TaskPlan = {
      subtasks: [
        {
          description: 'Create schema',
          domain: 'backend',
          files: ['schema.ts'],
          assignTo: 'backend-lead',
        },
        { description: 'Add API', domain: 'backend', files: ['api.ts'], assignTo: 'backend-lead' },
        { description: 'Write tests', domain: 'testing', files: ['test.ts'], assignTo: 'qa-lead' },
      ],
      dependencies: [
        [0, 1], // API depends on schema
        [1, 2], // Tests depend on API
      ],
    }

    const callOrder: string[] = []
    const delegateFn = vi
      .fn<[string, string], Promise<AgentResult>>()
      .mockImplementation(async (_agentId, task) => {
        callOrder.push(task)
        return makeResult()
      })

    const result = await executeOrchestration(plan, delegateFn)

    expect(result.success).toBe(true)
    expect(result.results).toHaveLength(3)

    // Schema must be called before API, API before tests
    expect(callOrder.indexOf('Create schema')).toBeLessThan(callOrder.indexOf('Add API'))
    expect(callOrder.indexOf('Add API')).toBeLessThan(callOrder.indexOf('Write tests'))
  })

  it('limits parallel delegations to maxParallelDelegations', async () => {
    const plan: TaskPlan = {
      subtasks: [
        { description: 'Task A', domain: 'frontend', files: [], assignTo: 'frontend-lead' },
        { description: 'Task B', domain: 'backend', files: [], assignTo: 'backend-lead' },
        { description: 'Task C', domain: 'testing', files: [], assignTo: 'qa-lead' },
        { description: 'Task D', domain: 'fullstack', files: [], assignTo: 'fullstack-lead' },
      ],
      dependencies: [],
    }

    let maxConcurrent = 0
    let currentConcurrent = 0
    const delegateFn = vi
      .fn<[string, string], Promise<AgentResult>>()
      .mockImplementation(async () => {
        currentConcurrent++
        maxConcurrent = Math.max(maxConcurrent, currentConcurrent)
        await new Promise((resolve) => setTimeout(resolve, 10))
        currentConcurrent--
        return makeResult()
      })

    await executeOrchestration(plan, delegateFn, { maxParallelDelegations: 2 })

    // Within a single batch, at most 2 should run
    expect(maxConcurrent).toBeLessThanOrEqual(2)
  })

  it('retries failed subtasks when configured', async () => {
    const plan: TaskPlan = {
      subtasks: [
        { description: 'Flaky task', domain: 'backend', files: [], assignTo: 'backend-lead' },
      ],
      dependencies: [],
    }

    let attempt = 0
    const delegateFn = vi
      .fn<[string, string], Promise<AgentResult>>()
      .mockImplementation(async () => {
        attempt++
        if (attempt < 2) return makeResult({ success: false, output: 'Flaky failure' })
        return makeResult({ success: true, output: 'Succeeded on retry' })
      })

    const result = await executeOrchestration(plan, delegateFn, {
      retryFailedSubtasks: true,
      maxRetries: 1,
    })

    expect(result.success).toBe(true)
    expect(delegateFn).toHaveBeenCalledTimes(2)
  })

  it('does not retry when retryFailedSubtasks is false', async () => {
    const plan: TaskPlan = {
      subtasks: [
        { description: 'Fail task', domain: 'backend', files: [], assignTo: 'backend-lead' },
      ],
      dependencies: [],
    }

    const delegateFn = vi
      .fn<[string, string], Promise<AgentResult>>()
      .mockResolvedValue(makeResult({ success: false, output: 'Permanent failure' }))

    const result = await executeOrchestration(plan, delegateFn, {
      retryFailedSubtasks: false,
    })

    expect(result.success).toBe(false)
    expect(delegateFn).toHaveBeenCalledTimes(1)
  })

  it('handles delegateFn throwing errors', async () => {
    const plan: TaskPlan = {
      subtasks: [
        { description: 'Error task', domain: 'backend', files: [], assignTo: 'backend-lead' },
      ],
      dependencies: [],
    }

    const delegateFn = vi
      .fn<[string, string], Promise<AgentResult>>()
      .mockRejectedValue(new Error('Network error'))

    const result = await executeOrchestration(plan, delegateFn, {
      retryFailedSubtasks: true,
      maxRetries: 1,
    })

    expect(result.success).toBe(false)
    expect(result.results[0]!.output).toBe('Failed after all retries')
    expect(delegateFn).toHaveBeenCalledTimes(2) // 1 initial + 1 retry
  })

  it('falls back to domain-based agent selection when assignTo is empty', async () => {
    const plan: TaskPlan = {
      subtasks: [
        {
          description: 'Build a new UI component with css and layout',
          domain: 'frontend',
          files: [],
          assignTo: '',
        },
      ],
      dependencies: [],
    }

    const delegateFn = vi
      .fn<[string, string], Promise<AgentResult>>()
      .mockResolvedValue(makeResult())

    const result = await executeOrchestration(plan, delegateFn)

    expect(result.success).toBe(true)
    // Domain analysis should pick frontend-lead based on "UI component css layout"
    expect(result.results[0]!.agentId).toBe('frontend-lead')
  })

  it('produces a summary with status indicators', async () => {
    const plan: TaskPlan = {
      subtasks: [
        { description: 'Good task', domain: 'frontend', files: [], assignTo: 'frontend-lead' },
        { description: 'Bad task', domain: 'backend', files: [], assignTo: 'backend-lead' },
      ],
      dependencies: [],
    }

    const delegateFn = vi
      .fn<[string, string], Promise<AgentResult>>()
      .mockImplementation(async (_agentId, task) => {
        if (task === 'Bad task') return makeResult({ success: false, output: 'Something broke' })
        return makeResult()
      })

    const result = await executeOrchestration(plan, delegateFn)

    expect(result.success).toBe(false)
    expect(result.summary).toContain('1 succeeded, 1 failed out of 2 subtasks')
    expect(result.summary).toContain('[OK]')
    expect(result.summary).toContain('[FAIL]')
    expect(result.summary).toContain('Error: Something broke')
  })

  it('handles an empty plan', async () => {
    const plan: TaskPlan = {
      subtasks: [],
      dependencies: [],
    }

    const delegateFn = vi.fn<[string, string], Promise<AgentResult>>()

    const result = await executeOrchestration(plan, delegateFn)

    expect(result.success).toBe(true)
    expect(result.results).toHaveLength(0)
    expect(delegateFn).not.toHaveBeenCalled()
  })

  describe('progress events', () => {
    it('emits orchestration:batch-start and orchestration:batch-complete', async () => {
      const plan: TaskPlan = {
        subtasks: [
          { description: 'Task A', domain: 'frontend', files: [], assignTo: 'frontend-lead' },
          { description: 'Task B', domain: 'backend', files: [], assignTo: 'backend-lead' },
        ],
        dependencies: [],
      }

      const delegateFn = vi
        .fn<[string, string], Promise<AgentResult>>()
        .mockResolvedValue(makeResult())
      const onEvent = vi.fn()

      await executeOrchestration(plan, delegateFn, undefined, onEvent)

      const startEvents = onEvent.mock.calls.filter(
        (c: unknown[]) => (c[0] as Record<string, unknown>).type === 'orchestration:batch-start'
      )
      const completeEvents = onEvent.mock.calls.filter(
        (c: unknown[]) => (c[0] as Record<string, unknown>).type === 'orchestration:batch-complete'
      )

      expect(startEvents.length).toBeGreaterThanOrEqual(1)
      expect(completeEvents.length).toBeGreaterThanOrEqual(1)
      expect(startEvents[0]![0]).toMatchObject({
        type: 'orchestration:batch-start',
        batchIndex: 0,
      })
      expect(completeEvents[0]![0]).toMatchObject({
        type: 'orchestration:batch-complete',
        batchIndex: 0,
        success: true,
      })
    })

    it('emits multiple batch events for dependent subtasks', async () => {
      const plan: TaskPlan = {
        subtasks: [
          { description: 'Schema', domain: 'backend', files: [], assignTo: 'backend-lead' },
          { description: 'API', domain: 'backend', files: [], assignTo: 'backend-lead' },
          { description: 'Tests', domain: 'testing', files: [], assignTo: 'qa-lead' },
        ],
        dependencies: [
          [0, 1],
          [1, 2],
        ],
      }

      const delegateFn = vi
        .fn<[string, string], Promise<AgentResult>>()
        .mockResolvedValue(makeResult())
      const onEvent = vi.fn()

      await executeOrchestration(plan, delegateFn, undefined, onEvent)

      const startEvents = onEvent.mock.calls.filter(
        (c: unknown[]) => (c[0] as Record<string, unknown>).type === 'orchestration:batch-start'
      )
      expect(startEvents).toHaveLength(3)
    })

    it('reports batch failure in completion event', async () => {
      const plan: TaskPlan = {
        subtasks: [
          { description: 'Fail', domain: 'frontend', files: [], assignTo: 'frontend-lead' },
        ],
        dependencies: [],
      }

      const delegateFn = vi
        .fn<[string, string], Promise<AgentResult>>()
        .mockResolvedValue(makeResult({ success: false, output: 'Oops' }))
      const onEvent = vi.fn()

      await executeOrchestration(plan, delegateFn, { retryFailedSubtasks: false }, onEvent)

      const completeEvent = onEvent.mock.calls.find(
        (c: unknown[]) => (c[0] as Record<string, unknown>).type === 'orchestration:batch-complete'
      )
      expect(completeEvent![0]).toMatchObject({
        type: 'orchestration:batch-complete',
        success: false,
      })
    })

    it('includes completedCount and totalCount in batch-complete', async () => {
      const plan: TaskPlan = {
        subtasks: [
          { description: 'A', domain: 'frontend', files: [], assignTo: 'frontend-lead' },
          { description: 'B', domain: 'backend', files: [], assignTo: 'backend-lead' },
        ],
        dependencies: [],
      }

      const delegateFn = vi
        .fn<[string, string], Promise<AgentResult>>()
        .mockResolvedValue(makeResult())
      const onEvent = vi.fn()

      await executeOrchestration(plan, delegateFn, undefined, onEvent)

      const completeEvent = onEvent.mock.calls.find(
        (c: unknown[]) => (c[0] as Record<string, unknown>).type === 'orchestration:batch-complete'
      )
      expect(completeEvent![0]).toMatchObject({
        completedCount: 2,
        totalCount: 2,
      })
    })
  })

  describe('parallel execution', () => {
    it('runs independent subtasks in parallel within a batch', async () => {
      const plan: TaskPlan = {
        subtasks: [
          { description: 'Task A', domain: 'frontend', files: [], assignTo: 'frontend-lead' },
          { description: 'Task B', domain: 'backend', files: [], assignTo: 'backend-lead' },
          { description: 'Task C', domain: 'testing', files: [], assignTo: 'qa-lead' },
        ],
        dependencies: [],
      }

      const timeline: Array<{ task: string; event: 'start' | 'end' }> = []
      const delegateFn = vi
        .fn<[string, string], Promise<AgentResult>>()
        .mockImplementation(async (_agentId, task) => {
          timeline.push({ task, event: 'start' })
          await new Promise((resolve) => setTimeout(resolve, 20))
          timeline.push({ task, event: 'end' })
          return makeResult()
        })

      await executeOrchestration(plan, delegateFn, { maxParallelDelegations: 3 })

      // All three start before any end (parallel execution)
      const firstEndIdx = timeline.findIndex((e) => e.event === 'end')
      const startsBeforeFirstEnd = timeline.slice(0, firstEndIdx).filter((e) => e.event === 'start')
      expect(startsBeforeFirstEnd.length).toBe(3)
    })

    it('serializes dependent subtasks across batches', async () => {
      const plan: TaskPlan = {
        subtasks: [
          { description: 'First', domain: 'backend', files: [], assignTo: 'backend-lead' },
          { description: 'Second', domain: 'backend', files: [], assignTo: 'backend-lead' },
        ],
        dependencies: [[0, 1]],
      }

      const callOrder: string[] = []
      const delegateFn = vi
        .fn<[string, string], Promise<AgentResult>>()
        .mockImplementation(async (_agentId, task) => {
          callOrder.push(task)
          return makeResult()
        })

      await executeOrchestration(plan, delegateFn)
      expect(callOrder).toEqual(['First', 'Second'])
    })

    it('handles diamond dependencies correctly', async () => {
      const plan: TaskPlan = {
        subtasks: [
          { description: 'A', domain: 'backend', files: [], assignTo: 'backend-lead' },
          { description: 'B', domain: 'frontend', files: [], assignTo: 'frontend-lead' },
          { description: 'C', domain: 'testing', files: [], assignTo: 'qa-lead' },
          { description: 'D', domain: 'fullstack', files: [], assignTo: 'fullstack-lead' },
        ],
        dependencies: [
          [0, 1],
          [0, 2], // B and C depend on A
          [1, 3],
          [2, 3], // D depends on B and C
        ],
      }

      const batchTasks: string[][] = []
      let currentBatch: string[] = []

      const delegateFn = vi
        .fn<[string, string], Promise<AgentResult>>()
        .mockImplementation(async (_agentId, task) => {
          currentBatch.push(task)
          return makeResult()
        })

      const onEvent = vi.fn().mockImplementation((event: Record<string, unknown>) => {
        if (event.type === 'orchestration:batch-start') {
          currentBatch = []
        }
        if (event.type === 'orchestration:batch-complete') {
          batchTasks.push([...currentBatch])
        }
      })

      await executeOrchestration(plan, delegateFn, undefined, onEvent)

      // Batch 1: [A], Batch 2: [B, C] in parallel, Batch 3: [D]
      expect(batchTasks).toHaveLength(3)
      expect(batchTasks[0]).toEqual(['A'])
      expect(batchTasks[1]).toHaveLength(2)
      expect(batchTasks[1]).toContain('B')
      expect(batchTasks[1]).toContain('C')
      expect(batchTasks[2]).toEqual(['D'])
    })
  })
})

describe('findReadySubtasks', () => {
  it('returns all indices when no dependencies exist', () => {
    const completed = new Set<number>()
    const depsOf = new Map<number, number[]>()
    const ready = findReadySubtasks(3, completed, depsOf)
    expect(ready).toEqual([0, 1, 2])
  })

  it('excludes completed subtasks', () => {
    const completed = new Set([0, 1])
    const depsOf = new Map<number, number[]>()
    const ready = findReadySubtasks(3, completed, depsOf)
    expect(ready).toEqual([2])
  })

  it('excludes subtasks with unmet dependencies', () => {
    const completed = new Set<number>()
    const depsOf = new Map<number, number[]>([
      [1, [0]],
      [2, [1]],
    ])
    const ready = findReadySubtasks(3, completed, depsOf)
    expect(ready).toEqual([0])
  })

  it('includes subtasks when all dependencies are completed', () => {
    const completed = new Set([0])
    const depsOf = new Map<number, number[]>([
      [1, [0]],
      [2, [0, 1]],
    ])
    const ready = findReadySubtasks(3, completed, depsOf)
    expect(ready).toEqual([1])
  })

  it('returns empty array for circular dependencies', () => {
    const completed = new Set<number>()
    const depsOf = new Map<number, number[]>([
      [0, [1]],
      [1, [0]],
    ])
    const ready = findReadySubtasks(2, completed, depsOf)
    expect(ready).toEqual([])
  })
})
