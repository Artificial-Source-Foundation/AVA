/**
 * Submit Button
 *
 * Send / Cancel button cluster rendered inside the textarea area.
 * Displays streaming elapsed time and a cancel button when processing.
 *
 * When the agent is running the send button stays enabled so users can
 * queue messages (Enter), interrupt (Ctrl+Enter), or
 * post-complete messages (Alt+Enter).
 *
 * Right-clicking the send button during processing opens a context menu
 * to choose between Queue, Interrupt, and Post-complete message tiers.
 *
 * Accessibility: Full keyboard navigation in menu (Arrow keys, Enter, Escape),
 * focus trap, and proper ARIA attributes.
 */

import { ArrowUp, ChevronDown, Clock, MessageSquare, Square, Zap } from 'lucide-solid'
import { type Accessor, type Component, createSignal, onCleanup, Show } from 'solid-js'
import { formatSeconds } from '../../../lib/format-time'

// ---------------------------------------------------------------------------
// Props
// ---------------------------------------------------------------------------

export interface SubmitButtonProps {
  isProcessing: Accessor<boolean>
  isStreaming: Accessor<boolean>
  elapsedSeconds: Accessor<number>
  onCancel: () => void
  inputHasText: Accessor<boolean>
  queuedCount?: Accessor<number>
  escapeHint?: Accessor<boolean>
  onQueue?: () => void
  onInterrupt?: () => void
  onPostComplete?: () => void
}

// ---------------------------------------------------------------------------
// Component
// ---------------------------------------------------------------------------

