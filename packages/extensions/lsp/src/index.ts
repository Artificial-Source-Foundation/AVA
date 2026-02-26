/**
 * LSP extension — Language Server Protocol integration.
 * Provides diagnostics, completions, and symbol information.
 */

import type { Disposable, ExtensionAPI } from '@ava/core-v2/extensions'

export function activate(api: ExtensionAPI): Disposable {
  api.log.debug('LSP extension activated')
  return { dispose() {} }
}
