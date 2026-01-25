/**
 * Tests for Delta9 Compaction Hooks
 */

import { describe, it, expect, beforeEach, vi } from 'vitest'
import {
  createCompactionHooks,
  getCompactionHistory,
  clearCompactionHistory,
  type PreCompactInput,
  type PreCompactOutput,
  type PostCompactInput,
  type PostCompactOutput,
} from '../../src/hooks/compaction.js'
import type { MissionState } from '../../src/mission/state.js'
import type { Mission, Task } from '../../src/types/mission.js'

// Mock dependencies
vi.mock('../../src/mission/history.js', () => ({
  appendHistory: vi.fn(),
}))

vi.mock('../../src/events/store.js', () => ({
  getEventStore: vi.fn(() => ({
    append: vi.fn(),
  })),
}))

// Create mock mission state
function createMockState(mission: Mission | null = null): MissionState {
  return {
    getMission: () => mission,
    getProgress: () => (mission ? 50 : 0),
    getTask: (taskId: string): Task | null => {
      if (!mission) return null
      for (const obj of mission.objectives) {
        for (const task of obj.tasks) {
          if (task.id === taskId) return task
        }
      }
      return null
    },
    load: () => mission,
  } as unknown as MissionState
}

// Helper to create a complete mission
function createMission(overrides: Partial<Mission> = {}): Mission {
  return {
    id: 'mission-1',
    description: 'Test mission',
    status: 'planning',
    complexity: 'medium',
    councilMode: 'standard',
    objectives: [],
    currentObjective: 0,
    budget: { limit: 10, spent: 0, breakdown: { council: 0, operators: 0, validators: 0, support: 0 } },
    createdAt: new Date().toISOString(),
    updatedAt: new Date().toISOString(),
    ...overrides,
  }
}

