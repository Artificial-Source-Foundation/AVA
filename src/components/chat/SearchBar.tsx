/**
 * Chat Search Bar
 *
 * Inline search bar triggered by Ctrl+F.
 * Searches message content with match count and prev/next navigation.
 */

import { ChevronDown, ChevronUp, Search, X } from 'lucide-solid'
import {
  type Component,
  createEffect,
  createMemo,
  createSignal,
  on,
  onCleanup,
  onMount,
  untrack,
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

  // Reset index when matches change, then notify parent.
  // We track `matches` only; `currentIndex` is written but untracked to
  // avoid the read-write cycle that triggers SolidJS infinite-loop detection.
  createEffect(
    on(matches, (ms) => {
      setCurrentIndex(0)
      const ids = new Set(ms.map((m) => m.id))
      const first = ms.length > 0 ? ms[0] : null
      props.onHighlightChange(ids, first?.id ?? null)
      if (first) props.onNavigate(first.id)
    })
  )

  // Navigate + notify when the user steps through matches (prev/next).
  // Tracks `currentIndex` only; reads `matches` untracked to avoid cycles.
  createEffect(
    on(
      currentIndex,
      (idx) => {
        const ms = untrack(matches)
        if (ms.length === 0) return
        const match = ms[idx]
        if (!match) return
        const ids = new Set(ms.map((m) => m.id))
        props.onHighlightChange(ids, match.id)
        props.onNavigate(match.id)
      },
      { defer: true }
    )
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
    <div
      class="flex items-center"
      style={{
        width: '380px',
        height: '40px',
        gap: '8px',
        padding: '0 10px 0 12px',
        background: 'var(--surface)',
        border: '1px solid var(--border-default)',
        'border-radius': 'var(--radius-md)',
        'box-shadow': '0 4px 16px rgba(0, 0, 0, 0.19)',
      }}
    >
      {/* Search icon */}
      <Search
        class="shrink-0"
        style={{ width: '14px', height: '14px', color: 'var(--text-muted)' }}
      />

      {/* Input */}
      <input
        ref={inputRef}
        type="text"
        value={query()}
        onInput={(e) => setQuery(e.currentTarget.value)}
        onKeyDown={handleKeyDown}
        placeholder="Search messages..."
        class="
          flex-1 min-w-0
          bg-transparent
          text-[var(--text-primary)]
          placeholder:text-[var(--text-muted)]
          focus:outline-none
        "
        style={{
          'font-family': 'var(--font-sans)',
          'font-size': '13px',
        }}
      />

      {/* Match count — mono */}
      <span
        class="shrink-0 tabular-nums whitespace-nowrap"
        style={{
          'font-family': 'var(--font-mono)',
          'font-size': '11px',
          color: 'var(--text-muted)',
        }}
      >
        {query().trim()
          ? matchCount() > 0
            ? `${currentIndex() + 1} / ${matchCount()}`
            : '0 / 0'
          : ''}
      </span>

      {/* Divider */}
      <div
        class="shrink-0"
        style={{
          width: '1px',
          height: '20px',
          background: 'var(--border-default)',
        }}
      />

      {/* Navigation — chevron-up, chevron-down */}
      <button
        type="button"
        onClick={goPrev}
        disabled={matchCount() === 0}
        class="shrink-0 flex items-center justify-center rounded transition-colors hover:text-[var(--text-primary)] disabled:opacity-30"
        style={{ color: 'var(--text-muted)', padding: '2px' }}
        title="Previous match (Shift+Enter)"
        aria-label="Previous match"
      >
        <ChevronUp style={{ width: '16px', height: '16px' }} />
      </button>
      <button
        type="button"
        onClick={goNext}
        disabled={matchCount() === 0}
        class="shrink-0 flex items-center justify-center rounded transition-colors hover:text-[var(--text-primary)] disabled:opacity-30"
        style={{ color: 'var(--text-muted)', padding: '2px' }}
        title="Next match (Enter)"
        aria-label="Next match"
      >
        <ChevronDown style={{ width: '16px', height: '16px' }} />
      </button>

      {/* Close — x */}
      <button
        type="button"
        onClick={() => props.onClose()}
        class="shrink-0 flex items-center justify-center rounded transition-colors hover:text-[var(--text-primary)]"
        style={{ color: 'var(--text-muted)', padding: '2px' }}
        title="Close search (Escape)"
        aria-label="Close search"
      >
        <X style={{ width: '14px', height: '14px' }} />
      </button>
    </div>
  )
}
