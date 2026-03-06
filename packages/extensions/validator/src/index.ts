/**
 * Validator extension — QA pipeline.
 *
 * Registers built-in validators and listens for agent:completing events
 * to run the validation pipeline before completion.
 */

import type { Disposable, ExtensionAPI } from '@ava/core-v2/extensions'
import { formatReport, registerValidator, runPipeline } from './pipeline.js'
import { reviewAgentOutput } from './reviewer.js'
import { DEFAULT_VALIDATOR_CONFIG } from './types.js'
import { lintValidator, syntaxValidator, testValidator, typescriptValidator } from './validators.js'

interface AgentCompletingEvent {
  agentId: string
  goal: string
  result: string
  filesChanged?: string[]
  diffs?: string[]
}

export function activate(api: ExtensionAPI): Disposable {
  const disposables: Disposable[] = []

  // Register built-in validators
  registerValidator(syntaxValidator)
  registerValidator(typescriptValidator)
  registerValidator(lintValidator)
  registerValidator(testValidator)

  // Run validation pipeline when agent is completing
  disposables.push(
    api.on('agent:completing', (data) => {
      const { agentId, goal, result, filesChanged = [], diffs = [] } = data as AgentCompletingEvent
      let config = DEFAULT_VALIDATOR_CONFIG
      try {
        config =
          api.getSettings<typeof DEFAULT_VALIDATOR_CONFIG>('validator') ?? DEFAULT_VALIDATOR_CONFIG
      } catch {
        // Settings category not registered — use defaults
      }
      const controller = new AbortController()

      void runPipeline(filesChanged, config, controller.signal, process.cwd())
        .then(async (pipelineResult) => {
          api.emit('validation:result', { agentId, ...pipelineResult })
          if (!pipelineResult.passed) {
            api.log.warn(`Validation failed:\n${formatReport(pipelineResult)}`)
          } else {
            api.log.debug(`Validation passed (${pipelineResult.totalDurationMs}ms)`)
          }

          if (!pipelineResult.passed || !config.reviewEnabled) {
            return
          }

          const provider = config.reviewProvider ?? 'anthropic'
          const model = config.reviewModel ?? 'claude-haiku-4-5'
          const maxRetries = Math.max(0, config.reviewMaxRetries ?? 1)

          try {
            let attempts = 0
            let reviewOutput = result
            let review = await reviewAgentOutput(
              goal,
              reviewOutput,
              filesChanged,
              diffs,
              provider,
              model,
              controller.signal
            )

            while (!review.approved && attempts < maxRetries) {
              attempts++
              const issueSummary =
                review.issues.length > 0 ? `\nIssues:\n- ${review.issues.join('\n- ')}` : ''
              reviewOutput = `${result}\n\n[Reviewer feedback]\n${review.feedback}${issueSummary}`
              review = await reviewAgentOutput(
                goal,
                reviewOutput,
                filesChanged,
                diffs,
                provider,
                model,
                controller.signal
              )
            }

            api.emit('validation:review', {
              agentId,
              approved: review.approved,
              feedback: review.feedback,
              confidence: review.confidence,
              issues: review.issues,
              attempts,
              model,
              provider,
            })

            if (!review.approved) {
              api.log.warn(`LLM reviewer rejected output: ${review.feedback}`)
            }
          } catch (err) {
            api.log.error(
              `Review phase failed: ${err instanceof Error ? err.message : String(err)}`
            )
          }
        })
        .catch((err) => {
          api.log.error(
            `Validation pipeline error: ${err instanceof Error ? err.message : String(err)}`
          )
        })
    })
  )

  api.log.debug('Registered 4 built-in validators: syntax, typescript, lint, test')

  return {
    dispose() {
      for (const d of disposables) d.dispose()
    },
  }
}
