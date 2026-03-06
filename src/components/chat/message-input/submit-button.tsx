/**
 * Submit Button
 *
 * Send / Cancel button cluster rendered inside the textarea area.
 * Displays streaming elapsed time, a cancel button when processing,
 * and a send button.
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
}

// ---------------------------------------------------------------------------
// Component
// ---------------------------------------------------------------------------

export const SubmitButton: Component<SubmitButtonProps> = (props) => (
  <div class="absolute right-2 top-0 bottom-0 flex items-center gap-1.5">
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
        class="p-1.5 bg-[var(--error)] hover:brightness-110 text-white rounded-[var(--radius-md)] transition-colors"
        title="Cancel"
      >
        <Square class="w-3.5 h-3.5" />
      </button>
    </Show>

    {/* Send button */}
    <button
      type="submit"
      disabled={!props.inputHasText() || props.isProcessing()}
      class="
        p-1.5 rounded-[var(--radius-md)] transition-colors
        disabled:opacity-30 disabled:cursor-not-allowed
        bg-[var(--accent)] hover:bg-[var(--accent-hover)] text-white
      "
      title="Send message"
    >
      <ArrowUp class="w-3.5 h-3.5" />
    </button>
  </div>
)
