/**
 * Conflict Detector Tests
 */

import { describe, it, expect } from 'vitest'
import {
  detectFileConflicts,
  checkTaskConflicts,
  formatConflicts,
} from '../../src/mission/conflict-detector.js'
import type { Task } from '../../src/types/mission.js'

const createTask = (id: string, files?: string[], filesReadonly?: string[], status: Task['status'] = 'pending'): Task => ({
  id,
  description: `Task ${id}`,
  status,
  attempts: 0,
  acceptanceCriteria: [],
  files,
  filesReadonly,
})

describe('detectFileConflicts', () => {
  describe('no conflicts', () => {
    it('should return no conflicts for empty task list', () => {
      const result = detectFileConflicts([])
      expect(result.hasConflicts).toBe(false)
      expect(result.conflicts).toHaveLength(0)
    })

    it('should return no conflicts when tasks claim different files', () => {
      const tasks = [
        createTask('task-1', ['src/a.ts']),
        createTask('task-2', ['src/b.ts']),
        createTask('task-3', ['src/c.ts']),
      ]
      const result = detectFileConflicts(tasks)
      expect(result.hasConflicts).toBe(false)
      expect(result.conflicts).toHaveLength(0)
    })

    it('should ignore completed tasks', () => {
      const tasks = [
        createTask('task-1', ['src/a.ts'], undefined, 'completed'),
        createTask('task-2', ['src/a.ts']),
      ]
      const result = detectFileConflicts(tasks)
      expect(result.hasConflicts).toBe(false)
    })

    it('should ignore failed tasks', () => {
      const tasks = [
        createTask('task-1', ['src/a.ts'], undefined, 'failed'),
        createTask('task-2', ['src/a.ts']),
      ]
      const result = detectFileConflicts(tasks)
      expect(result.hasConflicts).toBe(false)
    })
  })

  describe('write-write conflicts', () => {
    it('should detect two tasks claiming same file', () => {
      const tasks = [
        createTask('task-1', ['src/shared.ts']),
        createTask('task-2', ['src/shared.ts']),
      ]
      const result = detectFileConflicts(tasks)
      expect(result.hasConflicts).toBe(true)
      expect(result.conflicts).toHaveLength(1)
      expect(result.conflicts[0].type).toBe('write_write')
      expect(result.conflicts[0].tasks).toContain('task-1')
      expect(result.conflicts[0].tasks).toContain('task-2')
    })

    it('should detect multiple tasks claiming same file', () => {
      const tasks = [
        createTask('task-1', ['src/shared.ts']),
        createTask('task-2', ['src/shared.ts']),
        createTask('task-3', ['src/shared.ts']),
      ]
      const result = detectFileConflicts(tasks)
      expect(result.hasConflicts).toBe(true)
      expect(result.conflicts[0].tasks).toHaveLength(3)
    })

    it('should normalize file paths', () => {
      const tasks = [
        createTask('task-1', ['./src/shared.ts']),
        createTask('task-2', ['src/shared.ts']),
      ]
      const result = detectFileConflicts(tasks)
      expect(result.hasConflicts).toBe(true)
    })
  })

  describe('write-readonly conflicts', () => {
    it('should detect write vs readonly conflict', () => {
      const tasks = [
        createTask('task-1', ['src/config.ts']),
        createTask('task-2', undefined, ['src/config.ts']),
      ]
      const result = detectFileConflicts(tasks)
      expect(result.hasConflicts).toBe(true)
      expect(result.conflicts).toHaveLength(1)
      expect(result.conflicts[0].type).toBe('write_readonly')
    })

    it('should allow same task to have both write and readonly access', () => {
      const tasks = [
        createTask('task-1', ['src/target.ts'], ['src/source.ts']),
      ]
      const result = detectFileConflicts(tasks)
      expect(result.hasConflicts).toBe(false)
    })
  })

  describe('summary', () => {
    it('should include conflict count in summary', () => {
      const tasks = [
        createTask('task-1', ['src/a.ts', 'src/b.ts']),
        createTask('task-2', ['src/a.ts', 'src/b.ts']),
      ]
      const result = detectFileConflicts(tasks)
      expect(result.summary).toContain('2')
      expect(result.summary).toContain('write-write')
    })
  })
})

describe('checkTaskConflicts', () => {
  it('should check new task against existing tasks', () => {
    const existingTasks = [
      createTask('existing-1', ['src/shared.ts'], undefined, 'in_progress'),
    ]
    const newTask = {
      id: 'new-task',
      files: ['src/shared.ts'],
    }

    const result = checkTaskConflicts(newTask, existingTasks)
    expect(result.hasConflicts).toBe(true)
    expect(result.conflicts[0].tasks).toContain('existing-1')
    expect(result.conflicts[0].tasks).toContain('new-task')
  })

  it('should not conflict with completed tasks', () => {
    const existingTasks = [
      createTask('existing-1', ['src/shared.ts'], undefined, 'completed'),
    ]
    const newTask = {
      id: 'new-task',
      files: ['src/shared.ts'],
    }

    const result = checkTaskConflicts(newTask, existingTasks)
    expect(result.hasConflicts).toBe(false)
  })
})

describe('formatConflicts', () => {
  it('should format no conflicts message', () => {
    const result = formatConflicts({
      hasConflicts: false,
      conflicts: [],
      summary: 'No conflicts detected.',
    })
    expect(result).toBe('No file conflicts detected.')
  })

  it('should format conflict details', () => {
    const result = formatConflicts({
      hasConflicts: true,
      conflicts: [{
        file: 'src/shared.ts',
        tasks: ['task-1', 'task-2'],
        type: 'write_write',
        description: 'Multiple tasks claim write access to src/shared.ts',
      }],
      summary: 'Found 1 write-write conflict.',
    })
    expect(result).toContain('[FILE CONFLICT DETECTED]')
    expect(result).toContain('src/shared.ts')
    expect(result).toContain('Resolution Options')
  })
})
