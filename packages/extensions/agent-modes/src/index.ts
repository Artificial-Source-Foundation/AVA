/**
 * Agent modes extension.
 *
 * Registers plan mode, minimal mode, and doom loop detection.
 */

import { getSettingsManager } from '@ava/core-v2/config'
import type { Disposable, ExtensionAPI } from '@ava/core-v2/extensions'
import { registerBestOfNMode } from './best-of-n-mode.js'
import { registerDoomLoop } from './doom-loop.js'
import { activate as activateFocusChain } from './focus-chain/index.js'
import { registerMinimalMode } from './minimal-mode.js'
import { registerPlanMode } from './plan-mode.js'
import { createReliabilityMiddleware } from './reliability-middleware.js'
import { registerWindowedMode } from './windowed-mode.js'

export { selectAgentMode } from './selector.js'

const DEFAULT_AGENT_MODE_SETTINGS = {
  bestOfN: 1,
}

export function activate(api: ExtensionAPI): Disposable {
  getSettingsManager().registerCategory('agentModes', DEFAULT_AGENT_MODE_SETTINGS)
  const focusChainDisposable = activateFocusChain(api)

  const disposables = [
    focusChainDisposable,
    registerPlanMode(api),
    registerMinimalMode(api),
    registerWindowedMode(api),
    registerBestOfNMode(api),
    registerDoomLoop(api),
  ]

  const reliability = createReliabilityMiddleware(api)
  disposables.push(api.addToolMiddleware(reliability.middleware))
  disposables.push(reliability.dispose)

  return {
    dispose() {
      for (const d of disposables) d.dispose()
    },
  }
}
