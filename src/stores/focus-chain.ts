/**
 * Focus Chain Store
 * Bridges focus-chain extension events to SolidJS signals.
 * Provides a reactive task progress indicator.
 */

import { createMemo, createSignal, onCleanup, onMount } from 'solid-js'

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

export function useFocusChain() {
  onMount(() => {
    const handler = (e: Event) => {
      const detail = (e as CustomEvent<FocusUpdateDetail>).detail
      if (detail.items) setFocusItems(detail.items)
      if (detail.currentFocus !== undefined) setCurrentFocus(detail.currentFocus)
    }
    window.addEventListener('ava:focus-updated', handler)
    onCleanup(() => window.removeEventListener('ava:focus-updated', handler))
  })

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
