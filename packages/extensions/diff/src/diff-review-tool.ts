import { defineTool } from '@ava/core-v2/tools'
import { z } from 'zod'

import type { HunkReviewState } from './hunk-review/state.js'

const DiffReviewSchema = z.object({
  action: z.enum(['list', 'status', 'accept', 'reject']),
  sessionId: z.string().optional(),
  path: z.string().optional(),
  hunkId: z.string().optional(),
})

export function createDiffReviewTool(state: HunkReviewState) {
  return defineTool({
    name: 'diff_review',
    description: 'Review diff hunks and mark each as accepted/rejected',
    schema: DiffReviewSchema,
    permissions: ['read', 'write'],
    async execute(input, ctx) {
      const sessionId = input.sessionId ?? ctx.sessionId

      if (input.action === 'list') {
        const items = state.list(sessionId, input.path)
        return {
          success: true,
          output: JSON.stringify(items, null, 2),
          metadata: { items } as unknown as Record<string, unknown>,
        }
      }

      if (input.action === 'status') {
        const summary = state.summary(sessionId)
        return {
          success: true,
          output: JSON.stringify(summary, null, 2),
          metadata: summary as unknown as Record<string, unknown>,
        }
      }

      if (!input.hunkId) {
        return {
          success: false,
          output: '',
          error: 'hunkId is required for accept/reject actions',
        }
      }

      const ok = state.updateStatus(
        sessionId,
        input.hunkId,
        input.action === 'accept' ? 'accepted' : 'rejected'
      )

      if (!ok) {
        return {
          success: false,
          output: '',
          error: `Hunk not found: ${input.hunkId}`,
        }
      }

      return {
        success: true,
        output: `${input.hunkId} marked ${input.action === 'accept' ? 'accepted' : 'rejected'}`,
        metadata: state.summary(sessionId) as unknown as Record<string, unknown>,
      }
    },
  })
}
