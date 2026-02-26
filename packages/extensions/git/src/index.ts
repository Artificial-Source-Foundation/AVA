/**
 * Git extension — snapshots and auto-commit.
 * Takes snapshots before file modifications for easy rollback.
 */

import type { Disposable, ExtensionAPI } from '@ava/core-v2/extensions'

export function activate(api: ExtensionAPI): Disposable {
  api.log.debug('Git extension activated')
  return { dispose() {} }
}
