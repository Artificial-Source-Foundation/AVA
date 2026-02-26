/**
 * Diff extension — tracks file changes during agent sessions.
 * Captures before/after content for review and rollback.
 */

import type { Disposable, ExtensionAPI } from '@ava/core-v2/extensions'

export function activate(api: ExtensionAPI): Disposable {
  api.log.debug('Diff tracking extension activated')
  return { dispose() {} }
}
