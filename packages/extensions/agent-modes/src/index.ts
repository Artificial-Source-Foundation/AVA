/**
 * Agent modes extension.
 *
 * Registers plan mode, minimal mode, and doom loop detection.
 */

import { getSettingsManager } from '@ava/core-v2/config'
import type { Disposable, ExtensionAPI } from '@ava/core-v2/extensions'
import { registerBestOfNMode } from './best-of-n-mode.js'
import { registerDoomLoop } from './doom-loop.js'
import { registerMinimalMode } from './minimal-mode.js'
import { registerPlanMode } from './plan-mode.js'

export { selectAgentMode } from './selector.js'

const DEFAULT_AGENT_MODE_SETTINGS = {
  bestOfN: 1,
}

export function activate(api: ExtensionAPI): Disposable {
  getSettingsManager().registerCategory('agentModes', DEFAULT_AGENT_MODE_SETTINGS)

  const disposables = [
    registerPlanMode(api),
    registerMinimalMode(api),
    registerBestOfNMode(api),
    registerDoomLoop(api),
  ]

  return {
    dispose() {
      for (const d of disposables) d.dispose()
    },
  }
}
