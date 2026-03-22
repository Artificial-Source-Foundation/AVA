/**
 * Submit Button
 *
 * Send / Cancel button cluster rendered inside the textarea area.
 * Displays streaming elapsed time and a cancel button when processing.
 *
 * When the agent is running the send button stays enabled so users can
 * submit steering messages (Enter), follow-ups (Alt+Enter), or
 * post-complete messages (Ctrl+Alt+Enter).
 *
 * Right-clicking the send button during processing opens a context menu
 * to choose between Steer, Follow-up, and Post-complete message tiers.
 */

import { ArrowUp, ChevronDown, Clock, MessageSquare, Square, Zap } from 'lucide-solid'
import { type Accessor, type Component, createSignal, onCleanup, Show } from 'solid-js'

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
  onSteer?: () => void
  onFollowUp?: () => void
  onPostComplete?: () => void
}

// ---------------------------------------------------------------------------
// Component
// ---------------------------------------------------------------------------

export const SubmitButton: Component<SubmitButtonProps> = (props) => {
  const [menuOpen, setMenuOpen] = createSignal(false)
  let menuRef: HTMLDivElement | undefined

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
    // Defer so the current click doesn't immediately close it
    requestAnimationFrame(() => {
      document.addEventListener('click', handleDocClick, { once: true })
    })
  }

  onCleanup(() => {
    document.removeEventListener('click', handleDocClick)
  })

  const handleMenuAction = (action: 'steer' | 'followUp' | 'postComplete'): void => {
    setMenuOpen(false)
    if (action === 'steer') props.onSteer?.()
    else if (action === 'followUp') props.onFollowUp?.()
    else if (action === 'postComplete') props.onPostComplete?.()
  }

  return (
    <div class="absolute right-3 top-0 bottom-0 flex items-center gap-2">
      {/* Double-Escape hint */}
      <Show when={props.escapeHint?.()}>
        <span
          class="
            text-[11px] text-[var(--warning)] font-medium
            animate-pulse whitespace-nowrap
          "
        >
          Press Esc again to cancel
        </span>
      </Show>

      {/* Queued message count badge */}
      <Show when={props.queuedCount && props.queuedCount() > 0}>
        <span
          class="
            flex items-center justify-center
            min-w-[20px] h-[20px] px-1.5
            text-[10px] font-semibold tabular-nums
            bg-[var(--accent)] text-white
            rounded-full
          "
          title={`${props.queuedCount!()} queued message(s)`}
        >
          {props.queuedCount!()}
        </span>
      </Show>

      {/* Streaming elapsed time */}
      <Show when={props.isStreaming()}>
        <span class="flex items-center gap-1.5 text-[11px] text-[var(--text-tertiary)] tabular-nums">
          <span class="w-2 h-2 bg-[var(--accent)] rounded-full animate-pulse" />
          {props.elapsedSeconds()}s
        </span>
      </Show>

      {/* Cancel button */}
      <Show when={props.isProcessing()}>
        <button
          type="button"
          onClick={props.onCancel}
          class="
            flex items-center justify-center
            w-8 h-8
            bg-[var(--error)]/90 hover:bg-[var(--error)]
            text-white rounded-lg
            transition-all active:scale-95
          "
          title="Cancel (Esc Esc)"
        >
          <Square class="w-3.5 h-3.5" />
        </button>
      </Show>

      {/* Send button with context menu for mid-stream tier selection */}
      <div class="relative" ref={menuRef}>
        <div class="flex items-center">
          <button
            type="submit"
            disabled={!props.inputHasText()}
            class={`
              flex items-center justify-center
              w-8 h-8 rounded-lg
              transition-all active:scale-95
              ${
                props.inputHasText()
                  ? 'bg-[var(--accent)] hover:brightness-110 text-white shadow-sm shadow-[var(--accent)]/25'
                  : 'bg-[var(--gray-4)] text-[var(--gray-7)] cursor-not-allowed'
              }
              ${props.isProcessing() && props.inputHasText() ? 'rounded-r-none' : ''}
            `}
            title={props.isProcessing() ? 'Send steering message (Enter)' : 'Send message (Enter)'}
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
                setMenuOpen(!menuOpen())
                if (menuOpen()) {
                  requestAnimationFrame(() => {
                    document.addEventListener('click', handleDocClick, { once: true })
                  })
                }
              }}
              class="
                flex items-center justify-center
                w-5 h-8 rounded-r-lg border-l border-white/20
                bg-[var(--accent)] hover:brightness-110 text-white
                transition-all active:scale-95
              "
              title="Choose message tier"
            >
              <ChevronDown class="w-3 h-3" />
            </button>
          </Show>
        </div>

        {/* Tier selection dropdown menu */}
        <Show when={menuOpen()}>
          <div
            class="
              absolute right-0 bottom-full mb-2
              w-64 py-1
              bg-[var(--surface-raised)] border border-[var(--gray-5)]
              rounded-lg shadow-lg shadow-black/20
              z-50
            "
          >
            <button
              type="button"
              onClick={() => handleMenuAction('steer')}
              class="
                w-full flex items-center gap-3 px-3 py-2
                hover:bg-[var(--gray-3)] transition-colors text-left
              "
            >
              <Zap class="w-4 h-4 text-[var(--accent)] shrink-0" />
              <div class="flex-1 min-w-0">
                <div class="text-[13px] text-[var(--text-primary)] font-medium">Steer</div>
                <div class="text-[11px] text-[var(--text-muted)]">Inject between tool calls</div>
              </div>
              <span class="text-[11px] text-[var(--text-tertiary)] tabular-nums shrink-0">
                Enter
              </span>
            </button>

            <button
              type="button"
              onClick={() => handleMenuAction('followUp')}
              class="
                w-full flex items-center gap-3 px-3 py-2
                hover:bg-[var(--gray-3)] transition-colors text-left
              "
            >
              <MessageSquare class="w-4 h-4 text-[var(--success)] shrink-0" />
              <div class="flex-1 min-w-0">
                <div class="text-[13px] text-[var(--text-primary)] font-medium">Follow-up</div>
                <div class="text-[11px] text-[var(--text-muted)]">Queue for after current task</div>
              </div>
              <span class="text-[11px] text-[var(--text-tertiary)] tabular-nums shrink-0">
                Alt+Enter
              </span>
            </button>

            <button
              type="button"
              onClick={() => handleMenuAction('postComplete')}
              class="
                w-full flex items-center gap-3 px-3 py-2
                hover:bg-[var(--gray-3)] transition-colors text-left
              "
            >
              <Clock class="w-4 h-4 text-[var(--warning)] shrink-0" />
              <div class="flex-1 min-w-0">
                <div class="text-[13px] text-[var(--text-primary)] font-medium">Post-complete</div>
                <div class="text-[11px] text-[var(--text-muted)]">Queue for after agent stops</div>
              </div>
              <span class="text-[11px] text-[var(--text-tertiary)] tabular-nums shrink-0">
                Ctrl+Alt+Enter
              </span>
            </button>
          </div>
        </Show>
      </div>
    </div>
  )
}
