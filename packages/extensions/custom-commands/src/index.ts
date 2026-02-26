/**
 * Custom commands extension — TOML-based user commands.
 * Discovers and registers user-defined commands from TOML files.
 */

import type { Disposable, ExtensionAPI } from '@ava/core-v2/extensions'

export function activate(api: ExtensionAPI): Disposable {
  api.log.debug('Custom commands extension activated')
  return { dispose() {} }
}
