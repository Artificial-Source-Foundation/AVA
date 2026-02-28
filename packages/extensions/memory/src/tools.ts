/**
 * Memory tools — 4 tools for cross-session memory management.
 */

import type { Tool } from '@ava/core-v2/tools'
import type { MemoryCategory, MemoryStore } from './store.js'

export function createMemoryTools(store: MemoryStore): Tool[] {
  return [
    {
      definition: {
        name: 'memory_write',
        description:
          'Save a memory entry that persists across sessions. Use for project patterns, user preferences, debugging insights.',
        input_schema: {
          type: 'object' as const,
          properties: {
            key: { type: 'string', description: 'Unique identifier for this memory' },
            value: { type: 'string', description: 'The content to remember' },
            category: {
              type: 'string',
              enum: ['project', 'preferences', 'debug', 'context'],
              description:
                'Category: project (patterns/architecture), preferences (user prefs), debug (solutions), context (session context)',
            },
          },
          required: ['key', 'value'],
        },
      },
      async execute(input) {
        const { key, value, category } = input as {
          key: string
          value: string
          category?: MemoryCategory
        }
        const entry = await store.write(key, value, category ?? 'project')
        return { success: true, output: `Memory saved: "${key}" [${entry.category}]` }
      },
    },
    {
      definition: {
        name: 'memory_read',
        description: 'Read a specific memory entry by key.',
        input_schema: {
          type: 'object' as const,
          properties: {
            key: { type: 'string', description: 'The memory key to read' },
          },
          required: ['key'],
        },
      },
      async execute(input) {
        const { key } = input as { key: string }
        const entry = await store.read(key)
        if (!entry) return { success: false, output: `Memory not found: "${key}"` }
        return { success: true, output: `[${entry.category}] ${entry.key}: ${entry.value}` }
      },
    },
    {
      definition: {
        name: 'memory_list',
        description: 'List all saved memory entries, optionally filtered by category.',
        input_schema: {
          type: 'object' as const,
          properties: {
            category: {
              type: 'string',
              enum: ['project', 'preferences', 'debug', 'context'],
              description: 'Filter by category (optional)',
            },
          },
        },
      },
      async execute(input) {
        const { category } = (input ?? {}) as { category?: MemoryCategory }
        const entries = await store.list(category)
        if (entries.length === 0) return { success: true, output: 'No memories found.' }
        const lines = entries.map((e) => `[${e.category}] ${e.key}: ${e.value}`)
        return { success: true, output: lines.join('\n') }
      },
    },
    {
      definition: {
        name: 'memory_delete',
        description: 'Delete a memory entry by key.',
        input_schema: {
          type: 'object' as const,
          properties: {
            key: { type: 'string', description: 'The memory key to delete' },
          },
          required: ['key'],
        },
      },
      async execute(input) {
        const { key } = input as { key: string }
        const deleted = await store.remove(key)
        if (!deleted) return { success: false, output: `Memory not found: "${key}"` }
        return { success: true, output: `Memory deleted: "${key}"` }
      },
    },
  ]
}
