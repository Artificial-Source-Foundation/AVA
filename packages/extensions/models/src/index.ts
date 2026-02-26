/**
 * Models extension — model registry with capabilities and pricing.
 * Maintains a registry of available LLM models across all providers.
 */

import type { Disposable, ExtensionAPI } from '@ava/core-v2/extensions'

export function activate(api: ExtensionAPI): Disposable {
  api.log.debug('Models extension activated')
  return { dispose() {} }
}
