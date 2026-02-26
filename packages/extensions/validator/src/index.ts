/**
 * Validator extension — QA pipeline.
 *
 * Registers built-in validators and listens for agent:completing events
 * to block completion when validation fails.
 */

import type { Disposable, ExtensionAPI } from '@ava/core-v2/extensions'
import { registerValidator } from './pipeline.js'
import { lintValidator, syntaxValidator, testValidator, typescriptValidator } from './validators.js'

export function activate(api: ExtensionAPI): Disposable {
  // Register built-in validators
  registerValidator(syntaxValidator)
  registerValidator(typescriptValidator)
  registerValidator(lintValidator)
  registerValidator(testValidator)

  api.log.debug('Registered 4 built-in validators: syntax, typescript, lint, test')

  return {
    dispose() {
      // Validators are cleaned up with the registry
    },
  }
}
