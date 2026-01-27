/**
 * Tests for Delta9 Mission State Manager
 */

import { describe, it, expect, beforeEach, afterEach, vi } from 'vitest'
import { MissionState } from '../../src/mission/state.js'
import * as fs from 'node:fs'
import * as paths from '../../src/lib/paths.js'

// Mock file system and paths
vi.mock('node:fs', () => ({
  readFileSync: vi.fn(),
  writeFileSync: vi.fn(),
  existsSync: vi.fn(),
  mkdirSync: vi.fn(),
}))

vi.mock('../../src/lib/paths.js', () => ({
  getMissionPath: vi.fn((cwd: string) => `${cwd}/.delta9/mission.json`),
  getMissionMdPath: vi.fn((cwd: string) => `${cwd}/.delta9/mission.md`),
  ensureDelta9Dir: vi.fn(),
  missionExists: vi.fn(),
}))

vi.mock('../../src/lib/config.js', () => ({
  getBudgetLimit: vi.fn(() => 10.0),
}))

vi.mock('../../src/mission/history.js', () => ({
  appendHistory: vi.fn(),
}))

vi.mock('../../src/mission/markdown.js', () => ({
  generateMissionMarkdown: vi.fn(() => '# Mission'),
}))

vi.mock('../../src/schemas/mission.schema.js', () => ({
  validateMission: vi.fn((data) => data),
}))

