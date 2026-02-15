/**
 * Focus Chain Parser Tests
 */

import { describe, expect, it } from 'vitest'
import {
  addTaskToMarkdown,
  calculateProgress,
  getNextTask,
  parseMarkdown,
  removeTaskFromMarkdown,
  serializeToMarkdown,
  updateTaskInMarkdown,
} from './parser.js'
import type { FocusTask } from './types.js'

// ============================================================================
// parseMarkdown
// ============================================================================

describe('parseMarkdown', () => {
  it('parses empty content', () => {
    expect(parseMarkdown('')).toEqual([])
  })

  it('parses content with no tasks', () => {
    expect(parseMarkdown('# Title\n\nSome text')).toEqual([])
  })

  it('parses a single pending task', () => {
    const tasks = parseMarkdown('- [ ] Do something')
    expect(tasks).toHaveLength(1)
    expect(tasks[0].text).toBe('Do something')
    expect(tasks[0].status).toBe('pending')
    expect(tasks[0].level).toBe(0)
    expect(tasks[0].line).toBe(1)
  })

  it('parses all four status types', () => {
    const md = `- [ ] Pending
- [x] Completed
- [~] In progress
- [!] Blocked`
    const tasks = parseMarkdown(md)
    expect(tasks).toHaveLength(4)
    expect(tasks[0].status).toBe('pending')
    expect(tasks[1].status).toBe('completed')
    expect(tasks[2].status).toBe('in_progress')
    expect(tasks[3].status).toBe('blocked')
  })

  it('assigns sequential IDs', () => {
    const md = `- [ ] First\n- [ ] Second\n- [ ] Third`
    const tasks = parseMarkdown(md)
    expect(tasks[0].id).toBe('task-0')
    expect(tasks[1].id).toBe('task-1')
    expect(tasks[2].id).toBe('task-2')
  })

  it('parses nested tasks with indentation', () => {
    const md = `- [ ] Parent
  - [ ] Child 1
  - [ ] Child 2`
    const tasks = parseMarkdown(md)
    expect(tasks).toHaveLength(3)
    expect(tasks[0].level).toBe(0)
    expect(tasks[1].level).toBe(1)
    expect(tasks[2].level).toBe(1)
  })

  it('establishes parent-child relationships', () => {
    const md = `- [ ] Parent
  - [ ] Child`
    const tasks = parseMarkdown(md)
    expect(tasks[1].parentId).toBe('task-0')
    expect(tasks[0].childIds).toContain('task-1')
  })

  it('handles deeply nested tasks', () => {
    const md = `- [ ] Level 0
  - [ ] Level 1
    - [ ] Level 2
      - [ ] Level 3`
    const tasks = parseMarkdown(md)
    expect(tasks[0].level).toBe(0)
    expect(tasks[1].level).toBe(1)
    expect(tasks[2].level).toBe(2)
    expect(tasks[3].level).toBe(3)
    expect(tasks[3].parentId).toBe('task-2')
  })

  it('handles mixed content with non-task lines', () => {
    const md = `# Focus Chain

- [ ] Task one

Some notes

- [x] Task two`
    const tasks = parseMarkdown(md)
    expect(tasks).toHaveLength(2)
    expect(tasks[0].text).toBe('Task one')
    expect(tasks[1].text).toBe('Task two')
  })

  it('tracks correct line numbers', () => {
    const md = `# Title\n\n- [ ] Task on line 3\n\n- [ ] Task on line 5`
    const tasks = parseMarkdown(md)
    expect(tasks[0].line).toBe(3)
    expect(tasks[1].line).toBe(5)
  })

  it('handles sibling tasks after nested children', () => {
    const md = `- [ ] Parent A
  - [ ] Child A1
- [ ] Parent B`
    const tasks = parseMarkdown(md)
    expect(tasks[2].parentId).toBeUndefined()
    expect(tasks[2].level).toBe(0)
  })

  it('trims task text', () => {
    const tasks = parseMarkdown('- [ ] Some task  ')
    expect(tasks[0].text).toBe('Some task')
  })

  it('initializes empty childIds array', () => {
    const tasks = parseMarkdown('- [ ] Leaf task')
    expect(tasks[0].childIds).toEqual([])
  })
})

