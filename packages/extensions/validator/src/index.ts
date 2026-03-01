/**
 * Validator extension — QA pipeline.
 *
 * Registers built-in validators and listens for agent:completing events
 * to run the validation pipeline before completion.
 */

import type { Disposable, ExtensionAPI } from '@ava/core-v2/extensions'
import { formatReport, registerValidator, runPipeline } from './pipeline.js'
import { DEFAULT_VALIDATOR_CONFIG } from './types.js'
import { lintValidator, syntaxValidator, testValidator, typescriptValidator } from './validators.js'

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
      const { agentId } = data as { agentId: string; result: string }
      let config = DEFAULT_VALIDATOR_CONFIG
      try {
        config =
          api.getSettings<typeof DEFAULT_VALIDATOR_CONFIG>('validator') ?? DEFAULT_VALIDATOR_CONFIG
      } catch {
        // Settings category not registered — use defaults
      }
      const controller = new AbortController()

      void runPipeline([], config, controller.signal, process.cwd())
        .then((result) => {
          api.emit('validation:result', { agentId, ...result })
          if (!result.passed) {
            api.log.warn(`Validation failed:\n${formatReport(result)}`)
          } else {
            api.log.debug(`Validation passed (${result.totalDurationMs}ms)`)
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
