/**
 * Todo Tools
 * Read and write todo lists for tracking progress during sessions
 *
 * Based on OpenCode's todo.ts pattern
 */

import { ToolError, ToolErrorType } from './errors.js'
import type { Tool, ToolContext, ToolResult } from './types.js'

// ============================================================================
// Types
// ============================================================================

/**
 * Todo item structure (mirrors session/types.ts TodoItem)
 */
interface TodoItem {
  id: string
  content: string
  status: 'pending' | 'in_progress' | 'completed'
  createdAt: number
  completedAt?: number
}

interface TodoWriteParams {
  /** Complete todo list to set */
  todos: TodoItem[]
}

type TodoReadParams = {}

// ============================================================================
// In-Memory Todo Storage
// ============================================================================

/**
 * In-memory storage for todos (per session)
 * TODO: Integrate with SessionManager for persistence
 */
const todoStore = new Map<string, TodoItem[]>()

/**
 * Get todos for a session
 */
export function getTodos(sessionId: string): TodoItem[] {
  return todoStore.get(sessionId) ?? []
}

/**
 * Set todos for a session
 */
export function setTodos(sessionId: string, todos: TodoItem[]): void {
  todoStore.set(sessionId, todos)
}

/**
 * Clear todos for a session
 */
export function clearTodos(sessionId: string): void {
  todoStore.delete(sessionId)
}

// ============================================================================
// Helper Functions
// ============================================================================

/**
 * Generate a unique todo ID
 */
function generateTodoId(): string {
  return `todo-${Date.now()}-${Math.random().toString(36).slice(2, 6)}`
}

/**
 * Validate a todo item
 */
function validateTodoItem(item: unknown, index: number): TodoItem {
  if (typeof item !== 'object' || item === null) {
    throw new Error(`Todo item at index ${index} must be an object`)
  }

  const { id, content, status, createdAt, completedAt } = item as Record<string, unknown>

  // Content is required
  if (typeof content !== 'string' || !content.trim()) {
    throw new Error(`Todo item at index ${index} must have non-empty content`)
  }

  // Status validation
  const validStatuses = ['pending', 'in_progress', 'completed']
  const itemStatus =
    typeof status === 'string' && validStatuses.includes(status)
      ? (status as TodoItem['status'])
      : 'pending'

  // ID: use provided or generate
  const itemId = typeof id === 'string' && id.trim() ? id : generateTodoId()

  // CreatedAt: use provided or now
  const itemCreatedAt = typeof createdAt === 'number' ? createdAt : Date.now()

  // CompletedAt: only valid for completed items
  let itemCompletedAt: number | undefined
  if (itemStatus === 'completed') {
    itemCompletedAt = typeof completedAt === 'number' ? completedAt : Date.now()
  }

  return {
    id: itemId,
    content: content.trim(),
    status: itemStatus,
    createdAt: itemCreatedAt,
    completedAt: itemCompletedAt,
  }
}

/**
 * Format todo list for output
 */
function formatTodos(todos: TodoItem[]): string {
  if (todos.length === 0) {
    return 'No todos.'
  }

  const statusIcons: Record<TodoItem['status'], string> = {
    pending: '[ ]',
    in_progress: '[~]',
    completed: '[x]',
  }

  const lines = todos.map((todo, i) => {
    const icon = statusIcons[todo.status]
    return `${i + 1}. ${icon} ${todo.content}`
  })

  const pending = todos.filter((t) => t.status === 'pending').length
  const inProgress = todos.filter((t) => t.status === 'in_progress').length
  const completed = todos.filter((t) => t.status === 'completed').length

  lines.push('')
  lines.push(`Summary: ${pending} pending, ${inProgress} in progress, ${completed} completed`)

  return lines.join('\n')
}

// ============================================================================
// Todo Write Tool
// ============================================================================

