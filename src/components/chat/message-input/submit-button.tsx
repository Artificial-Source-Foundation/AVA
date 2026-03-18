/**
 * Submit Button
 *
 * Send / Cancel button cluster rendered inside the textarea area.
 * Send button is a purple circle with arrow-up icon.
 * Displays streaming elapsed time and a cancel button when processing.
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
  <div class="absolute right-2.5 top-0 bottom-0 flex items-center gap-1.5">
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

    {/* Send button — purple circle with arrow-up */}
    <button
      type="submit"
      disabled={!props.inputHasText() || props.isProcessing()}
      class="
        flex items-center justify-center
        w-8 h-8 rounded-full
        transition-all
        disabled:opacity-30 disabled:cursor-not-allowed
        bg-[var(--violet-8)] hover:bg-[var(--accent)] text-white
      "
      title="Send message"
    >
      <ArrowUp class="w-4 h-4" />
    </button>
  </div>
)
