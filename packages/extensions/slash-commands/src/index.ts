/**
 * Slash commands extension — built-in /commands.
 * Registers /help, /clear, /mode, /model, and other built-in commands.
 */

import type { Disposable, ExtensionAPI } from '@ava/core-v2/extensions'

export function activate(api: ExtensionAPI): Disposable {
  api.log.debug('Slash commands extension activated')
  return { dispose() {} }
}
