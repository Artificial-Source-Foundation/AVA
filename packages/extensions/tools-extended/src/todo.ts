/**
 * todoread / todowrite tools — in-memory task tracking.
 */

import { defineTool } from '@ava/core-v2/tools'
import * as z from 'zod'

interface TodoItem {
  id: string
  content: string
  status: 'pending' | 'in_progress' | 'completed'
}

const todos = new Map<string, TodoItem>()
let nextId = 1

export const todoReadTool = defineTool({
  name: 'todoread',
  description: 'Read the current todo list.',
  schema: z.object({}),
  async execute() {
    if (todos.size === 0) {
      return { success: true, output: 'No todos.' }
    }
    const lines = [...todos.values()].map((t) => `[${t.status}] #${t.id}: ${t.content}`)
    return { success: true, output: lines.join('\n') }
  },
})

export const todoWriteTool = defineTool({
  name: 'todowrite',
  description: 'Update the todo list. Add, modify, or remove items.',
  schema: z.object({
    todos: z.array(
      z.object({
        id: z.string().optional().describe('ID of existing todo to update'),
        content: z.string().describe('Todo content'),
        status: z
          .enum(['pending', 'in_progress', 'completed'])
          .optional()
          .describe('Status (default: pending)'),
      })
    ),
  }),
  async execute(input) {
    for (const item of input.todos) {
      const id = item.id ?? String(nextId++)
      todos.set(id, {
        id,
        content: item.content,
        status: item.status ?? 'pending',
      })
    }
    return { success: true, output: `Updated ${input.todos.length} todo(s)` }
  },
})