// ============================================================================
// serializeToMarkdown
// ============================================================================

describe('serializeToMarkdown', () => {
  it('serializes empty tasks', () => {
    expect(serializeToMarkdown([])).toBe('')
  })

  it('serializes a single task', () => {
    const tasks: FocusTask[] = [
      { id: 'task-0', text: 'Do thing', status: 'pending', level: 0, line: 1, childIds: [] },
    ]
    expect(serializeToMarkdown(tasks)).toBe('- [ ] Do thing')
  })

  it('serializes all status types', () => {
    const tasks: FocusTask[] = [
      { id: 't0', text: 'Pending', status: 'pending', level: 0, line: 1, childIds: [] },
      { id: 't1', text: 'Done', status: 'completed', level: 0, line: 2, childIds: [] },
      { id: 't2', text: 'Working', status: 'in_progress', level: 0, line: 3, childIds: [] },
      { id: 't3', text: 'Stuck', status: 'blocked', level: 0, line: 4, childIds: [] },
    ]
    const md = serializeToMarkdown(tasks)
    expect(md).toContain('- [ ] Pending')
    expect(md).toContain('- [x] Done')
    expect(md).toContain('- [~] Working')
    expect(md).toContain('- [!] Stuck')
  })

  it('serializes nested tasks with indentation', () => {
    const tasks: FocusTask[] = [
      { id: 't0', text: 'Parent', status: 'pending', level: 0, line: 1, childIds: ['t1'] },
      {
        id: 't1',
        text: 'Child',
        status: 'pending',
        level: 1,
        line: 2,
        parentId: 't0',
        childIds: [],
      },
    ]
    const md = serializeToMarkdown(tasks)
    expect(md).toBe('- [ ] Parent\n  - [ ] Child')
  })

  it('adds title when provided', () => {
    const tasks: FocusTask[] = [
      { id: 't0', text: 'Task', status: 'pending', level: 0, line: 1, childIds: [] },
    ]
    const md = serializeToMarkdown(tasks, 'My Tasks')
    expect(md.startsWith('# My Tasks\n\n')).toBe(true)
  })

  it('preserves original line order', () => {
    const tasks: FocusTask[] = [
      { id: 't1', text: 'Second', status: 'pending', level: 0, line: 5, childIds: [] },
      { id: 't0', text: 'First', status: 'pending', level: 0, line: 1, childIds: [] },
    ]
    const md = serializeToMarkdown(tasks)
    expect(md).toBe('- [ ] First\n- [ ] Second')
  })

  it('serializes tasks with notes', () => {
    const tasks: FocusTask[] = [
      {
        id: 't0',
        text: 'Task',
        status: 'pending',
        level: 0,
        line: 1,
        childIds: [],
        notes: 'Some note',
      },
    ]
    const md = serializeToMarkdown(tasks)
    expect(md).toContain('> Some note')
  })

  it('roundtrips parse → serialize', () => {
    const original = `- [ ] Top task
  - [x] Sub task
  - [~] Another sub`
    const tasks = parseMarkdown(original)
    const serialized = serializeToMarkdown(tasks)
    expect(serialized).toBe(original)
  })
})

// ============================================================================
// updateTaskInMarkdown
// ============================================================================

