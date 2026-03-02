/**
 * Recall tool — search past sessions from the agent.
 */

import { defineTool } from '@ava/core-v2/tools'
import { z } from 'zod'
import type { RecallSearch } from './search.js'

const recallSchema = z.object({
  query: z.string().describe('Search query to find in past conversations'),
  limit: z.number().optional().describe('Maximum results to return (default 10)'),
  role: z.enum(['user', 'assistant']).optional().describe('Filter by message role'),
})

export function createRecallTool(search: RecallSearch) {
  return defineTool({
    name: 'recall',
    description:
      'Search across past conversation sessions using full-text search. ' +
      'Finds relevant messages from previous sessions to recall context, ' +
      'decisions, and solutions discussed before.',
    schema: recallSchema,
    async execute(input) {
      const results = await search.search(input.query, {
        limit: input.limit ?? 10,
        role: input.role,
      })

      if (results.length === 0) {
        return {
          success: true,
          output: `No results found for "${input.query}".`,
        }
      }

      const formatted = results.map((r, i) => {
        const prefix = `[${i + 1}] Session: ${r.sessionId.slice(0, 8)}... | ${r.role} | msg #${r.messageIndex}`
        return `${prefix}\n${r.snippet}`
      })

      return {
        success: true,
        output: `Found ${results.length} results for "${input.query}":\n\n${formatted.join('\n\n')}`,
      }
    },
  })
}
