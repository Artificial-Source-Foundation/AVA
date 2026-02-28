/**
 * Chat Search Bar
 *
 * Inline search bar triggered by Ctrl+F.
 * Searches message content with match count and prev/next navigation.
 */

import { ChevronDown, ChevronUp, X } from 'lucide-solid'
import {
  type Component,
  createEffect,
  createMemo,
  createSignal,
  on,
  onCleanup,
  onMount,
} from 'solid-js'
import type { Message } from '../../types'

interface SearchBarProps {
  messages: Message[]
  onClose: () => void
  onNavigate: (messageId: string) => void
  onHighlightChange: (matchIds: Set<string>, currentId: string | null) => void
}

export const SearchBar: Component<SearchBarProps> = (props) => {
  let inputRef: HTMLInputElement | undefined
  const [query, setQuery] = createSignal('')
  const [currentIndex, setCurrentIndex] = createSignal(0)

  // Find all messages matching the query
  const matches = createMemo(() => {
    const q = query().toLowerCase().trim()
    if (!q) return [] as Message[]
    return props.messages.filter((m) => m.content.toLowerCase().includes(q))
  })

  const matchCount = () => matches().length
  const currentMatch = () => (matchCount() > 0 ? matches()[currentIndex()] : null)

  // Notify parent of highlight changes
  createEffect(
    on([matches, currentIndex] as const, () => {
      const ids = new Set(matches().map((m) => m.id))
      const current = currentMatch()
      props.onHighlightChange(ids, current?.id ?? null)
    })
  )

  // Navigate to current match
  createEffect(
    on(currentMatch, (match) => {
      if (match) props.onNavigate(match.id)
    })
  )

  // Reset index when matches change
  createEffect(
    on(matches, () => {
      setCurrentIndex(0)
    })
  )

  const goNext = () => {
    if (matchCount() === 0) return
    setCurrentIndex((i) => (i + 1) % matchCount())
  }

  const goPrev = () => {
    if (matchCount() === 0) return
    setCurrentIndex((i) => (i - 1 + matchCount()) % matchCount())
  }

  const handleKeyDown = (e: KeyboardEvent) => {
    if (e.key === 'Escape') {
      props.onClose()
    } else if (e.key === 'Enter') {
      if (e.shiftKey) {
        goPrev()
      } else {
        goNext()
      }
      e.preventDefault()
    }
  }

  // Auto-focus input on mount
  onMount(() => {
    inputRef?.focus()
  })

  // Clear highlights on unmount
  onCleanup(() => {
    props.onHighlightChange(new Set(), null)
  })

  return (
    <div class="flex items-center gap-2 px-3 py-1.5 bg-[var(--surface-raised)] border-b border-[var(--border-default)]">
      <input
        ref={inputRef}
        type="text"
        value={query()}
        onInput={(e) => setQuery(e.currentTarget.value)}
        onKeyDown={handleKeyDown}
        placeholder="Search messages..."
        class="
          flex-1 min-w-0
          bg-[var(--surface-sunken)] border border-[var(--border-default)]
          rounded-[var(--radius-sm)] px-2 py-1
          text-xs text-[var(--text-primary)]
          placeholder:text-[var(--text-muted)]
          focus:outline-none focus:border-[var(--accent)]
          transition-colors
        "
      />

      {/* Match count */}
      <span class="text-[10px] text-[var(--text-muted)] tabular-nums whitespace-nowrap">
        {query().trim()
          ? matchCount() > 0
            ? `${currentIndex() + 1} of ${matchCount()}`
            : 'No results'
          : ''}
      </span>

      {/* Navigation */}
      <button
        type="button"
        onClick={goPrev}
        disabled={matchCount() === 0}
        class="p-0.5 rounded text-[var(--text-muted)] hover:text-[var(--text-primary)] hover:bg-[var(--alpha-white-5)] transition-colors disabled:opacity-30"
        title="Previous match (Shift+Enter)"
        aria-label="Previous match"
      >
        <ChevronUp class="w-3.5 h-3.5" />
      </button>
      <button
        type="button"
        onClick={goNext}
        disabled={matchCount() === 0}
        class="p-0.5 rounded text-[var(--text-muted)] hover:text-[var(--text-primary)] hover:bg-[var(--alpha-white-5)] transition-colors disabled:opacity-30"
        title="Next match (Enter)"
        aria-label="Next match"
      >
        <ChevronDown class="w-3.5 h-3.5" />
      </button>

      {/* Close */}
      <button
        type="button"
        onClick={props.onClose}
        class="p-0.5 rounded text-[var(--text-muted)] hover:text-[var(--text-primary)] hover:bg-[var(--alpha-white-5)] transition-colors"
        title="Close search (Escape)"
        aria-label="Close search"
      >
        <X class="w-3.5 h-3.5" />
      </button>
    </div>
  )
}