describe('updateTaskInMarkdown', () => {
  it('updates task status', () => {
    const md = '- [ ] Task'
    const tasks = parseMarkdown(md)
    const result = updateTaskInMarkdown(md, 'task-0', tasks, 'completed')
    expect(result).toBe('- [x] Task')
  })

  it('returns unchanged content for unknown task ID', () => {
    const md = '- [ ] Task'
    const tasks = parseMarkdown(md)
    const result = updateTaskInMarkdown(md, 'nonexistent', tasks, 'completed')
    expect(result).toBe(md)
  })

  it('updates only the targeted task', () => {
    const md = '- [ ] First\n- [ ] Second'
    const tasks = parseMarkdown(md)
    const result = updateTaskInMarkdown(md, 'task-1', tasks, 'in_progress')
    expect(result).toBe('- [ ] First\n- [~] Second')
  })

  it('updates nested task status', () => {
    const md = '- [ ] Parent\n  - [ ] Child'
    const tasks = parseMarkdown(md)
    const result = updateTaskInMarkdown(md, 'task-1', tasks, 'blocked')
    expect(result).toBe('- [ ] Parent\n  - [!] Child')
  })
})

// ============================================================================
// addTaskToMarkdown
// ============================================================================

describe('addTaskToMarkdown', () => {
  it('adds task at the end by default', () => {
    const md = '- [ ] Existing'
    const result = addTaskToMarkdown(md, 'New task')
    expect(result).toContain('- [ ] New task')
  })

  it('adds task after specified line', () => {
    const md = '- [ ] First\n- [ ] Third'
    const result = addTaskToMarkdown(md, 'Second', { afterLine: 1 })
    const lines = result.split('\n')
    expect(lines[1]).toBe('- [ ] Second')
  })

  it('adds indented task', () => {
    const result = addTaskToMarkdown('- [ ] Parent', 'Child', { level: 1 })
    expect(result).toContain('  - [ ] Child')
  })

  it('adds task with non-default status', () => {
    const result = addTaskToMarkdown('', 'Blocked task', { status: 'blocked' })
    expect(result).toContain('- [!] Blocked task')
  })

  it('adds blank line before new task at end if needed', () => {
    const result = addTaskToMarkdown('- [ ] Existing', 'New')
    expect(result).toContain('\n- [ ] New')
  })
})

// ============================================================================
// removeTaskFromMarkdown
// ============================================================================

describe('removeTaskFromMarkdown', () => {
  it('removes a task', () => {
    const md = '- [ ] Keep\n- [ ] Remove'
    const tasks = parseMarkdown(md)
    const result = removeTaskFromMarkdown(md, 'task-1', tasks)
    expect(result).toBe('- [ ] Keep')
  })

  it('returns unchanged content for unknown task ID', () => {
    const md = '- [ ] Task'
    const tasks = parseMarkdown(md)
    const result = removeTaskFromMarkdown(md, 'nonexistent', tasks)
    expect(result).toBe(md)
  })

  it('removes task and all descendants', () => {
    const md = `- [ ] Parent
  - [ ] Child 1
    - [ ] Grandchild
  - [ ] Child 2
- [ ] Other`
    const tasks = parseMarkdown(md)
    const result = removeTaskFromMarkdown(md, 'task-0', tasks)
    expect(result).toBe('- [ ] Other')
  })

  it('preserves other tasks when removing', () => {
    const md = '- [ ] A\n- [ ] B\n- [ ] C'
    const tasks = parseMarkdown(md)
    const result = removeTaskFromMarkdown(md, 'task-1', tasks)
    expect(result).toBe('- [ ] A\n- [ ] C')
  })
})

// ============================================================================
// calculateProgress
// ============================================================================

