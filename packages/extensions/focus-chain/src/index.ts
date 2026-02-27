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

  function getOrCreateChain(sessionId: string): FocusChain {
    let chain = chains.get(sessionId)
    if (!chain) {
      chain = createFocusChain(sessionId)
      chains.set(sessionId, chain)
    }
    return chain
  }

  disposables.push(
    api.on('agent:turn-start', (data) => {
      const { sessionId, description } = data as { sessionId: string; description?: string }
      const chain = getOrCreateChain(sessionId)
      const item = addFocusItem(chain, description ?? 'Processing')
      startFocusItem(chain, item.id)
      api.emit('focus:updated', { sessionId, chain })
    })
  )

  disposables.push(
    api.on('agent:turn-end', (data) => {
      const { sessionId } = data as { sessionId: string }
      const chain = chains.get(sessionId)
      if (chain?.currentFocus) {
        completeFocusItem(chain, chain.currentFocus)
        void api.storage.set(`chain:${sessionId}`, chain)
        api.emit('focus:updated', { sessionId, chain })
      }
    })
  )

  disposables.push(
    api.on('agent:completed', (data) => {
      const { sessionId } = data as { sessionId: string }
      const chain = chains.get(sessionId)
      if (chain) {
        // Complete any remaining items
        for (const item of chain.items) {
          if (item.status !== 'completed') completeFocusItem(chain, item.id)
        }
        void api.storage.set(`chain:${sessionId}`, chain)
        api.emit('focus:completed', { sessionId, chain })
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
