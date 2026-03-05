/**
 * Focus chain tracker — tracks task progress within a session.
 */

import type { FocusChain, FocusItem } from './types.js'

let nextId = 1

export function createFocusChain(sessionId: string): FocusChain {
  return { sessionId, items: [], currentFocus: undefined }
}

export function addFocusItem(chain: FocusChain, description: string, parentId?: string): FocusItem {
  const item: FocusItem = {
    id: `focus-${nextId++}`,
    description,
    status: 'pending',
    createdAt: Date.now(),
    parentId,
  }
  chain.items.push(item)
  return item
}

export function startFocusItem(chain: FocusChain, itemId: string): boolean {
  const item = chain.items.find((i) => i.id === itemId)
  if (!item || item.status !== 'pending') return false
  item.status = 'in_progress'
  chain.currentFocus = itemId
  return true
}

export function completeFocusItem(chain: FocusChain, itemId: string): boolean {
  const item = chain.items.find((i) => i.id === itemId)
  if (!item || item.status === 'completed') return false
  item.status = 'completed'
  item.completedAt = Date.now()
  if (chain.currentFocus === itemId) chain.currentFocus = undefined
  return true
}

export function getActiveItems(chain: FocusChain): FocusItem[] {
  return chain.items.filter((i) => i.status === 'in_progress')
}

export function getPendingItems(chain: FocusChain): FocusItem[] {
  return chain.items.filter((i) => i.status === 'pending')
}

/** Reset the ID counter (for testing). */
export function resetIdCounter(): void {
  nextId = 1
}