export const todoWriteTool: Tool<TodoWriteParams> = {
  definition: {
    name: 'todowrite',
    description: `Update the todo list for tracking progress during the session.

Use this tool to:
- Create a new todo list at the start of a task
- Update status as you complete items
- Add new items as you discover sub-tasks

Todo statuses:
- pending: Not started
- in_progress: Currently working on
- completed: Finished

The entire todo list is replaced each time. Include all items (completed and pending) to preserve history.`,
    input_schema: {
      type: 'object',
      properties: {
        todos: {
          type: 'array',
          description: 'Complete todo list to set',
          items: {
            type: 'object',
            properties: {
              id: {
                type: 'string',
                description: 'Unique ID (optional, auto-generated if not provided)',
              },
              content: {
                type: 'string',
                description: 'Todo description',
              },
              status: {
                type: 'string',
                enum: ['pending', 'in_progress', 'completed'],
                description: 'Current status (default: pending)',
              },
            },
            required: ['content'],
          },
        },
      },
      required: ['todos'],
    },
  },

  validate(params: unknown): TodoWriteParams {
    if (typeof params !== 'object' || params === null) {
      throw new ToolError(
        'Invalid params: expected object',
        ToolErrorType.INVALID_PARAMS,
        'todowrite'
      )
    }

    const { todos } = params as Record<string, unknown>

    if (!Array.isArray(todos)) {
      throw new ToolError('Invalid todos: must be array', ToolErrorType.INVALID_PARAMS, 'todowrite')
    }

    // Validate each todo item
    const validatedTodos: TodoItem[] = []
    for (let i = 0; i < todos.length; i++) {
      try {
        validatedTodos.push(validateTodoItem(todos[i], i))
      } catch (err) {
        throw new ToolError(
          err instanceof Error ? err.message : String(err),
          ToolErrorType.INVALID_PARAMS,
          'todowrite'
        )
      }
    }

    return { todos: validatedTodos }
  },

  async execute(params: TodoWriteParams, ctx: ToolContext): Promise<ToolResult> {
    // Check abort signal
    if (ctx.signal.aborted) {
      return {
        success: false,
        output: 'Operation was cancelled',
        error: ToolErrorType.EXECUTION_ABORTED,
      }
    }

    // Store todos
    setTodos(ctx.sessionId, params.todos)

    // Calculate stats
    const pending = params.todos.filter((t) => t.status === 'pending').length
    const inProgress = params.todos.filter((t) => t.status === 'in_progress').length
    const completed = params.todos.filter((t) => t.status === 'completed').length

    // Stream metadata if available
    if (ctx.metadata) {
      ctx.metadata({
        title: `${pending + inProgress} todos remaining`,
        metadata: {
          todos: params.todos,
          pending,
          inProgress,
          completed,
        },
      })
    }

    const output = formatTodos(params.todos)

    return {
      success: true,
      output,
      metadata: {
        todoCount: params.todos.length,
        pending,
        inProgress,
        completed,
      },
    }
  },
}

// ============================================================================
// Todo Read Tool
// ============================================================================

export const todoReadTool: Tool<TodoReadParams> = {
  definition: {
    name: 'todoread',
    description: `Read the current todo list to check progress.

Use this tool to:
- Review what tasks are pending
- Check what you've completed
- Plan next steps`,
    input_schema: {
      type: 'object',
      properties: {},
      required: [],
    },
  },

  validate(params: unknown): TodoReadParams {
    if (params !== undefined && params !== null && typeof params !== 'object') {
      throw new ToolError(
        'Invalid params: expected object or empty',
        ToolErrorType.INVALID_PARAMS,
        'todoread'
      )
    }
    return {}
  },

  async execute(_params: TodoReadParams, ctx: ToolContext): Promise<ToolResult> {
    // Check abort signal
    if (ctx.signal.aborted) {
      return {
        success: false,
        output: 'Operation was cancelled',
        error: ToolErrorType.EXECUTION_ABORTED,
      }
    }

    // Get todos
    const todos = getTodos(ctx.sessionId)

    // Calculate stats
    const pending = todos.filter((t) => t.status === 'pending').length
    const inProgress = todos.filter((t) => t.status === 'in_progress').length
    const completed = todos.filter((t) => t.status === 'completed').length

    // Stream metadata if available
    if (ctx.metadata) {
      ctx.metadata({
        title: `${pending + inProgress} todos remaining`,
        metadata: {
          todos,
          pending,
          inProgress,
          completed,
        },
      })
    }

    const output = formatTodos(todos)

    return {
      success: true,
      output,
      metadata: {
        todoCount: todos.length,
        pending,
        inProgress,
        completed,
      },
    }
  },
}