describe('calculateProgress', () => {
  it('handles empty task list', () => {
    const progress = calculateProgress([])
    expect(progress).toEqual({
      total: 0,
      completed: 0,
      inProgress: 0,
      blocked: 0,
      pending: 0,
      percentComplete: 0,
    })
  })

  it('calculates all-completed progress', () => {
    const tasks: FocusTask[] = [
      { id: 't0', text: 'A', status: 'completed', level: 0, line: 1, childIds: [] },
      { id: 't1', text: 'B', status: 'completed', level: 0, line: 2, childIds: [] },
    ]
    const progress = calculateProgress(tasks)
    expect(progress.percentComplete).toBe(100)
    expect(progress.completed).toBe(2)
    expect(progress.total).toBe(2)
  })

  it('calculates mixed status progress', () => {
    const tasks: FocusTask[] = [
      { id: 't0', text: 'A', status: 'completed', level: 0, line: 1, childIds: [] },
      { id: 't1', text: 'B', status: 'in_progress', level: 0, line: 2, childIds: [] },
      { id: 't2', text: 'C', status: 'blocked', level: 0, line: 3, childIds: [] },
      { id: 't3', text: 'D', status: 'pending', level: 0, line: 4, childIds: [] },
    ]
    const progress = calculateProgress(tasks)
    expect(progress.total).toBe(4)
    expect(progress.completed).toBe(1)
    expect(progress.inProgress).toBe(1)
    expect(progress.blocked).toBe(1)
    expect(progress.pending).toBe(1)
    expect(progress.percentComplete).toBe(25)
  })

  it('rounds percentage to integer', () => {
    const tasks: FocusTask[] = [
      { id: 't0', text: 'A', status: 'completed', level: 0, line: 1, childIds: [] },
      { id: 't1', text: 'B', status: 'pending', level: 0, line: 2, childIds: [] },
      { id: 't2', text: 'C', status: 'pending', level: 0, line: 3, childIds: [] },
    ]
    const progress = calculateProgress(tasks)
    expect(progress.percentComplete).toBe(33) // 33.33 rounds to 33
  })
})

// ============================================================================
// getNextTask
// ============================================================================

describe('getNextTask', () => {
  it('returns null for empty list', () => {
    expect(getNextTask([])).toBeNull()
  })

  it('returns null when all tasks are completed', () => {
    const tasks: FocusTask[] = [
      { id: 't0', text: 'Done', status: 'completed', level: 0, line: 1, childIds: [] },
    ]
    expect(getNextTask(tasks)).toBeNull()
  })

  it('returns first pending task', () => {
    const tasks: FocusTask[] = [
      { id: 't0', text: 'Done', status: 'completed', level: 0, line: 1, childIds: [] },
      { id: 't1', text: 'Next', status: 'pending', level: 0, line: 2, childIds: [] },
    ]
    const next = getNextTask(tasks)
    expect(next?.id).toBe('t1')
  })

  it('skips tasks with incomplete children', () => {
    const tasks: FocusTask[] = [
      { id: 't0', text: 'Parent', status: 'pending', level: 0, line: 1, childIds: ['t1'] },
      {
        id: 't1',
        text: 'Child',
        status: 'pending',
        level: 1,
        line: 2,
        parentId: 't0',
        childIds: [],
      },
    ]
    const next = getNextTask(tasks)
    expect(next?.id).toBe('t1') // Child is actionable, parent is not
  })

  it('returns parent when all children completed', () => {
    const tasks: FocusTask[] = [
      { id: 't0', text: 'Parent', status: 'pending', level: 0, line: 1, childIds: ['t1'] },
      {
        id: 't1',
        text: 'Child',
        status: 'completed',
        level: 1,
        line: 2,
        parentId: 't0',
        childIds: [],
      },
    ]
    const next = getNextTask(tasks)
    expect(next?.id).toBe('t0')
  })

  it('skips blocked and in-progress tasks', () => {
    const tasks: FocusTask[] = [
      { id: 't0', text: 'Blocked', status: 'blocked', level: 0, line: 1, childIds: [] },
      { id: 't1', text: 'Working', status: 'in_progress', level: 0, line: 2, childIds: [] },
      { id: 't2', text: 'Ready', status: 'pending', level: 0, line: 3, childIds: [] },
    ]
    const next = getNextTask(tasks)
    expect(next?.id).toBe('t2')
  })

  it('returns earliest by line number', () => {
    const tasks: FocusTask[] = [
      { id: 't0', text: 'Later', status: 'pending', level: 0, line: 10, childIds: [] },
      { id: 't1', text: 'First', status: 'pending', level: 0, line: 1, childIds: [] },
    ]
    const next = getNextTask(tasks)
    expect(next?.id).toBe('t1')
  })
})
