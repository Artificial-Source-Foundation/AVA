/**
 * Quick Session Switcher
 *
 * Keyboard-driven overlay (Ctrl+J) with fuzzy search for fast session switching.
 */

import { Dialog } from '@kobalte/core/dialog'
import { MessageSquare } from 'lucide-solid'
import { type Component, createEffect, createMemo, createSignal, For, on, Show } from 'solid-js'
import { useSession } from '../../stores/session'

export const SessionSwitcher: Component<{ open: boolean; onClose: () => void }> = (props) => {
  let inputRef: HTMLInputElement | undefined
  const { sessions, currentSession, switchSession } = useSession()
  const [query, setQuery] = createSignal('')
  const [selectedIndex, setSelectedIndex] = createSignal(0)

  const filtered = createMemo(() => {
    const q = query().toLowerCase().trim()
    const all = sessions()
    if (!q) return all
    return all.filter((s) => s.name.toLowerCase().includes(q))
  })

  // Reset on open
  createEffect(
    on(
      () => props.open,
      (open) => {
        if (open) {
          setQuery('')
          setSelectedIndex(0)
          requestAnimationFrame(() => inputRef?.focus())
        }
      }
    )
  )

  // Clamp index when results change
  createEffect(
    on(filtered, (f) => {
      setSelectedIndex((i) => Math.min(i, Math.max(0, f.length - 1)))
    })
  )

  const handleSelect = (sessionId: string) => {
    if (sessionId !== currentSession()?.id) {
      switchSession(sessionId)
    }
    props.onClose()
  }

  const handleKeyDown = (e: KeyboardEvent) => {
    const count = filtered().length
    if (e.key === 'ArrowDown') {
      e.preventDefault()
      setSelectedIndex((i) => (i + 1) % count)
    } else if (e.key === 'ArrowUp') {
      e.preventDefault()
      setSelectedIndex((i) => (i - 1 + count) % count)
    } else if (e.key === 'Enter') {
      e.preventDefault()
      const session = filtered()[selectedIndex()]
      if (session) handleSelect(session.id)
    }
  }

  const formatTime = (ts: number) => {
    const diff = Date.now() - ts
    if (diff < 60_000) return 'just now'
    if (diff < 3_600_000) return `${Math.floor(diff / 60_000)}m ago`
    if (diff < 86_400_000) return `${Math.floor(diff / 3_600_000)}h ago`
    return new Date(ts).toLocaleDateString()
  }

  return (
    <Dialog open={props.open} onOpenChange={(open) => !open && props.onClose()}>
      <Dialog.Portal>
        <Dialog.Overlay class="fixed inset-0 z-[var(--z-modal)] bg-black/40" />
        <Dialog.Content
          class="
            fixed z-[var(--z-modal)]
            top-[20%] left-1/2 -translate-x-1/2
            w-[min(480px,90vw)]
            bg-[var(--surface-overlay)] border border-[var(--border-default)]
            rounded-[var(--radius-xl)] shadow-[var(--shadow-xl)]
            overflow-hidden
          "
          onKeyDown={handleKeyDown}
        >
          {/* Search input */}
          <div class="px-3 py-2 border-b border-[var(--border-subtle)]">
            <input
              ref={inputRef}
              type="text"
              value={query()}
              onInput={(e) => setQuery(e.currentTarget.value)}
              placeholder="Switch session..."
              class="
                w-full bg-transparent text-sm text-[var(--text-primary)]
                placeholder:text-[var(--text-muted)]
                focus:outline-none
              "
            />
          </div>

          {/* Results */}
          <div class="max-h-[320px] overflow-y-auto py-1 scroll-smooth">
            <Show
              when={filtered().length > 0}
              fallback={
                <div class="px-4 py-6 text-center text-xs text-[var(--text-muted)]">
                  No sessions found
                </div>
              }
            >
              <For each={filtered()}>
                {(session, index) => {
                  const isCurrent = () => session.id === currentSession()?.id
                  return (
                    <button
                      type="button"
                      onClick={() => handleSelect(session.id)}
                      class="
                        w-full flex items-center gap-3 px-3 py-2 text-left
                        transition-colors
                      "
                      classList={{
                        'bg-[var(--accent-subtle)]': index() === selectedIndex(),
                        'hover:bg-[var(--alpha-white-5)]': index() !== selectedIndex(),
                      }}
                    >
                      <MessageSquare
                        class="w-4 h-4 flex-shrink-0"
                        classList={{
                          'text-[var(--accent)]': isCurrent(),
                          'text-[var(--text-muted)]': !isCurrent(),
                        }}
                      />
                      <div class="flex-1 min-w-0">
                        <div class="flex items-center gap-2">
                          <span class="text-sm text-[var(--text-primary)] truncate">
                            {session.name}
                          </span>
                          <Show when={isCurrent()}>
                            <span class="text-[9px] text-[var(--accent)] font-medium uppercase">
                              current
                            </span>
                          </Show>
                        </div>
                        <span class="text-[10px] text-[var(--text-muted)]">
                          {formatTime(session.updatedAt)}
                          <Show when={session.messageCount}>
                            {' '}
                            &middot; {session.messageCount} messages
                          </Show>
                        </span>
                      </div>
                    </button>
                  )
                }}
              </For>
            </Show>
          </div>

          {/* Footer hints */}
          <div class="px-3 py-1.5 border-t border-[var(--border-subtle)] text-[9px] text-[var(--text-muted)] flex gap-3">
            <span>
              <kbd class="font-mono">↑↓</kbd> navigate
            </span>
            <span>
              <kbd class="font-mono">Enter</kbd> switch
            </span>
            <span>
              <kbd class="font-mono">Esc</kbd> close
            </span>
          </div>
        </Dialog.Content>
      </Dialog.Portal>
    </Dialog>
  )
}
