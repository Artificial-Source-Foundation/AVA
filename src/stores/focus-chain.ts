/**
 * Focus Chain Store
 * Bridges focus-chain extension events to SolidJS signals.
 * Provides a reactive task progress indicator.
 */

import { createMemo, createSignal } from 'solid-js'
import { installReplaceableWindowListener } from '../lib/replaceable-window-listener'

export interface FocusItem {
  id: string
  description: string
  status: 'pending' | 'in_progress' | 'completed'
}

interface FocusUpdateDetail {
  items: FocusItem[]
  currentFocus: string | null
}

const [focusItems, setFocusItems] = createSignal<FocusItem[]>([])
const [currentFocus, setCurrentFocus] = createSignal<string | null>(null)

if (typeof window !== 'undefined') {
  installReplaceableWindowListener('focus-chain:updated', (target) => {
    const listener = (e: Event) => {
      const detail = (e as CustomEvent<FocusUpdateDetail>).detail
      if (detail.items) setFocusItems(detail.items)
      if (detail.currentFocus !== undefined) setCurrentFocus(detail.currentFocus)
    }

    target.addEventListener('ava:focus-updated', listener)
    return () => target.removeEventListener('ava:focus-updated', listener)
  })
}

export function useFocusChain() {
  const completedCount = createMemo(
    () => focusItems().filter((i) => i.status === 'completed').length
  )
  const totalCount = createMemo(() => focusItems().length)
  const progressPercent = createMemo(() => {
    const total = totalCount()
    if (total === 0) return 0
    return Math.round((completedCount() / total) * 100)
  })
  const currentDescription = createMemo(() => {
    const id = currentFocus()
    if (!id) {
      const inProgress = focusItems().find((i) => i.status === 'in_progress')
      return inProgress?.description ?? null
    }
    return focusItems().find((i) => i.id === id)?.description ?? null
  })

  return {
    items: focusItems,
    currentFocus,
    completedCount,
    totalCount,
    progressPercent,
    currentDescription,
  }
}
