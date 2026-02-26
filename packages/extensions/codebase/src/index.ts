/**
 * Codebase extension — repo map, symbols, and PageRank.
 * Provides codebase intelligence for context-aware agent operations.
 */

import type { Disposable, ExtensionAPI } from '@ava/core-v2/extensions'

export function activate(api: ExtensionAPI): Disposable {
  api.log.debug('Codebase intelligence extension activated')
  return { dispose() {} }
}
