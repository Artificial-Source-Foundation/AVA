/**
 * Skills extension — auto-invoked knowledge modules.
 * Skills activate based on file globs and project type.
 */

import type { Disposable, ExtensionAPI } from '@ava/core-v2/extensions'

export function activate(api: ExtensionAPI): Disposable {
  api.log.debug('Skills extension activated')
  return { dispose() {} }
}
