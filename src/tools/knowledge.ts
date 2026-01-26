/**
 * Delta9 Knowledge Tools
 *
 * Tools for managing persistent knowledge blocks.
 * Agents can store and retrieve patterns, conventions, gotchas.
 *
 * Pattern: agent-memory plugin
 */

import { tool } from '@opencode-ai/plugin'
import type { ToolDefinition } from '@opencode-ai/plugin'

// Use tool.schema for argument definitions
const s = tool.schema
import {
  createKnowledgeStore,
  type KnowledgeScope,
  type KnowledgeStore,
} from '../knowledge/index.js'

// =============================================================================
// Store Instance
// =============================================================================

let knowledgeStore: KnowledgeStore | null = null

/**
 * Get or create the knowledge store
 */
function getStore(): KnowledgeStore {
  if (!knowledgeStore) {
    // Use current working directory as project directory
    knowledgeStore = createKnowledgeStore(process.cwd())
  }
  return knowledgeStore
}

// =============================================================================
// Knowledge Tools
// =============================================================================

export function createKnowledgeTools(): Record<string, ToolDefinition> {
  // ---------------------------------------------------------------------------
  // knowledge_list - List available knowledge blocks
  // ---------------------------------------------------------------------------
  const knowledge_list = tool({
    description: `List available knowledge blocks.

Returns all knowledge blocks with their labels, descriptions, and current sizes.
Use this to discover what knowledge is available before reading/writing.

Categories:
- patterns: Code patterns, architectural decisions
- conventions: Project conventions, naming, structure
- gotchas: Known issues, pitfalls to avoid
- decisions: Important decisions and rationale
- custom: User-defined blocks`,
    args: {
      scope: s
        .enum(['all', 'project', 'global'])
        .optional()
        .describe('Scope to list (default: all)'),
    },
    async execute(args) {
      const store = getStore()
      const scope = (args.scope ?? 'all') as KnowledgeScope | 'all'
      const blocks = await store.listBlocks(scope)

      if (blocks.length === 0) {
        return 'No knowledge blocks found. Use knowledge_set to create one.'
      }

      const lines = blocks.map((b) => {
        const usage = `${b.charCount}/${b.limit} chars`
        const readOnly = b.readOnly ? ' (read-only)' : ''
        return `**${b.scope}:${b.label}** [${b.category}]${readOnly}
  ${usage} | Updated: ${b.lastModified.toISOString().split('T')[0]}
  ${b.description}`
      })

      return lines.join('\n\n')
    },
  })

  // ---------------------------------------------------------------------------
  // knowledge_get - Read a specific knowledge block
  // ---------------------------------------------------------------------------
  const knowledge_get = tool({
    description: `Read the content of a specific knowledge block.

Use this to retrieve stored patterns, conventions, or gotchas.
The content is returned as markdown.`,
    args: {
      label: s.string().describe('Block label (e.g., "patterns", "conventions", "gotchas")'),
      scope: s.enum(['project', 'global']).optional().describe('Block scope (default: project)'),
    },
    async execute(args) {
      const store = getStore()
      const scope = (args.scope ?? 'project') as KnowledgeScope

      try {
        const block = await store.getBlock(scope, args.label)
        return `# ${scope}:${block.label}

**Category:** ${block.category}
**Description:** ${block.description}
**Size:** ${block.charCount}/${block.limit} chars
**Updated:** ${block.lastModified.toISOString()}

---

${block.value}`
      } catch (err) {
        if (err instanceof Error && err.message.includes('not found')) {
          return `Knowledge block not found: ${scope}:${args.label}

Available blocks can be listed with knowledge_list.`
        }
        throw err
      }
    },
  })

  // ---------------------------------------------------------------------------
  // knowledge_set - Create or update a knowledge block
  // ---------------------------------------------------------------------------
  const knowledge_set = tool({
    description: `Create or update a knowledge block (full overwrite).

Use this to store new patterns, conventions, or gotchas.
The content should be markdown format.

IMPORTANT: This overwrites the entire block. Use knowledge_append to add to existing content.`,
    args: {
      label: s.string().describe('Block label (lowercase letters, numbers, dashes, underscores)'),
      value: s.string().describe('Block content (markdown)'),
      scope: s.enum(['project', 'global']).optional().describe('Block scope (default: project)'),
      description: s.string().optional().describe('Block description'),
      category: s
        .enum(['patterns', 'conventions', 'gotchas', 'decisions', 'custom'])
        .optional()
        .describe('Block category (default: custom)'),
    },
    async execute(args) {
      const store = getStore()
      const scope = (args.scope ?? 'project') as KnowledgeScope

      await store.setBlock(scope, args.label, args.value, {
        description: args.description,
        category: args.category,
      })

      return `Updated knowledge block: ${scope}:${args.label} (${args.value.length} chars)`
    },
  })

  // ---------------------------------------------------------------------------
  // knowledge_append - Add content to a knowledge block
  // ---------------------------------------------------------------------------
  const knowledge_append = tool({
    description: `Append content to an existing knowledge block.

Use this to add new patterns, gotchas, or conventions incrementally.
Content is added with a separator (default: two newlines).

This is preferred over knowledge_set when adding to existing knowledge.`,
    args: {
      label: s.string().describe('Block label'),
      content: s.string().describe('Content to append (markdown)'),
      scope: s.enum(['project', 'global']).optional().describe('Block scope (default: project)'),
      separator: s
        .string()
        .optional()
        .describe('Separator between existing and new content (default: "\\n\\n")'),
    },
    async execute(args) {
      const store = getStore()
      const scope = (args.scope ?? 'project') as KnowledgeScope

      await store.appendBlock(scope, args.label, args.content, {
        separator: args.separator,
      })

      const block = await store.getBlock(scope, args.label)
      return `Appended to knowledge block: ${scope}:${args.label} (now ${block.charCount} chars)`
    },
  })

  // ---------------------------------------------------------------------------
  // knowledge_replace - Replace text in a knowledge block
  // ---------------------------------------------------------------------------
  const knowledge_replace = tool({
    description: `Replace a substring within a knowledge block.

Use this for targeted updates without overwriting the entire block.
The old text must exist in the block.`,
    args: {
      label: s.string().describe('Block label'),
      oldText: s.string().describe('Text to replace'),
      newText: s.string().describe('Replacement text'),
      scope: s.enum(['project', 'global']).optional().describe('Block scope (default: project)'),
    },
    async execute(args) {
      const store = getStore()
      const scope = (args.scope ?? 'project') as KnowledgeScope

      await store.replaceInBlock(scope, args.label, args.oldText, args.newText)
      return `Updated knowledge block: ${scope}:${args.label}`
    },
  })

  return {
    knowledge_list,
    knowledge_get,
    knowledge_set,
    knowledge_append,
    knowledge_replace,
  }
}
