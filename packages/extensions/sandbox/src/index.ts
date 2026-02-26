/**
 * Sandbox extension — Docker sandboxed execution.
 * Provides isolated execution environments for untrusted code.
 */

import type { Disposable, ExtensionAPI } from '@ava/core-v2/extensions'

export function activate(api: ExtensionAPI): Disposable {
  api.log.debug('Sandbox extension activated')
  return { dispose() {} }
}
