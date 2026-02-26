/**
 * Focus chain extension — tracks task progress during sessions.
 * Maintains a chain of focus items showing what the agent is working on.
 */

import type { Disposable, ExtensionAPI } from '@ava/core-v2/extensions'

export function activate(api: ExtensionAPI): Disposable {
  api.log.debug('Focus chain extension activated')
  return { dispose() {} }
}
