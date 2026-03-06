import type { ToolContext } from '@ava/core-v2/tools'
import type { InvokeSubagentInput, InvokeSubagentResult } from './invoke-subagent.js'

export interface EngineerLoopConfig {
  maxReviewAttempts: number
  autoReview: boolean
}

export interface EngineerResult {
  success: boolean
  summary: string
  reviewAttempts: number
  approved: boolean
  warnings: string[]
}

export type ReviewerInvoker = (
  input: InvokeSubagentInput,
  ctx: ToolContext
) => Promise<InvokeSubagentResult>
export type EngineerStep = (task: string, attempt: number, ctx: ToolContext) => Promise<string>

export async function runEngineerWithReview(
  task: string,
  config: EngineerLoopConfig,
  context: ToolContext,
  runEngineerStep: EngineerStep,
  invokeReviewer: ReviewerInvoker
): Promise<EngineerResult> {
  const maxAttempts = Math.max(1, config.maxReviewAttempts || 3)
  const warnings: string[] = []
  let lastSummary = ''

  for (let attempt = 1; attempt <= maxAttempts; attempt++) {
    lastSummary = await runEngineerStep(task, attempt, context)
    if (!config.autoReview) {
      return { success: true, summary: lastSummary, reviewAttempts: 0, approved: true, warnings }
    }

    context.onEvent?.({ type: 'praxis:review-requested', agentId: context.sessionId, attempt })
    const review = await invokeReviewer(
      {
        type: 'reviewer',
        task: `Review engineer output for: ${task}`,
        context: lastSummary,
        run_validation: true,
      },
      context
    )

    context.onEvent?.({
      type: 'praxis:review-complete',
      agentId: context.sessionId,
      attempt,
      approved: review.approved,
    })

    if (review.approved) {
      return {
        success: true,
        summary: lastSummary,
        reviewAttempts: attempt,
        approved: true,
        warnings,
      }
    }

    warnings.push(review.feedback || review.output || `Review failed on attempt ${attempt}`)
  }

  return {
    success: false,
    summary: lastSummary,
    reviewAttempts: maxAttempts,
    approved: false,
    warnings,
  }
}
