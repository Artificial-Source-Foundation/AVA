/**
 * Instructions extension — loads project/directory instructions.
 * Finds and loads instruction files that configure agent behavior.
 */

import type { Disposable, ExtensionAPI } from '@ava/core-v2/extensions'

export function activate(api: ExtensionAPI): Disposable {
  api.log.debug('Instructions extension activated')
  return { dispose() {} }
}
