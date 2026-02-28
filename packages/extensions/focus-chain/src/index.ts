/**
 * Focus chain extension — tracks task progress during sessions.
 *
 * Maintains a chain of focus items showing what the agent is working on.
 * Listens to agent lifecycle events and persists chains to storage.
 */

import type { Disposable, ExtensionAPI } from '@ava/core-v2/extensions'
import { addFocusItem, completeFocusItem, createFocusChain, startFocusItem } from './tracker.js'
import type { FocusChain } from './types.js'

export function activate(api: ExtensionAPI): Disposable {
  const chains = new Map<string, FocusChain>()
  const disposables: Disposable[] = []

  function getOrCreateChain(agentId: string): FocusChain {
    let chain = chains.get(agentId)
    if (!chain) {
      chain = createFocusChain(agentId)
      chains.set(agentId, chain)
    }
    return chain
  }

  disposables.push(
    api.on('turn:start', (data) => {
      const { agentId, description } = data as { agentId: string; description?: string }
      const chain = getOrCreateChain(agentId)
      const item = addFocusItem(chain, description ?? 'Processing')
      startFocusItem(chain, item.id)
      api.emit('focus:updated', { agentId, chain })
    })
  )

  disposables.push(
    api.on('turn:end', (data) => {
      const { agentId } = data as { agentId: string }
      const chain = chains.get(agentId)
      if (chain?.currentFocus) {
        completeFocusItem(chain, chain.currentFocus)
        void api.storage.set(`chain:${agentId}`, chain)
        api.emit('focus:updated', { agentId, chain })
      }
    })
  )

  disposables.push(
    api.on('agent:finish', (data) => {
      const { agentId } = data as { agentId: string }
      const chain = chains.get(agentId)
      if (chain) {
        // Complete any remaining items
        for (const item of chain.items) {
          if (item.status !== 'completed') completeFocusItem(chain, item.id)
        }
        void api.storage.set(`chain:${agentId}`, chain)
        api.emit('focus:completed', { agentId, chain })
      }
    })
  )

  api.log.debug('Focus chain extension activated')

  return {
    dispose() {
      for (const d of disposables) d.dispose()
      chains.clear()
    },
  }
}