export const SubmitButton: Component<SubmitButtonProps> = (props) => {
  const [menuOpen, setMenuOpen] = createSignal(false)
  let menuRef: HTMLDivElement | undefined
  let triggerRef: HTMLButtonElement | undefined
  const menuItemRefs: (HTMLButtonElement | undefined)[] = []
  let currentFocusIndex = 0

  const menuItems: Array<{ action: 'queue' | 'interrupt' | 'postComplete'; label: string }> = [
    { action: 'queue', label: 'Queue for next turn' },
    { action: 'interrupt', label: 'Interrupt and send now' },
    { action: 'postComplete', label: 'Queue for after agent stops' },
  ]

  // Close menu on outside click
  const handleDocClick = (e: MouseEvent): void => {
    if (menuRef && !menuRef.contains(e.target as Node)) {
      setMenuOpen(false)
    }
  }

  // Attach/detach listener based on menu state
  const openMenu = (e: MouseEvent): void => {
    e.preventDefault()
    e.stopPropagation()
    if (!props.isProcessing() || !props.inputHasText()) return
    setMenuOpen(true)
    currentFocusIndex = 0
    // Defer so the current click doesn't immediately close it
    requestAnimationFrame(() => {
      document.addEventListener('click', handleDocClick, { once: true })
      // Focus first menu item
      menuItemRefs[0]?.focus()
    })
  }

  // Handle keyboard navigation within menu
  const handleMenuKeyDown = (e: KeyboardEvent): void => {
    switch (e.key) {
      case 'ArrowDown':
        e.preventDefault()
        currentFocusIndex = (currentFocusIndex + 1) % menuItems.length
        menuItemRefs[currentFocusIndex]?.focus()
        break
      case 'ArrowUp':
        e.preventDefault()
        currentFocusIndex = (currentFocusIndex - 1 + menuItems.length) % menuItems.length
        menuItemRefs[currentFocusIndex]?.focus()
        break
      case 'Home':
        e.preventDefault()
        currentFocusIndex = 0
        menuItemRefs[0]?.focus()
        break
      case 'End':
        e.preventDefault()
        currentFocusIndex = menuItems.length - 1
        menuItemRefs[currentFocusIndex]?.focus()
        break
      case 'Escape':
        e.preventDefault()
        setMenuOpen(false)
        triggerRef?.focus()
        break
      case 'Tab':
        // Close menu on tab out
        setMenuOpen(false)
        break
    }
  }

  onCleanup(() => {
    document.removeEventListener('click', handleDocClick)
  })

  const handleMenuAction = (action: 'queue' | 'interrupt' | 'postComplete'): void => {
    setMenuOpen(false)
    triggerRef?.focus()
    if (action === 'queue') props.onQueue?.()
    else if (action === 'interrupt') props.onInterrupt?.()
    else if (action === 'postComplete') props.onPostComplete?.()
  }

  return (
    <div class="absolute right-0 top-1/2 -translate-y-1/2 flex items-center gap-2">
      {/* Double-Escape hint */}
      <Show when={props.escapeHint?.()}>
        <span class="text-[var(--text-xs)] text-[var(--warning)] font-medium whitespace-nowrap">
          Press Esc again to cancel
        </span>
      </Show>

      {/* Queued message count badge */}
      <Show when={props.queuedCount && props.queuedCount() > 0}>
        <span
          class="
            flex items-center justify-center
            min-w-[20px] h-[20px] px-1.5
            text-[var(--text-2xs)] font-semibold tabular-nums
            bg-[var(--accent)] text-[var(--text-on-accent)]
            rounded-full
          "
          title={`${props.queuedCount!()} queued message(s)`}
        >
          {props.queuedCount!()}
        </span>
      </Show>

      {/* Streaming elapsed time */}
      <Show when={props.isStreaming()}>
        <span class="flex items-center gap-1.5 text-[var(--text-xs)] text-[var(--text-tertiary)] tabular-nums">
          <span class="h-2 w-2 rounded-full bg-[var(--chat-streaming-indicator)] animate-pulse-subtle" />
          {formatSeconds(props.elapsedSeconds())}
        </span>
      </Show>

      {/* Cancel button */}
      <Show when={props.isProcessing()}>
        <button
          type="button"
          onClick={() => props.onCancel()}
          class="
            flex items-center justify-center
            w-8 h-8
            bg-[var(--error)]/90 hover:bg-[var(--error)]
            text-[var(--text-on-accent)] rounded-[10px]
            transition-[background-color,transform] active:scale-95
          "
          title="Cancel (Esc Esc)"
          aria-label="Cancel agent run"
        >
          <Square class="w-3.5 h-3.5" />
        </button>
      </Show>

      {/* Send button with context menu for mid-stream tier selection */}
      <div class="relative" ref={menuRef}>
        <div class="flex items-center">
          <button
            ref={triggerRef}
            type="submit"
            disabled={!props.inputHasText()}
            class={`
              flex items-center justify-center
              w-8 h-8 rounded-[10px]
              transition-[background-color,box-shadow,transform] active:scale-95
              focus:outline-none focus-visible:ring-2 focus-visible:ring-[var(--accent)] focus-visible:ring-offset-2
              ${
                props.inputHasText()
                  ? 'bg-[var(--accent)] hover:bg-[var(--accent-hover)] text-[var(--text-on-accent)]'
                  : 'bg-[var(--gray-4)] text-[var(--gray-7)] cursor-not-allowed'
              }
              ${props.isProcessing() && props.inputHasText() ? 'rounded-r-none' : ''}
            `}
            title={props.isProcessing() ? 'Queue for next turn (Enter)' : 'Send message (Enter)'}
            aria-label={props.isProcessing() ? 'Queue message for next turn' : 'Send message'}
            aria-expanded={menuOpen()}
            aria-haspopup="menu"
            onContextMenu={(e) => openMenu(e)}
          >
            <ArrowUp class="w-4 h-4" stroke-width={2.5} />
          </button>

          {/* Chevron dropdown trigger — only during processing with text */}
          <Show when={props.isProcessing() && props.inputHasText()}>
            <button
              type="button"
              onClick={(e) => {
                e.preventDefault()
                e.stopPropagation()
                const nextOpen = !menuOpen()
                setMenuOpen(nextOpen)
                currentFocusIndex = 0
                if (nextOpen) {
                  requestAnimationFrame(() => {
                    document.addEventListener('click', handleDocClick, { once: true })
                    menuItemRefs[0]?.focus()
                  })
                }
              }}
              class="
                flex items-center justify-center
                w-5 h-8 rounded-r-[10px] border-l border-white/20
                bg-[var(--accent)] hover:bg-[var(--accent-hover)] text-[var(--text-on-accent)]
                transition-[background-color,transform] active:scale-95
                focus:outline-none focus-visible:ring-2 focus-visible:ring-[var(--accent)] focus-visible:ring-offset-2
              "
              title="Choose message tier"
              aria-label="Choose message tier"
              aria-expanded={menuOpen()}
              aria-haspopup="menu"
              aria-controls={menuOpen() ? 'tier-selection-menu' : undefined}
            >
              <ChevronDown class="w-3 h-3" />
            </button>
          </Show>
        </div>

        {/* Tier selection dropdown menu */}
        <Show when={menuOpen()}>
          <div
            id="tier-selection-menu"
            ref={menuRef}
            class="
              absolute right-0 bottom-full mb-2
              w-64 py-1
              bg-[var(--surface-raised)] border border-[var(--border-default)]
              rounded-lg shadow-lg shadow-black/20
              z-50
            "
            role="menu"
            aria-label="Message tier options"
            onKeyDown={handleMenuKeyDown}
          >
            <button
              ref={(el) => {
                menuItemRefs[0] = el
              }}
              type="button"
              onClick={() => handleMenuAction('queue')}
              class="
                w-full flex items-center gap-3 px-3 py-2
                hover:bg-[var(--gray-3)] focus:bg-[var(--gray-3)] focus:outline-none
                transition-colors text-left
              "
              role="menuitem"
              tabindex="-1"
              aria-label="Queue for next turn. Press Enter to select."
            >
              <MessageSquare class="w-4 h-4 text-[var(--accent)] shrink-0" />
              <div class="flex-1 min-w-0">
                <div class="text-[var(--text-base)] text-[var(--text-primary)] font-medium">
                  Queue
                </div>
                <div class="text-[var(--text-xs)] text-[var(--text-muted)]">
                  Queue for next turn
                </div>
              </div>
              <span class="text-[var(--text-xs)] text-[var(--text-tertiary)] tabular-nums shrink-0">
                Enter
              </span>
            </button>

            <button
              ref={(el) => {
                menuItemRefs[1] = el
              }}
              type="button"
              onClick={() => handleMenuAction('interrupt')}
              class="
                w-full flex items-center gap-3 px-3 py-2
                hover:bg-[var(--gray-3)] focus:bg-[var(--gray-3)] focus:outline-none
                transition-colors text-left
              "
              role="menuitem"
              tabindex="-1"
              aria-label="Interrupt and send now. Press Enter to select."
            >
              <Zap class="w-4 h-4 text-[var(--warning)] shrink-0" />
              <div class="flex-1 min-w-0">
                <div class="text-[var(--text-base)] text-[var(--text-primary)] font-medium">
                  Interrupt & Send
                </div>
                <div class="text-[var(--text-xs)] text-[var(--text-muted)]">Stop and send now</div>
              </div>
              <span class="text-[var(--text-xs)] text-[var(--text-tertiary)] tabular-nums shrink-0">
                Ctrl+Enter
              </span>
            </button>

            <button
              ref={(el) => {
                menuItemRefs[2] = el
              }}
              type="button"
              onClick={() => handleMenuAction('postComplete')}
              class="
                w-full flex items-center gap-3 px-3 py-2
                hover:bg-[var(--gray-3)] focus:bg-[var(--gray-3)] focus:outline-none
                transition-colors text-left
              "
              role="menuitem"
              tabindex="-1"
              aria-label="Queue for after agent stops. Press Enter to select."
            >
              <Clock class="w-4 h-4 text-[var(--violet-4,var(--accent))] shrink-0" />
              <div class="flex-1 min-w-0">
                <div class="text-[var(--text-base)] text-[var(--text-primary)] font-medium">
                  Post-complete
                </div>
                <div class="text-[var(--text-xs)] text-[var(--text-muted)]">
                  Queue for after agent stops
                </div>
              </div>
              <span class="text-[var(--text-xs)] text-[var(--text-tertiary)] tabular-nums shrink-0">
                Alt+Enter
              </span>
            </button>
          </div>
        </Show>
      </div>
    </div>
  )
}
