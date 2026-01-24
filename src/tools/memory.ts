/**
 * Delta9 Memory Tools
 *
 * Tools for managing persistent memory blocks.
 * Used for cross-session learning and pattern storage.
 */

import { tool, type ToolDefinition } from '@opencode-ai/plugin'
import { getMemoryManager, seedDefaultBlocks } from '../memory/index.js'

// Use the tool's built-in schema (Zod 4 compatible)
const s = tool.schema

// =============================================================================
// Tool Definitions
// =============================================================================

/**
 * Create memory tools
 */
export function createMemoryTools(cwd: string): Record<string, ToolDefinition> {
  // Seed default blocks on first use
  seedDefaultBlocks(cwd)

  /**
   * List available memory blocks
   */
  const memory_list = tool({
    description:
      'List available memory blocks with their labels, descriptions, and sizes. Use to see what memories exist.',
    args: {
      scope: s
        .enum(['all', 'global', 'project'])
        .optional()
        .describe('Filter by scope (default: all)'),
    },

    async execute(args, _ctx) {
      const manager = getMemoryManager(cwd)
      let blocks = manager.list()

      // Filter by scope if specified
      if (args.scope && args.scope !== 'all') {
        blocks = blocks.filter((b) => b.scope === args.scope)
      }

      const summary = manager.getSummary()

      return JSON.stringify({
        success: true,
        blocks: blocks.map((b) => ({
          label: b.label,
          description: b.description,
          scope: b.scope,
          size: b.size,
          limit: b.limit,
          readOnly: b.readOnly,
        })),
        summary: {
          total: summary.total,
          global: summary.global,
          project: summary.project,
          totalSize: summary.totalSize,
        },
      })
    },
  })

  /**
   * Get a memory block's content
   */
  const memory_get = tool({
    description:
      'Get the content of a specific memory block by label. Use to read stored patterns, failures, or context.',
    args: {
      label: s.string().describe('Label of the memory block to read'),
    },

    async execute(args, _ctx) {
      const manager = getMemoryManager(cwd)
      const block = manager.get(args.label)

      if (!block) {
        return JSON.stringify({
          success: false,
          message: `Memory block "${args.label}" not found`,
        })
      }

      return JSON.stringify({
        success: true,
        block: {
          label: block.label,
          description: block.description,
          scope: block.scope,
          size: block.size,
          limit: block.limit,
          readOnly: block.readOnly,
          content: block.content,
        },
      })
    },
  })

  /**
   * Set (create or update) a memory block
   */
  const memory_set = tool({
    description:
      'Create or update a memory block with new content. Use to store patterns, learnings, or project context.',
    args: {
      label: s.string().describe('Label for the memory block'),
      content: s.string().describe('Content to store'),
      description: s.string().optional().describe('Description of what this block stores'),
      scope: s.enum(['global', 'project']).optional().describe('Scope (default: project)'),
      limit: s.number().optional().describe('Maximum characters allowed'),
    },

    async execute(args, _ctx) {
      const manager = getMemoryManager(cwd)

      // Check if block is read-only
      const existing = manager.get(args.label)
      if (existing?.readOnly) {
        return JSON.stringify({
          success: false,
          message: `Memory block "${args.label}" is read-only`,
        })
      }

      const success = manager.set(args.label, args.content, {
        scope: args.scope,
        description: args.description,
        limit: args.limit,
      })

      if (success) {
        const block = manager.get(args.label)
        return JSON.stringify({
          success: true,
          message: `Memory block "${args.label}" updated`,
          block: {
            label: block?.label,
            scope: block?.scope,
            size: block?.size,
          },
        })
      } else {
        return JSON.stringify({
          success: false,
          message: `Failed to update memory block "${args.label}"`,
        })
      }
    },
  })

  /**
   * Replace text within a memory block
   */
  const memory_replace = tool({
    description:
      'Replace a substring within a memory block. Use for incremental updates without rewriting the entire block.',
    args: {
      label: s.string().describe('Label of the memory block to modify'),
      oldText: s.string().describe('Text to find and replace'),
      newText: s.string().describe('Replacement text'),
    },

    async execute(args, _ctx) {
      const manager = getMemoryManager(cwd)

      const existing = manager.get(args.label)
      if (!existing) {
        return JSON.stringify({
          success: false,
          message: `Memory block "${args.label}" not found`,
        })
      }

      if (existing.readOnly) {
        return JSON.stringify({
          success: false,
          message: `Memory block "${args.label}" is read-only`,
        })
      }

      if (!existing.content.includes(args.oldText)) {
        return JSON.stringify({
          success: false,
          message: `Text not found in memory block "${args.label}"`,
        })
      }

      const success = manager.replace(args.label, args.oldText, args.newText)

      if (success) {
        const block = manager.get(args.label)
        return JSON.stringify({
          success: true,
          message: `Replaced text in memory block "${args.label}"`,
          block: {
            label: block?.label,
            size: block?.size,
          },
        })
      } else {
        return JSON.stringify({
          success: false,
          message: `Failed to replace text in memory block "${args.label}"`,
        })
      }
    },
  })

  /**
   * Append content to a memory block
   */
  const memory_append = tool({
    description:
      'Append content to an existing memory block. Use for adding new patterns or learnings.',
    args: {
      label: s.string().describe('Label of the memory block to append to'),
      content: s.string().describe('Content to append'),
      separator: s.string().optional().describe('Separator between existing and new content (default: newlines)'),
    },

    async execute(args, _ctx) {
      const manager = getMemoryManager(cwd)

      const existing = manager.get(args.label)
      if (existing?.readOnly) {
        return JSON.stringify({
          success: false,
          message: `Memory block "${args.label}" is read-only`,
        })
      }

      const success = manager.append(args.label, args.content, args.separator)

      if (success) {
        const block = manager.get(args.label)
        return JSON.stringify({
          success: true,
          message: `Appended content to memory block "${args.label}"`,
          block: {
            label: block?.label,
            size: block?.size,
            limit: block?.limit,
          },
        })
      } else {
        return JSON.stringify({
          success: false,
          message: `Failed to append to memory block "${args.label}"`,
        })
      }
    },
  })

  /**
   * Delete a memory block
   */
  const memory_delete = tool({
    description: 'Delete a memory block. Use with caution - this cannot be undone.',
    args: {
      label: s.string().describe('Label of the memory block to delete'),
      scope: s.enum(['global', 'project']).optional().describe('Scope to delete from (if block exists in both)'),
    },

    async execute(args, _ctx) {
      const manager = getMemoryManager(cwd)

      const existing = manager.get(args.label)
      if (!existing) {
        return JSON.stringify({
          success: false,
          message: `Memory block "${args.label}" not found`,
        })
      }

      if (existing.readOnly) {
        return JSON.stringify({
          success: false,
          message: `Memory block "${args.label}" is read-only`,
        })
      }

      const success = manager.delete(args.label, args.scope)

      if (success) {
        return JSON.stringify({
          success: true,
          message: `Deleted memory block "${args.label}"`,
        })
      } else {
        return JSON.stringify({
          success: false,
          message: `Failed to delete memory block "${args.label}"`,
        })
      }
    },
  })

  return {
    memory_list,
    memory_get,
    memory_set,
    memory_replace,
    memory_append,
    memory_delete,
  }
}

// =============================================================================
// Type Export
// =============================================================================

export type MemoryTools = ReturnType<typeof createMemoryTools>
