/**
 * Scheduler extension — background task scheduling.
 * Manages periodic tasks like auto-save, indexing, and cleanup.
 */

import type { Disposable, ExtensionAPI } from '@ava/core-v2/extensions'

export function activate(api: ExtensionAPI): Disposable {
  api.log.debug('Scheduler extension activated')
  return { dispose() {} }
}
