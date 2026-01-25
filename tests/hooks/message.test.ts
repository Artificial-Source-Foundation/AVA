/**
 * Tests for Delta9 Message Hooks
 */

import { describe, it, expect, beforeEach, vi } from 'vitest'
import {
  createMessageHooks,
  getMessageStats,
  clearMessageStats,
  type MessageBeforeInput,
  type MessageBeforeOutput,
  type MessageAfterInput,
  type MessageAfterOutput,
} from '../../src/hooks/message.js'
import type { MissionState } from '../../src/mission/state.js'
import type { Mission } from '../../src/types/mission.js'

// Mock dependencies
vi.mock('../../src/learning/insights.js', () => ({
  generateCoordinatorInsights: vi.fn(() => []),
  formatInsightsForPrompt: vi.fn(() => ''),
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
    getTask: (taskId: string) => {
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

describe('MessageHooks', () => {
  const cwd = '/test/project'
  const log = vi.fn()

  beforeEach(() => {
    vi.clearAllMocks()
    clearMessageStats()
    log.mockClear()
  })

  describe('createMessageHooks', () => {
    it('should create hooks with required methods', () => {
      const state = createMockState()
      const hooks = createMessageHooks({ state, cwd, log })

      expect(hooks['message.before']).toBeDefined()
      expect(hooks['message.after']).toBeDefined()
      expect(typeof hooks['message.before']).toBe('function')
      expect(typeof hooks['message.after']).toBe('function')
    })
  })

  describe('message.before hook', () => {
    it('should track message counts', async () => {
      const state = createMockState()
      const hooks = createMessageHooks({ state, cwd, log })

      const input: MessageBeforeInput = {
        sessionID: 'session-1',
        role: 'user',
        parts: [{ type: 'text', text: 'Hello' }],
      }
      const output: MessageBeforeOutput = { parts: [] }

      await hooks['message.before'](input, output)

      const stats = getMessageStats('session-1')
      expect(stats.user).toBe(1)
      expect(stats.assistant).toBe(0)
      expect(stats.total).toBe(1)
    })

    it('should track assistant messages separately', async () => {
      const state = createMockState()
      const hooks = createMessageHooks({ state, cwd, log })

      const userInput: MessageBeforeInput = {
        sessionID: 'session-1',
        role: 'user',
        parts: [{ type: 'text', text: 'Hello' }],
      }
      const assistantInput: MessageBeforeInput = {
        sessionID: 'session-1',
        role: 'assistant',
        parts: [{ type: 'text', text: 'Hi there!' }],
      }
      const output: MessageBeforeOutput = { parts: [] }

      await hooks['message.before'](userInput, output)
      await hooks['message.before'](assistantInput, output)

      const stats = getMessageStats('session-1')
      expect(stats.user).toBe(1)
      expect(stats.assistant).toBe(1)
      expect(stats.total).toBe(2)
    })

    it('should track multiple sessions independently', async () => {
      const state = createMockState()
      const hooks = createMessageHooks({ state, cwd, log })

      const input1: MessageBeforeInput = {
        sessionID: 'session-1',
        role: 'user',
        parts: [{ type: 'text', text: 'Hello' }],
      }
      const input2: MessageBeforeInput = {
        sessionID: 'session-2',
        role: 'user',
        parts: [{ type: 'text', text: 'Hi' }],
      }
      const output: MessageBeforeOutput = { parts: [] }

      await hooks['message.before'](input1, output)
      await hooks['message.before'](input1, output)
      await hooks['message.before'](input2, output)

      const stats1 = getMessageStats('session-1')
      const stats2 = getMessageStats('session-2')

      expect(stats1.user).toBe(2)
      expect(stats2.user).toBe(1)
    })

    it('should inject mission context for assistant messages when mission is in_progress', async () => {
      const mission: Mission = {
        id: 'mission-1',
        description: 'Build a feature',
        status: 'in_progress',
        complexity: 'medium',
        councilMode: 'standard',
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
        currentObjective: 0,
        budget: { limit: 10, spent: 0, breakdown: { council: 0, operators: 0, validators: 0, support: 0 } },
        createdAt: new Date().toISOString(),
        updatedAt: new Date().toISOString(),
      }

      const state = createMockState(mission)
      const hooks = createMessageHooks({ state, cwd, log })

      const input: MessageBeforeInput = {
        sessionID: 'session-1',
        role: 'assistant',
        parts: [{ type: 'text', text: 'Working on it' }],
      }
      const output: MessageBeforeOutput = { parts: [] }

      await hooks['message.before'](input, output)

      expect(output.systemAppend).toBeDefined()
      expect(output.systemAppend).toContain('mission-context')
      expect(output.systemAppend).toContain('Build a feature')
      expect(output.systemAppend).toContain('Create component')
    })

    it('should not inject context for user messages', async () => {
      const mission: Mission = {
        id: 'mission-1',
        description: 'Build a feature',
        status: 'in_progress',
        complexity: 'medium',
        councilMode: 'standard',
        objectives: [],
        currentObjective: 0,
        budget: { limit: 10, spent: 0, breakdown: { council: 0, operators: 0, validators: 0, support: 0 } },
        createdAt: new Date().toISOString(),
        updatedAt: new Date().toISOString(),
      }

      const state = createMockState(mission)
      const hooks = createMessageHooks({ state, cwd, log })

      const input: MessageBeforeInput = {
        sessionID: 'session-1',
        role: 'user',
        parts: [{ type: 'text', text: 'Hello' }],
      }
      const output: MessageBeforeOutput = { parts: [] }

      await hooks['message.before'](input, output)

      // systemAppend should not be set for user messages
      expect(output.systemAppend).toBeUndefined()
    })
  })

  describe('message.after hook', () => {
    it('should set continue to true by default', async () => {
      const state = createMockState()
      const hooks = createMessageHooks({ state, cwd, log })

      const input: MessageAfterInput = {
        sessionID: 'session-1',
        role: 'assistant',
        parts: [{ type: 'text', text: 'Done!' }],
        duration: 100,
      }
      const output: MessageAfterOutput = { continue: false }

      await hooks['message.after'](input, output)

      expect(output.continue).toBe(true)
    })

    it('should log message processing', async () => {
      const state = createMockState()
      const hooks = createMessageHooks({ state, cwd, log })

      const input: MessageAfterInput = {
        sessionID: 'session-1',
        role: 'assistant',
        parts: [{ type: 'text', text: 'Done!' }],
        duration: 100,
      }
      const output: MessageAfterOutput = { continue: false }

      await hooks['message.after'](input, output)

      expect(log).toHaveBeenCalledWith(
        'debug',
        'Message processed: assistant',
        expect.objectContaining({
          sessionId: 'session-1',
          partCount: 1,
          duration: 100,
        })
      )
    })
  })

  describe('getMessageStats', () => {
    it('should return empty stats for unknown session', () => {
      const stats = getMessageStats('unknown')
      expect(stats).toEqual({ user: 0, assistant: 0, total: 0 })
    })
  })

  describe('clearMessageStats', () => {
    it('should clear all message stats', async () => {
      const state = createMockState()
      const hooks = createMessageHooks({ state, cwd, log })

      const input: MessageBeforeInput = {
        sessionID: 'session-1',
        role: 'user',
        parts: [{ type: 'text', text: 'Hello' }],
      }
      const output: MessageBeforeOutput = { parts: [] }

      await hooks['message.before'](input, output)
      expect(getMessageStats('session-1').total).toBe(1)

      clearMessageStats()
      expect(getMessageStats('session-1').total).toBe(0)
    })
  })
})
