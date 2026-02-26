/**
 * Integrations extension — external API integrations.
 * Provides web search (Exa, Tavily) and other external services.
 */

import type { Disposable, ExtensionAPI } from '@ava/core-v2/extensions'

export function activate(api: ExtensionAPI): Disposable {
  api.log.debug('Integrations extension activated')
  return { dispose() {} }
}