describe('CompactionHooks', () => {
  const cwd = '/test/project'
  const log = vi.fn()

  beforeEach(() => {
    vi.clearAllMocks()
    clearCompactionHistory()
    log.mockClear()
  })

  describe('createCompactionHooks', () => {
    it('should create hooks with required methods', () => {
      const state = createMockState()
      const hooks = createCompactionHooks({ state, cwd, log })

      expect(hooks['compact.before']).toBeDefined()
      expect(hooks['compact.after']).toBeDefined()
      expect(typeof hooks['compact.before']).toBe('function')
      expect(typeof hooks['compact.after']).toBe('function')
    })
  })

  describe('compact.before hook', () => {
    it('should log compaction start', async () => {
      const state = createMockState()
      const hooks = createCompactionHooks({ state, cwd, log })

      const input: PreCompactInput = {
        sessionID: 'session-1',
        reason: 'context_limit',
        contextTokens: 100000,
      }
      const output: PreCompactOutput = {
        preservedState: {},
        criticalContext: '',
      }

      await hooks['compact.before'](input, output)

      expect(log).toHaveBeenCalledWith(
        'info',
        'Compaction starting',
        expect.objectContaining({
          sessionId: 'session-1',
          reason: 'context_limit',
          contextTokens: 100000,
        })
      )
    })

    it('should preserve mission state', async () => {
      const mission = createMission({
        description: 'Build a feature',
        status: 'in_progress',
        objectives: [
          {
            id: 'obj-1',
            description: 'Implement feature',
            status: 'in_progress',
            tasks: [
              {
                id: 'task-1',
                description: 'Create component',
                status: 'in_progress',
                attempts: 1,
                acceptanceCriteria: ['Works correctly'],
              },
            ],
          },
        ],
      })
      const state = createMockState(mission)
      const hooks = createCompactionHooks({ state, cwd, log })

      const input: PreCompactInput = {
        sessionID: 'session-1',
        reason: 'context_limit',
        contextTokens: 100000,
      }
      const output: PreCompactOutput = {
        preservedState: {},
        criticalContext: '',
      }

      await hooks['compact.before'](input, output)

      expect(output.preservedState).toMatchObject({
        missionId: mission.id,
        missionStatus: 'in_progress',
        inProgressTaskId: 'task-1',
      })
      expect(output.preservedState).toHaveProperty('progress')
      expect(output.preservedState).toHaveProperty('timestamp')
    })

    it('should build critical context', async () => {
      const mission = createMission({
        description: 'Build authentication',
        status: 'in_progress',
        objectives: [
          {
            id: 'obj-1',
            description: 'Implement login',
            status: 'in_progress',
            tasks: [
              {
                id: 'task-1',
                description: 'Create login form',
                status: 'in_progress',
                attempts: 1,
                acceptanceCriteria: ['Form validates', 'Form submits'],
              },
            ],
          },
        ],
      })
      const state = createMockState(mission)
      const hooks = createCompactionHooks({ state, cwd, log })

      const input: PreCompactInput = {
        sessionID: 'session-1',
        reason: 'context_limit',
        contextTokens: 100000,
      }
      const output: PreCompactOutput = {
        preservedState: {},
        criticalContext: '',
      }

      await hooks['compact.before'](input, output)

      expect(output.criticalContext).toContain('Mission Summary')
      expect(output.criticalContext).toContain('Build authentication')
      expect(output.criticalContext).toContain('Current Objective')
      expect(output.criticalContext).toContain('Implement login')
      expect(output.criticalContext).toContain('Current Task')
      expect(output.criticalContext).toContain('Create login form')
    })

    it('should handle no mission gracefully', async () => {
      const state = createMockState(null)
      const hooks = createCompactionHooks({ state, cwd, log })

      const input: PreCompactInput = {
        sessionID: 'session-1',
        reason: 'manual',
        contextTokens: 50000,
      }
      const output: PreCompactOutput = {
        preservedState: {},
        criticalContext: '',
      }

      await hooks['compact.before'](input, output)

      expect(output.preservedState).toMatchObject({
        missionId: undefined,
        missionStatus: undefined,
      })
      expect(output.criticalContext).toBe('')
    })
  })

  describe('compact.after hook', () => {
    it('should record compaction in history', async () => {
      const state = createMockState(null)
      const hooks = createCompactionHooks({ state, cwd, log })

      const input: PostCompactInput = {
        sessionID: 'session-1',
        preservedState: {},
        tokensBefore: 100000,
        tokensAfter: 20000,
      }
      const output: PostCompactOutput = {}

      await hooks['compact.after'](input, output)

      const history = getCompactionHistory('session-1')
      expect(history).toHaveLength(1)
      expect(history[0]).toMatchObject({
        tokensBefore: 100000,
        tokensAfter: 20000,
        reason: 'compaction',
      })
      expect(history[0]).toHaveProperty('timestamp')
    })

    it('should log compaction completion', async () => {
      const state = createMockState(null)
      const hooks = createCompactionHooks({ state, cwd, log })

      const input: PostCompactInput = {
        sessionID: 'session-1',
        preservedState: {},
        tokensBefore: 100000,
        tokensAfter: 20000,
      }
      const output: PostCompactOutput = {}

      await hooks['compact.after'](input, output)

      expect(log).toHaveBeenCalledWith(
        'info',
        'Compaction completed',
        expect.objectContaining({
          sessionId: 'session-1',
          tokensBefore: 100000,
          tokensAfter: 20000,
          tokensSaved: 80000,
        })
      )
    })

    it('should build continuation prompt with mission info', async () => {
      const mission = createMission({
        description: 'Build a feature',
        status: 'in_progress',
        objectives: [
          {
            id: 'obj-1',
            description: 'Implement feature',
            status: 'in_progress',
            tasks: [
              {
                id: 'task-1',
                description: 'Create component',
                status: 'pending',
                attempts: 0,
                acceptanceCriteria: ['Works'],
              },
              {
                id: 'task-2',
                description: 'Add styling',
                status: 'in_progress',
                attempts: 1,
                acceptanceCriteria: ['Looks good'],
              },
            ],
          },
        ],
      })
      const state = createMockState(mission)
      const hooks = createCompactionHooks({ state, cwd, log })

      const input: PostCompactInput = {
        sessionID: 'session-1',
        preservedState: { inProgressTaskId: 'task-2' },
        tokensBefore: 100000,
        tokensAfter: 20000,
      }
      const output: PostCompactOutput = {}

      await hooks['compact.after'](input, output)

      expect(output.continuationPrompt).toContain('Context Restored')
      expect(output.continuationPrompt).toContain('Build a feature')
      expect(output.continuationPrompt).toContain('Incomplete Tasks')
      expect(output.continuationPrompt).toContain('Add styling')
    })

    it('should inject compaction message', async () => {
      const mission = createMission({ description: 'Test mission' })
      const state = createMockState(mission)
      const hooks = createCompactionHooks({ state, cwd, log })

      const input: PostCompactInput = {
        sessionID: 'session-1',
        preservedState: {},
        tokensBefore: 100000,
        tokensAfter: 20000,
      }
      const output: PostCompactOutput = {}

      await hooks['compact.after'](input, output)

      expect(output.injectedMessage).toContain('Context compacted')
      expect(output.injectedMessage).toContain('80000 tokens freed')
      expect(output.injectedMessage).toContain('Mission state restored')
    })

    it('should handle no mission gracefully', async () => {
      const state = createMockState(null)
      const hooks = createCompactionHooks({ state, cwd, log })

      const input: PostCompactInput = {
        sessionID: 'session-1',
        preservedState: {},
        tokensBefore: 100000,
        tokensAfter: 20000,
      }
      const output: PostCompactOutput = {}

      await hooks['compact.after'](input, output)

      expect(log).toHaveBeenCalledWith('debug', 'No mission to restore after compaction')
      expect(output.continuationPrompt).toBeUndefined()
    })
  })

  describe('getCompactionHistory', () => {
    it('should return empty array for unknown session', () => {
      const history = getCompactionHistory('unknown')
      expect(history).toEqual([])
    })

    it('should track multiple compactions for same session', async () => {
      const state = createMockState(null)
      const hooks = createCompactionHooks({ state, cwd, log })

      const output: PostCompactOutput = {}

      await hooks['compact.after'](
        { sessionID: 'session-1', preservedState: {}, tokensBefore: 100000, tokensAfter: 20000 },
        output
      )
      await hooks['compact.after'](
        { sessionID: 'session-1', preservedState: {}, tokensBefore: 50000, tokensAfter: 10000 },
        output
      )

      const history = getCompactionHistory('session-1')
      expect(history).toHaveLength(2)
    })
  })

  describe('clearCompactionHistory', () => {
    it('should clear all history', async () => {
      const state = createMockState(null)
      const hooks = createCompactionHooks({ state, cwd, log })
      const output: PostCompactOutput = {}

      await hooks['compact.after'](
        { sessionID: 'session-1', preservedState: {}, tokensBefore: 100000, tokensAfter: 20000 },
        output
      )

      expect(getCompactionHistory('session-1')).toHaveLength(1)

      clearCompactionHistory()
      expect(getCompactionHistory('session-1')).toHaveLength(0)
    })
  })

  describe('Todo Continuation', () => {
    it('should extract incomplete todos sorted by status', async () => {
      const mission = createMission({
        description: 'Build feature',
        status: 'in_progress',
        objectives: [
          {
            id: 'obj-1',
            description: 'Feature',
            status: 'in_progress',
            tasks: [
              { id: 'task-1', description: 'Completed task', status: 'completed', attempts: 1, acceptanceCriteria: [] },
              { id: 'task-2', description: 'Pending task', status: 'pending', attempts: 0, acceptanceCriteria: [] },
              {
                id: 'task-3',
                description: 'In progress task',
                status: 'in_progress',
                attempts: 1,
                acceptanceCriteria: [],
              },
            ],
          },
        ],
      })
      const state = createMockState(mission)
      const hooks = createCompactionHooks({ state, cwd, log })

      const input: PostCompactInput = {
        sessionID: 'session-1',
        preservedState: {},
        tokensBefore: 100000,
        tokensAfter: 20000,
      }
      const output: PostCompactOutput = {}

      await hooks['compact.after'](input, output)

      // In progress should come before pending
      const prompt = output.continuationPrompt!
      const inProgressIndex = prompt.indexOf('In progress task')
      const pendingIndex = prompt.indexOf('Pending task')

      expect(inProgressIndex).toBeLessThan(pendingIndex)
      expect(prompt).not.toContain('Completed task')
    })

    it('should limit todos to top 10', async () => {
      const tasks = Array.from({ length: 20 }, (_, i) => ({
        id: `task-${i}`,
        description: `Task number ${i}`,
        status: 'pending' as const,
        attempts: 0,
        acceptanceCriteria: [],
      }))

      const mission = createMission({
        description: 'Many tasks',
        status: 'in_progress',
        objectives: [
          {
            id: 'obj-1',
            description: 'Many tasks',
            status: 'in_progress',
            tasks,
          },
        ],
      })
      const state = createMockState(mission)
      const hooks = createCompactionHooks({ state, cwd, log })

      const input: PostCompactInput = {
        sessionID: 'session-1',
        preservedState: {},
        tokensBefore: 100000,
        tokensAfter: 20000,
      }
      const output: PostCompactOutput = {}

      await hooks['compact.after'](input, output)

      expect(output.continuationPrompt).toContain('and 10 more tasks')
    })
  })
})
