/**
 * Slash commands extension — registers built-in commands.
 *
 * Commands emit events to stay decoupled from implementation details.
 * Other extensions (UI, agent loop) listen and act on these events.
 */

import type { Disposable, ExtensionAPI } from '@ava/core-v2/extensions'
import { createBuiltinCommands } from './commands.js'

export function activate(api: ExtensionAPI): Disposable {
  const commands = createBuiltinCommands(api.emit.bind(api))
  const disposables = commands.map((cmd) => api.registerCommand(cmd))

  api.log.debug(`Slash commands extension activated (${disposables.length} commands)`)

  return {
    dispose() {
      for (const d of disposables) d.dispose()
    },
  }
}
