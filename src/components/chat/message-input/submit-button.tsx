/**
 * Submit Button
 *
 * Send / Cancel button cluster rendered inside the textarea area.
 * Send button is a purple circle with arrow-up icon.
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
  <div class="absolute right-2.5 top-0 bottom-0 flex items-center gap-1.5">
    {/* Queued message count badge */}
    <Show when={props.queuedCount && props.queuedCount() > 0}>
      <span
        class="
          flex items-center justify-center
          min-w-[18px] h-[18px] px-1
          text-[9px] font-semibold tabular-nums
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
      <span class="flex items-center gap-1 text-[10px] text-[var(--text-tertiary)] tabular-nums">
        <span class="w-1.5 h-1.5 bg-[var(--accent)] rounded-full animate-pulse" />
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
          w-7 h-7
          bg-[var(--error)] hover:brightness-110
          text-white rounded-full
          transition-colors
        "
        title="Cancel"
      >
        <Square class="w-3 h-3" />
      </button>
    </Show>

    {/* Send button — purple circle with arrow-up.
        Stays enabled during processing so users can steer the agent. */}
    <button
      type="submit"
      disabled={!props.inputHasText()}
      class="
        flex items-center justify-center
        w-8 h-8 rounded-full
        transition-all
        disabled:opacity-30 disabled:cursor-not-allowed
        bg-[var(--violet-8)] hover:bg-[var(--accent)] text-white
      "
      title={
        props.isProcessing()
          ? 'Send steering message (Enter), follow-up (Alt+Enter), or post-complete (Ctrl+Alt+Enter)'
          : 'Send message'
      }
    >
      <ArrowUp class="w-4 h-4" />
    </button>
  </div>
)
