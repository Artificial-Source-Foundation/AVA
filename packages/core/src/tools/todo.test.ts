/**
 * Todo Storage Tests
 * Tests for in-memory todo storage functions: getTodos, setTodos, clearTodos
 */

import { afterEach, describe, expect, it } from 'vitest'
import { clearTodos, getTodos, setTodos } from './todo.js'

// ============================================================================
// Types (mirrors internal TodoItem)
// ============================================================================

interface TodoItem {
  id: string
  content: string
  status: 'pending' | 'in_progress' | 'completed'
  createdAt: number
  completedAt?: number
}

// ============================================================================
// Helpers
// ============================================================================

function makeTodo(overrides: Partial<TodoItem> = {}): TodoItem {
  return {
    id: `todo-${Math.random().toString(36).slice(2, 6)}`,
    content: 'Test task',
    status: 'pending',
    createdAt: Date.now(),
    ...overrides,
  }
}

// ============================================================================
// Cleanup
// ============================================================================

afterEach(() => {
  clearTodos('test-session')
  clearTodos('session-1')
  clearTodos('session-2')
})

// ============================================================================
// getTodos
// ============================================================================

describe('getTodos', () => {
  it('should return empty array for unknown session', () => {
    expect(getTodos('nonexistent-session')).toEqual([])
  })

  it('should return empty array for cleared session', () => {
    setTodos('test-session', [makeTodo()])
    clearTodos('test-session')
    expect(getTodos('test-session')).toEqual([])
  })

  it('should return the exact array reference stored', () => {
    const todos = [makeTodo({ content: 'Task A' })]
    setTodos('test-session', todos)
    expect(getTodos('test-session')).toBe(todos)
  })
})

// ============================================================================
// setTodos
// ============================================================================

describe('setTodos', () => {
  it('should store and retrieve todos', () => {
    const todos = [makeTodo({ content: 'First task' })]
    setTodos('test-session', todos)
    expect(getTodos('test-session')).toEqual(todos)
  })

  it('should overwrite existing todos', () => {
    const first = [makeTodo({ content: 'Old task' })]
    const second = [makeTodo({ content: 'New task' })]

    setTodos('test-session', first)
    setTodos('test-session', second)

    const result = getTodos('test-session')
    expect(result).toEqual(second)
    expect(result).not.toEqual(first)
  })

  it('should store empty array', () => {
    setTodos('test-session', [])
    expect(getTodos('test-session')).toEqual([])
  })

  it('should store todos with pending status', () => {
    const todos = [makeTodo({ status: 'pending', content: 'Pending task' })]
    setTodos('test-session', todos)
    expect(getTodos('test-session')[0].status).toBe('pending')
  })

  it('should store todos with in_progress status', () => {
    const todos = [makeTodo({ status: 'in_progress', content: 'In progress task' })]
    setTodos('test-session', todos)
    expect(getTodos('test-session')[0].status).toBe('in_progress')
  })

  it('should store todos with completed status', () => {
    const todos = [makeTodo({ status: 'completed', content: 'Done task', completedAt: Date.now() })]
    setTodos('test-session', todos)
    expect(getTodos('test-session')[0].status).toBe('completed')
    expect(getTodos('test-session')[0].completedAt).toBeDefined()
  })

  it('should store todos with all three statuses', () => {
    const todos = [
      makeTodo({ status: 'pending', content: 'Pending' }),
      makeTodo({ status: 'in_progress', content: 'In progress' }),
      makeTodo({ status: 'completed', content: 'Completed', completedAt: Date.now() }),
    ]
    setTodos('test-session', todos)
    const result = getTodos('test-session')
    expect(result).toHaveLength(3)
    expect(result[0].status).toBe('pending')
    expect(result[1].status).toBe('in_progress')
    expect(result[2].status).toBe('completed')
  })

  it('should store many todos', () => {
    const todos = Array.from({ length: 50 }, (_, i) => makeTodo({ content: `Task ${i}` }))
    setTodos('test-session', todos)
    expect(getTodos('test-session')).toHaveLength(50)
  })
})

// ============================================================================
// clearTodos
// ============================================================================

describe('clearTodos', () => {
  it('should remove todos for session', () => {
    setTodos('test-session', [makeTodo()])
    clearTodos('test-session')
    expect(getTodos('test-session')).toEqual([])
  })

  it('should do nothing for unknown session', () => {
    // Should not throw
    clearTodos('nonexistent-session')
    expect(getTodos('nonexistent-session')).toEqual([])
  })

  it('should only clear the specified session', () => {
    setTodos('session-1', [makeTodo({ content: 'Session 1 task' })])
    setTodos('session-2', [makeTodo({ content: 'Session 2 task' })])

    clearTodos('session-1')

    expect(getTodos('session-1')).toEqual([])
    expect(getTodos('session-2')).toHaveLength(1)
  })
})

// ============================================================================
// Session Isolation
// ============================================================================

describe('session isolation', () => {
  it('should keep sessions independent', () => {
    const todos1 = [makeTodo({ content: 'Session 1 task' })]
    const todos2 = [makeTodo({ content: 'Session 2 task' })]

    setTodos('session-1', todos1)
    setTodos('session-2', todos2)

    expect(getTodos('session-1')).toEqual(todos1)
    expect(getTodos('session-2')).toEqual(todos2)
  })

  it('should not leak data between sessions', () => {
    setTodos('session-1', [makeTodo({ content: 'Private data' })])
    expect(getTodos('session-2')).toEqual([])
  })

  it('should allow clearing one session without affecting another', () => {
    setTodos('session-1', [makeTodo({ content: 'Task A' })])
    setTodos('session-2', [makeTodo({ content: 'Task B' })])

    clearTodos('session-1')

    expect(getTodos('session-1')).toEqual([])
    expect(getTodos('session-2')[0].content).toBe('Task B')
  })
})

// ============================================================================
// Round-trip
// ============================================================================

describe('round-trip', () => {
  it('should return identical data after set then get', () => {
    const todos: TodoItem[] = [
      {
        id: 'todo-001',
        content: 'Write tests',
        status: 'in_progress',
        createdAt: 1700000000000,
      },
      {
        id: 'todo-002',
        content: 'Review PR',
        status: 'pending',
        createdAt: 1700000001000,
      },
      {
        id: 'todo-003',
        content: 'Deploy',
        status: 'completed',
        createdAt: 1700000002000,
        completedAt: 1700000003000,
      },
    ]

    setTodos('test-session', todos)
    const result = getTodos('test-session')

    expect(result).toEqual(todos)
    expect(result[0].id).toBe('todo-001')
    expect(result[1].content).toBe('Review PR')
    expect(result[2].completedAt).toBe(1700000003000)
  })

  it('should preserve completedAt as undefined when not set', () => {
    const todo = makeTodo({ status: 'pending' })
    setTodos('test-session', [todo])
    const result = getTodos('test-session')
    expect(result[0].completedAt).toBeUndefined()
  })
})
