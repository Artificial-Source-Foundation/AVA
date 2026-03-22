/**
 * Submit Button
 *
 * Send / Cancel button cluster rendered inside the textarea area.
 * Displays streaming elapsed time and a cancel button when processing.
 *
 * When the agent is running the send button stays enabled so users can
 * submit steering messages (Enter), follow-ups (Alt+Enter), or
 * post-complete messages (Ctrl+Alt+Enter).
 */

import { ArrowUp, Square } from 'lucide-solid'
import { type Accessor, type Component, Show } from 'solid-js'

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
}

// ---------------------------------------------------------------------------
// Component
// ---------------------------------------------------------------------------

export const SubmitButton: Component<SubmitButtonProps> = (props) => (
  <div class="absolute right-3 top-0 bottom-0 flex items-center gap-2">
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
        title="Cancel (Esc)"
      >
        <Square class="w-3.5 h-3.5" />
      </button>
    </Show>

    {/* Send button */}
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
      `}
      title={
        props.isProcessing()
          ? 'Send steering message (Enter), follow-up (Alt+Enter), or post-complete (Ctrl+Alt+Enter)'
          : 'Send message (Enter)'
      }
    >
      <ArrowUp class="w-4 h-4" stroke-width={2.5} />
    </button>
  </div>
)
