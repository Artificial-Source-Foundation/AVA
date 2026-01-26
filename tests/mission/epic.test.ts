/**
 * Delta9 Epic Manager Tests
 */

import { describe, it, expect, beforeEach, afterEach } from 'vitest'
import { join } from 'node:path'
import { rmSync, existsSync } from 'node:fs'
import {
  EpicManager,
  getEpicManager,
  resetEpicManager,
  type Epic,
} from '../../src/mission/epic.js'

// =============================================================================
// Test Helpers
// =============================================================================

const testBaseDir = join(process.cwd(), '.test-epic-' + Date.now())

// =============================================================================
// Epic Manager Tests
// =============================================================================

describe('EpicManager', () => {
  let manager: EpicManager

  beforeEach(() => {
    resetEpicManager()
    manager = new EpicManager({ baseDir: testBaseDir })
  })

  afterEach(() => {
    manager.destroy()
    if (existsSync(testBaseDir)) {
      rmSync(testBaseDir, { recursive: true })
    }
  })

  describe('create()', () => {
    it('should create an epic with required fields', () => {
      const epic = manager.create({
        title: 'User Authentication',
        description: 'Implement complete auth system',
      })

      expect(epic.id).toMatch(/^epic-/)
      expect(epic.title).toBe('User Authentication')
      expect(epic.description).toBe('Implement complete auth system')
      expect(epic.status).toBe('planning')
      expect(epic.priority).toBe('normal')
      expect(epic.tasks).toHaveLength(0)
      expect(epic.objectives).toHaveLength(0)
    })

    it('should create an epic with optional fields', () => {
      const epic = manager.create({
        title: 'Critical Feature',
        description: 'Must be done ASAP',
        priority: 'critical',
        acceptanceCriteria: ['Tests pass', 'Code reviewed'],
        missionId: 'mission-123',
        labels: ['auth', 'security'],
      })

      expect(epic.priority).toBe('critical')
      expect(epic.acceptanceCriteria).toEqual(['Tests pass', 'Code reviewed'])
      expect(epic.missionId).toBe('mission-123')
      expect(epic.labels).toEqual(['auth', 'security'])
    })
  })

  describe('get()', () => {
    it('should get an epic by ID', () => {
      const created = manager.create({
        title: 'Test Epic',
        description: 'Description',
      })

      const retrieved = manager.get(created.id)
      expect(retrieved).toEqual(created)
    })

    it('should return undefined for non-existent ID', () => {
      const result = manager.get('non-existent-id')
      expect(result).toBeUndefined()
    })
  })

  describe('list()', () => {
    beforeEach(() => {
      manager.create({ title: 'Epic 1', description: 'Desc 1', priority: 'high' })
      manager.create({ title: 'Epic 2', description: 'Desc 2', priority: 'low' })
      manager.create({ title: 'Epic 3', description: 'Desc 3', priority: 'high' })
    })

    it('should list all epics', () => {
      const epics = manager.list()
      expect(epics).toHaveLength(3)
    })

    it('should filter by priority', () => {
      const highPriority = manager.list({ priority: 'high' })
      expect(highPriority).toHaveLength(2)
    })

    it('should filter by status', () => {
      const planning = manager.list({ status: 'planning' })
      expect(planning).toHaveLength(3)
    })
  })

  describe('update()', () => {
    it('should update epic fields', () => {
      const epic = manager.create({
        title: 'Original Title',
        description: 'Original description',
      })

      const updated = manager.update(epic.id, {
        title: 'Updated Title',
        priority: 'high',
      })

      expect(updated?.title).toBe('Updated Title')
      expect(updated?.priority).toBe('high')
      expect(updated?.description).toBe('Original description')
    })

    it('should return undefined for non-existent ID', () => {
      const result = manager.update('non-existent', { title: 'New' })
      expect(result).toBeUndefined()
    })
  })

  describe('delete()', () => {
    it('should delete an epic', () => {
      const epic = manager.create({
        title: 'To Delete',
        description: 'Will be deleted',
      })

      const deleted = manager.delete(epic.id)
      expect(deleted).toBe(true)
      expect(manager.get(epic.id)).toBeUndefined()
    })

    it('should return false for non-existent ID', () => {
      const deleted = manager.delete('non-existent')
      expect(deleted).toBe(false)
    })
  })

  describe('linkTasks()', () => {
    it('should link tasks to an epic', () => {
      const epic = manager.create({
        title: 'Epic with tasks',
        description: 'Has tasks',
      })

      const updated = manager.linkTasks(epic.id, ['task-1', 'task-2'])
      expect(updated?.tasks).toContain('task-1')
      expect(updated?.tasks).toContain('task-2')
    })

    it('should not duplicate task IDs', () => {
      const epic = manager.create({
        title: 'Epic',
        description: 'Desc',
      })

      manager.linkTasks(epic.id, ['task-1'])
      const updated = manager.linkTasks(epic.id, ['task-1', 'task-2'])

      expect(updated?.tasks.filter(t => t === 'task-1')).toHaveLength(1)
    })
  })

  describe('unlinkTasks()', () => {
    it('should unlink tasks from an epic', () => {
      const epic = manager.create({
        title: 'Epic',
        description: 'Desc',
      })

      manager.linkTasks(epic.id, ['task-1', 'task-2', 'task-3'])
      const updated = manager.unlinkTasks(epic.id, ['task-2'])

      expect(updated?.tasks).toContain('task-1')
      expect(updated?.tasks).not.toContain('task-2')
      expect(updated?.tasks).toContain('task-3')
    })
  })

  describe('linkObjectives()', () => {
    it('should link objectives to an epic', () => {
      const epic = manager.create({
        title: 'Epic',
        description: 'Desc',
      })

      const updated = manager.linkObjectives(epic.id, ['obj-1', 'obj-2'])
      expect(updated?.objectives).toContain('obj-1')
      expect(updated?.objectives).toContain('obj-2')
    })
  })

  describe('updateTaskStatus()', () => {
    it('should track task status', () => {
      const epic = manager.create({
        title: 'Epic',
        description: 'Desc',
      })

      manager.linkTasks(epic.id, ['task-1', 'task-2'])
      manager.updateTaskStatus('task-1', 'completed')
      manager.updateTaskStatus('task-2', 'in_progress')

      const progress = manager.getStatus(epic.id)
      expect(progress?.completedTasks).toBe(1)
      expect(progress?.inProgressTasks).toBe(1)
    })
  })

  describe('getStatus()', () => {
    it('should return progress for empty epic', () => {
      const epic = manager.create({
        title: 'Empty Epic',
        description: 'No tasks',
      })

      const progress = manager.getStatus(epic.id)
      expect(progress?.totalTasks).toBe(0)
      expect(progress?.percentage).toBe(0)
    })

    it('should calculate correct progress', () => {
      const epic = manager.create({
        title: 'Epic',
        description: 'Desc',
      })

      manager.linkTasks(epic.id, ['t1', 't2', 't3', 't4'])
      manager.updateTaskStatus('t1', 'completed')
      manager.updateTaskStatus('t2', 'completed')
      manager.updateTaskStatus('t3', 'in_progress')
      manager.updateTaskStatus('t4', 'pending')

      const progress = manager.getStatus(epic.id)
      expect(progress?.totalTasks).toBe(4)
      expect(progress?.completedTasks).toBe(2)
      expect(progress?.inProgressTasks).toBe(1)
      expect(progress?.pendingTasks).toBe(1)
      expect(progress?.percentage).toBe(50)
    })
  })

  describe('getBreakdown()', () => {
    it('should return breakdown by objective', () => {
      const epic = manager.create({
        title: 'Epic',
        description: 'Desc',
      })

      manager.linkTasks(epic.id, ['t1', 't2', 't3'])
      manager.updateTaskStatus('t1', 'completed', 'obj-1')
      manager.updateTaskStatus('t2', 'completed', 'obj-1')
      manager.updateTaskStatus('t3', 'pending', 'obj-2')

      const breakdown = manager.getBreakdown(epic.id)
      expect(breakdown?.byObjective).toHaveLength(2)

      const obj1 = breakdown?.byObjective.find(o => o.objectiveId === 'obj-1')
      expect(obj1?.taskCount).toBe(2)
      expect(obj1?.completedCount).toBe(2)
    })
  })

  describe('setStatus()', () => {
    it('should set epic status', () => {
      const epic = manager.create({
        title: 'Epic',
        description: 'Desc',
      })

      const updated = manager.setStatus(epic.id, 'in_progress')
      expect(updated?.status).toBe('in_progress')
    })

    it('should set completedAt when completed', () => {
      const epic = manager.create({
        title: 'Epic',
        description: 'Desc',
      })

      const updated = manager.setStatus(epic.id, 'completed')
      expect(updated?.completedAt).toBeDefined()
    })
  })

  describe('setGitBranch()', () => {
    it('should set git branch', () => {
      const epic = manager.create({
        title: 'Epic',
        description: 'Desc',
      })

      const updated = manager.setGitBranch(epic.id, 'epic/feature')
      expect(updated?.gitBranch).toBe('epic/feature')
    })
  })

  describe('suggestBranchName()', () => {
    it('should generate valid branch name', () => {
      const epic = manager.create({
        title: 'User Authentication & Authorization!',
        description: 'Desc',
      })

      const branch = manager.suggestBranchName(epic)
      expect(branch).toMatch(/^epic\//)
      expect(branch).not.toContain('&')
      expect(branch).not.toContain('!')
      expect(branch).not.toContain(' ')
    })
  })

  describe('persistence', () => {
    it('should persist and reload epics', () => {
      const epic = manager.create({
        title: 'Persisted Epic',
        description: 'Should survive reload',
        priority: 'high',
      })

      manager.linkTasks(epic.id, ['task-1'])

      // Create new manager instance (simulates reload)
      const manager2 = new EpicManager({ baseDir: testBaseDir })
      const reloaded = manager2.get(epic.id)

      expect(reloaded?.title).toBe('Persisted Epic')
      expect(reloaded?.priority).toBe('high')
      expect(reloaded?.tasks).toContain('task-1')

      manager2.destroy()
    })
  })
})

// =============================================================================
// Singleton Tests
// =============================================================================

describe('Singleton Pattern', () => {
  afterEach(() => {
    resetEpicManager()
  })

  it('should return same instance', () => {
    const manager1 = getEpicManager()
    const manager2 = getEpicManager()
    expect(manager1).toBe(manager2)
  })

  it('should reset instance', () => {
    const manager1 = getEpicManager()
    resetEpicManager()
    const manager2 = getEpicManager()
    expect(manager1).not.toBe(manager2)
  })
})