describe('MissionState', () => {
  let state: MissionState
  const testCwd = '/test/project'

  beforeEach(() => {
    vi.clearAllMocks()
    state = new MissionState(testCwd)
  })

  afterEach(() => {
    vi.resetAllMocks()
  })

  describe('constructor', () => {
    it('creates state with cwd', () => {
      expect(state).toBeDefined()
      expect(state.getMission()).toBeNull()
    })
  })

  describe('create', () => {
    it('creates mission with description', () => {
      const mission = state.create('Build auth system')

      expect(mission).toBeDefined()
      expect(mission.id).toMatch(/^mission_/)
      expect(mission.description).toBe('Build auth system')
      expect(mission.status).toBe('planning')
      expect(mission.councilMode).toBe('standard')
      expect(mission.objectives).toEqual([])
    })

    it('creates mission with custom options', () => {
      const mission = state.create('Build auth', {
        councilMode: 'xhigh',
        complexity: 'high',
        budgetLimit: 50.0,
      })

      expect(mission.councilMode).toBe('xhigh')
      expect(mission.complexity).toBe('high')
      expect(mission.budget.limit).toBe(50.0)
    })

    it('saves mission after creation', () => {
      state.create('Test mission')
      expect(fs.writeFileSync).toHaveBeenCalled()
    })

    it('sets initial budget values', () => {
      const mission = state.create('Test mission')

      expect(mission.budget.spent).toBe(0)
      expect(mission.budget.breakdown).toEqual({
        council: 0,
        operators: 0,
        validators: 0,
        support: 0,
      })
    })
  })

  describe('load', () => {
    it('returns null when no mission exists', () => {
      vi.mocked(paths.missionExists).mockReturnValue(false)

      const mission = state.load()

      expect(mission).toBeNull()
    })

    it('loads existing mission', async () => {
      const missionData = {
        id: 'mission_test',
        description: 'Test',
        status: 'in_progress',
        objectives: [],
        councilMode: 'standard',
        complexity: 'medium',
        budget: { limit: 10, spent: 0, breakdown: { council: 0, operators: 0, validators: 0, support: 0 } },
        createdAt: '2026-01-01T00:00:00.000Z',
        updatedAt: '2026-01-01T00:00:00.000Z',
      }
      vi.mocked(paths.missionExists).mockReturnValue(true)
      vi.mocked(fs.readFileSync).mockReturnValue(JSON.stringify(missionData))

      // Import the mock module and set up return value
      const missionSchema = await import('../../src/schemas/mission.schema.js')
      vi.mocked(missionSchema.validateMission).mockReturnValue(missionData as any)

      const mission = state.load()

      expect(mission).toBeDefined()
      expect(mission?.id).toBe('mission_test')
    })

    it('returns null on parse error', () => {
      vi.mocked(paths.missionExists).mockReturnValue(true)
      vi.mocked(fs.readFileSync).mockReturnValue('invalid json')

      const consoleSpy = vi.spyOn(console, 'error').mockImplementation(() => {})
      const mission = state.load()

      expect(mission).toBeNull()
      consoleSpy.mockRestore()
    })
  })

  describe('save', () => {
    it('does nothing when no mission', () => {
      state.save()
      expect(fs.writeFileSync).not.toHaveBeenCalled()
    })

    it('saves mission to disk', () => {
      state.create('Test mission')
      vi.mocked(fs.writeFileSync).mockClear()

      state.save()

      expect(paths.ensureDelta9Dir).toHaveBeenCalled()
      expect(fs.writeFileSync).toHaveBeenCalledTimes(2) // mission.json and mission.md
    })
  })

  describe('clear', () => {
    it('clears mission state', () => {
      state.create('Test mission')
      expect(state.getMission()).not.toBeNull()

      state.clear()
      expect(state.getMission()).toBeNull()
    })
  })

  describe('getMission', () => {
    it('returns null when no mission', () => {
      expect(state.getMission()).toBeNull()
    })

    it('returns current mission', () => {
      const created = state.create('Test mission')
      expect(state.getMission()).toBe(created)
    })
  })

  describe('addObjective', () => {
    it('throws when no mission', () => {
      expect(() => state.addObjective({ description: 'Test' })).toThrow(
        'No active mission'
      )
    })

    it('adds objective to mission', () => {
      state.create('Test mission')
      const objective = state.addObjective({ description: 'First objective' })

      expect(objective.id).toMatch(/^obj_/)
      expect(objective.description).toBe('First objective')
      expect(objective.status).toBe('pending')
      expect(objective.tasks).toEqual([])
    })

    it('saves after adding objective', () => {
      state.create('Test mission')
      vi.mocked(fs.writeFileSync).mockClear()

      state.addObjective({ description: 'Test' })

      expect(fs.writeFileSync).toHaveBeenCalled()
    })
  })

  describe('addTask', () => {
    it('throws when objective not found', () => {
      state.create('Test mission')
      expect(() =>
        state.addTask('invalid_obj', {
          description: 'Test task',
          acceptanceCriteria: [],
        })
      ).toThrow('Objective invalid_obj not found')
    })

    it('adds task to objective', () => {
      state.create('Test mission')
      const objective = state.addObjective({ description: 'Test objective' })
      const task = state.addTask(objective.id, {
        description: 'Test task',
        acceptanceCriteria: ['Criteria 1', 'Criteria 2'],
      })

      expect(task.id).toMatch(/^task_/)
      expect(task.description).toBe('Test task')
      expect(task.status).toBe('pending')
      expect(task.attempts).toBe(0)
      expect(task.acceptanceCriteria).toEqual(['Criteria 1', 'Criteria 2'])
    })
  })

  describe('getTask', () => {
    it('returns null when no mission', () => {
      expect(state.getTask('task_123')).toBeNull()
    })

    it('finds task across objectives', () => {
      state.create('Test mission')
      const obj = state.addObjective({ description: 'Obj' })
      const task = state.addTask(obj.id, {
        description: 'Task',
        acceptanceCriteria: [],
      })

      expect(state.getTask(task.id)).toBe(task)
    })

    it('returns null for non-existent task', () => {
      state.create('Test mission')
      state.addObjective({ description: 'Obj' })

      expect(state.getTask('nonexistent')).toBeNull()
    })
  })

  describe('updateTask', () => {
    it('updates task fields', () => {
      state.create('Test mission')
      const obj = state.addObjective({ description: 'Obj' })
      const task = state.addTask(obj.id, {
        description: 'Task',
        acceptanceCriteria: [],
      })

      state.updateTask(task.id, { status: 'in_progress' })

      expect(state.getTask(task.id)?.status).toBe('in_progress')
    })
  })

  describe('startTask', () => {
    it('starts a pending task', () => {
      state.create('Test mission')
      const obj = state.addObjective({ description: 'Obj' })
      const task = state.addTask(obj.id, {
        description: 'Task',
        acceptanceCriteria: [],
      })

      state.startTask(task.id, 'operator')

      const updated = state.getTask(task.id)
      expect(updated?.status).toBe('in_progress')
      expect(updated?.assignedTo).toBe('operator')
      expect(updated?.startedAt).toBeDefined()
      expect(updated?.attempts).toBe(1)
    })

    it('does nothing for non-pending task', () => {
      state.create('Test mission')
      const obj = state.addObjective({ description: 'Obj' })
      const task = state.addTask(obj.id, {
        description: 'Task',
        acceptanceCriteria: [],
      })
      state.updateTask(task.id, { status: 'completed' })

      state.startTask(task.id, 'operator')

      expect(state.getTask(task.id)?.status).toBe('completed')
    })
  })

  describe('failTask', () => {
    it('marks task as failed', () => {
      state.create('Test mission')
      const obj = state.addObjective({ description: 'Obj' })
      const task = state.addTask(obj.id, {
        description: 'Task',
        acceptanceCriteria: [],
      })

      state.failTask(task.id, 'Task failed due to error')

      const updated = state.getTask(task.id)
      expect(updated?.status).toBe('failed')
      expect(updated?.error).toBe('Task failed due to error')
      expect(updated?.completedAt).toBeDefined()
    })
  })

  describe('getProgress', () => {
    it('returns zeros when no mission', () => {
      const progress = state.getProgress()

      expect(progress.total).toBe(0)
      expect(progress.completed).toBe(0)
      expect(progress.percentage).toBe(0)
    })

    it('calculates progress correctly', () => {
      state.create('Test mission')
      const obj = state.addObjective({ description: 'Obj' })
      state.addTask(obj.id, { description: 'Task 1', acceptanceCriteria: [] })
      state.addTask(obj.id, { description: 'Task 2', acceptanceCriteria: [] })
      const task3 = state.addTask(obj.id, {
        description: 'Task 3',
        acceptanceCriteria: [],
      })
      const task4 = state.addTask(obj.id, {
        description: 'Task 4',
        acceptanceCriteria: [],
      })

      state.updateTask(task3.id, { status: 'completed' })
      state.updateTask(task4.id, { status: 'completed' })

      const progress = state.getProgress()
      expect(progress.total).toBe(4)
      expect(progress.completed).toBe(2)
      expect(progress.pending).toBe(2)
      expect(progress.percentage).toBe(50)
    })
  })

  describe('getBlockedTasks', () => {
    it('returns empty when no mission', () => {
      expect(state.getBlockedTasks()).toEqual([])
    })

    it('identifies blocked tasks', () => {
      state.create('Test mission')
      const obj = state.addObjective({ description: 'Obj' })
      const task1 = state.addTask(obj.id, {
        description: 'Task 1',
        acceptanceCriteria: [],
      })
      state.addTask(obj.id, {
        description: 'Task 2',
        acceptanceCriteria: [],
        dependencies: [task1.id],
      })

      const blocked = state.getBlockedTasks()
      expect(blocked.length).toBe(1)
      expect(blocked[0].description).toBe('Task 2')
    })
  })

  describe('getReadyTasks', () => {
    it('returns empty when no mission', () => {
      expect(state.getReadyTasks()).toEqual([])
    })

    it('identifies ready tasks', () => {
      state.create('Test mission')
      const obj = state.addObjective({ description: 'Obj' })
      state.addTask(obj.id, { description: 'Ready task', acceptanceCriteria: [] })

      const ready = state.getReadyTasks()
      expect(ready.length).toBe(1)
      expect(ready[0].description).toBe('Ready task')
    })
  })

  describe('budget', () => {
    it('adds cost to budget', () => {
      state.create('Test mission')

      state.addCost(1.5, 'operators')

      const mission = state.getMission()
      expect(mission?.budget.spent).toBe(1.5)
      expect(mission?.budget.breakdown.operators).toBe(1.5)
    })

    it('tracks costs by category', () => {
      state.create('Test mission')

      state.addCost(1.0, 'council')
      state.addCost(0.5, 'validators')
      state.addCost(2.0, 'operators')

      const mission = state.getMission()
      expect(mission?.budget.breakdown.council).toBe(1.0)
      expect(mission?.budget.breakdown.validators).toBe(0.5)
      expect(mission?.budget.breakdown.operators).toBe(2.0)
      expect(mission?.budget.spent).toBe(3.5)
    })

    it('returns budget status', () => {
      state.create('Test mission', { budgetLimit: 10.0 })
      state.addCost(3.0, 'operators')

      const status = state.getBudgetStatus()

      expect(status.spent).toBe(3.0)
      expect(status.limit).toBe(10.0)
      expect(status.percentage).toBe(30)
      expect(status.remaining).toBe(7.0)
    })
  })

  describe('mission status transitions', () => {
    it('approves mission', () => {
      state.create('Test mission')
      expect(state.getMission()?.status).toBe('planning')

      state.approveMission()

      expect(state.getMission()?.status).toBe('approved')
      expect(state.getMission()?.approvedAt).toBeDefined()
    })

    it('starts mission', () => {
      state.create('Test mission')
      state.approveMission()

      state.startMission()

      expect(state.getMission()?.status).toBe('in_progress')
    })

    it('pauses mission', () => {
      state.create('Test mission')
      state.approveMission()
      state.startMission()

      state.pauseMission()

      expect(state.getMission()?.status).toBe('paused')
    })

    it('aborts mission', () => {
      state.create('Test mission')

      state.abortMission()

      expect(state.getMission()?.status).toBe('aborted')
    })
  })

  describe('validate', () => {
    it('returns error when no mission loaded', () => {
      const result = state.validate()

      expect(result.isValid).toBe(false)
      expect(result.errors).toHaveLength(1)
      expect(result.errors[0].code).toBe('NO_MISSION')
    })

    it('validates a valid mission', () => {
      state.create('Test mission')
      const objective = state.addObjective({ description: 'Test objective' })
      state.addTask(objective.id, {
        description: 'Test task',
        acceptanceCriteria: ['Works correctly'],
      })

      const result = state.validate()

      expect(result.isValid).toBe(true)
      expect(result.errors).toHaveLength(0)
      expect(result.summary).toBe('Mission state is valid')
    })

    it('detects duplicate IDs', () => {
      state.create('Test mission')
      const objective = state.addObjective({ description: 'Test objective' })
      const task1 = state.addTask(objective.id, {
        description: 'Task 1',
        acceptanceCriteria: ['Done'],
      })

      // Manually create duplicate ID (normally prevented by nanoid)
      const mission = state.getMission()!
      mission.objectives[0].tasks.push({
        id: task1.id, // Duplicate!
        description: 'Task 2',
        status: 'pending',
        attempts: 0,
        acceptanceCriteria: ['Done'],
      })

      const result = state.validate()

      expect(result.isValid).toBe(false)
      expect(result.errors.some((e) => e.code === 'DUPLICATE_ID')).toBe(true)
    })

    it('detects invalid task dependencies', () => {
      state.create('Test mission')
      const objective = state.addObjective({ description: 'Test objective' })

      // Add task with non-existent dependency
      const mission = state.getMission()!
      mission.objectives[0].tasks.push({
        id: 'task_test',
        description: 'Test task',
        status: 'pending',
        attempts: 0,
        acceptanceCriteria: ['Done'],
        dependencies: ['non_existent_task'],
      })

      const result = state.validate()

      expect(result.isValid).toBe(false)
      expect(result.errors.some((e) => e.code === 'INVALID_DEPENDENCY')).toBe(true)
    })

    it('detects self-dependency', () => {
      state.create('Test mission')
      const objective = state.addObjective({ description: 'Test objective' })

      // Add task with self-dependency
      const mission = state.getMission()!
      mission.objectives[0].tasks.push({
        id: 'task_self',
        description: 'Test task',
        status: 'pending',
        attempts: 0,
        acceptanceCriteria: ['Done'],
        dependencies: ['task_self'],
      })

      const result = state.validate()

      expect(result.isValid).toBe(false)
      expect(result.errors.some((e) => e.code === 'SELF_DEPENDENCY')).toBe(true)
    })

    it('detects budget mismatch', () => {
      state.create('Test mission')

      // Manually create budget mismatch
      const mission = state.getMission()!
      mission.budget.spent = 5.0
      mission.budget.breakdown = {
        council: 1.0,
        operators: 1.0, // Sum is 4.0, not 5.0
        validators: 1.0,
        support: 1.0,
      }

      const result = state.validate()

      expect(result.isValid).toBe(false)
      expect(result.errors.some((e) => e.code === 'BUDGET_MISMATCH')).toBe(true)
    })

    it('warns about over budget', () => {
      state.create('Test mission', { budgetLimit: 5.0 })

      // Go over budget
      state.addCost(6.0, 'operators')

      const result = state.validate()

      // Over budget is a warning, not an error
      expect(result.warnings.some((w) => w.code === 'OVER_BUDGET')).toBe(true)
    })

    it('detects invalid currentObjective index', () => {
      state.create('Test mission')
      state.addObjective({ description: 'Objective 1' })

      // Set invalid index
      const mission = state.getMission()!
      mission.currentObjective = 5

      const result = state.validate()

      expect(result.isValid).toBe(false)
      expect(result.errors.some((e) => e.code === 'INVALID_INDEX')).toBe(true)
    })

    it('warns about empty objectives', () => {
      state.create('Test mission')
      state.addObjective({ description: 'Empty objective' })

      const result = state.validate()

      // Empty objective is a warning
      expect(result.warnings.some((w) => w.code === 'EMPTY_OBJECTIVE')).toBe(true)
    })

    it('warns about missing acceptance criteria', () => {
      state.create('Test mission')
      const objective = state.addObjective({ description: 'Test objective' })

      // Add task without acceptance criteria
      const mission = state.getMission()!
      mission.objectives[0].tasks.push({
        id: 'task_no_criteria',
        description: 'Test task',
        status: 'pending',
        attempts: 0,
        acceptanceCriteria: [],
      })

      const result = state.validate()

      expect(result.warnings.some((w) => w.code === 'MISSING_CRITERIA')).toBe(true)
    })

    it('warns about completed task without completedAt', () => {
      state.create('Test mission')
      const objective = state.addObjective({ description: 'Test objective' })

      // Add completed task without completedAt
      const mission = state.getMission()!
      mission.objectives[0].tasks.push({
        id: 'task_completed',
        description: 'Test task',
        status: 'completed',
        attempts: 1,
        acceptanceCriteria: ['Done'],
        // Missing completedAt
      })

      const result = state.validate()

      expect(result.warnings.some((w) => w.code === 'MISSING_TIMESTAMP')).toBe(true)
    })

    it('generates correct summary', () => {
      state.create('Test mission')

      // Create multiple issues
      const mission = state.getMission()!
      mission.currentObjective = 99
      mission.budget.spent = -1

      const result = state.validate()

      expect(result.isValid).toBe(false)
      expect(result.summary).toContain('error')
    })
  })
})
