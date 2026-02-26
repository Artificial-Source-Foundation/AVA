/**
 * Agent modes extension.
 *
 * Registers plan mode, minimal mode, and doom loop detection.
 */

import type { Disposable, ExtensionAPI } from '@ava/core-v2/extensions'
import { registerDoomLoop } from './doom-loop.js'
import { registerMinimalMode } from './minimal-mode.js'
import { registerPlanMode } from './plan-mode.js'

export function activate(api: ExtensionAPI): Disposable {
  const disposables = [registerPlanMode(api), registerMinimalMode(api), registerDoomLoop(api)]

  return {
    dispose() {
      for (const d of disposables) d.dispose()
    },
  }
}
